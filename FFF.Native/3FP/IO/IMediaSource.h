#pragma once

#include "IoOptions.h"
#include "SourceCapabilities.h"

#include <cstdint>

// The only interface a demuxer needs from a byte source. Implementations may be
// local file, UNC, HTTP Range, WebDAV, memory, or a segment reader; the session
// must not branch on a URL string once this object exists.
namespace FFFPlayer::Io {

class IMediaSource {
public:
    virtual ~IMediaSource() = default;

    virtual SourceCapabilities Capabilities() const noexcept = 0;
    virtual std::int64_t Size() const noexcept = 0;

    virtual Result Open(const IoOptions& options) noexcept = 0;
    // `bytesRead` must be zero on WouldBlock/Retryable/Fatal, and >0 on Ok unless
    // the source is at an authoritative Eof.
    virtual Result Read(std::uint8_t* destination, std::uint32_t capacity,
        std::uint32_t& bytesRead, const IoOptions& options) noexcept = 0;
    virtual Result Seek(std::int64_t offset, const IoOptions& options) noexcept = 0;
    virtual void Close() noexcept = 0;
};

} // namespace FFFPlayer::Io
