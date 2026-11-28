#pragma once

#include "3FP/IO/IMediaSource.h"

#include <memory>
#include <string>

// Local and UNC file source. It is the P1 wrapper around the existing
// SharedFileInput fast path: the session no longer needs to branch on a path
// string once this object exists. Device namespaces and named pipes are
// rejected at Create time; UNC paths are accepted only when they identify a
// file, never a share root or a pipe/mailslot share.
namespace FFFPlayer::Io {

class FileMediaSource final : public IMediaSource {
public:
    static std::unique_ptr<FileMediaSource> Create(const std::wstring& path,
        const IoOptions& options, Result& result, std::string& error) noexcept;

    ~FileMediaSource() override;
    FileMediaSource(const FileMediaSource&) = delete;
    FileMediaSource& operator=(const FileMediaSource&) = delete;

    SourceCapabilities Capabilities() const noexcept override;
    std::int64_t Size() const noexcept override;
    Result Open(const IoOptions& options) noexcept override;
    Result Read(std::uint8_t* destination, std::uint32_t capacity,
        std::uint32_t& bytesRead, const IoOptions& options) noexcept override;
    Result Seek(std::int64_t offset, const IoOptions& options) noexcept override;
    void Close() noexcept override;

    bool IsUnc() const noexcept { return unc_; }
    const std::wstring& Path() const noexcept { return path_; }

private:
    FileMediaSource() = default;
    bool ReadOnce(std::uint8_t* destination, std::uint32_t capacity,
        std::uint32_t& bytesRead, std::uint32_t& error) noexcept;

    void* file_ = nullptr;
    std::wstring path_;
    std::int64_t size_ = -1;
    std::int64_t position_ = 0;
    bool unc_ = false;
    bool open_ = false;
};

} // namespace FFFPlayer::Io
