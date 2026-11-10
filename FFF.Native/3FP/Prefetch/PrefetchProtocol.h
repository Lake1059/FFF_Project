#pragma once

// Cross-PE prefetch wire protocol. Everything that travels between the player
// process and FFF.PrefetchHost.exe is plain-old-data: no pointers, no AVFrame,
// no AVFormatContext and no other FFmpeg type. The consumer is another process
// with a different address space, so a pointer would be a dangling number.
//
// Framing on both pipes is one little-endian uint32 payload length followed by
// exactly that many bytes; the payload is one of the fixed structs below. The
// same framing the AI subtitle worker uses for stdin, applied symmetrically to
// both directions so a partial frame can never be mistaken for a whole one.

#include <cstdint>
#include <type_traits>

#define FFF_PREFETCH_PROTOCOL_VERSION 1u
#define FFF_PREFETCH_FRAME_MAX_BYTES 65536u
#define FFF_PREFETCH_PATH_CAPACITY 2048u

enum FFFPrefetchTaskKind : std::uint32_t {
    FFF_PREFETCH_TASK_WARM = 1u,      // sequential byte warm-up, no demux
    FFF_PREFETCH_TASK_PROBE = 2u,     // light container / route description
    FFF_PREFETCH_TASK_SHUTDOWN = 3u,  // close stdin and exit
};

enum FFFPrefetchResultFlags : std::uint32_t {
    FFF_PREFETCH_FLAG_STILL = 0x1u,
    FFF_PREFETCH_FLAG_VIDEO = 0x2u,
    FFF_PREFETCH_FLAG_AUDIO = 0x4u,
    FFF_PREFETCH_FLAG_OTHER = 0x8u,
};

struct FFFPrefetchTask {
    std::uint32_t size;          // sizeof(FFFPrefetchTask)
    std::uint32_t version;       // FFF_PREFETCH_PROTOCOL_VERSION
    std::uint32_t kind;          // FFFPrefetchTaskKind
    std::uint32_t generation;    // plan generation; stale results are dropped
    std::int32_t  order;         // distance from the current item (0 = current)
    std::uint32_t pathBytes;     // bytes in pathUtf8, excluding the terminator
    std::uint32_t reserved[2];
    char pathUtf8[FFF_PREFETCH_PATH_CAPACITY];
};

struct FFFPrefetchResult {
    std::uint32_t size;
    std::uint32_t version;
    std::uint32_t kind;          // echoes the task kind
    std::uint32_t generation;
    std::int32_t  order;
    std::int32_t  status;        // 0 = ok, negative = the step was skipped
    std::uint32_t flags;         // FFFPrefetchResultFlags
    std::uint32_t pathBytes;
    std::uint64_t fileSizeBytes;
    std::uint32_t warmedBytes;
    std::uint32_t streamCount;
    std::int32_t  videoStreamIndex;   // -1 when absent
    std::int32_t  videoCodecId;       // AVCodecID as a plain integer
    std::int32_t  width;
    std::int32_t  height;
    std::uint32_t reserved[4];
    char pathUtf8[FFF_PREFETCH_PATH_CAPACITY];
};

static_assert(std::is_trivially_copyable<FFFPrefetchTask>::value,
    "FFFPrefetchTask must be plain-old-data: it crosses a process boundary");
static_assert(std::is_trivially_copyable<FFFPrefetchResult>::value,
    "FFFPrefetchResult must be plain-old-data: it crosses a process boundary");
static_assert(sizeof(FFFPrefetchTask) < FFF_PREFETCH_FRAME_MAX_BYTES,
    "a single prefetch task must fit in one frame");
static_assert(sizeof(FFFPrefetchResult) < FFF_PREFETCH_FRAME_MAX_BYTES,
    "a single prefetch result must fit in one frame");
