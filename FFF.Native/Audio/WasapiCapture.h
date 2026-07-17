#pragma once

#include "Api/FFF.Native.Api.h"
#include "Audio/AudioTypes.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

struct WasapiCaptureStatistics {
    std::uint64_t packetCount;
    std::uint64_t frameCount;
    std::uint64_t silentPacketCount;
    std::uint64_t discontinuityCount;
    std::uint64_t timestampErrorCount;
    std::uint64_t firstDevicePosition;
    std::uint64_t lastDevicePosition;
    std::uint64_t firstQpc100ns;
    std::uint64_t lastQpc100ns;
    std::uint64_t audioClockFrequency;
    std::uint32_t sampleRate;
    std::uint16_t channelCount;
    std::uint16_t bitsPerSample;
};

class WasapiCapture final {
public:
    // 保存端点、回环模式和包/失败回调；COM 资源在专用采集线程中创建。
    WasapiCapture(std::wstring endpointId, bool loopback, WasapiPacketCallback packetCallback = {},
        WasapiFailureCallback failureCallback = {});
    // 请求采集线程退出并释放事件及 COM 资源，不隐式切换音频设备。
    ~WasapiCapture();
    // 启动事件驱动 shared-mode WASAPI，并等待初始化成功或明确失败。
    FFFResult Start() noexcept;
    // 设置停止事件并等待线程结束；正常停止不会触发设备失败回调。
    void Stop() noexcept;
    // 返回包、设备位置、QPC 和 discontinuity 的一致快照。
    WasapiCaptureStatistics Statistics() const noexcept;
    // 返回最近一次初始化或运行时设备错误的 UTF-8 副本。
    std::string LastError() const;

private:
    // 在线程自己的 MTA 中创建、运行并销毁完整 WASAPI COM 链，逐包调用消费回调并记录时钟统计。
    void CaptureThread() noexcept;
    // 在线程安全区域保存最近错误；字符串分配失败时降级为固定后备文本。
    void SetError(std::string message) noexcept;

    std::wstring endpointId_;
    bool loopback_;
    WasapiPacketCallback packetCallback_;
    WasapiFailureCallback failureCallback_;
    HANDLE stopEvent_;
    HANDLE sampleEvent_;
    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable initializedCondition_;
    bool initialized_;
    bool initializationFinished_;
    std::atomic<bool> running_;
    std::atomic<bool> stopRequested_;
    WasapiCaptureStatistics statistics_;
    std::string lastError_;
};
