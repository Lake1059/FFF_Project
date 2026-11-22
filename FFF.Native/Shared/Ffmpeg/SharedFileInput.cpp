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
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

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
std::atomic<std::int64_t> EventResidentBytes{0};
std::atomic<int> EventRequestCount{0};
std::atomic<int> OpenJobCount{0};

int IoEnvironmentInt(const wchar_t* name, const int fallback,
    const int minimum, const int maximum) noexcept {
    wchar_t text[32]{};
    const auto length = GetEnvironmentVariableW(name, text, 32);
    if (length == 0 || length >= 32) return fallback;
    wchar_t* end = nullptr;
    const auto value = wcstol(text, &end, 10);
    return end != text && *end == L'\0' && value >= minimum && value <= maximum
        ? static_cast<int>(value) : fallback;
}

bool ReserveEventRequest(const int bytes) noexcept {
    constexpr int MaximumRequests = 256;
    auto count = EventRequestCount.load(std::memory_order_relaxed);
    while (count < MaximumRequests) {
        if (EventRequestCount.compare_exchange_weak(count, count + 1,
            std::memory_order_acq_rel, std::memory_order_relaxed)) break;
    }
    if (count >= MaximumRequests) return false;
    static const auto budget = static_cast<std::int64_t>(IoEnvironmentInt(
        L"FFF_EVENT_IO_BUDGET_MB", 32, 4, 1024)) * 1024 * 1024;
    auto current = EventResidentBytes.load(std::memory_order_relaxed);
    while (current <= budget - bytes) {
        if (EventResidentBytes.compare_exchange_weak(current, current + bytes,
            std::memory_order_acq_rel, std::memory_order_relaxed)) return true;
    }
    EventRequestCount.fetch_sub(1, std::memory_order_acq_rel);
    return false;
}
}

struct SharedFileInputEventReadRequest {
    OVERLAPPED ov{};
    HANDLE event = nullptr;
    std::uint8_t* data = nullptr;
    int capacity = 0;
    bool budgetCharged = false;
    SharedFileInputEventReadRequest* next = nullptr;

