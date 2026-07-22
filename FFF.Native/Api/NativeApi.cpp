#include "pch.h"
#include "Api/FFF.Native.Api.h"
#include "Audio/WasapiCapture.h"
#include "Core/Session.h"
#include "Timeline/QpcTimeline.h"
#include "Ffmpeg/VideoMuxer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}

using Microsoft::WRL::ComPtr;

namespace {
constexpr std::uint32_t ApiVersion = 3;

// 把 UTF-8 值复制到调用方缓冲区，并报告包含末尾 NUL 的所需字节数。空缓冲区或容量不足属于
// 合法的容量查询，函数绝不会写入截断字符串。requiredSize 可为空，其余指针所有权均归调用方。
FFFResult CopyUtf8(const std::string& value, char* outputUtf8, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    const auto bytes = value.size() + 1;
    if (bytes > UINT32_MAX) return FFFResult::NativeFailure;
    if (requiredSize != nullptr) *requiredSize = static_cast<std::uint32_t>(bytes);
    if (outputUtf8 == nullptr || outputSize < bytes) return FFFResult::BufferTooSmall;
    std::memcpy(outputUtf8, value.c_str(), bytes);
    return FFFResult::Success;
}

// 将 NUL 结尾的 Windows UTF-16 字符串转换为 UTF-8，不依赖进程区域设置。空结果表示输入为空
// 或包含非法 UTF-16，端点枚举据此跳过该设备；返回字符串完全由调用方值语义持有。
std::string ToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') return {};
    const auto required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
        nullptr, 0, nullptr, nullptr);
    if (required <= 1) return {};
    std::string result(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1,
        result.data(), required, nullptr, nullptr);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

// 将严格 UTF-8 字符串转换为 Windows UTF-16。转换使用 MB_ERR_INVALID_CHARS，任何非法字节都
// 返回空字符串而不是依据系统代码页替换字符，确保 WASAPI 端点 ID 不会被静默改写。
std::wstring FromUtf8(const char* value) {
    if (value == nullptr || *value == '\0') return {};
    const auto required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
    if (required <= 1) return {};
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), required);
    result.resize(static_cast<std::size_t>(required - 1));
    return result;
}

// 为发现接口返回的小型 JSON 文档转义一个 UTF-8 值。非 ASCII 字节保持不变；ASCII 控制字符、
// 引号和反斜线按 JSON 规则转义。函数不验证传入文本是否为完整 UTF-8。
std::string EscapeJson(const std::string& value) {
    std::ostringstream output;
    static constexpr char Hex[] = "0123456789abcdef";
    for (const auto raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        switch (character) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (character < 0x20) output << "\\u00" << Hex[character >> 4] << Hex[character & 15];
            else output << raw;
            break;
        }
    }
    return output.str();
}

// 使用固定栈缓冲区把 FFmpeg 负错误码转换为可读文本。FFmpeg 持有的内存不会离开本函数；
// av_strerror 自身失败时保留数值错误码，便于后续诊断。
std::string FfmpegError(const int error) {
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error, buffer, sizeof(buffer)) == 0) return buffer;
    return "FFmpeg error " + std::to_string(error);
}

// 把一种 WASAPI 数据流的全部活动端点追加到 JSON 数组。每次循环内都会释放 COM 指针和由
// GetId 分配的端点标识，因此单个异常设备不会泄漏资源，也不会破坏已经生成的结果。
HRESULT AppendEndpoints(IMMDeviceEnumerator* enumerator, const EDataFlow flow, const char* kind,
    bool& first, std::ostringstream& json) {
    ComPtr<IMMDeviceCollection> collection;
    auto result = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(result)) return result;
    UINT count = 0;
    result = collection->GetCount(&count);
    if (FAILED(result)) return result;
    for (UINT index = 0; index < count; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, &device))) continue;
        LPWSTR id = nullptr;
        if (FAILED(device->GetId(&id))) continue;
        const auto idUtf8 = ToUtf8(id);
        CoTaskMemFree(id);

        ComPtr<IPropertyStore> properties;
        PROPVARIANT nameValue{};
        PropVariantInit(&nameValue);
        std::string name;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
            SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &nameValue)) &&
            nameValue.vt == VT_LPWSTR) name = ToUtf8(nameValue.pwszVal);
        PropVariantClear(&nameValue);
        if (idUtf8.empty()) continue;
        if (!first) json << ',';
        first = false;
        json << "{\"type\":\"" << kind << "\",\"id\":\"" << EscapeJson(idUtf8)
             << "\",\"name\":\"" << EscapeJson(name) << "\"}";
    }
    return S_OK;
}
}

