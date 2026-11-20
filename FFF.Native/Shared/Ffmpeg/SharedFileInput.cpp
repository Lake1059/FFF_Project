#include "pch.h"
#include "Shared/Ffmpeg/SharedFileInput.h"
#include "Shared/Ffmpeg/IocpMediaReader.h"

extern "C" {
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <thread>

void NetTrace(const char* format, ...) noexcept {
    wchar_t enabled[8]{};
    if (GetEnvironmentVariableW(L"FFF_NET_IO_TRACE", enabled, 8) == 0) return;
    char line[512]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    wchar_t file[1024]{};
    const auto length = GetEnvironmentVariableW(L"FFF_NET_IO_TRACE_FILE", file, 1024);
    if (length > 0 && length < 1024) {
        if (auto* stream = _wfopen(file, L"a")) {
            std::fprintf(stream, "%s\n", line);
            std::fclose(stream);
        }
    } else {
        std::fputs(line, stderr);
        std::fputc('\n', stderr);
    }
}

namespace {
constexpr int AvioBufferSize = 64 * 1024;
constexpr DWORD IoPollIntervalMs = 50;
constexpr DWORD OpenRetryDelayMs = 250;
constexpr DWORD NetworkOpenTimeoutMs = 15000;

bool UseIocpReader() noexcept {
    wchar_t value[16]{};
    const auto length = GetEnvironmentVariableW(L"FFF_NET_IO", value, 16);
    if (length == 0 || length >= 16) return true;
    return !(value[0] == L'0' || _wcsicmp(value, L"event") == 0);
}

bool ForceIocpReader() noexcept {
    wchar_t value[16]{};
    const auto length = GetEnvironmentVariableW(L"FFF_NET_IO", value, 16);
    return length > 0 && length < 16 && _wcsicmp(value, L"force") == 0;
}

std::wstring FromUtf8(const char* value) {
    if (value == nullptr || *value == '\0') return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value, -1, nullptr, 0);
    if (length <= 1) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
        result.data(), length);
    result.resize(static_cast<std::size_t>(length - 1));
    return result;
}

std::string Win32ErrorText(const DWORD error) {
    char* message = nullptr;
    const auto length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&message), 0, nullptr);
    std::string result = "Windows error " + std::to_string(error);
    if (length > 0 && message != nullptr) {
        result.assign(message, message + length);
        while (!result.empty() &&
            (result.back() == '\r' || result.back() == '\n' || result.back() == ' '))
            result.pop_back();
    }
    if (message != nullptr) LocalFree(message);
    return result;
}

bool IsTransientNetworkError(const DWORD error) noexcept {
    switch (error) {
    case ERROR_BAD_NETPATH:
    case ERROR_NETNAME_DELETED:
    case ERROR_NETWORK_BUSY:
    case ERROR_SEM_TIMEOUT:
    case ERROR_IO_DEVICE:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_CONNECTION_ABORTED:
    case ERROR_NOT_READY:
    case ERROR_REM_NOT_LIST:
    case ERROR_NETWORK_UNREACHABLE:
    case ERROR_HOST_UNREACHABLE:
        return true;
    default:
        return false;
    }
}

struct OpenJob {
    std::wstring path;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size{};
    bool sizeKnown = false;
    DWORD error = 0;
    HANDLE done = nullptr;
    std::atomic<bool> abandoned{false};

    ~OpenJob() {
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
            file = INVALID_HANDLE_VALUE;
        }
        if (done != nullptr) {
            CloseHandle(done);
            done = nullptr;
        }
    }
};

struct OpenedFile {
    HANDLE file = INVALID_HANDLE_VALUE;
    std::int64_t size = 0;
    bool sizeKnown = false;
    DWORD error = 0;
    bool timedOut = false;
};

