#include "pch.h"
#include "3FP/Api/FFF.Player.Api.h"

#include <ass/ass.h>

#include <array>
#include <cstdarg>
#include <cstdio>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <limits>

namespace {
constexpr std::uint32_t ApiVersion = 1;
constexpr std::int64_t TicksPerMillisecond = 10'000;

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count) != count) return {};
    return result;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) != count) return {};
    return result;
}

bool IsFontFile(const std::filesystem::path& path) {
    auto extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](const wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    return extension == L".ttf" || extension == L".otf" || extension == L".ttc";
}

std::vector<char> ReadFile(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) throw std::runtime_error("Could not open the file.");
    const auto end = stream.tellg();
    if (end < 0 || static_cast<std::uint64_t>(end) >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        throw std::runtime_error("The file is too large.");
    std::vector<char> result(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!result.empty() && !stream.read(result.data(), static_cast<std::streamsize>(result.size())))
        throw std::runtime_error("Could not read the file.");
    return result;
}

class AssSubtitleRenderer final {
public:
    AssSubtitleRenderer() = default;
    ~AssSubtitleRenderer() { Close(); }
    AssSubtitleRenderer(const AssSubtitleRenderer&) = delete;
    AssSubtitleRenderer& operator=(const AssSubtitleRenderer&) = delete;

    FFFResult Open(const char* path, const char* fontDirectories) noexcept {
        if (path == nullptr || *path == '\0') return FFFResult::InvalidArgument;
        try {
            const auto widePath = Utf8ToWide(path);
            if (widePath.empty())
                return Fail("The ASS subtitle path is not valid UTF-8.", FFFResult::InvalidArgument);

            library_ = ass_library_init();
            if (library_ == nullptr)
                return Fail("Could not initialize libass.");
            ass_set_message_cb(library_, &AssMessageCallback, this);
            ass_set_extract_fonts(library_, 1);

            ParseFontDirectories(fontDirectories);
            AddMediaFonts();

            auto script = ReadFile(std::filesystem::path(widePath));
            if (script.empty()) return Fail("The ASS subtitle file is empty.");
            lastLibassMessage_.clear();
            track_ = ass_read_memory(library_, script.data(), script.size(), nullptr);
            if (track_ == nullptr)
                return Fail("Could not parse the ASS subtitle." + LibassDetail());

            renderer_ = ass_renderer_init(library_);
            if (renderer_ == nullptr)
                return Fail("Could not initialize the libass renderer." + LibassDetail());
            ass_set_cache_limits(renderer_, 1000, 128);
            ass_set_fonts(renderer_, nullptr, "Arial", ASS_FONTPROVIDER_AUTODETECT, nullptr, 1);
            return FFFResult::Success;
        } catch (const std::bad_alloc&) {
            return Fail("Could not allocate the ASS subtitle renderer.");
        } catch (const std::exception& error) {
            return Fail(std::string("Could not initialize the ASS subtitle renderer: ") + error.what());
        } catch (...) {
            return Fail("Could not initialize the ASS subtitle renderer.");
        }
    }

    FFFResult Render(const std::int64_t position, const std::int32_t width,
        const std::int32_t height, FFF3FPBitmapSubtitleFrame& output) noexcept {
        if (position < 0 || width <= 0 || height <= 0 ||
            width > 16'384 || height > 16'384 || renderer_ == nullptr || track_ == nullptr ||
            output.size < sizeof(FFF3FPBitmapSubtitleFrame) || output.version != ApiVersion)
            return FFFResult::InvalidArgument;
        try {
            const auto geometryChanged = width != canvasWidth_ || height != canvasHeight_;
            if (geometryChanged) {
                ass_set_frame_size(renderer_, width, height);
                ass_set_storage_size(renderer_, width, height);
                ass_set_pixel_aspect(renderer_, 1.0);
                canvasWidth_ = width;
                canvasHeight_ = height;
            }

            if (geometryChanged || position != lastPosition_ || !hasRenderedFrame_) {
                int changed = 0;
                const auto timestamp = position / TicksPerMillisecond;
                ASS_Image* images = ass_render_frame(renderer_, track_, timestamp, &changed);
                if (geometryChanged || changed != 0 || !hasRenderedFrame_)
                    Composite(images);
                lastPosition_ = position;
                hasRenderedFrame_ = true;
            }
            FillOutput(output, position);
            hasPendingCopy_ = true;
            return FFFResult::Success;
        } catch (const std::bad_alloc&) {
            return Fail("Could not allocate the rendered ASS subtitle bitmap.");
        } catch (const std::exception& error) {
            return Fail(std::string("Could not render the ASS subtitle: ") + error.what());
        } catch (...) {
            return Fail("Could not render the ASS subtitle.");
        }
    }

