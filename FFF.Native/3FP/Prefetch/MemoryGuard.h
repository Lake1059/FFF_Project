#pragma once

// 三档内存压力守门员。只负责「观测一次并定档」，不负责重建缓存等后续动作。
// 判据全部是事件 / 状态驱动（Windows 内存资源通知 + 一次内存状态快照），
// 绝不固定周期轮询；守门员自身不起线程、不定定时器。

#include <atomic>
#include <cstdint>
#include <mutex>

namespace FFFPrefetch {

enum class PressureTier : std::uint32_t {
    Normal   = 0,  // 允许预读
    Tight    = 1,  // 物理内存占用率过高，应收缩缓存
    Critical = 2,  // 可用物理内存极低，应释放全部
};

class MemoryGuard final {
public:
    static MemoryGuard& Instance() noexcept;

    // 观测一次并定档，返回本次档位。事件 / 状态驱动，无固定周期轮询。
    PressureTier Evaluate() noexcept;
    // 最近一次 Evaluate 的档位（atomic 读，可与 Evaluate 并发）。
    PressureTier Tier() const noexcept;
    bool AllowsPrefetch() const noexcept;    // 仅 Normal
    bool ShouldReleaseAll() const noexcept; // Critical
    bool ShouldTrim() const noexcept;        // Tight

    // 按用户定档：物理内存占用率 >= 95% 才算「吃紧」。
    std::uint32_t TightLoadPercent() const noexcept { return tightLoadPercent_; }

    // 关闭两个内存资源通知句柄；幂等。
    void Shutdown() noexcept;

    // 供调用方挂一条事件驱动等待线（不轮询）。首次调用惰性创建句柄。
    // 返回的只是电平句柄，所有权仍归守门员，调用方不得 CloseHandle。
    void* LowHandle() noexcept;
    void* HighHandle() noexcept;

private:
    MemoryGuard() noexcept;
    ~MemoryGuard() noexcept;
    MemoryGuard(const MemoryGuard&) = delete;
    MemoryGuard& operator=(const MemoryGuard&) = delete;

    // 惰性创建 Low / High 两个内存资源通知句柄；调用方须已持有 mutex_。
    void EnsureNotifications() noexcept;

    std::mutex mutex_;  // 保护句柄创建 / 关闭与档位写入
    std::atomic<std::uint32_t> tier_{static_cast<std::uint32_t>(PressureTier::Normal)};
    void* hLow_{nullptr};   // LowMemoryResourceNotification（紧急）
    void* hHigh_{nullptr};  // HighMemoryResourceNotification（充足）
    bool notificationsAttempted_{false};
    bool shutdown_{false};
    std::uint32_t tightLoadPercent_{95};
};

} // namespace FFFPrefetch
