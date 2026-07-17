#include "pch.h"
#include "Audio/WasapiCapture.h"

using Microsoft::WRL::ComPtr;

// 保存端点标识和采集类型，并创建两个仅供本对象使用的事件句柄。构造函数不初始化 COM，
// 因为 WASAPI 接口必须在实际采集线程内创建和释放。endpointId 在构造时复制，调用方可立即释放。
WasapiCapture::WasapiCapture(std::wstring endpointId, const bool loopback, WasapiPacketCallback packetCallback,
    WasapiFailureCallback failureCallback)
    : endpointId_(std::move(endpointId)), loopback_(loopback), packetCallback_(std::move(packetCallback)),
      failureCallback_(std::move(failureCallback)),
      stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
      sampleEvent_(CreateEventW(nullptr, FALSE, FALSE, nullptr)), initialized_(false),
      initializationFinished_(false), running_(false), stopRequested_(false), statistics_{} {
}

// 先请求线程退出并等待资源在其所属 COM Apartment 中释放，随后关闭事件句柄。Stop 保持幂等，
// 因而构造中途失败或托管 SafeHandle 清理都可以直接进入析构函数。
WasapiCapture::~WasapiCapture() {
    Stop();
    if (sampleEvent_ != nullptr) CloseHandle(sampleEvent_);
    if (stopEvent_ != nullptr) CloseHandle(stopEvent_);
}

// 启动事件驱动采集线程，并等待它完成端点、格式和 IAudioCaptureClient 初始化。返回成功只表示
// 端点已经开始采集，不表示已经收到第一包；详细失败文本由 LastError 返回。本方法不得并发调用。
FFFResult WasapiCapture::Start() noexcept {
    std::unique_lock lock(mutex_);
    if (running_.load() || thread_.joinable()) return FFFResult::InvalidState;
    if (endpointId_.empty() || stopEvent_ == nullptr || sampleEvent_ == nullptr) {
        lastError_ = "The WASAPI endpoint ID or event handles are invalid.";
        return FFFResult::InvalidArgument;
    }
    ResetEvent(stopEvent_);
    stopRequested_.store(false);
    initialized_ = false;
    initializationFinished_ = false;
    statistics_ = {};
    try {
        thread_ = std::thread(&WasapiCapture::CaptureThread, this);
    } catch (...) {
        lastError_ = "Could not create the WASAPI capture thread.";
        return FFFResult::NativeFailure;
    }
    initializedCondition_.wait(lock, [this] { return initializationFinished_; });
    return initialized_ ? FFFResult::Success : FFFResult::DeviceFailure;
}

// 设置停止事件并等待采集线程退出。线程负责调用 IAudioClient::Stop、释放全部 COM 接口以及
// CoUninitialize；本方法不从其他线程直接操作 WASAPI COM 对象，避免 Apartment 与生命周期错误。
void WasapiCapture::Stop() noexcept {
    stopRequested_.store(true);
    if (stopEvent_ != nullptr) SetEvent(stopEvent_);
    if (thread_.joinable() && thread_.get_id() != std::this_thread::get_id()) thread_.join();
    running_.store(false);
}

// 在互斥锁保护下返回统计值副本。所有字段都是原始设备/QPC 观测值，不把包到达时间伪装成
// 采集时间；调用方可在采集进行中读取，且不会获得指向线程内部存储的指针。
WasapiCaptureStatistics WasapiCapture::Statistics() const noexcept {
    std::scoped_lock lock(mutex_);
    return statistics_;
}

// 返回最近一次初始化或采集失败的 UTF-8 文本副本。消息存储与统计共用互斥锁，读取线程不会
// 与采集线程修改 std::string 发生数据竞争。
std::string WasapiCapture::LastError() const {
    std::scoped_lock lock(mutex_);
    return lastError_;
}

