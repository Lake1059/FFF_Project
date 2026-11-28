#include "pch.h"
#include "Shared/Windows/Utf8.h"
#include "Shared/Ffmpeg/HttpMediaReader.h"
#include "3FP/IO/ReadLedger.h"

extern "C" {
#include <libavutil/error.h>
}

#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace {
using FFFPlayer::Io::LedgerVerdict;
using FFFPlayer::Io::ReadLedger;
using FFFPlayer::Io::ReadObservation;

constexpr DWORD DefaultConnectTimeoutMs = 15000;
constexpr DWORD DefaultReceiveTimeoutMs = 30000;
constexpr std::uint64_t HttpSourceEpoch = 1;

std::string WinHttpErrorText(DWORD error);

// P0 HLS/DASH connection reuse: WinHTTP sessions own the connection pool. One
// session per origin, shared by every manifest and segment reader, lets FFmpeg's
// virtual demuxers reuse the TCP/TLS connection instead of paying a new handshake
// (and, on a long-RTT link, a new 30 s receive timeout) for each 2 s segment.
// The handles are process-lifetime; requests remain per-reader and abortable.
struct SharedHttpConnection final {
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
};
std::mutex SharedHttpMutex;
std::map<std::wstring, SharedHttpConnection> SharedHttpConnections;

bool AcquireSharedConnection(const std::wstring& host, const int port, const bool https,
    HINTERNET& session, HINTERNET& connect, std::string& error) noexcept {
    const auto key = std::wstring(https ? L"https://" : L"http://") + host +
        L":" + std::to_wstring(port);
    std::lock_guard lock(SharedHttpMutex);
    const auto it = SharedHttpConnections.find(key);
    if (it != SharedHttpConnections.end()) {
        session = it->second.session;
        connect = it->second.connect;
        return true;
    }
    auto open = [] {
        auto handle = WinHttpOpen(L"3FP/1.0 (I/O 2.0)",
            WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0);
        if (handle == nullptr) {
            handle = WinHttpOpen(L"3FP/1.0 (I/O 2.0)",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS, 0);
        }
        return handle;
    };
    auto newSession = open();
    if (newSession == nullptr) {
        error = "Could not initialize WinHTTP: " + WinHttpErrorText(GetLastError());
        return false;
    }
    auto newConnect = WinHttpConnect(newSession, host.c_str(),
        static_cast<INTERNET_PORT>(port), 0);
    if (newConnect == nullptr) {
        error = "Could not connect to the HTTP host: " + WinHttpErrorText(GetLastError());
        WinHttpCloseHandle(newSession);
        return false;
    }
    SharedHttpConnections.emplace(key, SharedHttpConnection{newSession, newConnect});
    session = newSession;
    connect = newConnect;
    return true;
}


std::string WinHttpErrorText(const DWORD error) {
    char* message = nullptr;
    const auto length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<char*>(&message), 0, nullptr);
    std::string result = "WinHTTP error " + std::to_string(error);
    if (length > 0 && message != nullptr) {
        result.assign(message, message + length);
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n' ||
            result.back() == ' ')) result.pop_back();
    }
    if (message != nullptr) LocalFree(message);
    return result;
}

