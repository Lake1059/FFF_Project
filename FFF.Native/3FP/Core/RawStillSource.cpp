#include "pch.h"

#include "3FP/Core/RawStillSource.h"

#include <libraw/libraw.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

namespace {

// A function's address is enough to identify the module that owns it; using
// the DLL's own image avoids depending on its filename or the process CWD.
void ModuleProbe() noexcept {}

std::mutex g_librawMutex;
HMODULE g_librawModule = nullptr;
bool g_librawAttempted = false;

bool LoadLibRawRuntime() noexcept {
    std::lock_guard<std::mutex> lock(g_librawMutex);
    if (g_librawAttempted) return g_librawModule != nullptr;
    g_librawAttempted = true;

    HMODULE self = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ModuleProbe), &self) && self != nullptr) {
        wchar_t path[MAX_PATH]{};
        const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
        if (length > 0 && length < MAX_PATH) {
            std::wstring dllPath(path, length);
            const std::size_t slash = dllPath.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                dllPath.resize(slash + 1);
                dllPath += L"libraw.dll";
                g_librawModule = LoadLibraryExW(dllPath.c_str(), nullptr,
                    LOAD_WITH_ALTERED_SEARCH_PATH);
            }
        }
    }
    // Last resort: the normal DLL search path. A host that ships the DLL beside
    // FFF.Native.dll takes the branch above; this keeps developer builds working
    // before the post-build copy has run.
    if (g_librawModule == nullptr) g_librawModule = LoadLibraryW(L"libraw.dll");
    return g_librawModule != nullptr;
}

const char* RawError(int code) noexcept {
    const char* text = libraw_strerror(code);
    return text != nullptr ? text : "LibRaw error";
}

void CopyCString(char* dst, std::size_t dstSize, const char* src) noexcept {
    if (dst == nullptr || dstSize == 0) return;
    dst[0] = '\0';
    if (src == nullptr) return;
    std::size_t i = 0;
    for (; i + 1 < dstSize && src[i] != '\0'; ++i) dst[i] = src[i];
    dst[i] = '\0';
}

bool IsX3FPath(const std::string& pathUtf8) noexcept {
    const std::size_t dot = pathUtf8.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= pathUtf8.size()) return false;
    const std::size_t length = pathUtf8.size() - dot - 1;
    if (length != 3) return false;
    return std::tolower(static_cast<unsigned char>(pathUtf8[dot + 1])) == 'x' &&
        std::tolower(static_cast<unsigned char>(pathUtf8[dot + 2])) == '3' &&
        std::tolower(static_cast<unsigned char>(pathUtf8[dot + 3])) == 'f';
}

bool HasX3FSupport() noexcept {
    using CapabilitiesFn = unsigned int (*)();
    const auto capabilities = reinterpret_cast<CapabilitiesFn>(
        GetProcAddress(g_librawModule, "libraw_capabilities"));
    if (capabilities == nullptr) return false;
    return (capabilities() & LIBRAW_CAPS_X3FTOOLS) != 0;
}

int LibRawQuality(FFF3FPRawDemosaic demosaic) noexcept {
    switch (demosaic) {
    case FFF3FPRawDemosaic::Linear: return 0;
    case FFF3FPRawDemosaic::Vng: return 1;
    case FFF3FPRawDemosaic::Ppg: return 2;
    case FFF3FPRawDemosaic::Ahd: return 3;
    case FFF3FPRawDemosaic::Dcb: return 4;
    default: return 3;
    }
}

