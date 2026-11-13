#include "pch.h"
#include "3FP/Prefetch/MemoryGuard.h"

namespace FFFPrefetch {
namespace {

// CreateMemoryResourceNotification / QueryMemoryResourceNotification 在部分
// Windows SDK 配置（尤其搭配 WIN32_LEAN_AND_MEAN）下不一定有声明。用
// GetProcAddress 从已加载的 kernel32.dll 动态解析并 typedef，保证 v145 SDK
// 下编译得过，也避免为此改动预编译头。
//
// MEMORY_RESOURCE_NOTIFICATION_TYPE 底层是 int：0 = LowMemoryResourceNotification，
// 1 = HighMemoryResourceNotification；函数指针用 int 与之 ABI 等价。
using CreateMemoryResourceNotificationFn = HANDLE (WINAPI*)(int);
using QueryMemoryResourceNotificationFn  = BOOL  (WINAPI*)(HANDLE, BOOL*);

struct MemoryNotificationApi {
    CreateMemoryResourceNotificationFn create{nullptr};
    QueryMemoryResourceNotificationFn  query{nullptr};
};

// 一次性解析并缓存（与 PlayerSession.cpp 的 static const lambda 开关风格一致）。
// 用 GetModuleHandleW 而非 LoadLibraryW：kernel32.dll 必然已加载，不增引用计数，
// 也不会在进程早期 / DllMain 语境下引入加载新模块的风险。
const MemoryNotificationApi& MemoryNotificationApiInstance() noexcept {
    static const MemoryNotificationApi api = [] {
        MemoryNotificationApi resolved{};
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32 != nullptr) {
            resolved.create = reinterpret_cast<CreateMemoryResourceNotificationFn>(
                GetProcAddress(kernel32, "CreateMemoryResourceNotification"));
            resolved.query = reinterpret_cast<QueryMemoryResourceNotificationFn>(
                GetProcAddress(kernel32, "QueryMemoryResourceNotification"));
        }
        return resolved;
    }();
    return api;
}

// FFF3FP_PREFETCH_MEMGUARD=0 时守门员完全让位（恒 Normal），用于对照实验。
// 变量名用全名，避免与启动期 DLL 预读的 FFF_PREFETCH 系列混淆。
bool MemGuardEnabled() noexcept {
    static const auto enabled = [] {
        wchar_t value[8]{};
        if (GetEnvironmentVariableW(L"FFF3FP_PREFETCH_MEMGUARD", value, 8) == 0) return true;
        return !(value[0] == L'0' && value[1] == L'\0');
    }();
    return enabled;
}

// FFF3FP_PREFETCH_TIGHT_LOAD 可覆盖「吃紧」阈值，仅用于量具对照，clamp 到 50..99。
// 默认 95：物理内存占用率 >= 95% 才算吃紧（按用户定档）。
std::uint32_t ResolveTightLoadPercent() noexcept {
    static const auto percent = []() -> std::uint32_t {
        constexpr std::uint32_t kDefault = 95;
        wchar_t value[16]{};
        const DWORD written = GetEnvironmentVariableW(L"FFF3FP_PREFETCH_TIGHT_LOAD", value, 16);
        // 变量不存在 (0) 或缓冲区不足 (>=16) 一律回落默认值。
        if (written == 0 || written >= 16) return kDefault;
        // 手写 ASCII 十进制解析，不依赖 <cwchar> / wcstol，保持纯 noexcept。
        std::uint32_t parsed = 0;
        bool anyDigit = false;
        for (DWORD i = 0; i < written; ++i) {
            const wchar_t c = value[i];
            if (c < L'0' || c > L'9') break;
            anyDigit = true;
            parsed = parsed * 10u + static_cast<std::uint32_t>(c - L'0');
            if (parsed > 999u) { parsed = 999u; break; }
        }
        if (!anyDigit) return kDefault;
        if (parsed < 50u) return 50u;
        if (parsed > 99u) return 99u;
        return parsed;
    }();
    return percent;
}

} // namespace

MemoryGuard& MemoryGuard::Instance() noexcept {
    static MemoryGuard instance;   // C++11 magic static：线程安全的惰性单例
    return instance;
}

MemoryGuard::MemoryGuard() noexcept {
    tightLoadPercent_ = ResolveTightLoadPercent();
}

MemoryGuard::~MemoryGuard() noexcept {
    Shutdown();
}

