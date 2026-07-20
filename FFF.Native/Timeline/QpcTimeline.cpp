#include "pch.h"
#include "Timeline/QpcTimeline.h"

// 创建以构造时 QPC 值为原点的时间线。本构造函数不启动工作线程，也没有可恢复的失败路径；
// 本产品支持的所有 Windows 版本均保证 QueryPerformanceCounter 可用。
QpcTimeline::QpcTimeline() noexcept
    : frequency_(0), origin_(0), pausedAt_(0), pausedTotal_(0), paused_(false) {
    LARGE_INTEGER frequency{};
    LARGE_INTEGER now{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&now);
    frequency_ = frequency.QuadPart;
    origin_ = now.QuadPart;
}

// 返回每秒包含的 QPC tick 数。该值在构造后不再变化，因此任意线程均可无锁读取。
std::int64_t QpcTimeline::Frequency() const noexcept {
    return frequency_;
}

// 读取当前原始 QPC 计数。这里不扣除暂停时间；暂停补偿只在 ToMediaTicks 中进行，
// 从而让诊断日志始终保留可用于复盘的原始硬件时间戳。
std::int64_t QpcTimeline::Now() const noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

// 把媒体时间线原点重置到指定原始 QPC 时间戳，并清空暂停状态。该方法只在所有流正式开始前调用；
// 运行中重置会破坏 PTS 单调性，因此 RecorderSession 仅在编码器和 Matroska header 初始化成功后使用。
void QpcTimeline::Reset(const std::int64_t qpcTimestamp) noexcept {
    std::scoped_lock lock(mutex_);
    origin_ = qpcTimestamp;
    pausedAt_ = 0;
    pausedTotal_ = 0;
    paused_ = false;
}

// 从调用方给出的原始 QPC 时间戳开始暂停。重复暂停或早于时间线原点的时间戳返回 false。
// 互斥锁用于处理 UI 控制与捕获关闭同时发生的情况，保证状态转换顺序确定。
bool QpcTimeline::Pause(const std::int64_t qpcTimestamp) noexcept {
    std::scoped_lock lock(mutex_);
    if (paused_ || qpcTimestamp < origin_) {
        return false;
    }
    pausedAt_ = qpcTimestamp;
    paused_ = true;
    return true;
}

// 结束当前暂停并累计精确暂停时长。没有活动暂停，或传入时间戳会造成时间倒退时返回 false。
bool QpcTimeline::Resume(const std::int64_t qpcTimestamp) noexcept {
    std::scoped_lock lock(mutex_);
    if (!paused_ || qpcTimestamp < pausedAt_) {
        return false;
    }
    pausedTotal_ += qpcTimestamp - pausedAt_;
    pausedAt_ = 0;
    paused_ = false;
    return true;
}

// 将原始 QPC 时间戳换算为整数媒体时间基，并扣除全部已完成暂停。活动暂停期间的时间被固定在
// 暂停起点。计算先拆分整秒与余数，避免长时间录制中的乘法溢出；负结果统一钳制为零。
std::int64_t QpcTimeline::ToMediaTicks(const std::int64_t qpcTimestamp,
    const std::int64_t timeBaseDenominator) const noexcept {
    std::scoped_lock lock(mutex_);
    const auto effectiveTimestamp = paused_ && qpcTimestamp > pausedAt_ ? pausedAt_ : qpcTimestamp;
    const auto elapsed = effectiveTimestamp - origin_ - pausedTotal_;
    if (elapsed <= 0 || timeBaseDenominator <= 0 || frequency_ <= 0) {
        return 0;
    }
    const auto wholeSeconds = elapsed / frequency_;
    const auto remainingTicks = elapsed % frequency_;
    // Round to the nearest media tick. Flooring an interval that represents exactly 1/60 s can
    // produce zero because the scheduler's integer QPC step is a few ticks short. That creates
    // duplicate PTS and makes a fixed-rate stream appear variable.
    return wholeSeconds * timeBaseDenominator +
        (remainingTicks * timeBaseDenominator + frequency_ / 2) / frequency_;
}

// 返回已完成暂停的累计 QPC tick 数。当前仍在进行的暂停尚无最终长度，因此不计入结果。
std::int64_t QpcTimeline::PausedTicks() const noexcept {
    std::scoped_lock lock(mutex_);
    return pausedTotal_;
}