    FFFResult Copy(void* output, const std::uint32_t outputSize) noexcept {
        if (!hasPendingCopy_) return FFFResult::InvalidState;
        if (pixels_.size() > outputSize || (!pixels_.empty() && output == nullptr))
            return FFFResult::BufferTooSmall;
        if (!pixels_.empty()) std::memcpy(output, pixels_.data(), pixels_.size());
        hasPendingCopy_ = false;
        return FFFResult::Success;
    }

    const std::string& LastError() const noexcept { return lastError_; }

private:
    static void AssMessageCallback(const int level, const char* format, va_list arguments,
        void* data) noexcept {
        if (data == nullptr || format == nullptr || level > 2) return;
        auto* self = static_cast<AssSubtitleRenderer*>(data);
        try {
            std::array<char, 2048> buffer{};
            std::vsnprintf(buffer.data(), buffer.size(), format, arguments);
            self->lastLibassMessage_ = buffer.data();
            while (!self->lastLibassMessage_.empty() &&
                (self->lastLibassMessage_.back() == '\r' || self->lastLibassMessage_.back() == '\n'))
                self->lastLibassMessage_.pop_back();
        } catch (...) {}
    }

    void ParseFontDirectories(const char* value) {
        if (value == nullptr) return;
        std::string all(value);
        std::size_t start = 0;
        while (start <= all.size()) {
            const auto end = all.find('\n', start);
            auto directory = all.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!directory.empty() && directory.back() == '\r') directory.pop_back();
            if (!directory.empty()) fontDirectories_.push_back(std::move(directory));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    void AddMediaFonts() {
        for (const auto& directoryUtf8 : fontDirectories_) {
            const auto directoryWide = Utf8ToWide(directoryUtf8);
            if (directoryWide.empty()) continue;
            std::error_code error;
            const std::filesystem::path directory(directoryWide);
            if (!std::filesystem::is_directory(directory, error)) continue;
            for (std::filesystem::directory_iterator iterator(directory,
                     std::filesystem::directory_options::skip_permission_denied, error), end;
                 iterator != end; iterator.increment(error)) {
                if (error) { error.clear(); continue; }
                if (!iterator->is_regular_file(error) || !IsFontFile(iterator->path())) continue;
                auto font = ReadFile(iterator->path());
                if (font.empty() || font.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                    continue;
                auto name = WideToUtf8(iterator->path().filename().wstring());
                if (name.empty()) name = "media-font";
                ass_add_font(library_, name.c_str(), font.data(), static_cast<int>(font.size()));
            }
        }
    }

    static bool VisibleBounds(const ASS_Image& image, const std::int32_t canvasWidth,
        const std::int32_t canvasHeight, std::int32_t& left, std::int32_t& top,
        std::int32_t& right, std::int32_t& bottom) noexcept {
        if (image.bitmap == nullptr || image.w <= 0 || image.h <= 0 || image.stride < image.w ||
            static_cast<std::uint8_t>(image.color) == 255)
            return false;
        const auto imageRight = static_cast<std::int64_t>(image.dst_x) + image.w;
        const auto imageBottom = static_cast<std::int64_t>(image.dst_y) + image.h;
        left = static_cast<std::int32_t>(std::max<std::int64_t>(0, image.dst_x));
        top = static_cast<std::int32_t>(std::max<std::int64_t>(0, image.dst_y));
        right = static_cast<std::int32_t>(std::min<std::int64_t>(canvasWidth, imageRight));
        bottom = static_cast<std::int32_t>(std::min<std::int64_t>(canvasHeight, imageBottom));
        return left < right && top < bottom;
    }

    void Composite(ASS_Image* images) {
        auto left = canvasWidth_;
        auto top = canvasHeight_;
        auto right = 0;
        auto bottom = 0;
        for (auto* image = images; image != nullptr; image = image->next) {
            std::int32_t imageLeft{}, imageTop{}, imageRight{}, imageBottom{};
            if (!VisibleBounds(*image, canvasWidth_, canvasHeight_,
                imageLeft, imageTop, imageRight, imageBottom)) continue;
            left = std::min(left, imageLeft);
            top = std::min(top, imageTop);
            right = std::max(right, imageRight);
            bottom = std::max(bottom, imageBottom);
        }

        if (left >= right || top >= bottom) {
            cropLeft_ = cropTop_ = cropWidth_ = cropHeight_ = cropStride_ = 0;
            pixels_.clear();
            AdvanceSequence();
            return;
        }

        cropLeft_ = std::max(0, left - 1);
        cropTop_ = std::max(0, top - 1);
        right = std::min(canvasWidth_, right + 1);
        bottom = std::min(canvasHeight_, bottom + 1);
        cropWidth_ = right - cropLeft_;
        cropHeight_ = bottom - cropTop_;
        cropStride_ = cropWidth_ * 4;
        pixels_.assign(static_cast<std::size_t>(cropStride_) * cropHeight_, 0);

        for (auto* image = images; image != nullptr; image = image->next)
            CompositeImage(*image);
        AdvanceSequence();
    }

    void CompositeImage(const ASS_Image& image) noexcept {
        std::int32_t left{}, top{}, right{}, bottom{};
        if (!VisibleBounds(image, canvasWidth_, canvasHeight_, left, top, right, bottom)) return;

        const auto red = static_cast<std::uint8_t>(image.color >> 24);
        const auto green = static_cast<std::uint8_t>(image.color >> 16);
        const auto blue = static_cast<std::uint8_t>(image.color >> 8);
        const auto opacity = 255u - static_cast<std::uint8_t>(image.color);
        if (opacity == 0) return;

        const auto sourceLeft = left - image.dst_x;
        const auto sourceTop = top - image.dst_y;
        for (auto y = top; y < bottom; ++y) {
            const auto* source = image.bitmap +
                static_cast<std::ptrdiff_t>(sourceTop + y - top) * image.stride + sourceLeft;
            auto* destination = pixels_.data() +
                static_cast<std::ptrdiff_t>(y - cropTop_) * cropStride_ + (left - cropLeft_) * 4;
            for (auto x = left; x < right; ++x, ++source, destination += 4) {
                const auto alpha = (static_cast<unsigned>(*source) * opacity + 127u) / 255u;
                if (alpha == 0) continue;
                const auto inverse = 255u - alpha;
                const auto sourceBlue = (static_cast<unsigned>(blue) * alpha + 127u) / 255u;
                const auto sourceGreen = (static_cast<unsigned>(green) * alpha + 127u) / 255u;
                const auto sourceRed = (static_cast<unsigned>(red) * alpha + 127u) / 255u;
                destination[0] = static_cast<std::uint8_t>(sourceBlue +
                    (static_cast<unsigned>(destination[0]) * inverse + 127u) / 255u);
                destination[1] = static_cast<std::uint8_t>(sourceGreen +
                    (static_cast<unsigned>(destination[1]) * inverse + 127u) / 255u);
                destination[2] = static_cast<std::uint8_t>(sourceRed +
                    (static_cast<unsigned>(destination[2]) * inverse + 127u) / 255u);
                destination[3] = static_cast<std::uint8_t>(alpha +
                    (static_cast<unsigned>(destination[3]) * inverse + 127u) / 255u);
            }
        }
    }

    void AdvanceSequence() noexcept {
        sequence_ = sequence_ == std::numeric_limits<std::int64_t>::max() ? 1 : sequence_ + 1;
    }

    void FillOutput(FFF3FPBitmapSubtitleFrame& output, const std::int64_t position) const noexcept {
        output = {};
        output.size = sizeof(FFF3FPBitmapSubtitleFrame);
        output.version = ApiVersion;
        output.flags = pixels_.empty() ? FFF3FPBitmapSubtitleFlags::Clear : FFF3FPBitmapSubtitleFlags::None;
        output.start100ns = position;
        output.canvasWidth = canvasWidth_;
        output.canvasHeight = canvasHeight_;
        output.x = cropLeft_;
        output.y = cropTop_;
        output.width = cropWidth_;
        output.height = cropHeight_;
        output.stride = cropStride_;
        output.pixelBytes = static_cast<std::uint32_t>(pixels_.size());
        output.sequence = pixels_.empty() ? 0 : sequence_;
    }

    std::string LibassDetail() const {
        return lastLibassMessage_.empty() ? std::string{} : " " + lastLibassMessage_;
    }

    FFFResult Fail(std::string message,
        const FFFResult result = FFFResult::NativeFailure) noexcept {
        try { lastError_ = std::move(message); } catch (...) {}
        return result;
    }

    void Close() noexcept {
        if (renderer_ != nullptr) ass_renderer_done(renderer_);
        if (track_ != nullptr) ass_free_track(track_);
        if (library_ != nullptr) ass_library_done(library_);
        renderer_ = nullptr;
        track_ = nullptr;
        library_ = nullptr;
        pixels_.clear();
    }

    ASS_Library* library_{};
    ASS_Renderer* renderer_{};
    ASS_Track* track_{};
    std::string lastError_;
    std::string lastLibassMessage_;
    std::vector<std::string> fontDirectories_;
    std::vector<std::uint8_t> pixels_;
    std::int64_t lastPosition_{std::numeric_limits<std::int64_t>::min()};
    std::int64_t sequence_{};
    std::int32_t canvasWidth_{};
    std::int32_t canvasHeight_{};
    std::int32_t cropLeft_{};
    std::int32_t cropTop_{};
    std::int32_t cropWidth_{};
    std::int32_t cropHeight_{};
    std::int32_t cropStride_{};
    bool hasRenderedFrame_{};
    bool hasPendingCopy_{};
};

FFFResult CopyUtf8(const std::string& value, char* output, const std::uint32_t outputSize,
    std::uint32_t* requiredSize) noexcept {
    const auto bytes = value.size() + 1;
    if (bytes > UINT32_MAX) return FFFResult::NativeFailure;
    if (requiredSize != nullptr) *requiredSize = static_cast<std::uint32_t>(bytes);
    if (output == nullptr || outputSize < bytes) return FFFResult::BufferTooSmall;
    std::memcpy(output, value.c_str(), bytes);
    return FFFResult::Success;
}
}

FFFResult FFF3FP_OpenAssSubtitle(const char* path, const char* fontDirectories,
    FFF3FPAssSubtitleHandle* output) noexcept {
    if (output == nullptr) return FFFResult::InvalidArgument;
    *output = nullptr;
    try {
        auto renderer = std::make_unique<AssSubtitleRenderer>();
        const auto result = renderer->Open(path, fontDirectories);
        *output = renderer.release();
        return result;
    } catch (...) { return FFFResult::NativeFailure; }
}

FFFResult FFF3FP_RenderAssSubtitle(const FFF3FPAssSubtitleHandle renderer,
    const std::int64_t position, const std::int32_t width, const std::int32_t height,
    FFF3FPBitmapSubtitleFrame* frame) noexcept {
    return renderer != nullptr && frame != nullptr
        ? static_cast<AssSubtitleRenderer*>(renderer)->Render(position, width, height, *frame)
        : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_CopyAssSubtitlePixels(const FFF3FPAssSubtitleHandle renderer,
    void* output, const std::uint32_t outputSize) noexcept {
    return renderer != nullptr
        ? static_cast<AssSubtitleRenderer*>(renderer)->Copy(output, outputSize)
        : FFFResult::InvalidArgument;
}

FFFResult FFF3FP_GetAssSubtitleLastError(const FFF3FPAssSubtitleHandle renderer,
    char* output, const std::uint32_t size, std::uint32_t* required) noexcept {
    return renderer != nullptr
        ? CopyUtf8(static_cast<AssSubtitleRenderer*>(renderer)->LastError(), output, size, required)
        : FFFResult::InvalidArgument;
}

void FFF3FP_DestroyAssSubtitle(const FFF3FPAssSubtitleHandle renderer) noexcept {
    delete static_cast<AssSubtitleRenderer*>(renderer);
}