// 在 MTA 中完成完整 WASAPI 生命周期：按 ID 打开端点、读取 mix format、事件驱动 shared mode
// 初始化、取得 CaptureClient/AudioClock，然后逐包记录 device position 与 100 ns QPC position。
// 循环不复制音频内容；后续重采样器会在 ReleaseBuffer 前通过同一位置消费 data 和 frameCount。
void WasapiCapture::CaptureThread() noexcept {
    const auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool clientStarted = false;
    ComPtr<IAudioClient> audioClient;
    try {
        if (FAILED(comResult)) {
            SetError("CoInitializeEx failed for the WASAPI capture thread.");
            throw std::runtime_error("COM initialization failed");
        }
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator));
        if (FAILED(result)) {
            SetError("Could not create MMDeviceEnumerator: " + std::to_string(result));
            throw std::runtime_error("MMDeviceEnumerator failed");
        }
        ComPtr<IMMDevice> endpoint;
        result = enumerator->GetDevice(endpointId_.c_str(), &endpoint);
        if (FAILED(result)) {
            SetError("Could not open the selected audio endpoint: " + std::to_string(result));
            throw std::runtime_error("GetDevice failed");
        }
        result = endpoint->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audioClient);
        if (FAILED(result)) {
            SetError("Could not activate IAudioClient: " + std::to_string(result));
            throw std::runtime_error("IAudioClient activation failed");
        }
        WAVEFORMATEX* mixFormat = nullptr;
        result = audioClient->GetMixFormat(&mixFormat);
        if (FAILED(result) || mixFormat == nullptr) {
            SetError("Could not read the endpoint mix format: " + std::to_string(result));
            throw std::runtime_error("GetMixFormat failed");
        }
        {
            std::scoped_lock lock(mutex_);
            statistics_.sampleRate = mixFormat->nSamplesPerSec;
            statistics_.channelCount = mixFormat->nChannels;
            statistics_.bitsPerSample = mixFormat->wBitsPerSample;
        }
        WasapiSampleFormat sampleFormat{
            mixFormat->nSamplesPerSec,
            mixFormat->nChannels,
            mixFormat->wBitsPerSample,
            mixFormat->wBitsPerSample,
            mixFormat->nBlockAlign,
            mixFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT
        };
        if (mixFormat->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            mixFormat->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mixFormat);
            sampleFormat.validBitsPerSample = extensible->Samples.wValidBitsPerSample;
            sampleFormat.floatingPoint = IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        }
        const DWORD streamFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
            AUDCLNT_STREAMFLAGS_NOPERSIST | (loopback_ ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
        result = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, streamFlags, 0, 0, mixFormat, nullptr);
        CoTaskMemFree(mixFormat);
        if (FAILED(result)) {
            SetError("IAudioClient::Initialize failed: " + std::to_string(result));
            throw std::runtime_error("IAudioClient initialization failed");
        }
        result = audioClient->SetEventHandle(sampleEvent_);
        if (FAILED(result)) {
            SetError("IAudioClient::SetEventHandle failed: " + std::to_string(result));
            throw std::runtime_error("SetEventHandle failed");
        }
        ComPtr<IAudioCaptureClient> captureClient;
        result = audioClient->GetService(IID_PPV_ARGS(&captureClient));
        if (FAILED(result)) {
            SetError("Could not obtain IAudioCaptureClient: " + std::to_string(result));
            throw std::runtime_error("CaptureClient service failed");
        }
        ComPtr<IAudioClock> audioClock;
        result = audioClient->GetService(IID_PPV_ARGS(&audioClock));
        if (SUCCEEDED(result)) {
            UINT64 frequency = 0;
            if (SUCCEEDED(audioClock->GetFrequency(&frequency))) {
                std::scoped_lock lock(mutex_);
                statistics_.audioClockFrequency = frequency;
            }
        }
        result = audioClient->Start();
        if (FAILED(result)) {
            SetError("IAudioClient::Start failed: " + std::to_string(result));
            throw std::runtime_error("Audio start failed");
        }
        clientStarted = true;
        running_.store(true);
        {
            std::scoped_lock lock(mutex_);
            initialized_ = true;
            initializationFinished_ = true;
        }
        initializedCondition_.notify_all();

        HANDLE events[] = { stopEvent_, sampleEvent_ };
        while (WaitForMultipleObjects(2, events, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
            UINT32 nextPacketFrames = 0;
            while (SUCCEEDED(captureClient->GetNextPacketSize(&nextPacketFrames)) && nextPacketFrames > 0) {
                BYTE* data = nullptr;
                UINT32 frameCount = 0;
                DWORD flags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;
                result = captureClient->GetBuffer(&data, &frameCount, &flags, &devicePosition, &qpcPosition);
                if (FAILED(result)) {
                    SetError("IAudioCaptureClient::GetBuffer failed: " + std::to_string(result));
                    SetEvent(stopEvent_);
                    break;
                }
                {
                    std::scoped_lock lock(mutex_);
                    if (statistics_.packetCount == 0) {
                        statistics_.firstDevicePosition = devicePosition;
                        statistics_.firstQpc100ns = qpcPosition;
                    }
                    ++statistics_.packetCount;
                    statistics_.frameCount += frameCount;
                    statistics_.lastDevicePosition = devicePosition;
                    statistics_.lastQpc100ns = qpcPosition;
                    if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) ++statistics_.silentPacketCount;
                    if ((flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0) ++statistics_.discontinuityCount;
                    if ((flags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR) != 0) ++statistics_.timestampErrorCount;
                }
                FFFResult callbackResult = FFFResult::Success;
                if (packetCallback_) {
                    try {
                        callbackResult = packetCallback_(data, frameCount, flags, qpcPosition, sampleFormat);
                    } catch (...) {
                        callbackResult = FFFResult::NativeFailure;
                    }
                }
                captureClient->ReleaseBuffer(frameCount);
                if (callbackResult != FFFResult::Success) {
                    SetError("The audio packet consumer rejected a WASAPI packet.");
                    SetEvent(stopEvent_);
                    break;
                }
                captureClient->GetNextPacketSize(&nextPacketFrames);
            }
        }
    } catch (...) {
        std::scoped_lock lock(mutex_);
        if (!initializationFinished_) {
            initialized_ = false;
            initializationFinished_ = true;
            initializedCondition_.notify_all();
        }
    }
    if (clientStarted) audioClient->Stop();
    running_.store(false);
    if (SUCCEEDED(comResult)) CoUninitialize();
    if (clientStarted && !stopRequested_.load() && failureCallback_) {
        const auto failure = LastError();
        if (!failure.empty()) {
            try { failureCallback_(failure); } catch (...) {}
        }
    }
}

// 保存采集线程的最近错误。赋值失败时使用固定后备文本；函数不抛异常，也不让 C++ 异常越过
// 线程入口或 DLL 边界。
void WasapiCapture::SetError(std::string message) noexcept {
    std::scoped_lock lock(mutex_);
    try {
        lastError_ = std::move(message);
    } catch (...) {
        lastError_ = "WASAPI error text allocation failed.";
    }
}
