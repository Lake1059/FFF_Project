#pragma once
#include <cstdint>

// Versioned C ABI. No C++ objects, retained frame pointers or allocator ownership
// cross the module boundary. The extension supplies a precompiled ps_5_0 blob;
// constants are opaque to the player and bound at b1.
constexpr std::uint32_t FFFColorExtensionVersion = 7;
constexpr std::uint32_t FFFColorExtensionCapacity = 8192;
constexpr std::uint32_t FFFColorExtensionBytecodeLimit = 1024 * 1024;
struct FFFColorExtensionInput {
    std::uint32_t size, version, avutilVersion, profile;
    const void* metadata;
    std::uint64_t metadataSize;
};
struct FFFColorExtensionOutput {
    std::uint32_t size;
    float sourcePeakNits;
    std::uint32_t reserved[2];
    alignas(16) unsigned char constants[FFFColorExtensionCapacity];
};
struct FFFColorExtensionApi {
    std::uint32_t size, version, constantsSize, shaderBytecodeSize;
    const void* shaderBytecode;
    int (__cdecl* prepare)(const FFFColorExtensionInput*, FFFColorExtensionOutput*);
    int (__cdecl* getAuthorizationStatus)();
    int (__cdecl* authenticate)(const char*);
    const char* (__cdecl* getStatusText)(std::uint32_t, std::uint32_t);
    void (__cdecl* requestAuthorizationPrompt)();
    void (__cdecl* setAuthorizationPrompt)(int (__cdecl*)(char*, std::uint32_t));
};
using FFFGetColorExtensionApi = int (__cdecl*)(std::uint32_t, FFFColorExtensionApi*);