    ~SharedFileInputEventReadRequest() {
        if (event != nullptr) CloseHandle(event);
        std::free(data);
        if (budgetCharged) {
            EventResidentBytes.fetch_sub(capacity, std::memory_order_acq_rel);
            EventRequestCount.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
};

namespace {
constexpr int AvioBufferSize = 64 * 1024;
constexpr DWORD IoPollIntervalMs = 50;
constexpr DWORD CancelGraceMs = 250;
constexpr DWORD OpenRetryDelayMs = 250;
constexpr DWORD NetworkOpenTimeoutMs = 15000;

// A cancellation that cannot observe completion within CancelGraceMs hands
// the request to this list. The request owns its OVERLAPPED and staging
// buffer, so a late kernel completion can never touch a dead stack frame or
// a reused AVIO buffer. The reaper frees it only after the event signals.
std::mutex AbandonedEventsMutex;
SharedFileInputEventReadRequest* AbandonedEvents = nullptr;
std::once_flag AbandonedEventsStart;

void AbandonedEventsLoop() noexcept {
    while (true) {
        SharedFileInputEventReadRequest* ready = nullptr;
        {
            std::lock_guard lock(AbandonedEventsMutex);
            auto** link = &AbandonedEvents;
            while (*link != nullptr) {
                auto* request = *link;
                if (WaitForSingleObject(request->event, 0) == WAIT_OBJECT_0) {
                    *link = request->next;
                    request->next = ready;
                    ready = request;
                } else {
                    link = &request->next;
                }
            }
        }
        while (ready != nullptr) {
            auto* request = ready;
            ready = ready->next;
            delete request;
        }
        Sleep(500);
    }
}

void AbandonEventRequest(
    std::unique_ptr<SharedFileInputEventReadRequest> request) noexcept {
    if (request == nullptr) return;
    // The intrusive handoff never allocates while the OS still owns a request.
    // Active and abandoned requests share the same byte/count admission budget.
    {
        std::lock_guard lock(AbandonedEventsMutex);
        request->next = AbandonedEvents;
        AbandonedEvents = request.release();
    }
    try {
        std::call_once(AbandonedEventsStart, [] {
            std::thread(AbandonedEventsLoop).detach();
        });
    } catch (...) {
        // Keep the bounded requests alive; call_once can retry on the next handoff.
    }
}

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

bool AdaptiveReadAheadEnabled() noexcept {
    wchar_t value[16]{};
    const auto length = GetEnvironmentVariableW(L"FFF_NET_IO_ADAPTIVE", value, 16);
    if (length == 0 || length >= 16) return true;
    return !(value[0] == L'0' || _wcsicmp(value, L"off") == 0);
}

int AvioBufferBytes() noexcept {
    wchar_t value[32]{};
    const auto length = GetEnvironmentVariableW(L"FFF_NET_IO_AVIO", value, 32);
    if (length == 0 || length >= 32) return AvioBufferSize;
    wchar_t* end = nullptr;
    const auto bytes = wcstol(value, &end, 10);
    if (end == value || *end != L'\0' || bytes < AvioBufferSize ||
        bytes > 1024 * 1024) return AvioBufferSize;
    return static_cast<int>(bytes);
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
    DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE file = INVALID_HANDLE_VALUE;
    LARGE_INTEGER size{};
    bool sizeKnown = false;
    DWORD error = 0;
    HANDLE done = nullptr;
    std::atomic<bool> abandoned{false};
    bool budgetCharged = false;

    ~OpenJob() {
        if (budgetCharged) OpenJobCount.fetch_sub(1, std::memory_order_acq_rel);
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

OpenedFile OpenNetworkFile(const std::wstring& path, const ULONGLONG deadlineTick,
    const AVIOInterruptCB* interrupt,
    const DWORD shareMode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE) noexcept {
    OpenedFile result{};
    struct TraceResult {
        const OpenedFile& value;
        ~TraceResult() {
            NetTrace("net-open error=%lu timedOut=%d sizeKnown=%d size=%lld jobs=%d",
                static_cast<unsigned long>(value.error), value.timedOut ? 1 : 0,
                value.sizeKnown ? 1 : 0, static_cast<long long>(value.size),
                OpenJobCount.load(std::memory_order_relaxed));
        }
    } traceResult{result};
    const auto interrupted = [interrupt]() noexcept {
        return interrupt != nullptr && interrupt->callback != nullptr &&
            interrupt->callback(interrupt->opaque) != 0;
    };
    const auto started = GetTickCount64();
    const auto deadline = deadlineTick == 0 ? started + NetworkOpenTimeoutMs
        : (std::min)(started + NetworkOpenTimeoutMs, deadlineTick);
    const bool cancelledBeforeOpen = interrupted();
    if (cancelledBeforeOpen || started >= deadline) {
        result.error = cancelledBeforeOpen ? ERROR_OPERATION_ABORTED : ERROR_TIMEOUT;
        result.timedOut = result.error == ERROR_TIMEOUT;
        return result;
    }

    std::shared_ptr<OpenJob> job;
    try {
        job = std::make_shared<OpenJob>();
        job->path = path;
        job->shareMode = shareMode;
    } catch (...) {
        result.error = ERROR_NOT_ENOUGH_MEMORY;
        return result;
    }
    static const int maximumJobs = IoEnvironmentInt(L"FFF_NET_IO_OPEN_JOBS", 8, 1, 64);
    auto count = OpenJobCount.load(std::memory_order_relaxed);
    while (count < maximumJobs) {
        if (OpenJobCount.compare_exchange_weak(count, count + 1,
            std::memory_order_acq_rel, std::memory_order_relaxed)) break;
    }
    if (count >= maximumJobs) {
        result.error = ERROR_NOT_ENOUGH_QUOTA;
        return result;
    }
    job->budgetCharged = true;
    job->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (job->done == nullptr) {
        result.error = GetLastError();
        return result;
    }

    std::thread worker;
    try {
        worker = std::thread([job, deadline]() noexcept {
            for (int attempt = 0; attempt < 2; ++attempt) {
                if (job->abandoned.load(std::memory_order_acquire)) {
                    job->error = ERROR_OPERATION_ABORTED;
                    break;
                }
                if (GetTickCount64() >= deadline) {
                    job->error = ERROR_TIMEOUT;
                    break;
                }
                job->file = CreateFileW(job->path.c_str(), GENERIC_READ,
                    job->shareMode, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED |
                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (job->file != INVALID_HANDLE_VALUE) break;
                job->error = GetLastError();
                if (!IsTransientNetworkError(job->error) || attempt == 1) break;
                Sleep(OpenRetryDelayMs);
            }
            if (job->file != INVALID_HANDLE_VALUE) {
                // Validate the opened object inside the cancellable job. String
                // classification alone also accepts remote named-pipe UNC paths.
                BY_HANDLE_FILE_INFORMATION information{};
                DWORD validationError = ERROR_SUCCESS;
                if (GetFileType(job->file) != FILE_TYPE_DISK)
                    validationError = ERROR_INVALID_HANDLE;
                else if (!GetFileInformationByHandle(job->file, &information))
                    validationError = GetLastError();
                else if ((information.dwFileAttributes &
                    (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
                    validationError = ERROR_ACCESS_DENIED;
                if (validationError != ERROR_SUCCESS) {
                    job->error = validationError;
                    CloseHandle(job->file);
                    job->file = INVALID_HANDLE_VALUE;
                } else {
                    LARGE_INTEGER size{};
                    if (GetFileSizeEx(job->file, &size)) {
                        job->size = size;
                        job->sizeKnown = true;
                    }
                    job->error = ERROR_SUCCESS;
                }
            }
            if (job->abandoned.load(std::memory_order_acquire) &&
                job->file != INVALID_HANDLE_VALUE) {
                CloseHandle(job->file);
                job->file = INVALID_HANDLE_VALUE;
            }
            SetEvent(job->done);
        });
    } catch (...) {
        result.error = ERROR_NOT_ENOUGH_MEMORY;
        return result;
    }

    for (;;) {
        const auto now = GetTickCount64();
        const bool cancelled = interrupted();
        if (cancelled || now >= deadline) {
            result.error = cancelled ? ERROR_OPERATION_ABORTED : ERROR_TIMEOUT;
            result.timedOut = result.error == ERROR_TIMEOUT;
            job->abandoned.store(true, std::memory_order_release);
            // Best effort cancellation of CreateFile/metadata calls. The bounded
            // job retains its ownership if the redirector cannot cancel yet.
            CancelSynchronousIo(worker.native_handle());
            worker.detach();
            return result;
        }
        const auto wait = WaitForSingleObject(job->done,
            static_cast<DWORD>((std::min<ULONGLONG>)(IoPollIntervalMs, deadline - now)));
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_FAILED) {
            result.error = GetLastError();
            job->abandoned.store(true, std::memory_order_release);
            CancelSynchronousIo(worker.native_handle());
            worker.detach();
            return result;
        }
    }
    worker.join();
    if (interrupted()) {
        result.error = ERROR_OPERATION_ABORTED;
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
    // ReadFileOnce never leaves a request in eventRead_ while still pending:
    // an uncompleted cancellation is moved to the abandoned reaper instead.
    eventRead_.reset();
    if (file_ != INVALID_HANDLE_VALUE) {
        CancelIoEx(file_, nullptr);
        CloseHandle(file_);
        file_ = INVALID_HANDLE_VALUE;
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
    // Pass the actual bitrate, not only the derived byte count: the adaptive
    // window needs it to translate a high-latency coverage target into bytes.
    iocpReader_->SetStreamBitRate(bitsPerSecond > 0 ? bitsPerSecond : 0);
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
    std::string& error, const ULONGLONG deadlineTick, const bool probe,
    const AVIOInterruptCB* interrupt) noexcept {
    try {
        const auto path = FromUtf8(pathUtf8);
        if (path.empty()) {
            error = "The local path is not valid UTF-8.";
            return nullptr;
        }

        if ((interrupt != nullptr && Interrupted(*interrupt)) ||
            (deadlineTick != 0 && GetTickCount64() >= deadlineTick)) {
            error = "Media input open was cancelled or its deadline expired.";
            return nullptr;
        }
        auto input = std::make_unique<SharedFileInput>();
        if (interrupt != nullptr) input->interrupt_ = *interrupt;
        input->path_ = path;
        input->network_ = IsNetworkPath(path);
        NetTrace("open network=%d driveRemote=%d probe=%d",
            input->network_ ? 1 : 0,
            IsRemoteDrive(path) ? 1 : 0,
            probe ? 1 : 0);

        if (input->network_) {
            if (deadlineTick != 0 && deadlineTick <= GetTickCount64()) {
                error = "Network media open deadline expired.";
                return nullptr;
            }
            const auto opened = OpenNetworkFile(path, deadlineTick, interrupt);
            if (opened.file == INVALID_HANDLE_VALUE) {
                error = opened.error == ERROR_OPERATION_ABORTED
                    ? "Network media open was cancelled."
                    : opened.timedOut
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

        // The event fallback allocates its event and staging buffer lazily on
        // the first read, so a failed open never leaves a half-created request.

        if ((input->network_ || ForceIocpReader()) && UseIocpReader()) {
            std::string iocpError;
            input->iocpReader_ = IocpMediaReader::Create(
                input->file_, input->sizeKnown_ ? input->size_ : -1, iocpError,
                probe ? 2 : 0);
            if (input->iocpReader_ != nullptr && input->network_ && !probe &&
                AdaptiveReadAheadEnabled()) {
                input->iocpReader_->SetAdaptiveReadAhead(true);
            }
            // IOCP is the preferred network path. If the completion port cannot
            // be initialized, keep the per-read overlapped fallback instead of
            // making the media unopenable.
        }

        const int avioBufferSize = AvioBufferBytes();
        input->buffer_ = static_cast<std::uint8_t*>(av_malloc(avioBufferSize));
        if (input->buffer_ == nullptr) {
            error = "Could not allocate the media input buffer.";
            return nullptr;
        }

        input->context_ = avio_alloc_context(input->buffer_, avioBufferSize, 0,
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
    if (buffer == nullptr || bufferSize <= 0) {
        lastError_ = ERROR_INVALID_PARAMETER;
        return AVERROR(EINVAL);
    }
    if (eventRead_ == nullptr || eventRead_->capacity < bufferSize) {
        eventRead_.reset(); // The previous request is complete on every return path.
        auto request = std::unique_ptr<SharedFileInputEventReadRequest>(
            new (std::nothrow) SharedFileInputEventReadRequest());
        if (request == nullptr) {
            lastError_ = ERROR_NOT_ENOUGH_MEMORY;
            return AVERROR(EIO);
        }
        request->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (request->event == nullptr) {
            lastError_ = GetLastError();
            return AVERROR(EIO);
        }
        if (!ReserveEventRequest(bufferSize)) {
            NetTrace("event-io admission rejected requestBytes=%d residentBytes=%lld requests=%d",
                bufferSize, static_cast<long long>(EventResidentBytes.load(std::memory_order_relaxed)),
                EventRequestCount.load(std::memory_order_relaxed));
            lastError_ = ERROR_NOT_ENOUGH_QUOTA;
            return AVERROR(ENOMEM);
        }
        request->capacity = bufferSize;
        request->budgetCharged = true;
        request->data = static_cast<std::uint8_t*>(std::malloc(
            static_cast<std::size_t>(bufferSize)));
        if (request->data == nullptr) {
            lastError_ = ERROR_NOT_ENOUGH_MEMORY;
            return AVERROR(EIO);
        }
        request->capacity = bufferSize;
        eventRead_ = std::move(request);
    }

    auto* request = eventRead_.get();
    ResetEvent(request->event);
    std::memset(&request->ov, 0, sizeof(request->ov));
    request->ov.Offset = static_cast<DWORD>(static_cast<std::uint64_t>(position_) & 0xffffffffu);
    request->ov.OffsetHigh = static_cast<DWORD>(static_cast<std::uint64_t>(position_) >> 32);
    // Suppress IOCP packets if a failed IOCP initialization already associated
    // the same file object. The untagged handle remains our completion signal.
    request->ov.hEvent = reinterpret_cast<HANDLE>(
        reinterpret_cast<ULONG_PTR>(request->event) | static_cast<ULONG_PTR>(1));

    const auto copyAndAdvance = [this, buffer, request](const DWORD bytes) noexcept {
        if (bytes > static_cast<DWORD>(request->capacity)) {
            lastError_ = ERROR_INVALID_PARAMETER;
            return AVERROR(EIO);
        }
        std::memcpy(buffer, request->data, static_cast<std::size_t>(bytes));
        position_ += static_cast<std::int64_t>(bytes);
        return bytes == 0 ? AVERROR_EOF : static_cast<int>(bytes);
    };

    // A cancellation that cannot be observed within CancelGraceMs must not
    // return the caller's AVIO buffer to FFmpeg: the kernel may still own the
    // request. Move the whole request (OVERLAPPED + staging buffer) to the
    // reaper and let the late completion finish there.
    const auto cancelAndDrain = [this](const DWORD cancelError, const int result) noexcept {
        auto* pending = eventRead_.get();
        if (pending == nullptr) {
            lastError_ = cancelError;
            return result;
        }
        CancelIoEx(file_, &pending->ov);
        const auto wait = WaitForSingleObject(pending->event, CancelGraceMs);
        if (wait == WAIT_OBJECT_0) {
            DWORD ignored = 0;
            GetOverlappedResult(file_, &pending->ov, &ignored, FALSE);
        } else {
            AbandonEventRequest(std::move(eventRead_));
        }
        lastError_ = cancelError;
        return result;
    };

    DWORD bytesRead = 0;
    if (ReadFile(file_, request->data, static_cast<DWORD>(bufferSize), &bytesRead,
        &request->ov)) {
        return copyAndAdvance(bytesRead);
    }
    lastError_ = GetLastError();
    if (lastError_ != ERROR_IO_PENDING) {
        if (lastError_ == ERROR_HANDLE_EOF) return AVERROR_EOF;
        if (lastError_ == ERROR_OPERATION_ABORTED) return AVERROR_EXIT;
        return AVERROR(EIO);
    }

    while (true) {
        if (Interrupted(interrupt_)) {
            return cancelAndDrain(ERROR_OPERATION_ABORTED, AVERROR_EXIT);
        }
        if (deadlineTick != 0 && GetTickCount64() >= deadlineTick) {
            return cancelAndDrain(ERROR_TIMEOUT, AVERROR(ETIMEDOUT));
        }
        const auto wait = WaitForSingleObject(request->event, IoPollIntervalMs);
        if (wait == WAIT_OBJECT_0) {
            if (GetOverlappedResult(file_, &request->ov, &bytesRead, FALSE)) {
                return copyAndAdvance(bytesRead);
            }
            lastError_ = GetLastError();
            if (lastError_ == ERROR_HANDLE_EOF) return AVERROR_EOF;
            if (lastError_ == ERROR_OPERATION_ABORTED) return AVERROR_EXIT;
            return AVERROR(EIO);
        }
        if (wait == WAIT_FAILED) {
            const auto waitError = GetLastError();
            return cancelAndDrain(waitError, AVERROR(EIO));
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

    // One network Read call is bounded as a whole. The old loop could spend
    // 3 x 8 s in retries; that made a dead share look like a very slow one and
    // delayed every cancel/close behind it.
    const auto deadline = input->network_
        ? GetTickCount64() + SharedFileInput::NetworkReadTimeoutMs
        : 0;
    const auto remainingMs = [deadline]() noexcept -> DWORD {
        if (deadline == 0) return SharedFileInput::NetworkReadTimeoutMs;
        const auto now = GetTickCount64();
        if (now >= deadline) return 0;
        return static_cast<DWORD>((std::min<ULONGLONG>)(deadline - now, 0xffffffffu));
    };

    if (input->iocpReader_ != nullptr) {
        int result = AVERROR(EIO);
        DWORD error = 0;
        for (int attempt = 0; attempt <= SharedFileInput::NetworkReadRetryCount; ++attempt) {
            if (Interrupted(input->interrupt_)) return AVERROR_EXIT;
            const auto timeout = remainingMs();
            if (deadline != 0 && timeout == 0) return AVERROR(ETIMEDOUT);
            result = input->iocpReader_->Read(buffer, bufferSize, input->interrupt_,
                deadline == 0 ? SharedFileInput::NetworkReadTimeoutMs : timeout, error);
            input->lastError_ = error;
            if (result >= 0) {
                input->position_ += result;
                if (input->IsUnwrittenBlock(buffer, result)) {
                    input->position_ -= result;
                    DWORD resetError = 0;
                    if (!input->iocpReader_->Seek(input->position_,
                        SharedFileInput::NetworkSeekTimeoutMs, resetError,
                        &input->interrupt_))
                        return Interrupted(input->interrupt_) ? AVERROR_EXIT : AVERROR(EIO);
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
            if (deadline != 0 && remainingMs() == 0) return result;
            DWORD resetError = 0;
            const DWORD resetTimeout = deadline == 0
                ? SharedFileInput::NetworkSeekTimeoutMs
                : (std::min)(SharedFileInput::NetworkSeekTimeoutMs, remainingMs());
            if (!input->iocpReader_->Seek(input->position_, resetTimeout, resetError,
                &input->interrupt_)) {
                if (Interrupted(input->interrupt_)) return AVERROR_EXIT;
                return result;
            }
            if (deadline != 0) {
                const auto delay = remainingMs();
                if (delay == 0) return result;
                Sleep((std::min)(static_cast<DWORD>(125 * (attempt + 1)), delay));
            } else {
                Sleep(static_cast<DWORD>(125 * (attempt + 1)));
            }
        }
        return result;
    }
    const ULONGLONG eventDeadline = deadline;

    int result = AVERROR(EIO);
    for (int attempt = 0; attempt <= SharedFileInput::NetworkReadRetryCount; ++attempt) {
        if (Interrupted(input->interrupt_)) return AVERROR_EXIT;
        if (deadline != 0 && GetTickCount64() >= deadline) return AVERROR(ETIMEDOUT);
        result = input->ReadFileOnce(buffer, bufferSize, eventDeadline);
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
        if (deadline != 0) {
            const auto delay = remainingMs();
            if (delay == 0) return result;
            Sleep((std::min)(static_cast<DWORD>(125 * (attempt + 1)), delay));
        } else {
            Sleep(static_cast<DWORD>(125 * (attempt + 1)));
        }
    }
    return result;
}

bool SharedFileInput::HasWriter() const noexcept {
    if (file_ == INVALID_HANDLE_VALUE || path_.empty() || Interrupted(interrupt_)) return false;
    if (network_) {
        // Writer detection is advisory: never let an extra SMB metadata open
        // bypass the cancellation/deadline policy of the actual media reader.
        const auto opened = OpenNetworkFile(path_, GetTickCount64() + 1000,
            &interrupt_, FILE_SHARE_READ | FILE_SHARE_DELETE);
        if (opened.file == INVALID_HANDLE_VALUE)
            return opened.error == ERROR_SHARING_VIOLATION;
        CloseHandle(opened.file);
        return false;
    }
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
    if (Interrupted(input->interrupt_)) return AVERROR_EXIT;
    if (input->iocpReader_ != nullptr) {
        DWORD error = 0;
        if (!input->iocpReader_->Seek(target, SharedFileInput::NetworkSeekTimeoutMs,
            error, &input->interrupt_)) {
            input->lastError_ = error;
            if (Interrupted(input->interrupt_) || error == ERROR_OPERATION_ABORTED)
                return AVERROR_EXIT;
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
