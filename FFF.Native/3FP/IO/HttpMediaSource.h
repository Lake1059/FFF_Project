#pragma once

#include "3FP/IO/IMediaSource.h"
#include "Shared/Ffmpeg/HttpMediaReader.h"

#include <memory>
#include <string>

// IMediaSource adapter for the WinHTTP Range byte source. The demuxer-facing
// class stays Windows-free; only this adapter and HttpMediaReader know about
// HTTP status, ETag and Content-Range validation.
namespace FFFPlayer::Io {

class HttpMediaSource final : public IMediaSource {
public:
    static std::unique_ptr<HttpMediaSource> Create(const std::string& urlUtf8,
        const IoOptions& options, Result& result, std::string& error) noexcept;

    ~HttpMediaSource() override = default;
    HttpMediaSource(const HttpMediaSource&) = delete;
    HttpMediaSource& operator=(const HttpMediaSource&) = delete;

    SourceCapabilities Capabilities() const noexcept override;
    std::int64_t Size() const noexcept override;
    Result Open(const IoOptions& options) noexcept override;
    Result Read(std::uint8_t* destination, std::uint32_t capacity,
        std::uint32_t& bytesRead, const IoOptions& options) noexcept override;
    Result Seek(std::int64_t offset, const IoOptions& options) noexcept override;
    void Close() noexcept override;

    bool IsEof() const noexcept { return reader_ && reader_->IsEof(); }
    std::uint32_t RttMs() const noexcept { return reader_ ? reader_->RttMs() : 0; }
    std::uint64_t GoodputBps() const noexcept { return reader_ ? reader_->GoodputBps() : 0; }

private:
    HttpMediaSource() = default;
    std::unique_ptr<HttpMediaReader> reader_;
    AVIOInterruptCB interrupt_{};
    std::int64_t position_ = 0;
};

} // namespace FFFPlayer::Io
