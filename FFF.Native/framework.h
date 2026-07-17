#pragma once

// 禁用 Windows SDK 中的 min/max 宏，避免它们破坏 C++ 标准库同名函数。
#define NOMINMAX
// 排除业务代码不需要的低频 Windows 声明，缩短预编译头的处理时间。
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
