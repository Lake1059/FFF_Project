#include "pch.h"

#include <delayimp.h>

extern HMODULE g_fffNativeModule;

namespace {

struct FfmpegModule {
    const char* importName;
    const wchar_t* filePrefix;
};

constexpr FfmpegModule Modules[] = {
    { "avcodec-63.dll", L"avcodec" },
    { "avformat-63.dll", L"avformat" },
    { "avutil-61.dll", L"avutil" },
    { "swresample-7.dll", L"swresample" },
};

bool EqualsIgnoreCase(const wchar_t* left, const wchar_t* right) {
    return CompareStringOrdinal(left, -1, right, -1, TRUE) == CSTR_EQUAL;
}

bool IsSupportedModuleName(const wchar_t* fileName, const wchar_t* prefix) {
    const auto prefixLength = wcslen(prefix);
    const auto fileNameLength = wcslen(fileName);
    if (fileNameLength < prefixLength + 4 ||
        CompareStringOrdinal(fileName, static_cast<int>(prefixLength), prefix,
            static_cast<int>(prefixLength), TRUE) != CSTR_EQUAL ||
        CompareStringOrdinal(fileName + fileNameLength - 4, 4, L".dll", 4, TRUE) != CSTR_EQUAL) {
        return false;
    }

    auto suffix = fileName + prefixLength;
    if (*suffix == L'\0') return false;
    if (EqualsIgnoreCase(suffix, L".dll")) return true;
    if (*suffix++ != L'-') return false;
    if (*suffix == L'\0') return false;
    while (*suffix != L'\0' && *suffix != L'.') {
        if (*suffix < L'0' || *suffix > L'9') return false;
        ++suffix;
    }
    return EqualsIgnoreCase(suffix, L".dll");
}

std::wstring GetDirectory(HMODULE module) {
    std::vector<wchar_t> path(MAX_PATH);
    for (;;) {
        const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0) return {};
        if (length < path.size() - 1) {
            std::wstring result(path.data(), length);
            const auto separator = result.find_last_of(L"\\/");
            return separator == std::wstring::npos ? std::wstring{} : result.substr(0, separator);
        }
        path.resize(path.size() * 2);
    }
}

HMODULE LoadMatchingModule(const wchar_t* directory, const wchar_t* prefix) {
    if (directory == nullptr || *directory == L'\0') return nullptr;

    const std::wstring searchPath = std::wstring(directory) + L"\\" + prefix + L"*.dll";
    WIN32_FIND_DATAW findData{};
    const auto search = FindFirstFileW(searchPath.c_str(), &findData);
    if (search == INVALID_HANDLE_VALUE) return nullptr;

    std::wstring selected;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            IsSupportedModuleName(findData.cFileName, prefix)) {
            const std::wstring candidate = std::wstring(directory) + L"\\" + findData.cFileName;
            if (EqualsIgnoreCase(findData.cFileName, (std::wstring(prefix) + L".dll").c_str())) {
                selected = candidate;
                break;
            }
            if (selected.empty() || CompareStringOrdinal(candidate.c_str(), -1, selected.c_str(), -1, TRUE) == CSTR_GREATER_THAN) {
                selected = candidate;
            }
        }
    } while (FindNextFileW(search, &findData));
    FindClose(search);

    return selected.empty() ? nullptr : LoadLibraryExW(selected.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
}

FARPROC WINAPI DelayLoadHook(const unsigned notification, PDelayLoadInfo delayLoadInfo) {
    if (notification != dliNotePreLoadLibrary || delayLoadInfo == nullptr || delayLoadInfo->szDll == nullptr) {
        return nullptr;
    }

    for (const auto& module : Modules) {
        if (_stricmp(delayLoadInfo->szDll, module.importName) != 0) continue;

        const auto nativeDirectory = GetDirectory(g_fffNativeModule);
        if (const auto loaded = LoadMatchingModule(nativeDirectory.c_str(), module.filePrefix)) {
            return reinterpret_cast<FARPROC>(loaded);
        }
        const auto applicationDirectory = GetDirectory(nullptr);
        if (const auto loaded = LoadMatchingModule(applicationDirectory.c_str(), module.filePrefix)) {
            return reinterpret_cast<FARPROC>(loaded);
        }
        return nullptr;
    }
    return nullptr;
}

} // namespace

extern "C" const PfnDliHook __pfnDliNotifyHook2 = DelayLoadHook;