// 返回产品自有 C ABI 的主版本。本函数不分配内存，不依赖会话或 COM 初始化，可用于最早阶段
// 诊断托管层与 Native DLL 不匹配的问题。
std::uint32_t FFF_GetApiVersion() noexcept { return ApiVersion; }

// 执行确定性的 QPC 与链接库版本检查，不打开音频/GPU 设备或文件。结果使用 CopyUtf8 定义的
// 两次调用 UTF-8 容量约定，因此第一次返回 BufferTooSmall 属于正常行为而非测试失败。
FFFResult FFF_RunSelfTest(char* outputUtf8, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    try {
        QpcTimeline timeline;
        const auto first = timeline.Now();
        const auto second = timeline.Now();
        const bool clockOk = timeline.Frequency() > 0 && second >= first;
        const bool versionsOk = AV_VERSION_MAJOR(avcodec_version()) == LIBAVCODEC_VERSION_MAJOR &&
            AV_VERSION_MAJOR(avformat_version()) == LIBAVFORMAT_VERSION_MAJOR &&
            AV_VERSION_MAJOR(avutil_version()) == LIBAVUTIL_VERSION_MAJOR &&
            AV_VERSION_MAJOR(swresample_version()) == LIBSWRESAMPLE_VERSION_MAJOR;
        const std::string json = std::string("{\"passed\":") + (clockOk && versionsOk ? "true" : "false") +
            ",\"qpcFrequency\":" + std::to_string(timeline.Frequency()) +
            ",\"ffmpegMajorsMatch\":" + (versionsOk ? "true" : "false") + "}";
        return CopyUtf8(json, outputUtf8, outputSize, requiredSize);
    } catch (...) { return FFFResult::NativeFailure; }
}

// 报告 Windows Loader 实际解析到的 FFmpeg 精确版本。这里检查产品支持的 ABI 信息，但无法也
// 刻意不保证把二进制不兼容 DLL 重命名后强行加载的安全性。
FFFResult FFF_GetRuntimeInfo(char* outputUtf8, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    try {
        const std::string json = "{\"apiVersion\":" + std::to_string(ApiVersion) +
            ",\"ffmpegVersion\":\"" + EscapeJson(av_version_info()) +
            "\",\"avcodec\":" + std::to_string(avcodec_version()) +
            ",\"avformat\":" + std::to_string(avformat_version()) +
            ",\"avutil\":" + std::to_string(avutil_version()) +
            ",\"swresample\":" + std::to_string(swresample_version()) + "}";
        return CopyUtf8(json, outputUtf8, outputSize, requiredSize);
    } catch (...) { return FFFResult::NativeFailure; }
}

// 通过 MMDevice 直接枚举活动的系统播放与麦克风端点，不经过 NAudio。函数尽可能自行初始化
// MTA；若调用线程已经选择其他 COM Apartment，则沿用现有初始化且不错误地 CoUninitialize。
FFFResult FFF_EnumerateAudioEndpoints(char* outputUtf8, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    const auto initialization = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialization);
    if (FAILED(initialization) && initialization != RPC_E_CHANGED_MODE) return FFFResult::DeviceFailure;
    try {
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(&enumerator)))) {
            if (uninitialize) CoUninitialize();
            return FFFResult::DeviceFailure;
        }
        std::ostringstream json;
        json << '[';
        bool first = true;
        const auto render = AppendEndpoints(enumerator.Get(), eRender, "render", first, json);
        const auto capture = AppendEndpoints(enumerator.Get(), eCapture, "microphone", first, json);
        json << ']';
        if (uninitialize) CoUninitialize();
        if (FAILED(render) || FAILED(capture)) return FFFResult::DeviceFailure;
        return CopyUtf8(json.str(), outputUtf8, outputSize, requiredSize);
    } catch (...) {
        if (uninitialize) CoUninitialize();
        return FFFResult::NativeFailure;
    }
}

