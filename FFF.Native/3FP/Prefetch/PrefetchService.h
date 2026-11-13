#pragma once

// Runtime "previous / next" prefetch service, step 1 (no shared memory).
//
// Ownership model (deliberately boring):
//   * one in-process worker thread does lightweight work only;
//   * the optional FFF.PrefetchHost.exe helper does sequential warm-up plus
//     container POD probing, and owns no player handle;
//   * still-image CPU AVFrames are decoded IN THIS PROCESS (an AVFrame is a
//     bag of pointers + refcounts, so it can never cross a pipe);
//   * everything cached is derived data and may be dropped wholesale.
//
// The public ABI lives in 3FP/Api/FFF.Player.Prefetch.h. PlayerApiVersion (20),
// SnapshotVersion (23) and the 608-byte snapshot are untouched.

#include "3FP/Api/FFF.Player.Prefetch.h"
#include "3FP/Prefetch/PrefetchProtocol.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct AVFrame;

namespace FFFPrefetch {

struct FileFingerprint {
    std::uint64_t sizeBytes = 0;
    std::uint64_t lastWrite100ns = 0;
    bool operator==(const FileFingerprint& other) const noexcept {
        return sizeBytes == other.sizeBytes && lastWrite100ns == other.lastWrite100ns;
    }
};

// A value object only. No AVFormatContext, no decoder, no device.
struct ProbePod {
    std::uint32_t flags = 0;
    std::uint32_t streamCount = 0;
    std::int32_t  videoStreamIndex = -1;
    std::int32_t  videoCodecId = 0;
    std::int32_t  width = 0;
    std::int32_t  height = 0;
    std::uint64_t fileSizeBytes = 0;
    std::uint32_t generation = 0;
    FileFingerprint fingerprint{};
};

// Player-process route memory. The key is the same hash PlayerSession uses
// for codec + profile + pixel format + frame size; it is a plain uint64,
// never a pointer, so this stays a pure in-process value object. A route
// conclusion outlives a plan generation: it describes the FILE, not the
// neighbour list, so PruneToPlanLocked deliberately leaves it alone.
struct RoutePod {
    std::uint64_t routeKey = 0;
    std::uint32_t generation = 0;
    FileFingerprint fingerprint{};
};

class PrefetchService final {
public:
    static PrefetchService& Instance() noexcept;

    FFFResult SetPlan(const FFF3FPPrefetchPlan* plan) noexcept;
    FFFResult GetStatus(FFF3FPPrefetchStatus* status) noexcept;

    // Returns an av_frame_clone() the caller must av_frame_free(). The hit
    // branch in DoOpen / DecodeInitialFrame is a LATER stage; this round only
    // provides the query seam, so opening a file still takes the original path.
    bool TakeStill(const std::string& pathUtf8, AVFrame** out) noexcept;
    bool TakeProbe(const std::string& pathUtf8, ProbePod* out) noexcept;

    // P1-a: remember / restore a hardware trial-decode refusal for one
    // file. Both are process-local, fingerprint-checked and allocation-
    // light; the caller owns the switch that disables the whole arm.
    void NoteUnsupportedRoute(const std::string& pathUtf8,
        std::uint64_t routeKey) noexcept;
    bool TakeUnsupportedRoute(const std::string& pathUtf8,
        std::uint64_t routeKey) noexcept;

    // P0-b: cheap existence check, no av_frame_clone and no ownership
    // transfer. Used only to decide whether a bounded probe window is safe.
    bool HasStillEntry(const std::string& pathUtf8) noexcept;

private:
    struct StillEntry {
        AVFrame* frame = nullptr;   // owned by the cache
        std::uint64_t bytes = 0;
        std::uint64_t lastUse = 0;
        std::uint32_t order = 0;    // distance from the current item
        std::uint32_t generation = 0;
        FileFingerprint fingerprint{};
    };

    PrefetchService() noexcept;
    ~PrefetchService() = default;
    PrefetchService(const PrefetchService&) = delete;
    PrefetchService& operator=(const PrefetchService&) = delete;

    void WorkerLoop() noexcept;
    void GuardLoop() noexcept;
    void EvaluatePressure() noexcept;
    void ReleaseAllLocked() noexcept;
    void TrimToHalfLocked() noexcept;
    void EnforceBudgetLocked(std::uint64_t incoming) noexcept;
    void PruneToPlanLocked(const std::vector<std::string>& paths) noexcept;
    bool AllowsWork() noexcept;
    bool Cancelled(std::uint32_t generation) noexcept;
    bool ShouldStopHelper() noexcept;
    bool HelperAlive() noexcept;

    void EnsureHelper() noexcept;
    void StopHelper() noexcept;
    void MarkHelperFailure() noexcept;
    bool WriteTask(const FFFPrefetchTask& task) noexcept;
    bool ReadResult(FFFPrefetchResult* out) noexcept;
    void NoteResult(const FFFPrefetchResult& result) noexcept;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread worker_;
    std::thread guardThread_;
    bool planDirty_ = false;
    bool stop_ = false;
    bool paused_ = false;
    bool helperStopRequested_ = false;
    bool helperRunning_ = false;
    bool helperDisabled_ = false;
    std::atomic<bool> cancel_{false};
    std::atomic<std::uint32_t> generation_{0};

    std::vector<std::string> plan_;
    std::uint32_t planFlags_ = 0;
    std::unordered_map<std::string, StillEntry> stills_;
    std::unordered_map<std::string, ProbePod> probes_;
    std::unordered_map<std::string, RoutePod> routes_;

    std::uint64_t cachedBytes_ = 0;
    std::uint64_t budgetBytes_ = 256ull * 1024ull * 1024ull;
    std::uint64_t useTick_ = 0;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t releasedFrames_ = 0;
    std::uint64_t helperFailures_ = 0;
    std::uint32_t mode_ = 2;         // 0 off / 1 still / 2 still+probe / 3 reserved
    bool disabled_ = false;
    bool hostEnabled_ = true;

    void* processHandle_ = nullptr;
    void* stdinHandle_ = nullptr;
    void* stdoutHandle_ = nullptr;
};

} // namespace FFFPrefetch
