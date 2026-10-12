#pragma once
#include <cstdint>

// Versioned C ABI. No C++ objects, retained frame pointers or allocator ownership
// cross the module boundary. The extension supplies precompiled ps_5_0/cs_5_0
// blobs; constants are opaque to the player and bound at b1.
constexpr std::uint32_t FFFColorExtensionVersion = 10;
// Optional GPU reconstruction uses BL at t0..t2, EL at t3..t5 and output at u0.
// Software EL planes contain unshifted 10-bit samples; D3D11 EL is P010.
constexpr std::uint32_t FFFColorExtensionGpuEnhancement = 1u;
// getStatusText supplies extension-owned Dolby text. State 3 is authorization;
// 4=full summary, 5=RPU status, 6=enhancement status, 7=format, 8=path.
constexpr std::uint32_t FFFColorExtensionAuthorizationText = 3;
constexpr std::uint32_t FFFColorExtensionDolbySummaryText = 4;
constexpr std::uint32_t FFFColorExtensionDolbyRpuText = 5;
constexpr std::uint32_t FFFColorExtensionDolbyEnhancementText = 6;
constexpr std::uint32_t FFFColorExtensionDolbyFormatText = 7;
constexpr std::uint32_t FFFColorExtensionDolbyPathText = 8;
// Variant packing for states 4-8: P[0..7], L[8..15], RPU[16], EL[17],
// layer[18..19] (1=MEL, 2=FEL, 3=unknown), active[20], unselected[21].
// Returned UTF-8 is borrowed until the next call on this thread; copy promptly.
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
struct FFFColorExtensionMetadataInfo {
    std::uint32_t size;
    std::uint32_t enhancementLayer; // 0=none, 1=MEL, 2=FEL, 3=unknown
    std::uint32_t requiresEnhancement;
    float sourcePeakNits;
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
    int (__cdecl* analyzeMetadata)(const FFFColorExtensionInput*, std::uint32_t hasEnhancementLayer,
        FFFColorExtensionMetadataInfo*);
};
using FFFGetColorExtensionApi = int (__cdecl*)(std::uint32_t, FFFColorExtensionApi*);