// 对指定端点执行一次短时事件驱动 WASAPI 采集，并返回包级时钟统计。loopback 非零时端点必须
// 是 render 设备；duration 限制在 50–10000 ms，避免互操作调用无限占用线程。函数只做采集
// 自检，不保存样本内容，也不会把包到达时间当作采集时间。
FFFResult FFF_TestAudioEndpoint(const char* endpointIdUtf8, const std::uint32_t loopback,
    const std::uint32_t durationMilliseconds, char* outputUtf8, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    if (durationMilliseconds < 50 || durationMilliseconds > 10'000) return FFFResult::InvalidArgument;
    try {
        auto endpointId = FromUtf8(endpointIdUtf8);
        if (endpointId.empty()) return FFFResult::InvalidArgument;
        WasapiCapture capture(std::move(endpointId), loopback != 0);
        const auto started = capture.Start();
        if (started != FFFResult::Success) {
            const auto copied = CopyUtf8("{\"passed\":false,\"error\":\"" +
                EscapeJson(capture.LastError()) + "\"}", outputUtf8, outputSize, requiredSize);
            return copied == FFFResult::Success ? started : copied;
        }
        Sleep(durationMilliseconds);
        capture.Stop();
        const auto statistics = capture.Statistics();
        const std::string json = "{\"passed\":" + std::string(statistics.packetCount > 0 ? "true" : "false") +
            ",\"packets\":" + std::to_string(statistics.packetCount) +
            ",\"frames\":" + std::to_string(statistics.frameCount) +
            ",\"silentPackets\":" + std::to_string(statistics.silentPacketCount) +
            ",\"discontinuities\":" + std::to_string(statistics.discontinuityCount) +
            ",\"timestampErrors\":" + std::to_string(statistics.timestampErrorCount) +
            ",\"firstDevicePosition\":" + std::to_string(statistics.firstDevicePosition) +
            ",\"lastDevicePosition\":" + std::to_string(statistics.lastDevicePosition) +
            ",\"firstQpc100ns\":" + std::to_string(statistics.firstQpc100ns) +
            ",\"lastQpc100ns\":" + std::to_string(statistics.lastQpc100ns) +
            ",\"audioClockFrequency\":" + std::to_string(statistics.audioClockFrequency) +
            ",\"sampleRate\":" + std::to_string(statistics.sampleRate) +
            ",\"channels\":" + std::to_string(statistics.channelCount) +
            ",\"bitsPerSample\":" + std::to_string(statistics.bitsPerSample) +
            ",\"error\":\"" + EscapeJson(capture.LastError()) + "\"}";
        return CopyUtf8(json, outputUtf8, outputSize, requiredSize);
    } catch (...) {
        return FFFResult::NativeFailure;
    }
}

// 使用编码器实际接受的保守 8-bit 4:2:0 输入执行真实 avcodec_open2 初始化探测，而不是只检查
// 编码器名称。会话启动时还要结合具体适配器设备，重新探测 D3D11、10-bit 和 4:4:4 组合。
FFFResult FFF_ProbeEncoder(const char* encoderNameUtf8, const std::uint32_t width,
    const std::uint32_t height, const std::uint32_t frameRateNumerator,
    const std::uint32_t frameRateDenominator, char* outputUtf8, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    if (encoderNameUtf8 == nullptr || *encoderNameUtf8 == '\0' || width == 0 || height == 0 ||
        frameRateNumerator == 0 || frameRateDenominator == 0) return FFFResult::InvalidArgument;
    try {
        const AVCodec* codec = avcodec_find_encoder_by_name(encoderNameUtf8);
        if (codec == nullptr) {
            const auto copied = CopyUtf8("{\"supported\":false,\"reason\":\"encoder missing\"}",
                outputUtf8, outputSize, requiredSize);
            return copied == FFFResult::Success ? FFFResult::NotSupported : copied;
        }
        AVCodecContext* context = avcodec_alloc_context3(codec);
        if (context == nullptr) return FFFResult::NativeFailure;
        context->width = static_cast<int>(width);
        context->height = static_cast<int>(height);
        context->time_base = { static_cast<int>(frameRateDenominator), static_cast<int>(frameRateNumerator) };
        context->framerate = { static_cast<int>(frameRateNumerator), static_cast<int>(frameRateDenominator) };
        // Software encoders in this build intentionally expose planar YUV input;
        // NV12 is a hardware-facing format and makes x265/SVT-AV1 look unsupported.
        context->pix_fmt = (std::string(encoderNameUtf8) == "libx265" ||
            std::string(encoderNameUtf8) == "libsvtav1") ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
        context->bit_rate = 8'000'000;
        context->gop_size = std::max(1, static_cast<int>(frameRateNumerator / frameRateDenominator * 2));
        const auto opened = avcodec_open2(context, codec, nullptr);
        const std::string detail = opened >= 0
            ? "{\"supported\":true,\"encoder\":\"" + EscapeJson(encoderNameUtf8) + "\"}"
            : "{\"supported\":false,\"encoder\":\"" + EscapeJson(encoderNameUtf8) +
                "\",\"reason\":\"" + EscapeJson(FfmpegError(opened)) + "\"}";
        avcodec_free_context(&context);
        const auto copied = CopyUtf8(detail, outputUtf8, outputSize, requiredSize);
        if (copied != FFFResult::Success) return copied;
        return opened >= 0 ? FFFResult::Success : FFFResult::NotSupported;
    } catch (...) { return FFFResult::NativeFailure; }
}

// 使用与正式录制完全相同的 D3D11 hardware device/frame context 和编码器选项做适配器级探测，
// 但把 Matroska 写到 Windows NUL 设备且不提交帧。configuration 中的字符串仅在调用期间借用；
// 音频字段被忽略。返回 JSON 始终说明支持状态和 FFmpeg 原因，调用方应提供至少 4096 字节缓冲区
// 以避免为了容量查询重复初始化硬件编码器。
FFFResult FFF_ProbeD3D11Encoder(const FFFSessionConfiguration* configuration,
    char* outputUtf8, const std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept {
    if (configuration == nullptr || configuration->size < sizeof(FFFSessionConfiguration) ||
        configuration->version != 1 || configuration->d3d11Device == nullptr ||
        configuration->encoderNameUtf8 == nullptr) return FFFResult::InvalidArgument;
    try {
        VideoMuxer muxer;
        const std::vector<float> noAudioGains;
        const auto result = muxer.Initialize(static_cast<ID3D11Device*>(configuration->d3d11Device),
            "NUL", configuration->encoderNameUtf8, configuration->width, configuration->height,
            configuration->frameRateNumerator, configuration->frameRateDenominator,
            configuration->bitRate, configuration->gopSize, configuration->bFrameCount,
            configuration->tenBit != 0, configuration->hdr10 != 0, false,
            configuration->inputTextureFormat, configuration->chromaSampling,
            configuration->rateControl, configuration->qualityMode,
            configuration->customVideoParametersUtf8 == nullptr ? "" : configuration->customVideoParametersUtf8,
            configuration->quality, configuration->maximumBitRate,
            configuration->lookaheadFrames,
            configuration->presetUtf8 == nullptr ? "" : configuration->presetUtf8,
            configuration->profileUtf8 == nullptr ? "" : configuration->profileUtf8,
            configuration->sceneOptimizationUtf8 == nullptr ? "" : configuration->sceneOptimizationUtf8,
            configuration->multipass, configuration->colorRange, noAudioGains,
            "aac", 48'000, 2, 192'000, 0);
        const std::string json = result == FFFResult::Success
            ? "{\"supported\":true,\"encoder\":\"" + EscapeJson(configuration->encoderNameUtf8) + "\"}"
            : "{\"supported\":false,\"encoder\":\"" + EscapeJson(configuration->encoderNameUtf8) +
                "\",\"reason\":\"" + EscapeJson(muxer.LastError()) + "\"}";
        muxer.Abort();
        const auto copied = CopyUtf8(json, outputUtf8, outputSize, requiredSize);
        if (copied != FFFResult::Success) return copied;
        return result == FFFResult::Success ? FFFResult::Success : FFFResult::NotSupported;
    } catch (...) {
        return FFFResult::NativeFailure;
    }
}

// 验证带版本的配置并复制进不透明 C++ 会话。异常、CRT 分配所有权和 C++ 对象布局均不会越过
// 稳定 C 边界；成功返回的句柄必须由 FFF_DestroySession 释放。
FFFResult FFF_CreateSession(const FFFSessionConfiguration* configuration,
    FFFSessionHandle* session) noexcept {
    if (configuration == nullptr || session == nullptr ||
        configuration->size < sizeof(FFFSessionConfiguration) || configuration->version != 1)
        return FFFResult::InvalidArgument;
    try {
        *session = new RecorderSession(*configuration);
        return FFFResult::Success;
    } catch (...) {
        *session = nullptr;
        return FFFResult::NativeFailure;
    }
}

// 验证句柄非空后启动会话。详细失败消息由会话持有，调用方通过 FFF_GetLastError 复制读取。
FFFResult FFF_StartSession(const FFFSessionHandle session) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument : static_cast<RecorderSession*>(session)->Start();
}

// 提交一张借用的 D3D11 纹理和原始 QPC 时间戳。当前实现返回后不保留 COM 指针，调用方可立即复用。
FFFResult FFF_SubmitVideoTexture(const FFFSessionHandle session, void* texture,
    const std::uint32_t arrayIndex, const std::int64_t timestamp,
    const std::uint32_t submissionFlags) noexcept {
    if (session == nullptr) return FFFResult::InvalidArgument;
    return static_cast<RecorderSession*>(session)->Submit(static_cast<ID3D11Texture2D*>(texture), arrayIndex,
        timestamp, (submissionFlags & 1U) != 0);
}

// 把托管捕获层在提交前丢弃的源帧数量汇入原生会话统计。该接口只更新原子计数，不触碰纹理、
// 编码器或时间线，因此可在捕获线程决定替换旧帧时立即调用。
FFFResult FFF_ReportDroppedVideoFrames(const FFFSessionHandle session,
    const std::uint32_t frameCount) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument :
        static_cast<RecorderSession*>(session)->ReportDroppedFrames(frameCount);
}

