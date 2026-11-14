#pragma once

#include "3FP/Api/FFF.Player.Api.h"

#include <memory>
#include <string>

struct AVFormatContext;
struct AVFrame;

// Kernel-side LibRaw bridge for one still image.
//
// Ownership and threading: the object is created and destroyed on the session
// worker thread. It never shares LibRaw state with another session, and it is
// deliberately not copyable. The runtime DLL is loaded lazily so ordinary
// media never pays for LibRaw and a missing libraw.dll cannot stop the kernel
// from starting.
class RawStillSource final {
public:
    RawStillSource() noexcept;
    ~RawStillSource();
    RawStillSource(const RawStillSource&) = delete;
    RawStillSource& operator=(const RawStillSource&) = delete;

    // Extension gate. This is only a cheap pre-filter: LibRaw's open_file is
    // the authoritative answer, and a false return here sends the path down
    // the existing avformat route.
    static bool LooksLikeRawPath(const std::string& pathUtf8) noexcept;

    // Loads libraw.dll from the FFF.Native.dll directory (falling back to the
    // normal DLL search path). Safe to call from any thread; the answer is
    // cached for the process lifetime.
    static bool EnsureRuntime() noexcept;

    // Opens the file and reads metadata without unpacking the sensor data.
    // False means "not a RAW this build can handle"; OpenFormat reports
    // NotSupported instead of letting FFmpeg's TIFF shell fake a successful
    // open that decodes nothing.
    bool Open(const std::string& pathUtf8, const FFF3FPRawOptions& options,
        std::string& error) noexcept;

    // Unpacks and develops one frame. The returned AVFrame owns the
    // libraw_processed_image_t allocation through its AVBufferRef (and shares
    // LibRaw's lifetime), so the caller only has to av_frame_free() it after
    // presenting.
    bool Decode(AVFrame** frame, std::string& error) noexcept;

    // Builds the minimal one-stream AVFormatContext that makes the rest of the
    // session treat this source as an ordinary single-image file. Ownership
    // passes to the caller; on failure returns nullptr and OpenFormat reports
    // NativeFailure rather than falling back to an empty FFmpeg open.
    AVFormatContext* CreateFormatContext(std::string& error) noexcept;

    const FFF3FPRawInfo& Info() const noexcept { return info_; }
    std::int32_t OutputWidth() const noexcept { return info_.outputWidth; }
    std::int32_t OutputHeight() const noexcept { return info_.outputHeight; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    FFF3FPRawInfo info_{};
};

// Defaults used when the host never calls FFF3FP_SetRawProcessingOptions.
FFF3FPRawOptions DefaultRawOptions() noexcept;
// Diagnostic sink. It is silent unless FFF_RAW_TRACE=1 is set in the
// environment, so shipping builds never write a trace file.
void RawTrace(const char* text) noexcept;
// Same rules the setter applies: supported size/version, enum ranges and the
// M1 colour/tone-curve subset.
bool ValidateRawOptions(const FFF3FPRawOptions& options) noexcept;
