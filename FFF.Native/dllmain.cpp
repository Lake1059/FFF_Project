#include "pch.h"

HMODULE g_fffNativeModule = nullptr;

// Windows 加载器入口。加载器锁持有期间执行 COM、FFmpeg、内存分配、加锁或创建线程都不安全，
// 因此这里刻意不做任何业务初始化。进程附加时关闭无用的逐线程通知；实际资源全部由显式会话持有。
BOOL APIENTRY DllMain(const HMODULE module, const DWORD reason, LPVOID reserved) {
    static_cast<void>(reserved);
    if (reason == DLL_PROCESS_ATTACH) {
        g_fffNativeModule = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
