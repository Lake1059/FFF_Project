#include "pch.h"
#include "3FP/IO/HttpMediaSource.h"

extern "C" {
#include <libavutil/error.h>
}

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>

namespace FFFPlayer::Io {
namespace {
Result MapReadResult(const int result, const DWORD lastError) noexcept {
    if (result >= 0) return MakeOk(true);
    if (result == AVERROR_EOF) return MakeEof();
    if (result == AVERROR(ETIMEDOUT) || lastError == ERROR_WINHTTP_TIMEOUT)
        return MakeRetryable(Error::Timeout);
    if (result == AVERROR_EXIT) return MakeWouldBlock();
    switch (lastError) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_MORE_DATA:
        return MakeRetryable(Error::Disconnected);
    case ERROR_INVALID_DATA:
        return MakeFatal(Error::IntegrityMismatch);
    default:
        return MakeFatal(Error::Internal);
    }
}
} // namespace

std::unique_ptr<HttpMediaSource> HttpMediaSource::Create(const std::string& urlUtf8,
    const IoOptions& options, Result& result, std::string& error) noexcept {
    auto source = std::unique_ptr<HttpMediaSource>(new HttpMediaSource());
    const auto deadline = GetTickCount64() +
        static_cast<ULONGLONG>(std::max<std::uint32_t>(options.openDeadlineMs, 1000));
    source->reader_ = HttpMediaReader::Open(urlUtf8, deadline, &source->interrupt_, error);
    if (source->reader_ == nullptr) {
        result = MakeFatal(Error::NotFound);
        return nullptr;
    }
    result = MakeOk(false);
    return source;
}

SourceCapabilities HttpMediaSource::Capabilities() const noexcept {
    SourceCapabilities capabilities;
    capabilities.domain = SourceDomain::ByteRandomAccess;
    // A 200-only origin is still byte-exact for sequential playback, but it
    // cannot serve a jump: do not advertise RandomAccess/RangeRequests unless
    // the first response was a validated 206. The session uses this to answer
    // Seek with NotSupported instead of failing after Ready.
    capabilities.features = SourceFeature::ByteExact | SourceFeature::Reconnectable;
    if (reader_ && reader_->Seekable())
        capabilities.features = capabilities.features |
            SourceFeature::RandomAccess | SourceFeature::RangeRequests;
    capabilities.sizeBytes = Size();
    return capabilities;
}

std::int64_t HttpMediaSource::Size() const noexcept {
    return reader_ ? reader_->Size() : -1;
}

Result HttpMediaSource::Open(const IoOptions&) noexcept {
    return reader_ ? MakeOk(false) : MakeFatal(Error::Internal);
}

Result HttpMediaSource::Read(std::uint8_t* destination, const std::uint32_t capacity,
    std::uint32_t& bytesRead, const IoOptions& options) noexcept {
    bytesRead = 0;
    if (reader_ == nullptr) return MakeFatal(Error::Internal);
    if (capacity == 0) return MakeWouldBlock();
    // A Range-backed origin can resume at the exact byte after a dropped
    // connection. Retry the same position with a fresh request instead of
    // surfacing one transient disconnect as EOF/session failure. Without a
    // validated 206 there is no resumable coordinate, so keep the failure.
    for (int attempt = 0; attempt <= 3; ++attempt) {
        const auto result = reader_->Read(destination, static_cast<int>(capacity),
            options.readIdleTimeoutMs, interrupt_, position_);
        if (result > 0) {
            bytesRead = static_cast<std::uint32_t>(result);
            position_ += result;
            return MakeOk(true);
        }
        const auto mapped = MapReadResult(result, reader_->LastError());
        if (result == AVERROR_EXIT) return MakeWouldBlock();
        // A valid 206 may end before the resource does (origin-side short
        // range). EOF is then a range boundary, not the media end; resume at
        // the exact next byte with a new Range request.
        if (result == AVERROR_EOF && reader_->Seekable() && reader_->SizeKnown() &&
            position_ < reader_->Size() && attempt < 3) {
            if (interrupt_.callback != nullptr && interrupt_.callback(interrupt_.opaque) != 0)
                return MakeWouldBlock();
            std::string error;
            const DWORD seekBudget = options.seekDeadlineMs == 0
                ? 3000 : options.seekDeadlineMs;
            if (!reader_->Seek(position_, seekBudget, interrupt_, error))
                return MapReadResult(AVERROR(EIO), reader_->LastError());
            continue;
        }
        if (mapped.status != Status::Retryable || attempt == 3 || !reader_->Seekable())
            return mapped.status == Status::Ok ? MakeFatal(Error::Internal) : mapped;
        if (interrupt_.callback != nullptr && interrupt_.callback(interrupt_.opaque) != 0)
            return MakeWouldBlock();
        Sleep(static_cast<DWORD>(50 * (attempt + 1)));
        if (interrupt_.callback != nullptr && interrupt_.callback(interrupt_.opaque) != 0)
            return MakeWouldBlock();
        std::string error;
        const DWORD seekBudget = options.seekDeadlineMs == 0
            ? 3000 : options.seekDeadlineMs;
        if (!reader_->Seek(position_, seekBudget, interrupt_, error))
            return MapReadResult(AVERROR(EIO), reader_->LastError());
    }
    return MakeRetryable(Error::Disconnected);
}

Result HttpMediaSource::Seek(const std::int64_t offset, const IoOptions& options) noexcept {
    if (reader_ == nullptr) return MakeFatal(Error::Internal);
    if (offset < 0) return MakeFatal(Error::Internal);
    if (reader_->SizeKnown() && offset > reader_->Size())
        return MakeFatal(Error::RangeUnsupported);
    if (!reader_->Seekable() && offset != position_)
        return MakeFatal(Error::RangeUnsupported);
    std::string error;
    if (!reader_->Seek(offset, options.seekDeadlineMs, interrupt_, error)) {
        const auto mapped = MapReadResult(AVERROR(EIO), reader_->LastError());
        return mapped.status == Status::Ok ? MakeFatal(Error::Internal) : mapped;
    }
    position_ = offset;
    return MakeOk(false);
}

void HttpMediaSource::Close() noexcept { reader_.reset(); }

} // namespace FFFPlayer::Io