OpenedFile OpenNetworkFile(const std::wstring& path, const ULONGLONG deadlineTick) noexcept {
    OpenedFile result{};
    struct TraceResult {
        const OpenedFile& result;
        ~TraceResult() {
            NetTrace("net-open error=%lu timedOut=%d sizeKnown=%d size=%lld",
                static_cast<unsigned long>(result.error), result.timedOut ? 1 : 0,
                result.sizeKnown ? 1 : 0, static_cast<long long>(result.size));
        }
    } traceResult{result};
    auto job = std::make_shared<OpenJob>();
    job->path = path;
    job->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (job->done == nullptr) {
        result.error = GetLastError();
        return result;
    }

    std::thread worker;
    try {
        worker = std::thread([job]() noexcept {
            for (int attempt = 0; attempt < 2; ++attempt) {
                job->file = CreateFileW(job->path.c_str(), GENERIC_READ,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
                if (job->file != INVALID_HANDLE_VALUE) break;
                job->error = GetLastError();
                if (!IsTransientNetworkError(job->error) || attempt == 1) break;
                Sleep(OpenRetryDelayMs * static_cast<DWORD>(attempt + 1));
            }
            if (job->file != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER size{};
                if (GetFileSizeEx(job->file, &size)) {
                    job->size = size;
                    job->sizeKnown = true;
                } else {
                    // Size is advisory for AVSEEK_SIZE. A failed metadata query
                    // must not turn a readable share into an unopenable one.
                    job->error = GetLastError();
                }
            }
            if (job->abandoned.load(std::memory_order_acquire)) {
                if (job->file != INVALID_HANDLE_VALUE) {
                    CloseHandle(job->file);
                    job->file = INVALID_HANDLE_VALUE;
                }
            }
            SetEvent(job->done);
        });
    } catch (...) {
        result.error = ERROR_NOT_ENOUGH_MEMORY;
        return result;
    }

    DWORD timeoutMs = NetworkOpenTimeoutMs;
    if (deadlineTick != 0) {
        const auto now = GetTickCount64();
        if (deadlineTick <= now) {
            job->abandoned.store(true, std::memory_order_release);
            worker.detach();
            result.timedOut = true;
            result.error = ERROR_TIMEOUT;
            return result;
        }
        timeoutMs = static_cast<DWORD>((std::min<ULONGLONG>)(timeoutMs, deadlineTick - now));
    }
    const auto wait = WaitForSingleObject(job->done, timeoutMs);
    if (wait != WAIT_OBJECT_0) {
        // A blocking CreateFileW cannot be timed out from the same thread. The
        // caller returns now and the worker owns the job until the stale open
        // finishes; it closes the handle if the caller abandoned it.
        job->abandoned.store(true, std::memory_order_release);
        worker.detach();
        result.timedOut = true;
        result.error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        return result;
    }

    worker.join();
    if (job->abandoned.load(std::memory_order_acquire)) {
        result.timedOut = true;
        result.error = ERROR_TIMEOUT;
        return result;
    }
    result.file = job->file;
    job->file = INVALID_HANDLE_VALUE;
    result.size = static_cast<std::int64_t>(job->size.QuadPart);
    result.sizeKnown = job->sizeKnown;
    result.error = job->error;
    return result;
}
} // namespace

SharedFileInput::~SharedFileInput() {
    CloseHandles();
    if (context_ != nullptr) {
        av_freep(&context_->buffer);
        avio_context_free(&context_);
        buffer_ = nullptr;
    } else if (buffer_ != nullptr) {
        av_free(buffer_);
        buffer_ = nullptr;
    }
    if (data_ != nullptr) {
        av_free(data_);
        data_ = nullptr;
    }
}

void SharedFileInput::CloseHandles() noexcept {
    // Stop the IOCP worker before closing the file it is reading.
    iocpReader_.reset();
    if (file_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(file_, nullptr);
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
    }
    if (readEvent_ != INVALID_HANDLE_VALUE) {
        CloseHandle(readEvent_);
        readEvent_ = INVALID_HANDLE_VALUE;
    }
}

static bool IsRemoteDrive(const std::wstring& path) noexcept {
    if (path.size() < 2 || path[1] != L':') return false;
    const wchar_t root[4] = {path[0], L':', L'\\', L'\0'};
    return GetDriveTypeW(root) == DRIVE_REMOTE;
}

bool SharedFileInput::IsNetworkPath(const std::wstring& path) noexcept {
    if (path.size() >= 2 &&
        ((path[0] == L'\\' && path[1] == L'\\') ||
         (path[0] == L'/' && path[1] == L'/'))) return true;
    // A mapped SMB drive is still a network media path even though it has a
    // drive letter. Classifying it here keeps it on the IOCP/overlapped path;
    // otherwise it silently falls back to the local synchronous read path.
    return IsRemoteDrive(path);
}

bool SharedFileInput::IsTransientNetworkError(const DWORD error) noexcept {
    return ::IsTransientNetworkError(error);
}

void SharedFileInput::SetNetworkWindowHint(const std::int64_t bitsPerSecond) noexcept {
    if (!iocpReader_) return;
    if (bitsPerSecond <= 0) {
        iocpReader_->SetTargetBytes(0);
        return;
    }
    // Keep roughly half a second of video in the prefetch window, plus one
    // slot for jitter. The reader clamps this to its 2..12 slot range.
    const auto bytesPerSecond = bitsPerSecond / 8;
    const auto targetBytes = bytesPerSecond / 2 + 1024 * 1024;
    iocpReader_->SetTargetBytes(targetBytes);
}

