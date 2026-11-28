#pragma once

#include <cstdint>
#include <mutex>

// A per-source byte ledger. Every successful read is checked against the
// identity and offset it was issued for; a late completion from an older epoch
// can never be appended to the current position.
namespace FFFPlayer::Io {

enum class LedgerVerdict : std::uint8_t {
    Ok = 0,
    StaleEpoch = 1,
    OffsetMismatch = 2,
    ContentRangeMismatch = 3,
    SourceChanged = 4,
    TruncatedEof = 5,
    NoProgress = 6,
    NotConfigured = 7,
};

struct ReadObservation final {
    std::uint64_t sourceEpoch = 0;
    std::uint64_t offset = 0;
    std::uint32_t bytes = 0;
    std::uint64_t etagHash = 0;
    std::uint64_t contentRangeStart = 0;
    // Bytes declared by the Content-Range/Content-Length of the response that
    // owns this read. A single Range response may legitimately cover many
    // sequential reads; `rangeBytes == 0` keeps the strict legacy rule (one
    // addressable block per observation), which is what callers without a
    // response length need.
    std::uint64_t rangeBytes = 0;
    bool rangeResponse = false;
    bool eof = false;
};

struct LedgerSnapshot final {
    std::uint64_t sourceEpoch = 0;
    std::uint64_t configEpoch = 0;
    std::uint64_t expectedOffset = 0;
    std::int64_t sizeBytes = -1;
    std::uint64_t etagHash = 0;
    std::uint64_t validatedBytes = 0;
    std::uint64_t rejectedReads = 0;
    bool configured = false;
};

class ReadLedger final {
public:
    void ResetSource(std::uint64_t sourceEpoch, std::int64_t sizeBytes,
        std::uint64_t etagHash = 0) noexcept {
        std::lock_guard lock(mutex_);
        sourceEpoch_ = sourceEpoch;
        configEpoch_ = 0;
        expectedOffset_ = 0;
        sizeBytes_ = sizeBytes;
        etagHash_ = etagHash;
        validatedBytes_ = 0;
        rejectedReads_ = 0;
        configured_ = true;
    }

    // Configuration changes (budget/window) are not source changes. Keeping
    // them separate means a resized ByteWindow cannot invalidate valid bytes.
    void SetConfigEpoch(std::uint64_t configEpoch) noexcept {
        std::lock_guard lock(mutex_);
        configEpoch_ = configEpoch;
    }

    void ResetExpectedOffset(std::uint64_t offset) noexcept {
        std::lock_guard lock(mutex_);
        expectedOffset_ = offset;
        configured_ = true;
    }

    LedgerVerdict Observe(const ReadObservation& observation) noexcept {
        std::lock_guard lock(mutex_);
        if (!configured_) return LedgerVerdict::NotConfigured;
        if (observation.sourceEpoch != sourceEpoch_) return Reject(LedgerVerdict::StaleEpoch);
        if (observation.offset != expectedOffset_) return Reject(LedgerVerdict::OffsetMismatch);
        if (observation.rangeResponse) {
            if (observation.contentRangeStart > observation.offset) {
                return Reject(LedgerVerdict::ContentRangeMismatch);
            }
            if (observation.contentRangeStart < observation.offset) {
                // A later read can continue the same response only when its
                // declared length proves that this offset is still inside it.
                if (observation.rangeBytes == 0) {
                    return Reject(LedgerVerdict::ContentRangeMismatch);
                }
                const std::uint64_t consumed =
                    observation.offset - observation.contentRangeStart + observation.bytes;
                if (consumed > observation.rangeBytes) {
                    return Reject(LedgerVerdict::ContentRangeMismatch);
                }
            }
        }
        if (etagHash_ != 0 && observation.etagHash != 0 &&
            observation.etagHash != etagHash_) {
            return Reject(LedgerVerdict::SourceChanged);
        }
        if (observation.eof) {
            if (sizeBytes_ >= 0 && expectedOffset_ < static_cast<std::uint64_t>(sizeBytes_)) {
                return Reject(LedgerVerdict::TruncatedEof);
            }
            return LedgerVerdict::Ok;
        }
        if (observation.bytes == 0) return LedgerVerdict::NoProgress;
        expectedOffset_ += observation.bytes;
        validatedBytes_ += observation.bytes;
        return LedgerVerdict::Ok;
    }

    LedgerSnapshot Snapshot() const noexcept {
        std::lock_guard lock(mutex_);
        return LedgerSnapshot{sourceEpoch_, configEpoch_, expectedOffset_, sizeBytes_,
            etagHash_, validatedBytes_, rejectedReads_, configured_};
    }

private:
    LedgerVerdict Reject(LedgerVerdict verdict) noexcept {
        ++rejectedReads_;
        return verdict;
    }

    mutable std::mutex mutex_;
    std::uint64_t sourceEpoch_ = 0;
    std::uint64_t configEpoch_ = 0;
    std::uint64_t expectedOffset_ = 0;
    std::int64_t sizeBytes_ = -1;
    std::uint64_t etagHash_ = 0;
    std::uint64_t validatedBytes_ = 0;
    std::uint64_t rejectedReads_ = 0;
    bool configured_ = false;
};

} // namespace FFFPlayer::Io
