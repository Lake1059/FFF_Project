#include "pch.h"
#include "3FP/IO/FileMediaSource.h"

#include <windows.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace FFFPlayer::Io {
namespace {
bool IsUncPath(const std::wstring& path) noexcept {
    return path.size() > 2 && path[0] == L'\\' && path[1] == L'\\';
}

bool ValidateUncPath(const std::wstring& path, std::string& error) noexcept {
    if (!IsUncPath(path)) return true;
    if (path.size() >= 4 && (path[2] == L'?' || path[2] == L'.')) {
        error = "Device and extended-length paths are not media files.";
        return false;
    }
    const auto serverEnd = path.find(L'\\', 2);
    if (serverEnd == std::wstring::npos || serverEnd == 2) {
        error = "The UNC path has no server.";
        return false;
    }
    const auto shareEnd = path.find(L'\\', serverEnd + 1);
    if (shareEnd == std::wstring::npos || shareEnd == serverEnd + 1) {
        error = "The UNC path identifies a share root, not a file.";
        return false;
    }
    const std::wstring share = path.substr(serverEnd + 1, shareEnd - serverEnd - 1);
    if (_wcsicmp(share.c_str(), L"pipe") == 0 || _wcsicmp(share.c_str(), L"mailslot") == 0 ||
        _wcsicmp(share.c_str(), L"IPC$") == 0) {
        error = "Named-pipe and mailslot shares are not media files.";
        return false;
    }
    if (shareEnd + 1 >= path.size()) {
        error = "The UNC path has no file name.";
        return false;
    }
    return true;
}

bool Canonicalize(const std::wstring& path, std::wstring& full, std::string& error) noexcept {
    if (path.empty()) {
        error = "A non-empty media path is required.";
        return false;
    }
    std::vector<wchar_t> buffer(32768);
    const auto length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()),
        buffer.data(), nullptr);
    if (length == 0 || length >= buffer.size()) {
        error = "The media path is invalid.";
        return false;
    }
    full.assign(buffer.data(), length);
    return ValidateUncPath(full, error);
}

Result MapOpenError(const DWORD error) noexcept {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
        return MakeFatal(Error::NotFound);
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
        return MakeFatal(Error::AuthRequired);
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        return MakeRetryable(Error::Internal);
    default:
        return MakeFatal(Error::Internal);
    }
}

Result MapReadError(const DWORD error) noexcept {
    switch (error) {
    case ERROR_HANDLE_EOF:
        return MakeEof();
    case ERROR_TIMEOUT:
    case ERROR_OPERATION_ABORTED:
        return MakeRetryable(Error::Timeout);
    case ERROR_NETNAME_DELETED:
    case ERROR_BAD_NETPATH:
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_CONNECTION_ABORTED:
        return MakeRetryable(Error::Disconnected);
    case ERROR_ACCESS_DENIED:
        return MakeFatal(Error::AuthRequired);
    default:
        return MakeFatal(Error::Internal);
    }
}
} // namespace

FileMediaSource::~FileMediaSource() { Close(); }

std::unique_ptr<FileMediaSource> FileMediaSource::Create(const std::wstring& path,
    const IoOptions&, Result& result, std::string& error) noexcept {
    std::wstring full;
    if (!Canonicalize(path, full, error)) {
        result = MakeFatal(IsUncPath(path) ? Error::NotFound : Error::Internal);
        return nullptr;
    }
    auto source = std::unique_ptr<FileMediaSource>(new FileMediaSource());
    source->path_ = std::move(full);
    source->unc_ = IsUncPath(source->path_);
    source->file_ = CreateFileW(source->path_.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (source->file_ == INVALID_HANDLE_VALUE) {
        const DWORD openError = GetLastError();
        error = "Could not open the media file (Win32 error " + std::to_string(openError) + ").";
        result = MapOpenError(openError);
        return nullptr;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(source->file_, &size) || size.QuadPart < 0) {
        error = "Could not read the media file size.";
        CloseHandle(source->file_);
        source->file_ = INVALID_HANDLE_VALUE;
        result = MakeFatal(Error::Internal);
        return nullptr;
    }
    source->size_ = static_cast<std::int64_t>(size.QuadPart);
    source->position_ = 0;
    source->open_ = true;
    result = MakeOk(false);
    return source;
}

SourceCapabilities FileMediaSource::Capabilities() const noexcept {
    SourceCapabilities capabilities;
    capabilities.domain = SourceDomain::ByteRandomAccess;
    capabilities.features = SourceFeature::ByteExact | SourceFeature::RandomAccess;
    if (unc_) capabilities.features = capabilities.features | SourceFeature::Reconnectable;
    capabilities.sizeBytes = size_;
    return capabilities;
}

std::int64_t FileMediaSource::Size() const noexcept { return size_; }

Result FileMediaSource::Open(const IoOptions&) noexcept {
    return open_ ? MakeOk(false) : MakeFatal(Error::Internal);
}

Result FileMediaSource::Read(std::uint8_t* destination, const std::uint32_t capacity,
    std::uint32_t& bytesRead, const IoOptions&) noexcept {
    bytesRead = 0;
    if (!open_ || file_ == nullptr || file_ == INVALID_HANDLE_VALUE)
        return MakeFatal(Error::Internal);
    if (destination == nullptr && capacity != 0) return MakeFatal(Error::Internal);
    if (position_ >= size_) return MakeEof();
    if (capacity == 0) return MakeWouldBlock();
    const auto remaining = size_ - position_;
    const auto requested = static_cast<std::uint32_t>(
        (std::min<std::int64_t>)(remaining, static_cast<std::int64_t>(capacity)));
    std::uint32_t error = 0;
    if (!ReadOnce(destination, requested, bytesRead, error)) return MapReadError(error);
    if (bytesRead == 0) {
        // A local file that was truncated under an open handle is not EOF at
        // the old size: the ledger must see an integrity failure, not a clean end.
        return MakeFatal(Error::IntegrityMismatch);
    }
    position_ += bytesRead;
    return MakeOk(true);
}

bool FileMediaSource::ReadOnce(std::uint8_t* destination, const std::uint32_t capacity,
    std::uint32_t& bytesRead, std::uint32_t& error) noexcept {
    DWORD read = 0;
    if (ReadFile(file_, destination, capacity, &read, nullptr)) {
        bytesRead = read;
        return true;
    }
    error = GetLastError();
    bytesRead = 0;
    return false;
}

Result FileMediaSource::Seek(const std::int64_t offset, const IoOptions&) noexcept {
    if (!open_ || file_ == nullptr || file_ == INVALID_HANDLE_VALUE)
        return MakeFatal(Error::Internal);
    if (offset < 0) return MakeFatal(Error::Internal);
    LARGE_INTEGER target{};
    target.QuadPart = offset;
    if (!SetFilePointerEx(file_, target, nullptr, FILE_BEGIN))
        return MakeFatal(Error::Internal);
    position_ = offset;
    return MakeOk(false);
}

void FileMediaSource::Close() noexcept {
    if (file_ != nullptr && file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
    file_ = nullptr;
    open_ = false;
}

} // namespace FFFPlayer::Io