bool SharedFileInput::Interrupted(const AVIOInterruptCB& interrupt) noexcept {
    return interrupt.callback != nullptr && interrupt.callback(interrupt.opaque) != 0;
}

bool SharedFileInput::IsUnwrittenBlock(const std::uint8_t* buffer,
    const int bytes) const noexcept {
    // Incomplete Matroska allocations contain unwritten zero blocks. Do not let
    // the EBML resynchronizer scan gigabytes past the download frontier.
    return detectUnwrittenData_ && bytes >= 4096 &&
        std::all_of(buffer, buffer + bytes, [](const std::uint8_t value) { return value == 0; });
}

std::unique_ptr<SharedFileInput> SharedFileInput::Open(const char* pathUtf8,
    std::string& error, const ULONGLONG deadlineTick) noexcept {
    try {
        const auto path = FromUtf8(pathUtf8);
        if (path.empty()) {
            error = "The local path is not valid UTF-8.";
            return nullptr;
        }

        auto input = std::make_unique<SharedFileInput>();
        input->path_ = path;
        input->network_ = IsNetworkPath(path);
        NetTrace("open network=%d driveRemote=%d",
            input->network_ ? 1 : 0,
            IsRemoteDrive(path) ? 1 : 0);

        if (input->network_) {
            if (deadlineTick != 0 && deadlineTick <= GetTickCount64()) {
                error = "Network media open deadline expired.";
                return nullptr;
            }
            const auto opened = OpenNetworkFile(path, deadlineTick);
            if (opened.file == INVALID_HANDLE_VALUE) {
                error = opened.timedOut
                    ? "Network media open timed out; the share did not answer in time."
                    : "Could not open network media with shared access: " +
                        Win32ErrorText(opened.error);
                return nullptr;
            }
            input->file_ = opened.file;
            input->size_ = opened.size;
            input->sizeKnown_ = opened.sizeKnown;
        } else {
            input->file_ = CreateFileW(path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr);
            if (input->file_ == INVALID_HANDLE_VALUE) {
                error = "Could not open local media with shared access: " + Win32ErrorText(GetLastError());
                return nullptr;
            }
            LARGE_INTEGER size{};
            if (GetFileSizeEx(input->file_, &size)) {
                input->size_ = static_cast<std::int64_t>(size.QuadPart);
                input->sizeKnown_ = true;
            }
        }

        input->readEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (input->readEvent_ == nullptr) {
            error = "Could not create the media read event.";
            return nullptr;
        }

        if ((input->network_ || ForceIocpReader()) && UseIocpReader()) {
            std::string iocpError;
            input->iocpReader_ = IocpMediaReader::Create(
                input->file_, input->sizeKnown_ ? input->size_ : -1, iocpError);
            // IOCP is the preferred network path. If the completion port cannot
            // be initialized, keep the per-read overlapped fallback instead of
            // making the media unopenable.
        }

        input->buffer_ = static_cast<std::uint8_t*>(av_malloc(AvioBufferSize));
        if (input->buffer_ == nullptr) {
            error = "Could not allocate the media input buffer.";
            return nullptr;
        }

        input->context_ = avio_alloc_context(input->buffer_, AvioBufferSize, 0,
            input.get(), &SharedFileInput::Read, nullptr, &SharedFileInput::Seek);
        if (input->context_ == nullptr) {
            error = "Could not allocate the FFmpeg media input context.";
            return nullptr;
        }
        input->buffer_ = nullptr;
        input->context_->seekable = AVIO_SEEKABLE_NORMAL;
        return input;
    } catch (...) {
        error = "Could not prepare the shared media input.";
        return nullptr;
    }
}