bool IsValidOptions(const FFF3FPRawOptions& o) noexcept {
    if (o.size != sizeof(FFF3FPRawOptions) || o.version != 1) return false;
    if (o.demosaic < static_cast<std::int32_t>(FFF3FPRawDemosaic::Linear) ||
        o.demosaic > static_cast<std::int32_t>(FFF3FPRawDemosaic::Dcb)) return false;
    if (o.whiteBalance < static_cast<std::int32_t>(FFF3FPRawWhiteBalance::Camera) ||
        o.whiteBalance > static_cast<std::int32_t>(FFF3FPRawWhiteBalance::Daylight)) return false;
    // Adobe RGB / ProPhoto are declared for ABI stability but not implemented
    // until the colour-management step (M2). Rejecting them here is safer than
    // silently displaying mistagged primaries.
    if (o.outputColor != static_cast<std::int32_t>(FFF3FPRawOutputColor::Srgb) &&
        o.outputColor != static_cast<std::int32_t>(FFF3FPRawOutputColor::LinearSrgb)) return false;
    if (o.halfSize != 0 && o.halfSize != 1) return false;
    if (o.applyToneCurve != 0 && o.applyToneCurve != 1) return false;
    if (o.highlight < static_cast<std::int32_t>(FFF3FPRawHighlight::Clip) ||
        o.highlight > static_cast<std::int32_t>(FFF3FPRawHighlight::Blend)) return false;
    if (o.maxOutputSide < 0) return false;
    return true;
}

std::pair<int, int> ClampOutputSide(int width, int height, int maxSide) noexcept {
    if (maxSide <= 0 || (width <= maxSide && height <= maxSide)) return {width, height};
    const double scale = static_cast<double>(maxSide) / static_cast<double>(std::max(width, height));
    const int w = std::max(1, static_cast<int>(width * scale + 0.5));
    const int h = std::max(1, static_cast<int>(height * scale + 0.5));
    return {w, h};
}

AVPixelFormat PixelFormatFor(int colors, int bits) noexcept {
    if (colors == 4) return bits == 16 ? AV_PIX_FMT_RGBA64LE : AV_PIX_FMT_RGBA;
    if (colors == 1) return bits == 16 ? AV_PIX_FMT_GRAY16LE : AV_PIX_FMT_GRAY8;
    if (colors == 3) return bits == 16 ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24;
    return AV_PIX_FMT_NONE;
}

struct RawMemOwner {
    // The AVFrame may outlive the RawStillSource that produced it: the
    // session keeps a still-image frame across the next Open so the old
    // picture stays on screen. Sharing LibRaw's lifetime with the frame
    // buffer makes dcraw_clear_mem valid even after rawStill_ is reset.
    std::shared_ptr<LibRaw> raw;
    libraw_processed_image_t* image = nullptr;
    ~RawMemOwner() {
        if (image != nullptr) LibRaw::dcraw_clear_mem(image);
    }
};

void FreeMemImage(void* opaque, std::uint8_t* /*data*/) noexcept {
    RawTrace("FreeMemImage enter");
    delete static_cast<RawMemOwner*>(opaque);
    RawTrace("FreeMemImage done");
}

void SetSdrColor(AVFrame* frame, bool linear) noexcept {
    frame->color_primaries = AVCOL_PRI_BT709;
    frame->color_trc = linear ? AVCOL_TRC_LINEAR : AVCOL_TRC_IEC61966_2_1;
    frame->colorspace = AVCOL_SPC_BT709;
    frame->color_range = AVCOL_RANGE_JPEG;
}

AVFrame* ScaleFrame(AVFrame* source, int width, int height) noexcept {
    if (source == nullptr || width <= 0 || height <= 0 ||
        (source->width == width && source->height == height)) return source;
    AVFrame* dst = av_frame_alloc();
    if (dst == nullptr) return nullptr;
    dst->format = source->format;
    dst->width = width;
    dst->height = height;
    dst->color_primaries = source->color_primaries;
    dst->color_trc = source->color_trc;
    dst->colorspace = source->colorspace;
    dst->color_range = source->color_range;
    dst->sample_aspect_ratio = source->sample_aspect_ratio;
    if (av_frame_get_buffer(dst, 0) < 0) {
        av_frame_free(&dst);
        return nullptr;
    }
    SwsContext* sws = sws_getContext(source->width, source->height,
        static_cast<AVPixelFormat>(source->format), width, height,
        static_cast<AVPixelFormat>(source->format), SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws == nullptr) {
        av_frame_free(&dst);
        return nullptr;
    }
    sws_scale(sws, source->data, source->linesize, 0, source->height, dst->data, dst->linesize);
    sws_freeContext(sws);
    av_frame_free(&source);
    return dst;
}

} // namespace

