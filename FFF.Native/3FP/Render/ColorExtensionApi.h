#pragma once
#include <cstdint>

// Versioned C ABI. No C++ objects, retained frame pointers or allocator ownership
// cross the module boundary. The extension supplies precompiled ps_5_0/cs_5_0
// blobs; constants are opaque to the player and bound at b1.
constexpr std::uint32_t FFFColorExtensionVersion = 9;
// Optional GPU reconstruction uses BL at t0..t2, EL at t3..t5 and output at u0.
// Software EL planes contain unshifted 10-bit samples; D3D11 EL is P010.
constexpr std::uint32_t FFFColorExtensionGpuEnhancement = 1u;
// getStatusText(3, 0/1) supplies the extension-owned authorization body/title.
constexpr std::uint32_t FFFColorExtensionAuthorizationText = 3;
constexpr std::uint32_t FFFColorExtensionCapacity = 8192;
constexpr std::uint32_t FFFColorExtensionBytecodeLimit = 1024 * 1024;
constexpr std::uint64_t FFFColorExtensionEnhancementReferenceMagic =
    0x464646334650454Cull;
struct FFFColorExtensionEnhancementFrameReference {
    std::uint64_t magic;
    void* frame;
};
struct FFFColorExtensionInput {
    std::uint32_t size, version, avutilVersion, profile;
    const void* metadata;
    std::uint64_t metadataSize;
    // Optional decoded Dolby Vision enhancement-layer frame. The pointer is
    // borrowed for the duration of the callback and is never retained by the
    // extension. It is intentionally opaque so the public player ABI does not
    // expose FFmpeg C++ types.
    const void* enhancementFrame;
    std::uint32_t enhancementWidth, enhancementHeight, enhancementFormat;
};
struct FFFColorExtensionOutput {
    std::uint32_t size;
    float sourcePeakNits;
    std::uint32_t flags, reserved;
    alignas(16) unsigned char constants[FFFColorExtensionCapacity];
};
struct FFFColorExtensionApi {
    std::uint32_t size, version, constantsSize, shaderBytecodeSize;
    const void* shaderBytecode;
    int (__cdecl* prepare)(const FFFColorExtensionInput*, FFFColorExtensionOutput*);
    int (__cdecl* applyFrame)(const FFFColorExtensionInput*, void* baseFrame);
    int (__cdecl* getAuthorizationStatus)();
    int (__cdecl* authenticate)(const char*);
    const char* (__cdecl* getStatusText)(std::uint32_t, std::uint32_t);
    void (__cdecl* requestAuthorizationPrompt)();
    void (__cdecl* setAuthorizationPrompt)(int (__cdecl*)(char*, std::uint32_t));
    std::uint32_t enhancementShaderBytecodeSize;
    const void* enhancementShaderBytecode;
};
using FFFGetColorExtensionApi = int (__cdecl*)(std::uint32_t, FFFColorExtensionApi*);