std::unique_ptr<SharedFileInput> SharedFileInput::OpenMemory(const std::uint8_t* data,
    const std::size_t size, std::string& error) noexcept {
    try {
        if (data == nullptr || size == 0) {
            error = "The in-memory media is empty.";
            return nullptr;
        }
        auto input = std::make_unique<SharedFileInput>();
        input->data_ = static_cast<std::uint8_t*>(av_malloc(size));
        if (input->data_ == nullptr) {
            error = "Could not allocate the in-memory media buffer.";
            return nullptr;
        }
        std::memcpy(input->data_, data, size);
        input->buffer_ = static_cast<std::uint8_t*>(av_malloc(AvioBufferSize));
        if (input->buffer_ == nullptr) {
            error = "Could not allocate the media input buffer.";
            return nullptr;
        }
        input->context_ = avio_alloc_context(input->buffer_, AvioBufferSize, 0,
            input.get(), &SharedFileInput::Read, nullptr, &SharedFileInput::Seek);
        if (input->context_ == nullptr) {
            error = "Could not allocate the FFmpeg media input context.";
            return nullptr;
        }
        input->buffer_ = nullptr;
        input->size_ = static_cast<std::int64_t>(size);
        input->sizeKnown_ = true;
        input->context_->seekable = AVIO_SEEKABLE_NORMAL;
        return input;
    } catch (...) {
        error = "Could not prepare the in-memory media input.";
        return nullptr;
    }
}

int SharedFileInput::ReadFileOnce(std::uint8_t* buffer, const int bufferSize,
    const ULONGLONG deadlineTick) noexcept {
    OVERLAPPED overlapped{};
    overlapped.Offset = static_cast<DWORD>(static_cast<std::uint64_t>(position_) & 0xffffffffu);
    overlapped.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(position_) >> 32);
    overlapped.hEvent = readEvent_;

    DWORD bytesRead = 0;
    if (ReadFile(file_, buffer, static_cast<DWORD>(bufferSize), &bytesRead, &overlapped)) {
        position_ += static_cast<std::int64_t>(bytesRead);
        return bytesRead == 0 ? AVERROR_EOF : static_cast<int>(bytesRead);
    }
    lastError_ = GetLastError();
    if (lastError_ != ERROR_IO_PENDING) {
        if (lastError_ == ERROR_HANDLE_EOF) return AVERROR_EOF;
        if (lastError_ == ERROR_OPERATION_ABORTED) return AVERROR_EXIT;
        return AVERROR(EIO);
    }

    while (true) {
        if (Interrupted(interrupt_)) {
            CancelIoEx(file_, &overlapped);
            WaitForSingleObject(readEvent_, 1000);
            lastError_ = ERROR_OPERATION_ABORTED;
            return AVERROR_EXIT;
        }
        if (deadlineTick != 0 && GetTickCount64() >= deadlineTick) {
            CancelIoEx(file_, &overlapped);
            WaitForSingleObject(readEvent_, 1000);
            lastError_ = ERROR_TIMEOUT;
            return AVERROR(ETIMEDOUT);
        }
        const auto wait = WaitForSingleObject(readEvent_, IoPollIntervalMs);
        if (wait == WAIT_OBJECT_0) {
            if (GetOverlappedResult(file_, &overlapped, &bytesRead, FALSE)) {
                position_ += static_cast<std::int64_t>(bytesRead);
                return bytesRead == 0 ? AVERROR_EOF : static_cast<int>(bytesRead);
            }
            lastError_ = GetLastError();
            if (lastError_ == ERROR_HANDLE_EOF) return AVERROR_EOF;
            if (lastError_ == ERROR_OPERATION_ABORTED) return AVERROR_EXIT;
            return AVERROR(EIO);
        }
        if (wait == WAIT_FAILED) {
            lastError_ = GetLastError();
            return AVERROR(EIO);
        }
    }
}