void RawTrace(const char* text) noexcept {
    if (text == nullptr) return;
    // Diagnostic only. The production path must not write a log for every
    // RAW open; set FFF_RAW_TRACE=1 when diagnosing a specific sample.
    static const bool enabled =
        GetEnvironmentVariableW(L"FFF_RAW_TRACE", nullptr, 0) != 0;
    if (!enabled) return;
    OutputDebugStringA(text);
    std::FILE* file = nullptr;
    if (fopen_s(&file, "rawtrace.log", "a") == 0 && file != nullptr) {
        std::fprintf(file, "%s\n", text);
        std::fclose(file);
    }
}

bool ValidateRawOptions(const FFF3FPRawOptions& options) noexcept {
    return IsValidOptions(options);
}

struct RawStillSource::Impl {
    std::shared_ptr<LibRaw> raw;
    FFF3FPRawOptions options{};
    bool opened = false;
};

FFF3FPRawOptions DefaultRawOptions() noexcept {    FFF3FPRawOptions o{};
    o.size = sizeof(o);
    o.version = 1;
    o.demosaic = static_cast<std::int32_t>(FFF3FPRawDemosaic::Ahd);
    o.whiteBalance = static_cast<std::int32_t>(FFF3FPRawWhiteBalance::Camera);
    o.outputColor = static_cast<std::int32_t>(FFF3FPRawOutputColor::Srgb);
    o.halfSize = 1;
    o.applyToneCurve = 1;
    o.highlight = static_cast<std::int32_t>(FFF3FPRawHighlight::Clip);
    o.maxOutputSide = 0;
    return o;
}

RawStillSource::RawStillSource() noexcept : impl_(std::make_unique<Impl>()) {}
RawStillSource::~RawStillSource() = default;

bool RawStillSource::EnsureRuntime() noexcept {
    return LoadLibRawRuntime();
}

bool RawStillSource::LooksLikeRawPath(const std::string& pathUtf8) noexcept {
    const std::size_t dot = pathUtf8.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= pathUtf8.size()) return false;
    std::string ext;
    ext.reserve(pathUtf8.size() - dot - 1);
    for (std::size_t i = dot + 1; i < pathUtf8.size(); ++i) {
        ext.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(pathUtf8[i]))));
    }
    static const char* const kExtensions[] = {
        "cr2", "cr3", "crw", "nef", "nrw", "arw", "srf", "sr2", "raf", "orf",
        "rw2", "rwl", "pef", "srw", "dng", "3fr", "fff", "iiq", "iiw", "mos", "mrw",
        "x3f", "gpr", "erf", "kdc", "k25", "dcr", "raw", "rwz", "cine", "ia", "cs1",
        "sti", "mef"
    };
    for (const char* candidate : kExtensions) {
        if (ext == candidate) return true;
    }
    return false;
}