// 把托管捕获后端的 resize、access-lost、目标关闭等事件交给原生会话统一记录和回调。两个
// UTF-8 指针只在调用期间借用；空事件名被拒绝，空消息允许。
FFFResult FFF_ReportDiagnosticEvent(const FFFSessionHandle session, const char* eventNameUtf8,
    const char* messageUtf8) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument :
        static_cast<RecorderSession*>(session)->ReportDiagnosticEvent(eventNameUtf8, messageUtf8);
}

// 用同一个原始 QPC 时间戳暂停会话中的全部媒体流，避免音频和视频各自形成不同暂停区间。
FFFResult FFF_PauseSession(const FFFSessionHandle session, const std::int64_t timestamp) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument : static_cast<RecorderSession*>(session)->Pause(timestamp);
}

// 恢复全部媒体流，并从媒体时间中扣除精确的共享暂停区间；时间戳不得早于暂停起点。
FFFResult FFF_ResumeSession(const FFFSessionHandle session, const std::int64_t timestamp) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument : static_cast<RecorderSession*>(session)->Resume(timestamp);
}

FFFResult FFF_SplitSession(const FFFSessionHandle session, const char* outputPathUtf8) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument :
        static_cast<RecorderSession*>(session)->Split(outputPathUtf8);
}