int SharedFileInput::Read(void* opaque, std::uint8_t* buffer,
    const int bufferSize) noexcept {
    auto* input = static_cast<SharedFileInput*>(opaque);
    if (input == nullptr || buffer == nullptr || bufferSize < 0)
        return AVERROR(EINVAL);
    if (Interrupted(input->interrupt_)) return AVERROR_EXIT;
    // In-memory media: copy what is left of the buffer and advance the cursor.
    if (input->file_ == INVALID_HANDLE_VALUE) {
        if (input->data_ == nullptr) return AVERROR(EINVAL);
        if (bufferSize == 0 || input->position_ >= input->size_) return AVERROR_EOF;
        const auto available = static_cast<int>(
            (std::min)(static_cast<std::int64_t>(bufferSize), input->size_ - input->position_));
        std::memcpy(buffer, input->data_ + input->position_,
            static_cast<std::size_t>(available));
        input->position_ += available;
        return available;
    }
    if (bufferSize == 0) return 0;
    if (input->iocpReader_ != nullptr) {
        int result = AVERROR(EIO);
        DWORD error = 0;
        for (int attempt = 0; attempt <= SharedFileInput::NetworkReadRetryCount; ++attempt) {
            if (Interrupted(input->interrupt_)) return AVERROR_EXIT;
            result = input->iocpReader_->Read(buffer, bufferSize, input->interrupt_,
                SharedFileInput::NetworkReadTimeoutMs, error);
            input->lastError_ = error;
            if (result >= 0) {
                input->position_ += result;
                if (input->IsUnwrittenBlock(buffer, result)) {
                    input->position_ -= result;
                    DWORD resetError = 0;
                    if (!input->iocpReader_->Seek(input->position_, 1000, resetError))
                        return AVERROR(EIO);
                    return AVERROR(EAGAIN);
                }
                return result;
            }
            if (result == AVERROR_EXIT || result == AVERROR_EOF)
                return result;
            const bool retryable = result == AVERROR(ETIMEDOUT) ||
                IsTransientNetworkError(error);
            if (!retryable || attempt == SharedFileInput::NetworkReadRetryCount)
                return result;
            DWORD resetError = 0;
            if (!input->iocpReader_->Seek(input->position_, 1000, resetError))
                return result;
            Sleep(static_cast<DWORD>(125 * (attempt + 1)));
        }
        return result;
    }
    if (input->readEvent_ == INVALID_HANDLE_VALUE) return AVERROR(EIO);

    const auto deadline = input->network_
        ? GetTickCount64() + SharedFileInput::NetworkReadTimeoutMs
        : 0;
    int result = AVERROR(EIO);
    for (int attempt = 0; attempt <= SharedFileInput::NetworkReadRetryCount; ++attempt) {
        if (Interrupted(input->interrupt_)) return AVERROR_EXIT;
        result = input->ReadFileOnce(buffer, bufferSize, deadline);
        if (result >= 0) {
            if (input->IsUnwrittenBlock(buffer, result)) {
                input->position_ -= result;
                return AVERROR(EAGAIN);
            }
            return result;
        }
        if (result == AVERROR_EXIT || result == AVERROR_EOF)
            return result;
        if (!input->network_ || attempt == SharedFileInput::NetworkReadRetryCount)
            return result;
        if (!IsTransientNetworkError(input->lastError_)) return result;
        if (deadline != 0 && GetTickCount64() >= deadline) return result;
        Sleep(static_cast<DWORD>(125 * (attempt + 1)));
    }
    return result;
}

bool SharedFileInput::HasWriter() const noexcept {
    if (file_ == INVALID_HANDLE_VALUE || path_.empty()) return false;
    const auto probe = CreateFileW(path_.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (probe == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_SHARING_VIOLATION;
    CloseHandle(probe);
    return false;
}

std::int64_t SharedFileInput::Seek(void* opaque, const std::int64_t offset,
    const int whence) noexcept {
    auto* input = static_cast<SharedFileInput*>(opaque);
    if (input == nullptr) return AVERROR(EINVAL);
    const auto origin = whence & ~AVSEEK_FORCE;
    if (origin == AVSEEK_SIZE) {
        if (input->sizeKnown_) return input->size_;
        return AVERROR(EIO);
    }
    // In-memory media: the cursor is all there is to move.
    if (input->file_ == INVALID_HANDLE_VALUE) {
        if (input->data_ == nullptr) return AVERROR(EINVAL);
        std::int64_t target = 0;
        switch (origin) {
        case SEEK_SET: target = offset; break;
        case SEEK_CUR: target = input->position_ + offset; break;
        case SEEK_END: target = input->size_ + offset; break;
        default: return AVERROR(EINVAL);
        }
        if (target < 0) return AVERROR(EINVAL);
        if (target > input->size_) target = input->size_;
        input->position_ = target;
        return target;
    }

    std::int64_t target = 0;
    switch (origin) {
    case SEEK_SET: target = offset; break;
    case SEEK_CUR: target = input->position_ + offset; break;
    case SEEK_END:
        if (!input->sizeKnown_) return AVERROR(EIO);
        target = input->size_ + offset;
        break;
    default:
        return AVERROR(EINVAL);
    }
    if (target < 0) return AVERROR(EINVAL);
    if (input->sizeKnown_ && target > input->size_) target = input->size_;
    if (input->iocpReader_ != nullptr) {
        DWORD error = 0;
        if (!input->iocpReader_->Seek(target, 3000, error)) {
            input->lastError_ = error;
            return error == ERROR_TIMEOUT ? AVERROR(ETIMEDOUT) : AVERROR(EIO);
        }
        input->position_ = target;
        return target;
    }
    // The fallback path uses overlapped I/O and every read carries an explicit
    // offset, so a seek is only a cursor update. This removes the synchronous
    // SetFilePointerEx call that could block on an SMB share.
    input->position_ = target;
    return target;
}