bool RawStillSource::Open(const std::string& pathUtf8, const FFF3FPRawOptions& options,
    std::string& error) noexcept {
    RawTrace("RawStillSource::Open enter");
    error.clear();
    info_ = {};
    info_.size = sizeof(info_);
    info_.version = 1;
    impl_->opened = false;
    impl_->raw.reset();
    impl_->options = IsValidOptions(options) ? options : DefaultRawOptions();
    info_.demosaic = impl_->options.demosaic;
    info_.whiteBalance = impl_->options.whiteBalance;
    info_.outputColor = impl_->options.outputColor;
    info_.halfSize = impl_->options.halfSize;
    info_.applyToneCurve = impl_->options.applyToneCurve;
    info_.highlight = impl_->options.highlight;
    info_.maxOutputSide = impl_->options.maxOutputSide;

    if (!EnsureRuntime()) {
        error = "libraw.dll not found beside FFF.Native.dll";
        return false;
    }
    // .x3f 不是普通 LibRaw 解码器：只有启用 USE_X3FTOOLS 的构建才带这个
    // 位。没有该能力时必须在打开前说清楚，不能让用户只看到笼统的 -2。
    if (IsX3FPath(pathUtf8) && !HasX3FSupport()) {
        error = "当前 libraw.dll 未编译 X3F 支持（需要 USE_X3FTOOLS）";
        return false;
    }
    try {
        impl_->raw = std::make_shared<LibRaw>();
    } catch (...) {
        error = "LibRaw allocation failed";
        return false;
    }

    const int openResult = impl_->raw->open_file(pathUtf8.c_str());
    if (openResult != LIBRAW_SUCCESS) {
        error = std::string("LibRaw open_file: ") + RawError(openResult);
        impl_->raw.reset();
        return false;
    }
    RawTrace("RawStillSource::open_file ok");

    const auto& sizes = impl_->raw->imgdata.sizes;
    int width = static_cast<int>(sizes.width);
    int height = static_cast<int>(sizes.height);
    if (width <= 0 || height <= 0) {
        // Some DNG variants leave the visible crop unset until unpack; the
        // sensor dimensions still describe a usable frame.
        width = static_cast<int>(sizes.raw_width);
        height = static_cast<int>(sizes.raw_height);
    }
    if (impl_->options.halfSize) {
        width = (width + 1) / 2;
        height = (height + 1) / 2;
    }
    // LibRaw's mem-image writer swaps width/height when flip & 4, so the
    // reported output dimensions must do the same. The renderer is never
    // asked to rotate a RAW frame.
    if ((sizes.flip & 4) != 0) std::swap(width, height);
    const auto clamped = ClampOutputSide(width, height, impl_->options.maxOutputSide);
    width = clamped.first;
    height = clamped.second;
    if (width <= 0 || height <= 0) {
        error = "LibRaw reported empty output dimensions";
        impl_->raw.reset();
        return false;
    }

    info_.rawWidth = static_cast<std::int32_t>(sizes.raw_width);
    info_.rawHeight = static_cast<std::int32_t>(sizes.raw_height);
    info_.outputWidth = width;
    info_.outputHeight = height;
    info_.flip = sizes.flip;
    CopyCString(info_.cameraMake, sizeof(info_.cameraMake), impl_->raw->imgdata.idata.make);
    CopyCString(info_.cameraModel, sizeof(info_.cameraModel), impl_->raw->imgdata.idata.model);
    CopyCString(info_.lens, sizeof(info_.lens), impl_->raw->imgdata.lens.Lens);
    CopyCString(info_.cfaDescription, sizeof(info_.cfaDescription), impl_->raw->imgdata.idata.cdesc);
    CopyCString(info_.libRawVersion, sizeof(info_.libRawVersion), libraw_version());

    info_.iso = static_cast<std::int32_t>(impl_->raw->imgdata.other.iso_speed + 0.5f);
    info_.shutterSeconds = impl_->raw->imgdata.other.shutter;
    info_.aperture = impl_->raw->imgdata.other.aperture;
    info_.focalLengthMm = impl_->raw->imgdata.other.focal_len;
    info_.timestampUnix = static_cast<std::int64_t>(impl_->raw->imgdata.other.timestamp);
    info_.blackLevel = impl_->raw->imgdata.color.black;
    info_.whiteLevel = impl_->raw->imgdata.color.maximum;
    info_.rawBits = static_cast<std::int32_t>(impl_->raw->imgdata.color.raw_bps);
    for (int i = 0; i < 4; ++i) info_.cameraMultipliers[i] = impl_->raw->imgdata.color.cam_mul[i];

    // CFA pattern from filters, not from cdesc: cdesc only names the colours.
    // Filters==9 is X-Trans, which has no 2x2 tile to report.
    const unsigned filters = impl_->raw->imgdata.idata.filters;
    if (filters == 9) {
        CopyCString(info_.cfaDescription, sizeof(info_.cfaDescription), "X-Trans");
        info_.cfaPattern = 0;
    } else if (filters != 0 && impl_->raw->imgdata.idata.colors == 3) {
        auto channel = [](unsigned c) -> char {
            switch (c & 3u) {
            case 0: return 'R';
            case 2: return 'B';
            default: return 'G';
            }
        };
        const char c00 = channel(filters);
        const char c01 = channel(filters >> 2);
        const char c10 = channel(filters >> 4);
        const char c11 = channel(filters >> 6);
        char pattern[5] = {c00, c01, c10, c11, '\0'};
        CopyCString(info_.cfaDescription, sizeof(info_.cfaDescription), pattern);
        if (std::strcmp(pattern, "RGGB") == 0) info_.cfaPattern = 1;
        else if (std::strcmp(pattern, "BGGR") == 0) info_.cfaPattern = 2;
        else if (std::strcmp(pattern, "GRBG") == 0) info_.cfaPattern = 3;
        else if (std::strcmp(pattern, "GBRG") == 0) info_.cfaPattern = 4;
    }

    impl_->opened = true;
    RawTrace("RawStillSource::Open done");
    return true;
}