FFFResult FFF_SwitchSystemAudioEndpoint(const FFFSessionHandle session,
    const char* endpointIdUtf8) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument :
        static_cast<RecorderSession*>(session)->SwitchSystemAudioEndpoint(endpointIdUtf8);
}

// 请求正常终止转换。编码模块接入后，此调用负责排空编码器并完整写入 Matroska trailer。
FFFResult FFF_StopSession(const FFFSessionHandle session) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument : static_cast<RecorderSession*>(session)->Stop();
}

// 请求紧急清理，不保证写入正常 Matroska trailer；本路径用于不可恢复的设备或时间戳错误。
FFFResult FFF_AbortSession(const FFFSessionHandle session) noexcept {
    return session == nullptr ? FFFResult::InvalidArgument : static_cast<RecorderSession*>(session)->Abort();
}

// 将一次带版本的诊断快照复制到调用方内存，不返回任何指向会话内部状态的指针。
FFFResult FFF_GetSessionStatistics(const FFFSessionHandle session,
    FFFSessionStatistics* statistics) noexcept {
    if (session == nullptr || statistics == nullptr) return FFFResult::InvalidArgument;
    return static_cast<RecorderSession*>(session)->GetStatistics(*statistics);
}

// 使用标准两次调用 UTF-8 约定复制最新会话错误。requiredSize 包含末尾 NUL。
FFFResult FFF_GetLastError(const FFFSessionHandle session, char* outputUtf8,
    const std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept {
    if (session == nullptr) return FFFResult::InvalidArgument;
    try { return CopyUtf8(static_cast<RecorderSession*>(session)->LastError(), outputUtf8, outputSize, requiredSize); }
    catch (...) { return FFFResult::NativeFailure; }
}

// 删除由 FFF_CreateSession 分配的会话。显式允许空句柄，以简化 SafeHandle 的部分初始化清理。
void FFF_DestroySession(const FFFSessionHandle session) noexcept {
    delete static_cast<RecorderSession*>(session);
}
