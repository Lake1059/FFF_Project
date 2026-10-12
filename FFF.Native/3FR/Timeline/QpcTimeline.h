#pragma once

#include <cstdint>
#include <mutex>

class QpcTimeline final {
public:
    // 读取系统 QPC 频率并建立尚未启动的共享媒体时钟。
    QpcTimeline() noexcept;
    // 返回固定 QPC 频率，供时间单位转换和诊断使用。
    std::int64_t Frequency() const noexcept;
    // 返回当前原始 QPC，不使用 DateTime 或线程到达时间替代硬件时钟。
    std::int64_t Now() const noexcept;
    // 把首个有效视频时间戳设为 T0，并清除旧暂停累计值。
    void Reset(std::int64_t qpcTimestamp) noexcept;
    // 记录暂停起点；状态或时间戳非法时返回 false。
    bool Pause(std::int64_t qpcTimestamp) noexcept;
    // 累加暂停区间并恢复时间线，后续媒体 PTS 会扣除该区间。
    bool Resume(std::int64_t qpcTimestamp) noexcept;
    // 将原始 QPC 映射到指定分母的媒体 tick，并保证暂停区间不进入输出。
    std::int64_t ToMediaTicks(std::int64_t qpcTimestamp, std::int64_t timeBaseDenominator) const noexcept;
    // 返回已完成暂停区间的累计 QPC tick，供统计和诊断使用。
    std::int64_t PausedTicks() const noexcept;

private:
    mutable std::mutex mutex_;
    std::int64_t frequency_;
    std::int64_t origin_;
    std::int64_t pausedAt_;
    std::int64_t pausedTotal_;
    bool paused_;
};