bool RawStillSource::Decode(AVFrame** frame, std::string& error) noexcept {
    RawTrace("RawStillSource::Decode enter");
    error.clear();
    if (frame == nullptr) {
        error = "null output frame";
        return false;
    }
    *frame = nullptr;
    if (!impl_->opened || impl_->raw == nullptr) {
        error = "RAW source is not open";
        return false;
    }

    LibRaw& raw = *impl_->raw;
    auto& params = raw.imgdata.params;
    params.half_size = impl_->options.halfSize;
    params.output_bps = 16;
    params.user_qual = LibRawQuality(static_cast<FFF3FPRawDemosaic>(impl_->options.demosaic));
    params.output_color = 1; // sRGB primaries; LinearSrgb differs only in gamma.
    params.highlight = impl_->options.highlight ==
        static_cast<std::int32_t>(FFF3FPRawHighlight::Blend) ? 1 : 0;
    if (impl_->options.applyToneCurve != 0) {
        params.gamm[0] = 0.45;
        params.gamm[1] = 4.5;
    } else {
        params.gamm[0] = 1.0;
        params.gamm[1] = 0.0;
    }
    params.gamm[2] = params.gamm[3] = params.gamm[4] = params.gamm[5] = 0.0;
    params.use_camera_wb = impl_->options.whiteBalance ==
        static_cast<std::int32_t>(FFF3FPRawWhiteBalance::Camera) ? 1 : 0;
    params.use_auto_wb = impl_->options.whiteBalance ==
        static_cast<std::int32_t>(FFF3FPRawWhiteBalance::Auto) ? 1 : 0;
    params.user_flip = -1;
    params.no_auto_bright = 0;

    int result = raw.unpack();
    if (result != LIBRAW_SUCCESS) {
        error = std::string("LibRaw unpack: ") + RawError(result);
        return false;
    }
    RawTrace("RawStillSource::unpack ok");
    result = raw.dcraw_process();
    if (result != LIBRAW_SUCCESS) {
        error = std::string("LibRaw dcraw_process: ") + RawError(result);
        return false;
    }
    RawTrace("RawStillSource::dcraw_process ok");

    int memError = LIBRAW_SUCCESS;
    libraw_processed_image_t* mem = raw.dcraw_make_mem_image(&memError);
    if (mem == nullptr || memError != LIBRAW_SUCCESS) {
        error = std::string("LibRaw dcraw_make_mem_image: ") + RawError(memError);
        return false;
    }
    RawTrace("RawStillSource::mem_image ok");
    AVPixelFormat format = PixelFormatFor(static_cast<int>(mem->colors), static_cast<int>(mem->bits));
    if (format == AV_PIX_FMT_NONE) {
        error = "LibRaw returned an unsupported channel/bit layout";
        LibRaw::dcraw_clear_mem(mem);
        return false;
    }

    AVFrame* f = av_frame_alloc();
    if (f == nullptr) {
        error = "av_frame_alloc failed";
        LibRaw::dcraw_clear_mem(mem);
        return false;
    }
    f->format = format;
    f->width = static_cast<int>(mem->width);
    f->height = static_cast<int>(mem->height);
    f->pts = 0;
    f->pkt_dts = AV_NOPTS_VALUE;
    f->best_effort_timestamp = 0;
    f->duration = 1;
    f->sample_aspect_ratio = AVRational{1, 1};
    SetSdrColor(f, impl_->options.outputColor ==
        static_cast<std::int32_t>(FFF3FPRawOutputColor::LinearSrgb));
    f->data[0] = mem->data;
    f->linesize[0] = f->width * (mem->colors == 1 ? 1 : mem->colors) * (mem->bits / 8);
    auto* owner = new (std::nothrow) RawMemOwner{impl_->raw, mem};
    if (owner == nullptr) {
        error = "RAW frame owner allocation failed";
        av_frame_free(&f);
        LibRaw::dcraw_clear_mem(mem);
        return false;
    }
    f->buf[0] = av_buffer_create(reinterpret_cast<std::uint8_t*>(mem),
        static_cast<int>(sizeof(libraw_processed_image_t) + mem->data_size - 1),
        &FreeMemImage, owner, 0);
    if (f->buf[0] == nullptr) {
        error = "av_buffer_create failed";
        av_frame_free(&f);
        delete owner;
        return false;
    }
    f->extended_data = f->data;
    RawTrace("RawStillSource::frame buffer ok");

    // M1 honours maxOutputSide with a single sws pass. The common path
    // (maxOutputSide==0) never reaches this branch.
    if (f->width != info_.outputWidth || f->height != info_.outputHeight) {
        AVFrame* scaled = ScaleFrame(f, info_.outputWidth, info_.outputHeight);
        if (scaled == nullptr) {
            error = "RAW output downscale failed";
            av_frame_free(&f);
            return false;
        }
        f = scaled;
    }
    *frame = f;
    RawTrace("RawStillSource::Decode done");
    return true;
}