void MemoryGuard::EnsureNotifications() noexcept {
    // 调用方已持有 mutex_。只尝试一次；解析或创建失败时对应句柄保持 nullptr，
    // Evaluate 会跳过该句柄的判据，绝不因此把档位误判成 Critical。
    if (notificationsAttempted_ || shutdown_) return;
    notificationsAttempted_ = true;
    const auto& api = MemoryNotificationApiInstance();
    if (api.create == nullptr || api.query == nullptr) return;
    hLow_  = api.create(0);   // 0 = LowMemoryResourceNotification（紧急）
    hHigh_ = api.create(1);   // 1 = HighMemoryResourceNotification（充足）
}

void* MemoryGuard::LowHandle() noexcept {
    std::lock_guard lock(mutex_);
    EnsureNotifications();
    return hLow_;
}

void* MemoryGuard::HighHandle() noexcept {
    std::lock_guard lock(mutex_);
    EnsureNotifications();
    return hHigh_;
}

PressureTier MemoryGuard::Evaluate() noexcept {
    // 对照实验开关：关闭时守门员不参与定档，恒为 Normal。
    if (!MemGuardEnabled()) {
        tier_.store(static_cast<std::uint32_t>(PressureTier::Normal), std::memory_order_release);
        return PressureTier::Normal;
    }
    try {
        std::lock_guard lock(mutex_);
        EnsureNotifications();

        // 「上一档」= 本次观测开始前的最近一次定档；死区保持的就是它。
        const auto prev = static_cast<PressureTier>(tier_.load(std::memory_order_acquire));
        const auto& api = MemoryNotificationApiInstance();

        // 判据一：Low 通知 = 紧急档（Critical）。
        // LowMemoryResourceNotification 只在系统可用物理内存极低时置位（默认阈值
        // 约每 4GB 仅剩 32MB），它是紧急信号，绝非「高负载」早预警；高负载早预警
        // 只能靠下面 GlobalMemoryStatusEx 的 dwMemoryLoad。
        if (hLow_ != nullptr) {
            BOOL signaled = FALSE;
            if (api.query(hLow_, &signaled) && signaled != FALSE) {
                tier_.store(static_cast<std::uint32_t>(PressureTier::Critical), std::memory_order_release);
                return PressureTier::Critical;
            }
        }

        // 判据二：物理内存占用率 = 早预警（Tight）。只有 dwMemoryLoad 达到
        // tightLoadPercent_（默认 95）才算吃紧。
        MEMORYSTATUSEX ms{};
        ms.dwLength = static_cast<DWORD>(sizeof(ms));
        if (GlobalMemoryStatusEx(&ms) && ms.dwMemoryLoad >= tightLoadPercent_) {
            tier_.store(static_cast<std::uint32_t>(PressureTier::Tight), std::memory_order_release);
            return PressureTier::Tight;
        }

        // 判据三：High 通知 = 压力解除（回到 Normal）。只有上一档不是 Normal 时才回落。
        // 回到 Normal 只是「允许开工」：是否重建缓存由调用方按内容驱动决定，守门员
        // 只负责定档，不负责重建，压力解除后不会自动重建缓存。
        if (hHigh_ != nullptr && prev != PressureTier::Normal) {
            BOOL signaled = FALSE;
            if (api.query(hHigh_, &signaled) && signaled != FALSE) {
                tier_.store(static_cast<std::uint32_t>(PressureTier::Normal), std::memory_order_release);
                return PressureTier::Normal;
            }
        }

        // 死区：Low / High 都是持续电平，二者之间存在既不 Low 也不 High 的中间区间。
        // 该区间必须保持上一档，避免档位在阈值附近来回跳。
        return prev;
    } catch (...) {
        // noexcept 承诺：加锁等异常一律退化为「保持最近一次定档」。
        return static_cast<PressureTier>(tier_.load(std::memory_order_acquire));
    }
}

PressureTier MemoryGuard::Tier() const noexcept {
    return static_cast<PressureTier>(tier_.load(std::memory_order_acquire));
}

bool MemoryGuard::AllowsPrefetch() const noexcept {
    return Tier() == PressureTier::Normal;
}

bool MemoryGuard::ShouldReleaseAll() const noexcept {
    return Tier() == PressureTier::Critical;
}

bool MemoryGuard::ShouldTrim() const noexcept {
    return Tier() == PressureTier::Tight;
}

void MemoryGuard::Shutdown() noexcept {
    try {
        std::lock_guard lock(mutex_);
        shutdown_ = true;   // 置位后 EnsureNotifications 不再重建句柄
        if (hLow_ != nullptr) {
            CloseHandle(hLow_);
            hLow_ = nullptr;
        }
        if (hHigh_ != nullptr) {
            CloseHandle(hHigh_);
            hHigh_ = nullptr;
        }
    } catch (...) {
        // noexcept 承诺：锁异常时不抛。
    }
}

} // namespace FFFPrefetch
