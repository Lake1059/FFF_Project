#pragma once

#include <cstdint>

// I/O 2.0 status contract. This header is deliberately Windows-free so the
// state machine and its arithmetic can be unit-tested offline.
//
// The single rule that keeps high-latency playback correct: a timeout is a
// `Retryable` status, never `Eof`. Only the source itself may report `Eof`, and
// only after the byte range has been validated by the caller's ReadLedger.
namespace FFFPlayer::Io {

inline constexpr std::uint32_t IoContractVersion = 1;

enum class Status : std::uint8_t {
    Ok = 0,
    WouldBlock = 1,   // no bytes yet, retry later; not an error
    Retryable = 2,    // timeout, 5xx, 429, disconnected; retry with backoff
    Fatal = 3,        // permanent error for this source/session
    Eof = 4,          // authoritative end of source, never a timeout
};

enum class Error : std::uint8_t {
    None = 0,
    Timeout = 1,
    Disconnected = 2,
    Http5xx = 3,
    Http429 = 4,
    AuthRequired = 5,
    NotFound = 6,
    Gone = 7,
    SegmentExpired = 8,
    DrmProtected = 9,
    FormatUnsupported = 10,
    RangeUnsupported = 11,
    ProtocolUnsupported = 12,
    IntegrityMismatch = 13,
    Internal = 14,
};

enum class Operation : std::uint8_t {
    Open = 0,
    Read = 1,
    Seek = 2,
    Segment = 3,
    Close = 4,
};

struct Result final {
    Status status = Status::Ok;
    Error error = Error::None;
    std::uint32_t retryAfterMs = 0;
    bool progress = false; // at least one byte, segment or packet was consumed
};

constexpr Result MakeOk(bool progress = true) noexcept {
    return Result{Status::Ok, Error::None, 0, progress};
}

constexpr Result MakeWouldBlock() noexcept {
    return Result{Status::WouldBlock, Error::None, 0, false};
}

constexpr Result MakeRetryable(Error error, std::uint32_t retryAfterMs = 0) noexcept {
    return Result{Status::Retryable, error, retryAfterMs, false};
}

constexpr Result MakeFatal(Error error) noexcept {
    return Result{Status::Fatal, error, 0, false};
}

constexpr Result MakeEof() noexcept {
    return Result{Status::Eof, Error::None, 0, true};
}

constexpr bool IsTerminal(Status status) noexcept {
    return status == Status::Fatal || status == Status::Eof;
}

constexpr bool IsRetryable(Status status) noexcept {
    return status == Status::WouldBlock || status == Status::Retryable;
}

// Segment-level expiration is not a session-level permanent failure. An HLS/DASH
// source may skip it as an explicit gap and rebuild the timeline when a later
// segment exists; only a source with no successor turns it into Fatal.
constexpr bool IsSegmentRecoverable(Error error) noexcept {
    return error == Error::SegmentExpired;
}

constexpr bool IsPermanent(Error error) noexcept {
    return error == Error::NotFound || error == Error::Gone ||
        error == Error::DrmProtected || error == Error::FormatUnsupported ||
        error == Error::ProtocolUnsupported || error == Error::IntegrityMismatch;
}

constexpr bool IsAuthRetryable(Error error) noexcept {
    return error == Error::AuthRequired;
}

} // namespace FFFPlayer::Io