bool IsHttpUrlText(const std::wstring& url, bool& https) noexcept {
    const auto starts = [&url](const wchar_t* prefix) {
        const auto length = std::wcslen(prefix);
        if (url.size() < length) return false;
        for (std::size_t i = 0; i < length; ++i) {
            if (towlower(url[i]) != towlower(prefix[i])) return false;
        }
        return true;
    };
    if (starts(L"https://")) { https = true; return true; }
    if (starts(L"http://")) { https = false; return true; }
    return false;
}
std::wstring TrimUrl(const std::wstring& value) noexcept {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::wstring PercentDecode(const std::wstring& value) noexcept {
    std::wstring out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == L'%' && i + 2 < value.size()) {
            const auto hex = [](wchar_t c) -> int {
                if (c >= L'0' && c <= L'9') return c - L'0';
                if (c >= L'a' && c <= L'f') return c - L'a' + 10;
                if (c >= L'A' && c <= L'F') return c - L'A' + 10;
                return -1;
            };
            const int hi = hex(value[i + 1]);
            const int lo = hex(value[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<wchar_t>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(value[i]);
    }
    return out;
}

bool ParseLittleEndianNumber(const std::wstring& text, const std::size_t offset,
    std::int64_t& value, std::size_t& consumed) noexcept {
    std::wstring digits;
    consumed = offset;
    if (offset >= text.size()) return false;
    while (consumed < text.size() && iswdigit(text[consumed])) {
        digits.push_back(text[consumed]);
        ++consumed;
    }
    if (digits.empty()) return false;
    wchar_t* end = nullptr;
    const auto parsed = _wcstoi64(digits.c_str(), &end, 10);
    if (end == digits.c_str() || *end != L'\0' || parsed < 0) return false;
    value = parsed;
    return true;
}

bool ParseContentRange(const std::wstring& text, std::int64_t& start,
    std::int64_t& total, bool& totalKnown) noexcept {
    const auto unit = text.find(L"bytes");
    if (unit == std::wstring::npos) return false;
    std::size_t cursor = unit + 5;
    while (cursor < text.size() && text[cursor] == L' ') ++cursor;
    std::size_t next = 0;
    if (!ParseLittleEndianNumber(text, cursor, start, next)) return false;
    cursor = next;
    if (cursor >= text.size() || text[cursor] != L'-') return false;
    ++cursor;
    std::int64_t end = 0;
    if (!ParseLittleEndianNumber(text, cursor, end, next)) return false;
    cursor = next;
    if (end < start) return false;
    if (cursor >= text.size() || text[cursor] != L'/') return false;
    ++cursor;
    if (cursor < text.size() && text[cursor] == L'*') {
        totalKnown = false;
        total = -1;
        return true;
    }
    if (!ParseLittleEndianNumber(text, cursor, total, next)) return false;
    cursor = next;
    if (total < 0 || end >= total) return false;
    totalKnown = true;
    return true;
}

std::wstring QueryHeader(HINTERNET request, const DWORD query,
    const wchar_t* name) noexcept {
    DWORD bytes = 0;
    if (WinHttpQueryHeaders(request, query, name, WINHTTP_NO_OUTPUT_BUFFER,
        &bytes, WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return {};
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2);
    if (!WinHttpQueryHeaders(request, query, name, buffer.data(), &bytes,
        WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    auto count = bytes / sizeof(wchar_t);
    if (count > 0 && buffer[count - 1] == L'\0') --count;
    return std::wstring(buffer.data(), count);
}

DWORD QueryStatus(HINTERNET request) noexcept {
    DWORD status = 0;
    DWORD bytes = sizeof(status);
    if (!WinHttpQueryHeaders(request,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    return status;
}
} // namespace

struct HttpMediaReader::Impl final {
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;
    mutable std::mutex requestMutex;
    std::atomic<bool> aborted{false};
    std::wstring path;
    std::wstring user;
    std::wstring password;
    bool https = false;
    bool sizeKnown = false;
    bool eof = false;
    std::int64_t size = -1;
    std::int64_t position = 0;
    std::int64_t rangeStart = 0;
    std::uint64_t rangeBytes = 0;
    bool rangeResponse = false;
    DWORD lastError = 0;
    std::uint64_t etagHash = 0;
    std::uint32_t rttMs = 0;
    std::uint64_t goodputBps = 0;
    std::uint64_t requestStartedTick = 0;
    std::uint64_t requestBytes = 0;
    ReadLedger ledger;

    ~Impl() {
        if (request != nullptr) WinHttpCloseHandle(request);
        // session/connect are process-lifetime shared handles. Closing them here
        // would tear down the connection pool every time a 2 s HLS segment ends.
    }

    void CloseRequest() noexcept {
        std::lock_guard lock(requestMutex);
        if (request != nullptr) {
            WinHttpCloseHandle(request);
            request = nullptr;
        }
    }

    bool AuthenticateWithChallenge(std::string& error) noexcept {
        if (user.empty()) return false;
        DWORD supported = 0;
        DWORD first = 0;
        if (!WinHttpQueryAuthSchemes(request, &supported, &first, nullptr)) {
            lastError = GetLastError();
            error = "HTTP auth challenge could not be queried: " + WinHttpErrorText(lastError);
            return false;
        }
        DWORD scheme = 0;
        if ((supported & WINHTTP_AUTH_SCHEME_DIGEST) != 0) scheme = WINHTTP_AUTH_SCHEME_DIGEST;
        else if ((supported & WINHTTP_AUTH_SCHEME_NEGOTIATE) != 0) scheme = WINHTTP_AUTH_SCHEME_NEGOTIATE;
        else if ((supported & WINHTTP_AUTH_SCHEME_BASIC) != 0) scheme = WINHTTP_AUTH_SCHEME_BASIC;
        if (scheme == 0) {
            error = "HTTP server offered no supported authentication scheme.";
            return false;
        }
        if (!WinHttpSetCredentials(request, WINHTTP_AUTH_TARGET_SERVER, scheme,
            user.c_str(), password.c_str(), nullptr)) {
            lastError = GetLastError();
            error = "HTTP credentials were rejected: " + WinHttpErrorText(lastError);
            return false;
        }
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            lastError = GetLastError();
            error = "HTTP authenticated request failed: " + WinHttpErrorText(lastError);
            return false;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            lastError = GetLastError();
            error = "HTTP authenticated response failed: " + WinHttpErrorText(lastError);
            return false;
        }
        return true;
    }

    bool EnsureRequest(const std::int64_t offset, const DWORD timeoutMs,
        std::string& error) noexcept {
        CloseRequest();
        requestStartedTick = GetTickCount64();
        requestBytes = 0;
        request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
            https ? WINHTTP_FLAG_SECURE : 0);
        if (request == nullptr) {
            lastError = GetLastError();
            error = "Could not create the HTTP request: " + WinHttpErrorText(lastError);
            return false;
        }
        const DWORD receive = timeoutMs == 0 ? DefaultReceiveTimeoutMs : timeoutMs;
        WinHttpSetTimeouts(session, static_cast<int>(DefaultConnectTimeoutMs),
            static_cast<int>(DefaultConnectTimeoutMs), static_cast<int>(receive),
            static_cast<int>(receive));
        if (!user.empty()) {
            WinHttpSetCredentials(request, WINHTTP_AUTH_TARGET_SERVER,
                WINHTTP_AUTH_SCHEME_BASIC, user.c_str(), password.c_str(), nullptr);
        }
        const std::wstring range = L"Range: bytes=" + std::to_wstring(offset) + L"-\r\n";
        WinHttpAddRequestHeaders(request, range.c_str(), static_cast<DWORD>(-1L),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        WinHttpAddRequestHeaders(request, L"Accept-Encoding: identity\r\n",
            static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        wchar_t bearer[512]{};
        if (GetEnvironmentVariableW(L"FFF_HTTP_BEARER", bearer,
            static_cast<DWORD>(std::size(bearer))) > 0) {
            const std::wstring auth = L"Authorization: Bearer " +
                std::wstring(bearer) + L"\r\n";
            WinHttpAddRequestHeaders(request, auth.c_str(), static_cast<DWORD>(-1L),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
            lastError = GetLastError();
            error = "HTTP request failed: " + WinHttpErrorText(lastError);
            return false;
        }
        if (!WinHttpReceiveResponse(request, nullptr)) {
            lastError = GetLastError();
            error = "HTTP response failed: " + WinHttpErrorText(lastError);
            return false;
        }
        const auto headerLatency = GetTickCount64() - requestStartedTick;
        const auto sample = static_cast<std::uint32_t>(std::min<std::uint64_t>(headerLatency, 30000));
        rttMs = rttMs == 0 ? std::max<std::uint32_t>(sample, 1)
                           : (rttMs * 3 + std::max<std::uint32_t>(sample, 1)) / 4;
        DWORD status = QueryStatus(request);
        if (status == 401 && !user.empty()) {
            // WebDAV commonly answers Basic/Digest 401 even when the first
            // request already carried Basic credentials. Query the advertised
            // scheme, not a guessed one, and retry once on the same request.
            std::string authError;
            if (AuthenticateWithChallenge(authError))
                status = QueryStatus(request);
        }
        if (status == 401 || status == 403) {
            error = "HTTP authentication is required.";
            return false;
        }
        if (status == 404 || status == 410) {
            error = status == 404 ? "HTTP resource was not found."
                                  : "HTTP resource is gone.";
            return false;
        }
        if (status == 429) {
            error = "HTTP resource is rate limited.";
            return false;
        }
        if (status >= 500) {
            error = "HTTP server returned status " + std::to_string(status) + ".";
            return false;
        }
        if (status != 200 && status != 206) {
            error = "HTTP server returned unsupported status " + std::to_string(status) + ".";
            return false;
        }
        rangeResponse = status == 206;
        rangeStart = offset;
        rangeBytes = 0;
        if (rangeResponse) {
            const std::wstring contentRange = QueryHeader(request,
                WINHTTP_QUERY_CONTENT_RANGE, nullptr);
            if (GetEnvironmentVariableW(L"FFF_HTTP_TRACE", nullptr, 0))
                std::fwprintf(stderr, L"[http] status=206 range=[%ls] len=%zu\n",
                    contentRange.c_str(), contentRange.size());
            std::int64_t responseStart = 0;
            std::int64_t total = -1;
            bool totalKnown = false;
            if (!ParseContentRange(contentRange, responseStart, total, totalKnown)) {
                error = "HTTP Content-Range is malformed.";
                return false;
            }
            if (responseStart != offset) {
                error = "HTTP Content-Range does not match the requested offset.";
                return false;
            }
            if (totalKnown) {
                if (!sizeKnown) size = total;
                else if (size != total) {
                    error = "HTTP media size changed during the session.";
                    return false;
                }
                sizeKnown = true;
            }
        } else if (offset != 0) {
            error = "HTTP server does not support Range requests.";
            return false;
        }
        const std::wstring length = QueryHeader(request,
            WINHTTP_QUERY_CONTENT_LENGTH, nullptr);
        std::int64_t parsedLength = 0;
        std::size_t consumed = 0;
        if (ParseLittleEndianNumber(length, 0, parsedLength, consumed) && parsedLength >= 0) {
            rangeBytes = static_cast<std::uint64_t>(parsedLength);
            if (!rangeResponse) {
                size = parsedLength;
                sizeKnown = true;
            }
        }
        const std::wstring etag = QueryHeader(request, WINHTTP_QUERY_ETAG, nullptr);
        if (!etag.empty()) {
            std::uint64_t hash = 1469598103934665603ull;
            for (const auto c : etag) {
                hash ^= static_cast<std::uint64_t>(c);
                hash *= 1099511628211ull;
            }
            if (etagHash != 0 && hash != etagHash) {
                error = "HTTP ETag changed during the session.";
                return false;
            }
            etagHash = hash;
        }
        ledger.ResetSource(HttpSourceEpoch, size, etagHash);
        ledger.ResetExpectedOffset(offset);
        return true;
    }
};

HttpMediaReader::HttpMediaReader() : impl_(std::make_unique<Impl>()) {}
HttpMediaReader::~HttpMediaReader() = default;

std::unique_ptr<HttpMediaReader> HttpMediaReader::Open(const std::string& urlUtf8,
    const ULONGLONG deadlineTick, const AVIOInterruptCB* interrupt,
    std::string& error, const DWORD responseTimeoutMs) noexcept {
    try {
        if (interrupt != nullptr && interrupt->callback != nullptr &&
            interrupt->callback(interrupt->opaque) != 0) {
            error = "HTTP media open was cancelled.";
            return nullptr;
        }
        if (deadlineTick != 0 && GetTickCount64() >= deadlineTick) {
            error = "HTTP media open deadline expired.";
            return nullptr;
        }
        const std::wstring url = TrimUrl(FFFText::FromUtf8(urlUtf8.c_str()));
        bool https = false;
        if (!IsHttpUrlText(url, https)) {
            error = "The URL is not an HTTP or HTTPS URL.";
            return nullptr;
        }
        const std::wstring withoutScheme = url.substr(https ? 8 : 7);
        const auto slash = withoutScheme.find(L'/');
        const std::wstring authority = slash == std::wstring::npos
            ? withoutScheme : withoutScheme.substr(0, slash);
        if (authority.empty()) {
            error = "The HTTP URL has no host.";
            return nullptr;
        }
        std::wstring path = slash == std::wstring::npos
            ? L"/" : withoutScheme.substr(slash);
        const auto fragment = path.find(L'#');
        if (fragment != std::wstring::npos) path.resize(fragment);

        std::wstring host = authority;
        std::wstring user;
        std::wstring password;
        const auto at = host.find(L'@');
        if (at != std::wstring::npos) {
            std::wstring userInfo = host.substr(0, at);
            host = host.substr(at + 1);
            const auto colon = userInfo.find(L':');
            user = PercentDecode(userInfo.substr(0, colon));
            if (colon != std::wstring::npos) {
                password = PercentDecode(userInfo.substr(colon + 1));
            }
        }
        int port = https ? 443 : 80;
        if (!host.empty() && host.front() == L'[') {
            const auto close = host.find(L']');
            if (close == std::wstring::npos) {
                error = "The HTTP URL has a malformed IPv6 host.";
                return nullptr;
            }
            if (close + 1 < host.size() && host[close + 1] == L':') {
                port = _wtoi(host.c_str() + close + 2);
                host.resize(close + 1);
            }
        } else {
            const auto colon = host.rfind(L':');
            if (colon != std::wstring::npos) {
                port = _wtoi(host.c_str() + colon + 1);
                host.resize(colon);
            }
        }
        if (host.empty() || port <= 0 || port > 65535) {
            error = "The HTTP URL has an invalid host or port.";
            return nullptr;
        }

        auto reader = std::make_unique<HttpMediaReader>();
        auto& impl = *reader->impl_;
        impl.https = https;
        impl.path = std::move(path);
        impl.user = std::move(user);
        impl.password = std::move(password);
        if (!AcquireSharedConnection(host, port, https, impl.session, impl.connect, error))
            return nullptr;
        const DWORD baseTimeout = responseTimeoutMs == 0
            ? DefaultConnectTimeoutMs : responseTimeoutMs;
        const DWORD timeout = deadlineTick == 0 ? baseTimeout
            : static_cast<DWORD>((std::min<ULONGLONG>)(
                deadlineTick - GetTickCount64(), baseTimeout));
        if (!impl.EnsureRequest(0, timeout == 0 ? baseTimeout : timeout, error))
            return nullptr;
        impl.position = 0;
        impl.eof = false;
        return reader;
    } catch (...) {
        error = "Could not prepare the HTTP media input.";
        return nullptr;
    }
}

int HttpMediaReader::Read(std::uint8_t* buffer, const int bufferSize,
    const DWORD timeoutMs, const AVIOInterruptCB& interrupt,
    const std::int64_t position) noexcept {
    if (impl_ == nullptr || buffer == nullptr || bufferSize <= 0) return AVERROR(EINVAL);
    if (impl_->aborted.load(std::memory_order_acquire)) return AVERROR_EXIT;
    if (interrupt.callback != nullptr && interrupt.callback(interrupt.opaque) != 0)
        return AVERROR_EXIT;
    if (impl_->eof) return AVERROR_EOF;
    if (impl_->request == nullptr || position != impl_->position) {
        std::string error;
        if (!impl_->EnsureRequest(position, timeoutMs, error)) {
            impl_->lastError = ERROR_INVALID_STATE;
            return impl_->aborted.load(std::memory_order_acquire) ? AVERROR_EXIT : AVERROR(EIO);
        }
        // EnsureRequest reset the ledger to `position`; keep the reader cursor
        // in the same coordinate system or the next read would be issued from
        // the stale pre-request position.
        impl_->position = position;
        impl_->eof = false;
    }
    if (impl_->aborted.load(std::memory_order_acquire)) return AVERROR_EXIT;
    const DWORD receive = timeoutMs == 0 ? DefaultReceiveTimeoutMs : timeoutMs;
    // Keep response/connection timeouts at the caller's budget, but wake a blocking
    // segment read every 200 ms so Abort() can close the request promptly. The
    // retry loop below preserves the caller's overall receive deadline; only the
    // wake-up granularity changes. A shared session timeout is used because
    // per-request WINHTTP_OPTION_RECEIVE_TIMEOUT is not honored by all WinHTTP
    // versions for synchronous reads.
    const DWORD sliceMs = std::min<DWORD>(receive, 200);
    WinHttpSetTimeouts(impl_->session, static_cast<int>(DefaultConnectTimeoutMs),
        static_cast<int>(DefaultConnectTimeoutMs), static_cast<int>(sliceMs),
        static_cast<int>(sliceMs));
    const auto readDeadline = GetTickCount64() + receive;
    DWORD bytesRead = 0;
    DWORD readError = 0;
    bool readDone = false;
    for (;;) {
        {
            // The request handle is not reference counted; serialize the blocking
            // read against Abort(), which closes it to break a stalled server.
            std::lock_guard lock(impl_->requestMutex);
            if (impl_->request == nullptr || impl_->aborted.load(std::memory_order_acquire)) {
                readError = ERROR_WINHTTP_OPERATION_CANCELLED;
                break;
            }
            bytesRead = 0;
            if (WinHttpReadData(impl_->request, buffer,
                    static_cast<DWORD>(bufferSize), &bytesRead)) {
                readDone = true;
                break;
            }
            readError = GetLastError();
            if (readError == ERROR_WINHTTP_TIMEOUT && GetTickCount64() < readDeadline &&
                !impl_->aborted.load(std::memory_order_acquire)) {
                continue;
            }
            break;
        }
    }
    WinHttpSetTimeouts(impl_->session, static_cast<int>(DefaultConnectTimeoutMs),
        static_cast<int>(DefaultConnectTimeoutMs), static_cast<int>(receive),
        static_cast<int>(receive));
    if (!readDone) {
        if (impl_->aborted.load(std::memory_order_acquire)) return AVERROR_EXIT;
        impl_->lastError = readError;
        return readError == ERROR_WINHTTP_TIMEOUT ? AVERROR(ETIMEDOUT) : AVERROR(EIO);
    }
    if (impl_->aborted.load(std::memory_order_acquire)) return AVERROR_EXIT;

    if (bytesRead == 0) {
        // A 206 can be a deliberately short but internally consistent range:
        // every byte its Content-Range promised has arrived, yet the resource
        // continues past this range. That is a resume boundary, not corruption
        // and not the end of the media. A truncated response fails the
        // rangeStart + rangeBytes test below and falls through to the ledger.
        if (impl_->sizeKnown && impl_->rangeResponse &&
            position < impl_->size &&
            position == impl_->rangeStart + static_cast<std::int64_t>(impl_->rangeBytes)) {
            impl_->lastError = ERROR_MORE_DATA;
            return AVERROR(EAGAIN);
        }
        // At a validated end the logical cursor is size, not the start of the
        // last Range response; EOF is the one observation that is not a
        // continuation of that response's Content-Range.
        const ReadObservation observation{HttpSourceEpoch,
            static_cast<std::uint64_t>(position), 0, impl_->etagHash,
            static_cast<std::uint64_t>(position), impl_->rangeBytes, impl_->rangeResponse, true};
        if (impl_->ledger.Observe(observation) != LedgerVerdict::Ok) {
            if (GetEnvironmentVariableW(L"FFF_HTTP_TRACE", nullptr, 0))
                std::fwprintf(stderr, L"[http] ledger reject eof offset=%lld size=%lld\n",
                    static_cast<long long>(position),
                    static_cast<long long>(impl_->size));
            impl_->lastError = ERROR_INVALID_DATA;
            return AVERROR(EIO);
        }
        impl_->eof = true;
        return AVERROR_EOF;
    }
    const ReadObservation observation{HttpSourceEpoch,
        static_cast<std::uint64_t>(position), bytesRead, impl_->etagHash,
        static_cast<std::uint64_t>(impl_->rangeStart), impl_->rangeBytes,
        impl_->rangeResponse, false};
    if (impl_->ledger.Observe(observation) != LedgerVerdict::Ok) {
        if (GetEnvironmentVariableW(L"FFF_HTTP_TRACE", nullptr, 0))
            std::fwprintf(stderr, L"[http] ledger reject read offset=%lld start=%lld bytes=%llu rangeBytes=%llu\n",
                static_cast<long long>(position), static_cast<long long>(impl_->rangeStart),
                static_cast<unsigned long long>(bytesRead),
                static_cast<unsigned long long>(impl_->rangeBytes));
        impl_->lastError = ERROR_INVALID_DATA;
        return AVERROR(EIO);
    }
    impl_->position += bytesRead;
    impl_->requestBytes += bytesRead;
    const auto elapsed = GetTickCount64() - impl_->requestStartedTick;
    if (elapsed >= 100 && impl_->requestBytes >= 16384) {
        const auto sample = impl_->requestBytes > (std::numeric_limits<std::uint64_t>::max)() / 1000
            ? (std::numeric_limits<std::uint64_t>::max)()
            : impl_->requestBytes * 1000 / elapsed;
        impl_->goodputBps = impl_->goodputBps == 0 ? sample
            : (impl_->goodputBps * 3 + sample) / 4;
    }
    return static_cast<int>(bytesRead);
}

bool HttpMediaReader::Seek(const std::int64_t target, const DWORD timeoutMs,
    const AVIOInterruptCB& interrupt, std::string& error) noexcept {
    if (impl_ == nullptr) return false;
    if (impl_->aborted.load(std::memory_order_acquire)) {
        error = "HTTP seek was cancelled.";
        return false;
    }
    if (interrupt.callback != nullptr && interrupt.callback(interrupt.opaque) != 0) {
        error = "HTTP seek was cancelled.";
        return false;
    }
    if (target < 0) {
        error = "The HTTP seek target is negative.";
        return false;
    }
    if (impl_->sizeKnown && target > impl_->size) {
        error = "The HTTP seek target is beyond the media size.";
        return false;
    }
    if (impl_->sizeKnown && target == impl_->size) {
        impl_->CloseRequest();
        impl_->position = target;
        impl_->eof = true;
        impl_->ledger.ResetExpectedOffset(static_cast<std::uint64_t>(target));
        return true;
    }
    if (!impl_->EnsureRequest(target, timeoutMs, error)) return false;
    impl_->position = target;
    impl_->eof = false;
    return true;
}

void HttpMediaReader::Abort() noexcept {
    if (impl_ == nullptr) return;
    impl_->aborted.store(true, std::memory_order_release);
    // Closing the request while WinHttpReadData is blocked is the documented
    // way to break out of a slow server response; Read holds requestMutex for
    // the blocking call, so the handle cannot be closed underneath it.
    std::lock_guard lock(impl_->requestMutex);
    if (impl_->request != nullptr) {
        WinHttpCloseHandle(impl_->request);
        impl_->request = nullptr;
    }
}

void HttpMediaReader::ClearAbort() noexcept {
    if (impl_ == nullptr) return;
    impl_->aborted.store(false, std::memory_order_release);
}

std::int64_t HttpMediaReader::Size() const noexcept { return impl_ ? impl_->size : -1; }
std::int64_t HttpMediaReader::Position() const noexcept { return impl_ ? impl_->position : -1; }
bool HttpMediaReader::Seekable() const noexcept { return impl_ && impl_->rangeResponse; }
bool HttpMediaReader::SizeKnown() const noexcept { return impl_ && impl_->sizeKnown; }
DWORD HttpMediaReader::LastError() const noexcept { return impl_ ? impl_->lastError : 0; }
bool HttpMediaReader::IsEof() const noexcept { return impl_ && impl_->eof; }
std::uint32_t HttpMediaReader::RttMs() const noexcept { return impl_ ? impl_->rttMs : 0; }
std::uint64_t HttpMediaReader::GoodputBps() const noexcept {
    return impl_ ? impl_->goodputBps : 0;
}
