#include "pch.h"
#include "3FP/Core/Fmp4FragmentScanner.h"

#include "Shared/Ffmpeg/SharedFileInput.h"

extern "C" {
#include <libavutil/mathematics.h>  // av_rescale_q（avformat.h 已传递包含，显式引入表明意图）
}

#include <algorithm>
#include <limits>

namespace {

// ISO 14496-12 box types（big-endian fourcc 常量）。
constexpr std::uint32_t BoxTypeMoov = 0x6D6F6F76; // 'moov'
constexpr std::uint32_t BoxTypeMvex = 0x6D766578; // 'mvex'
constexpr std::uint32_t BoxTypeMoof = 0x6D6F6F66; // 'moof'
constexpr std::uint32_t BoxTypeMfhd = 0x6D666864; // 'mfhd'
constexpr std::uint32_t BoxTypeTraf = 0x74726166; // 'traf'
constexpr std::uint32_t BoxTypeTfhd = 0x74666864; // 'tfhd'
constexpr std::uint32_t BoxTypeTfdt = 0x74666474; // 'tfdt'
constexpr std::uint32_t BoxTypeTrun = 0x7472756E; // 'trun'

// tfhd flags（ISO 14496-12 §8.8.5.2）。
constexpr std::uint32_t TfhdBaseDataOffsetPresent = 0x000001;
constexpr std::uint32_t TfhdSampleDescriptionIndexPresent = 0x000002;
constexpr std::uint32_t TfhdDefaultSampleDurationPresent = 0x000008;
constexpr std::uint32_t TfhdDefaultSampleSizePresent = 0x000010;
constexpr std::uint32_t TfhdDefaultSampleFlagsPresent = 0x000020;

// trun flags（ISO 14496-12 §8.8.8.2）。
constexpr std::uint32_t TrunDataOffsetPresent = 0x000001;
constexpr std::uint32_t TrunFirstSampleFlagsPresent = 0x000004;
constexpr std::uint32_t TrunSampleDurationPresent = 0x000100;
constexpr std::uint32_t TrunSampleSizePresent = 0x000200;
constexpr std::uint32_t TrunSampleFlagsPresent = 0x000400;
constexpr std::uint32_t TrunSampleCompositionTimeOffsetPresent = 0x000800;

// 关键帧判定的两个标志位（C-FMP4-SCANNER@1 第 4 条公式，与 FFmpeg mov demuxer
// mov_read_trun 的判定一致）。
constexpr std::uint32_t SampleFlagDependsOnBit = 0x01000000; // 置位 → sample_depends_on==1（I 帧）
constexpr std::uint32_t SampleFlagNonSyncBit = 0x00010000;   // 清零 → sync sample

// 3FP 播放时间轴 100ns ticks（与 PlayerSession 的 TicksPerSecond 同一坐标系）。
constexpr std::int64_t PlaybackTicksPerSecond = 10'000'000;

// 表容量上限：防病态文件内存膨胀（每条 32B 量级，10^6 条 ≈ 32MB 封顶）。
// 触顶后扫描置 IsComplete 并停止增长（自截断，对应计划开放决策的默认值）。
constexpr std::size_t MaximumTableEntries = 1'000'000;

std::uint32_t ReadBe32(const std::uint8_t* source) noexcept {
    return (std::uint32_t(source[0]) << 24) | (std::uint32_t(source[1]) << 16) |
        (std::uint32_t(source[2]) << 8) | std::uint32_t(source[3]);
}

std::uint64_t ReadBe64(const std::uint8_t* source) noexcept {
    return (std::uint64_t(ReadBe32(source)) << 32) | std::uint64_t(ReadBe32(source + 4));
}

bool ReadBytes(AVIOContext* io, void* destination, int size) noexcept {
    return avio_read(io, static_cast<unsigned char*>(destination), size) == size;
}

bool ReadBe32(AVIOContext* io, std::uint32_t& value) noexcept {
    std::uint8_t buffer[4];
    if (!ReadBytes(io, buffer, 4)) return false;
    value = ReadBe32(buffer);
    return true;
}

bool ReadBe64(AVIOContext* io, std::uint64_t& value) noexcept {
    std::uint8_t buffer[8];
    if (!ReadBytes(io, buffer, 8)) return false;
    value = ReadBe64(buffer);
    return true;
}

// C-FMP4-SCANNER@1 第 4 条：关键帧样本判定。
bool IsKeyframeSampleFlags(std::uint32_t flags) noexcept {
    return (flags & SampleFlagDependsOnBit) != 0 || (flags & SampleFlagNonSyncBit) == 0;
}

std::uint64_t SaturatingAdd(std::uint64_t left, std::uint64_t right) noexcept {
    const auto sum = left + right;
    return sum < left ? ~std::uint64_t{0} : sum;
}

std::int64_t SaturatingToInt64(std::uint64_t value) noexcept {
    constexpr std::uint64_t maxInt64 = std::uint64_t{0x7FFFFFFFFFFFFFFF};
    return value > maxInt64 ? std::numeric_limits<std::int64_t>::max()
        : static_cast<std::int64_t>(value);
}

// 有符号饱和加减：病态容器字段（近 2^63 的 tfdt 基准、极端 trun 时长）经
// av_rescale_q 得到不可表示结果时直接相减/相加会有符号溢出（UB，G.INT.OVERFLOW.001）。
// 饱和化把时间轴极端值裁到 int64 边界，后果仅为一条 fragment 时间轴记录失真。
std::int64_t SaturatingAddInt64(std::int64_t left, std::int64_t right) noexcept {
    if (right >= 0 && left > std::numeric_limits<std::int64_t>::max() - right)
        return std::numeric_limits<std::int64_t>::max();
    if (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)
        return std::numeric_limits<std::int64_t>::min();
    return left + right;
}

std::int64_t SaturatingSubInt64(std::int64_t left, std::int64_t right) noexcept {
    if (right >= 0 && left < std::numeric_limits<std::int64_t>::min() + right)
        return std::numeric_limits<std::int64_t>::min();
    if (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)
        return std::numeric_limits<std::int64_t>::max();
    return left - right;
}

// 返回文件大小；无法确定（<0 或 0）时返回 0（0 不是合法的绝对终点，调用方据此判失败）。
std::uint64_t ResolveEndOfFile(AVIOContext* io) noexcept {
    const auto size = avio_size(io);
    return size > 0 ? static_cast<std::uint64_t>(size) : 0;
}

// box 头解析结果。toEndOfFile: size==0 或 largesize==0（box 延伸到文件尾）。
struct BoxHeader {
    std::uint64_t size{};
    std::uint32_t type{};
    std::uint64_t bodyOffset{};  // box 载荷的绝对文件偏移
    std::uint64_t endOffset{};   // box 末尾绝对偏移（toEndOfFile 时为 0）
    bool toEndOfFile{};
};

// 从 boxOffset 读一个 8/16 字节 box 头。失败（EOF/IO/病态头）返回 false。
// 病态防御：声明的 box 连自己的头都装不下（含 uint64 回绕）会死循环，判为失败。
bool ReadBoxHeader(AVIOContext* io, std::uint64_t boxOffset, BoxHeader& header) noexcept {
    std::uint8_t raw[8];
    if (!ReadBytes(io, raw, 8)) return false;
    header.size = ReadBe32(raw);
    header.type = ReadBe32(raw + 4);
    header.bodyOffset = boxOffset + 8;
    header.toEndOfFile = false;
    if (header.size == 1) {
        std::uint8_t large[8];
        if (!ReadBytes(io, large, 8)) return false;
        header.size = ReadBe64(large);
        header.bodyOffset = boxOffset + 16;
        if (header.size == 0) header.toEndOfFile = true;
    } else if (header.size == 0) {
        header.toEndOfFile = true;
    }
    header.endOffset = header.toEndOfFile ? 0 : boxOffset + header.size;
    return header.toEndOfFile || header.endOffset >= header.bodyOffset;
}

// 解析一个 trun 并把参考流样本时长累计进 totalDurationTicks / hasKeyframe
// （C-FMP4-SCANNER@1 第 3/4 条）。data_offset 与 composition time offset
// 不参与时间轴，读后丢弃。返回 false = 读失败（EOF/越界），调用方丢弃该 moof 的 entry。
bool AccumulateTrun(AVIOContext* io, std::uint32_t defaultDuration,
    std::uint32_t defaultFlags, std::uint64_t& totalDurationTicks,
    bool& hasKeyframe) noexcept {
    std::uint32_t versionFlags = 0;
    if (!ReadBe32(io, versionFlags)) return false;
    const auto trunFlags = versionFlags & 0x00FFFFFF;
    std::uint32_t sampleCount = 0;
    if (!ReadBe32(io, sampleCount)) return false;

    std::uint32_t firstSampleFlags = 0;
    std::uint32_t skip32 = 0;
    if ((trunFlags & TrunDataOffsetPresent) != 0 && !ReadBe32(io, skip32)) return false;
    if ((trunFlags & TrunFirstSampleFlagsPresent) != 0 && !ReadBe32(io, firstSampleFlags)) return false;

    const auto perSampleTable = TrunSampleDurationPresent | TrunSampleSizePresent |
        TrunSampleFlagsPresent | TrunSampleCompositionTimeOffsetPresent;
    if ((trunFlags & perSampleTable) == 0) {
        // 全 default 快速路径：per-sample 表不占 IO 字节，逐样本循环对病态大
        // sample_count 无法靠 EOF 终止，改用饱和乘法一次算完（防卡死防溢出）。
        totalDurationTicks = SaturatingAdd(totalDurationTicks,
            std::uint64_t{sampleCount} * std::uint64_t{defaultDuration});
        if (sampleCount > 0) {
            const auto firstFlags = (trunFlags & TrunFirstSampleFlagsPresent) != 0
                ? firstSampleFlags : defaultFlags;
            if (IsKeyframeSampleFlags(firstFlags) ||
                (sampleCount > 1 && IsKeyframeSampleFlags(defaultFlags)))
                hasKeyframe = true;
        }
        return true;
    }

    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        std::uint32_t duration = defaultDuration; // per-sample duration 缺省回落 tfhd 默认
        std::uint32_t flags = defaultFlags;
        if ((trunFlags & TrunSampleDurationPresent) != 0 && !ReadBe32(io, duration)) return false;
        if ((trunFlags & TrunSampleSizePresent) != 0 && !ReadBe32(io, skip32)) return false;
        if ((trunFlags & TrunSampleFlagsPresent) != 0 && !ReadBe32(io, flags)) return false;
        if ((trunFlags & TrunSampleCompositionTimeOffsetPresent) != 0 && !ReadBe32(io, skip32)) return false;
        // 首样本用 first_sample_flags，其余用 sample_flags（契约第 4 条）。
        if (index == 0 && (trunFlags & TrunFirstSampleFlagsPresent) != 0)
            flags = firstSampleFlags;
        totalDurationTicks = SaturatingAdd(totalDurationTicks, duration);
        if (IsKeyframeSampleFlags(flags)) hasKeyframe = true;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// C-FMP4-DETECT@1
// ---------------------------------------------------------------------------
bool Fmp4FragmentScanner::DetectFragmented(const char* pathUtf8) noexcept {
    try {
        if (pathUtf8 == nullptr || *pathUtf8 == '\0') return false;   // 空路径
        std::string error;
        // 独立旁路只读句柄：不触碰调用方的 format_/formatIo_（demux 句柄独立）。
        const auto input = SharedFileInput::Open(pathUtf8, error);
        if (!input) return false;
        auto* io = input->Context();
        if (io == nullptr) return false;

        // 从偏移 0 逐顶层 box 读头；只读 box 头，绝不读 mdat 载荷（R1.3）。
        for (;;) {
            const auto boxOffset = avio_tell(io);
            if (boxOffset < 0) return false;
            BoxHeader header;
            if (!ReadBoxHeader(io, static_cast<std::uint64_t>(boxOffset), header)) return false;

            if (header.type == BoxTypeMoov) {
                // 进入 moov 后仅遍历其子 box 头，发现 mvex 立即返回 true。
                const auto moovEnd = header.toEndOfFile ? ResolveEndOfFile(io) : header.endOffset;
                if (moovEnd == 0) return false;
                auto childOffset = header.bodyOffset;
                while (childOffset < moovEnd && moovEnd - childOffset >= 8) {
                    if (avio_seek(io, static_cast<std::int64_t>(childOffset), SEEK_SET) < 0)
                        return false;
                    BoxHeader child;
                    if (!ReadBoxHeader(io, childOffset, child)) return false; // IO/EOF → false
                    if (child.type == BoxTypeMvex) return true;
                    if (child.toEndOfFile) break;
                    if (child.endOffset > moovEnd) break; // 越界子 box：停止该 moov 遍历
                    childOffset = child.endOffset;
                }
            }

            if (header.toEndOfFile) return false;   // size==0 / largesize==0 → 到文件尾
            if (avio_seek(io, static_cast<std::int64_t>(header.endOffset), SEEK_SET) < 0)
                return false;
        }
    } catch (...) {
        // noexcept 承诺：打不开/IO/EOF/无 moov/无 mvex/空路径一律 false。
        return false;
    }
}

// ---------------------------------------------------------------------------
// C-FMP4-SCANNER@1 — 生命周期
// ---------------------------------------------------------------------------
bool Fmp4FragmentScanner::Start(const char* pathUtf8, AVRational referenceStreamTimeBase,
    std::int64_t timelineOrigin100ns, std::int32_t referenceTrackId) noexcept {
    try {
        // 契约失败路径：空路径 / timeBase 分母 0 → 不产生线程与表。
        if (pathUtf8 == nullptr || *pathUtf8 == '\0') return false;
        if (referenceStreamTimeBase.den == 0) return false;

        StopAndJoin();   // 重入防御（正常只调用一次；幂等）

        // 先同步打开：SharedFileInput::Open 失败时不启动线程（契约失败路径）；
        // 成功则把该独立旁路句柄移交给扫描线程（R5.1，与 demux 句柄无共享）。
        std::string error;
        auto input = SharedFileInput::Open(pathUtf8, error);
        if (!input) return false;

        // 此时无线程运行、无并发读者：安全重置共享状态。
        {
            std::lock_guard lock(tableMutex_);
            table_.clear();
        }
        scannedCount_.store(0, std::memory_order_release);
        stopRequested_.store(false, std::memory_order_release);
        complete_.store(false, std::memory_order_release);

        worker_ = std::thread(&Fmp4FragmentScanner::ScanWorker, this,
            referenceStreamTimeBase, timelineOrigin100ns, referenceTrackId,
            std::move(input));
        return true;
    } catch (...) {
        // std::thread 构造失败等：未产生线程；表保持空且视为完结（无异常路径）。
        try {
            std::lock_guard lock(tableMutex_);
            table_.clear();
        } catch (...) {
        }
        scannedCount_.store(0, std::memory_order_release);
        complete_.store(true, std::memory_order_release);
        return false;
    }
}

Fmp4FragmentScanner::~Fmp4FragmentScanner() {
    StopAndJoin();   // 析构兜底（C-FMP4-SCANNER@1）
}

void Fmp4FragmentScanner::RequestStop() noexcept {
    stopRequested_.store(true, std::memory_order_release);
}

void Fmp4FragmentScanner::StopAndJoin() noexcept {
    RequestStop();
    if (worker_.joinable()) worker_.join();   // 线程已结束时直接返回（幂等）
}

bool Fmp4FragmentScanner::IsRunning() const noexcept {
    return worker_.joinable();   // 已 Start 且未 join 返回
}

bool Fmp4FragmentScanner::IsComplete() const noexcept {
    return complete_.load(std::memory_order_acquire);
}

std::size_t Fmp4FragmentScanner::ScannedCount() const noexcept {
    return scannedCount_.load(std::memory_order_acquire);
}

std::size_t Fmp4FragmentScanner::CopyTable(std::vector<Fmp4FragmentEntry>& out) const {
    std::lock_guard lock(tableMutex_);
    // worker 按文件序追加，快照天然保持按文件序排列的前缀（增量可见，R5.3）。
    out.assign(table_.begin(), table_.end());
    return out.size();
}

// 在 mutex 内追加（每完成一个 entry 即可读）。返回 false = 触顶自截断，调用方应停止扫描。
bool Fmp4FragmentScanner::AppendEntry(const Fmp4FragmentEntry& entry) noexcept {
    try {
        std::lock_guard lock(tableMutex_);
        if (table_.size() >= MaximumTableEntries) {
            complete_.store(true, std::memory_order_release);
            return false;
        }
        table_.push_back(entry);
        scannedCount_.store(table_.size(), std::memory_order_release);
        return true;
    } catch (...) {
        // bad_alloc 等：保留已有表，停止扫描（无异常路径承诺）。
        complete_.store(true, std::memory_order_release);
        return false;
    }
}

// ---------------------------------------------------------------------------
// 扫描线程
// ---------------------------------------------------------------------------
void Fmp4FragmentScanner::ScanWorker(AVRational referenceStreamTimeBase,
    std::int64_t timelineOrigin100ns, std::int32_t referenceTrackId,
    std::unique_ptr<SharedFileInput> input) noexcept {
    try {
        auto* io = input ? input->Context() : nullptr;
        if (io == nullptr) return;

        // 顶层遍历（C-FMP4-SCANNER@1 第 2 条）：moof 深入解析；
        // moov（检测已由 DetectFragmented 完成）、mdat 与其他 box 按 size 跳过 body。
        for (;;) {
            // 中断纪律：每个 box 读取周期开始前检查停止标志。
            if (stopRequested_.load(std::memory_order_acquire)) break;

            const auto boxOffset = avio_tell(io);
            if (boxOffset < 0) break;
            BoxHeader header;
            if (!ReadBoxHeader(io, static_cast<std::uint64_t>(boxOffset), header)) break;
            // 读失败（EOF/IO）→ 置 IsComplete 退出，保留已扫表（R5.3）。

            if (header.type == BoxTypeMoof) {
                const auto moofEnd = header.toEndOfFile ? ResolveEndOfFile(io) : header.endOffset;
                if (moofEnd == 0) break;   // 无法确定 moof 终点（文件尾未知）
                if (!ParseMoof(io, boxOffset, header.bodyOffset, moofEnd,
                    referenceStreamTimeBase, timelineOrigin100ns, referenceTrackId)) break;
            }

            if (header.toEndOfFile) break;
            if (avio_seek(io, static_cast<std::int64_t>(header.endOffset), SEEK_SET) < 0) break;
        }
    } catch (...) {
        // 无异常路径：扫描失败表现为表提前 IsComplete（保留部分表）。
    }
    complete_.store(true, std::memory_order_release);
}

// 解析一个 moof（C-FMP4-SCANNER@1 第 3/5/8 条）：mfhd.sequence_number + 各 traf 的
// tfhd/tfdt/trun。只有 track_ID == referenceTrackId 的 traf 参与时间累计与关键帧判定；
// 不含参考流 traf 的 moof 不产生 entry（仅推进文件位置）。
// 返回 false 表示顶层循环应退出（stop 或不可恢复 IO）；病态字段已在内部归一为
// "丢弃该 moof 的 entry、跳到 moof 声明 size 末尾继续"，无错误回调。
bool Fmp4FragmentScanner::ParseMoof(AVIOContext* io, std::int64_t moofOffset,
    std::uint64_t moofBodyOffset, std::uint64_t moofEnd, AVRational timeBase,
    std::int64_t timelineOrigin100ns, std::int32_t referenceTrackId) noexcept {
    std::uint32_t sequenceNumber = 0;   // mfhd 缺失时回落 0（容错）
    bool parseOk = true;

    // 时间累计（契约第 5 条）：首个匹配 traf 以其 tfdt 为准；同一 moof 内多个匹配
    // traf 时样本时长累计接续；end = start + 本 moof 参考流样本总时长换算。
    std::int64_t startDecodeTime100ns = 0;
    std::uint64_t totalDurationTicks = 0;
    bool hasKeyframe = false;
    bool hasBaseTime = false;   // 存在参考流 traf 且已取其 tfdt（否则不产生 entry）

    auto childOffset = moofBodyOffset;
    while (parseOk && childOffset < moofEnd && moofEnd - childOffset >= 8) {
        // 中断纪律：子 box 读取周期前同样检查（RequestStop 后至多完成当前 box 即退出）。
        if (stopRequested_.load(std::memory_order_acquire)) return false;
        if (avio_seek(io, static_cast<std::int64_t>(childOffset), SEEK_SET) < 0) return false;
        BoxHeader child;
        if (!ReadBoxHeader(io, childOffset, child)) {
            parseOk = false;
            break;
        }

        if (child.type == BoxTypeMfhd) {
            std::uint8_t payload[8];   // version/flags(4) + sequence_number(4)
            if (!ReadBytes(io, payload, sizeof(payload))) {
                parseOk = false;
                break;
            }
            sequenceNumber = ReadBe32(payload + 4);
        } else if (child.type == BoxTypeTraf) {
            const auto trafEnd = child.toEndOfFile ? moofEnd
                : std::min<std::uint64_t>(child.endOffset, moofEnd);
            std::uint32_t trackId = 0;
            bool trackIdKnown = false;
            std::uint32_t defaultDuration = 0;   // tfhd default_sample_duration
            std::uint32_t defaultFlags = 0;      // tfhd default_sample_flags

            auto subOffset = child.bodyOffset;
            while (subOffset < trafEnd && trafEnd - subOffset >= 8) {
                if (stopRequested_.load(std::memory_order_acquire)) return false;
                if (avio_seek(io, static_cast<std::int64_t>(subOffset), SEEK_SET) < 0) return false;
                BoxHeader sub;
                if (!ReadBoxHeader(io, subOffset, sub)) {
                    parseOk = false;
                    break;
                }

                if (sub.type == BoxTypeTfhd) {
                    std::uint32_t versionFlags = 0;
                    if (!ReadBe32(io, versionFlags)) {
                        parseOk = false;
                        break;
                    }
                    const auto tfhdFlags = versionFlags & 0x00FFFFFF;
                    if (!ReadBe32(io, trackId)) {
                        parseOk = false;
                        break;
                    }
                    trackIdKnown = true;
                    // 只有参考流 traf 才继续读默认值并深入 tfdt/trun；
                    // 非参考流 traf 剩余子 box 由外层按 traf size 整体跳过。
                    if (static_cast<std::int32_t>(trackId) != referenceTrackId) break;
                    std::uint32_t skip32 = 0;
                    std::uint64_t skip64 = 0;
                    if ((tfhdFlags & TfhdBaseDataOffsetPresent) != 0 && !ReadBe64(io, skip64)) {
                        parseOk = false;
                        break;
                    }
                    if ((tfhdFlags & TfhdSampleDescriptionIndexPresent) != 0 && !ReadBe32(io, skip32)) {
                        parseOk = false;
                        break;
                    }
                    if ((tfhdFlags & TfhdDefaultSampleDurationPresent) != 0 && !ReadBe32(io, defaultDuration)) {
                        parseOk = false;
                        break;
                    }
                    if ((tfhdFlags & TfhdDefaultSampleSizePresent) != 0 && !ReadBe32(io, skip32)) {
                        parseOk = false;
                        break;
                    }
                    if ((tfhdFlags & TfhdDefaultSampleFlagsPresent) != 0 && !ReadBe32(io, defaultFlags)) {
                        parseOk = false;
                        break;
                    }
                } else if (sub.type == BoxTypeTfdt && trackIdKnown &&
                           static_cast<std::int32_t>(trackId) == referenceTrackId) {
                    std::uint32_t versionFlags = 0;
                    if (!ReadBe32(io, versionFlags)) {
                        parseOk = false;
                        break;
                    }
                    std::uint64_t baseMediaDecodeTime = 0;
                    if ((versionFlags >> 24) == 1) {
                        // version=1 读 64 位；version=0 读 32 位。
                        if (!ReadBe64(io, baseMediaDecodeTime)) {
                            parseOk = false;
                            break;
                        }
                    } else {
                        std::uint32_t base32 = 0;
                        if (!ReadBe32(io, base32)) {
                            parseOk = false;
                            break;
                        }
                        baseMediaDecodeTime = base32;
                    }
                    if (!hasBaseTime) {
                        // 契约第 5 条基准换算（与 PlayerSession 同一坐标系）。
                        // tfdt 是文件直读的 uint64，极端值经 av_rescale_q 后与
                        // origin 相减需饱和化（G.INT.OVERFLOW.001）。
                        startDecodeTime100ns = SaturatingSubInt64(av_rescale_q(
                            static_cast<std::int64_t>(baseMediaDecodeTime), timeBase,
                            AVRational{1, static_cast<int>(PlaybackTicksPerSecond)}),
                            timelineOrigin100ns);
                        hasBaseTime = true;
                    }
                } else if (sub.type == BoxTypeTrun && trackIdKnown &&
                           static_cast<std::int32_t>(trackId) == referenceTrackId) {
                    if (!AccumulateTrun(io, defaultDuration, defaultFlags,
                        totalDurationTicks, hasKeyframe)) {
                        parseOk = false;
                        break;
                    }
                }
                // 其余 traf 子 box（sdtp/sbgp 等）：按 size 跳过。

                if (sub.toEndOfFile) break;
                if (sub.endOffset > trafEnd) {   // 越界子 box → 病态，丢弃 entry
                    parseOk = false;
                    break;
                }
                subOffset = sub.endOffset;
            }
        }
        // 其余 moof 子 box（mfro 等）：按 size 跳过。

        if (child.toEndOfFile) break;
        if (child.endOffset > moofEnd) {   // 越界子 box → 病态，丢弃 entry
            parseOk = false;
            break;
        }
        childOffset = child.endOffset;
    }

    // 契约第 8 条：无论解析成败，跳到该 moof 声明 size 的末尾继续，无错误回调。
    if (avio_seek(io, static_cast<std::int64_t>(moofEnd), SEEK_SET) < 0) return false;

    if (parseOk && hasBaseTime) {
        Fmp4FragmentEntry entry{};
        entry.moofOffset = moofOffset;
        entry.sequenceNumber = sequenceNumber;
        entry.startDecodeTime100ns = startDecodeTime100ns;
        // end = start + 参考流样本总时长；总和已饱和化，此处再加权换算结果仍需
        // 饱和化相加（极端 tfdt 基准 + 极端时长叠加，G.INT.OVERFLOW.001）。
        entry.endDecodeTime100ns = SaturatingAddInt64(startDecodeTime100ns, av_rescale_q(
            SaturatingToInt64(totalDurationTicks), timeBase,
            AVRational{1, static_cast<int>(PlaybackTicksPerSecond)}));
        entry.hasKeyframe = hasKeyframe;
        // 样本时长全缺省（per-sample 与 tfhd default 皆无）时 totalDurationTicks==0，
        // end==start（时长 0，消费方须容忍）。
        if (!AppendEntry(entry)) return false;   // 触顶自截断 → IsComplete，顶层退出
    }
    return true;
}