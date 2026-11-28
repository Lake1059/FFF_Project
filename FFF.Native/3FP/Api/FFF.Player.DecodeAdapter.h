#pragma once

#include "3FP/Api/FFF.Player.Api.h"

// ---- Actual decode-adapter read-back (appended export) ---------------------------
//
// The host has to name the GPU that is decoding right now, not the one the user
// picked: the open path walks a candidate plan (the playback GPU first, then the
// remaining hardware adapters, then the CPU), and the route that actually succeeded
// is only known inside the session. This reports it.
//
// Appended additively like the prefetch / CPU-placement entries: this header is NOT
// included from FFF.Player.Api.h, adds no FFF3FPSnapshot field and moves neither
// PlayerApiVersion (20) nor SnapshotVersion (23). Hosts resolve
// FFF3FP_GetDecodeAdapterInfo dynamically and degrade to "unknown" when an older
// kernel does not export it.
//
// adapterIndex uses the same index space as IDXGIFactory1::EnumAdapters1 -- exactly
// what the host's GPU picker enumerates -- so the display name can be looked up in
// that same list. It is only meaningful while hardware != 0; -1 means "unknown" (the
// device had no adapter yet, or the route is software decoding).

#define FFF3FP_DECODE_ADAPTER_VERSION 1u

struct FFF3FPDecodeAdapterInfo {
    std::uint32_t size;        // caller sets sizeof(...) before the call
    std::uint32_t version;     // FFF3FP_DECODE_ADAPTER_VERSION
    std::int32_t adapterIndex; // DXGI EnumAdapters1 index; -1 = unknown
    std::uint32_t hardware;    // 1 = a hardware decoder is decoding this media
};

static_assert(sizeof(FFF3FPDecodeAdapterInfo) == 16,
    "FFF3FPDecodeAdapterInfo layout changed: bump FFF3FP_DECODE_ADAPTER_VERSION and "
    "mirror it in the host's VB structure.");

// Fills `info` for a session. InvalidArgument when player or info is null, when
// version != FFF3FP_DECODE_ADAPTER_VERSION, or when size is smaller than the struct.
// Success overwrites the caller's struct end to end (size/version included).
FFF3FP_API FFFResult FFF3FP_GetDecodeAdapterInfo(FFF3FPHandle player,
    FFF3FPDecodeAdapterInfo* info) noexcept;