AVFormatContext* RawStillSource::CreateFormatContext(std::string& error) noexcept {
    RawTrace("RawStillSource::CreateFormatContext enter");
    error.clear();
    if (!impl_->opened) {
        error = "RAW source is not open";
        return nullptr;
    }
    AVFormatContext* context = avformat_alloc_context();
    if (context == nullptr) {
        error = "avformat_alloc_context failed";
        return nullptr;
    }
    static AVInputFormat rawPipe = [] {
        AVInputFormat format{};
        format.name = "3fp_raw_pipe";
        format.long_name = "3FP LibRaw still image";
        format.flags = AVFMT_NOFILE | AVFMT_NOTIMESTAMPS;
        return format;
    }();
    context->iformat = &rawPipe;
    context->flags |= AVFMT_FLAG_CUSTOM_IO;
    context->duration = 0;

    AVStream* stream = avformat_new_stream(context, nullptr);
    if (stream == nullptr) {
        avformat_free_context(context);
        error = "avformat_new_stream failed";
        return nullptr;
    }
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    stream->codecpar->format = AV_PIX_FMT_RGB48LE;
    stream->codecpar->width = info_.outputWidth;
    stream->codecpar->height = info_.outputHeight;
    stream->codecpar->color_primaries = AVCOL_PRI_BT709;
    stream->codecpar->color_trc = impl_->options.outputColor ==
        static_cast<std::int32_t>(FFF3FPRawOutputColor::LinearSrgb) ?
        AVCOL_TRC_LINEAR : AVCOL_TRC_IEC61966_2_1;
    stream->codecpar->color_space = AVCOL_SPC_BT709;
    stream->codecpar->color_range = AVCOL_RANGE_JPEG;
    stream->time_base = AVRational{1, 1};
    stream->avg_frame_rate = AVRational{1, 1};
    stream->r_frame_rate = AVRational{1, 1};
    stream->start_time = 0;
    stream->duration = 1;
    stream->nb_frames = 1;
    RawTrace("RawStillSource::CreateFormatContext done");
    return context;
}
