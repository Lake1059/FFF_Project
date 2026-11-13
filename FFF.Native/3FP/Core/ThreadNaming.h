#pragma once

// 原生线程命名（P0-3）：把线程名交给 SetThreadDescription，调试器、ETW 与任务管理器
// 才能按名归因。只在各线程函数入口调用一次，不碰优先级、亲和性或其他调度状态；失败静默
// ——老系统没有该导出（Windows 10 1607 之前）时整条链路照常工作。
//
// 本头是 header-only（不新增源文件、不改 vcxproj），且依赖 pch.h 已经引入的
// <windows.h>：只能被 pch.h 预处理过的翻译单元包含。
//
// 名字一律用 ASCII（"FFF.Player.*"），与宿主 CLR 侧的线程名各占一套，互不覆盖。

namespace FFFThreadNaming {

using SetThreadDescriptionFn = HRESULT(WINAPI*)(HANDLE, PCWSTR);

// 一次解析并缓存（与 3FP/Prefetch/MemoryGuard.cpp:22-38 的 static const lambda 惯例
// 一致）。用 GetModuleHandleW 而非 LoadLibraryW：kernel32.dll 必然已加载，不增引用计数，
// 也不会在进程早期 / DllMain 语境下引入加载新模块的风险。
inline SetThreadDescriptionFn ResolveSetThreadDescription() noexcept {
    static const SetThreadDescriptionFn resolve = []() noexcept -> SetThreadDescriptionFn {
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (kernel32 == nullptr) return nullptr;
        return reinterpret_cast<SetThreadDescriptionFn>(
            GetProcAddress(kernel32, "SetThreadDescription"));
    }();
    return resolve;
}

// UTF-8 → 宽字符后在当前线程上设置名字。栈上 64 个宽字符覆盖全部 "FFF.Player.*"
// 名字；超长 / 非法 UTF-8 直接放弃，不截断、不抛异常（线程入口不能有失败路径）。
inline void SetCurrentThreadName(const char* utf8Name) noexcept {
    if (utf8Name == nullptr || utf8Name[0] == '\0') return;
    const SetThreadDescriptionFn setThreadDescription = ResolveSetThreadDescription();
    if (setThreadDescription == nullptr) return;   // 无该 API：静默跳过
    wchar_t wide[64] = {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8Name, -1, wide, 64);
    if (length <= 1) return;   // 0 = 转换失败，1 = 只有终止符
    (void)setThreadDescription(GetCurrentThread(), wide);
}

}  // namespace FFFThreadNaming