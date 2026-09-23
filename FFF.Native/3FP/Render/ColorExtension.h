#pragma once
#include "3FP/Render/ColorExtensionApi.h"
#include <windows.h>
#include <cwchar>

// Process lifetime: modules cannot unload while renderers use their shader code.
inline const FFFColorExtensionApi* GetColorExtension() noexcept {
    static const FFFColorExtensionApi api = []() noexcept {
        FFFColorExtensionApi result{};
        wchar_t path[32768]{};
        const auto length = GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        if (!length || length >= ARRAYSIZE(path)) return result;
        auto* slash = wcsrchr(path, L'\\');
        if (!slash || wcscpy_s(slash + 1, ARRAYSIZE(path) - (slash + 1 - path),
            L"FFF.DolbyVision.Test.dll") != 0) return result;
        // Deliberately exclude PATH/current-directory probing and extraction cache.
        const auto module = LoadLibraryExW(path, nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!module) return result;
        const auto entry = reinterpret_cast<FFFGetColorExtensionApi>(
            GetProcAddress(module, "FFF_GetColorExtensionApi"));
        result.size = sizeof(result);
        if (!entry || !entry(FFFColorExtensionVersion, &result) ||
            result.size != sizeof(result) || result.version != FFFColorExtensionVersion ||
            !result.prepare || !result.getAuthorizationStatus || !result.authenticate ||
            !result.getStatusText || !result.requestAuthorizationPrompt || !result.setAuthorizationPrompt ||
            !result.shaderBytecode || !result.shaderBytecodeSize ||
            result.shaderBytecodeSize > FFFColorExtensionBytecodeLimit ||
            !result.constantsSize || result.constantsSize > FFFColorExtensionCapacity ||
            result.constantsSize % 16) {
            FreeLibrary(module);
            return FFFColorExtensionApi{};
        }
        return result;
    }();
    return api.prepare ? &api : nullptr;
}

inline bool IsColorExtensionAuthorized() noexcept {
    const auto* api = GetColorExtension();
    return api != nullptr && api->getAuthorizationStatus() == 2;
}
