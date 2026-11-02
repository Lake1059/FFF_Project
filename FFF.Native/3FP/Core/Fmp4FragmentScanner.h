#pragma once

#include "framework.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class SharedFileInput;

// 一个 moof fragment 的定位记录；时间轴为 3FP 的 100ns 播放时间轴
// （已减去 timelineOrigin100ns，与 snapshot_.position100ns 同一坐标系）。
struct Fmp4FragmentEntry {
    std::int64_t  moofOffset{};           // 该 fragment 的 moof box 在文件中的字节偏移
    std::int64_t  startDecodeTime100ns{}; // 参考流在本 fragment 的第一个样本解码时间
    std::int64_t  endDecodeTime100ns{};   // start + 参考流本 fragment 全部样本时长之和
    std::uint32_t sequenceNumber{};       // mfhd sequence_number（文件序单调递增）
    bool          hasKeyframe{};          // 参考流在本 fragment 内存在关键帧样本
};

// Background scanner for fragmented MP4 (C-FMP4-DETECT@1 / C-FMP4-SCANNER@1).
//
// DetectFragmented is a one-shot box-header walk that reports whether the file
// carries an mvex box. Start spawns a worker thread that walks every moof box
// (mfhd/traf/tfhd/tfdt/trun) and incrementally publishes an Fmp4FragmentEntry
// table for the reference stream. The only data shared with consumers is the
// table mutex and the stop/complete atomics; every other member is owned by
// the scan worker alone.
class Fmp4FragmentScanner final {
public:
    Fmp4FragmentScanner() = default;
    ~Fmp4FragmentScanner();                       // StopAndJoin() 兜底
    Fmp4FragmentScanner(const Fmp4FragmentScanner&) = delete;
    Fmp4FragmentScanner& operator=(const Fmp4FragmentScanner&) = delete;

    // 判定 pathUtf8 指向的文件是否为 fragmented MP4：用独立旁路只读句柄
    // （SharedFileInput::Open）顺序扫描顶层 box（仅读 8/16 字节 box 头），
    // 当存在 moov 且其子 box 中含 mvex 时返回 true。
    static bool DetectFragmented(const char* pathUtf8) noexcept;

    // 启动后台扫描线程。referenceStreamTimeBase 为参考流 time_base（视频优先、
    // 无视频用音频，由调用方从 format_->streams[ref]->time_base 取值传入）；
    // timelineOrigin100ns 为 PlayerSession::TimelineOrigin100ns(format_) 的结果；
    // referenceTrackId 为参考流的 track id（调用方取 format_->streams[ref]->id）。
    // 返回 false（未启动任何线程）的情形：pathUtf8 为空、SharedFileInput::Open
    // 失败、timeBase 分母为 0。成功返回 true 后 IsRunning() 为 true。
    bool Start(const char* pathUtf8, AVRational referenceStreamTimeBase,
               std::int64_t timelineOrigin100ns, std::int32_t referenceTrackId) noexcept;

    // 置停止标志（acquire/release），不等待线程。幂等。
    void RequestStop() noexcept;
    // RequestStop + join（幂等；线程已结束时直接返回）。
    void StopAndJoin() noexcept;

    // 线程活跃（已 Start 且未 join 返回）。
    bool IsRunning() const noexcept;
    // 扫描已自然结束（扫到 EOF、遇不可恢复 box 错误或被 stop），此后表不再增长。
    bool IsComplete() const noexcept;
    // 已产出的条目数（atomic 读）。
    std::size_t ScannedCount() const noexcept;
    // 将当前表快照拷入 out（内部 mutex；out 被清空后填充，按文件序排列）。返回条目数。
    std::size_t CopyTable(std::vector<Fmp4FragmentEntry>& out) const;

private:
    // 私有实现细节（线程体与 box 解析，契约不约束；仅工作线程调用 ParseMoof 链）。
    void ScanWorker(AVRational referenceStreamTimeBase, std::int64_t timelineOrigin100ns,
        std::int32_t referenceTrackId, std::unique_ptr<SharedFileInput> input) noexcept;
    bool ParseMoof(AVIOContext* io, std::int64_t moofOffset, std::uint64_t moofBodyOffset,
        std::uint64_t moofEnd, AVRational timeBase, std::int64_t timelineOrigin100ns,
        std::int32_t referenceTrackId) noexcept;
    bool AppendEntry(const Fmp4FragmentEntry& entry) noexcept;

    std::thread worker_;
    std::vector<Fmp4FragmentEntry> table_;    // worker 追加 / CopyTable 读取（tableMutex_ 保护）
    mutable std::mutex tableMutex_;
    std::atomic<bool> stopRequested_{false};  // acquire/release 停止标志
    std::atomic<bool> complete_{true};        // 未 Start 时表不会再增长，视为已完结
    std::atomic<std::size_t> scannedCount_{0};
};