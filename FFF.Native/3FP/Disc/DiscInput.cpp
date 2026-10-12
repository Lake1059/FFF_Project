#include "pch.h"
#include "3FP/Disc/DiscInput.h"
#include <libbluray/bluray.h>
#include <libbluray/overlay.h>
#include <libbluray/keys.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <algorithm>
#include <filesystem>
#include <sstream>

namespace {
std::uint32_t Color(int y, int cb, int cr, int alpha, bool hd) {
    const double luma = 1.164383 * (y - 16);
    const auto channel = [alpha](double value) { return static_cast<unsigned>(
        std::clamp(static_cast<int>(value + 0.5), 0, 255)) * alpha / 255; };
    return (static_cast<unsigned>(alpha) << 24) |
        (channel(luma + (hd ? 1.792741 : 1.596027) * (cr - 128)) << 16) |
        (channel(luma - (hd ? 0.213249 : 0.391762) * (cb - 128) -
            (hd ? 0.532909 : 0.812968) * (cr - 128)) << 8) |
        channel(luma + (hd ? 2.112402 : 2.017232) * (cb - 128));
}
std::filesystem::path LocalPath(const std::string& path) {
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
}
std::string Utf8Path(const std::filesystem::path& path) {
    auto u8 = path.u8string(); return {reinterpret_cast<const char*>(u8.data()), u8.size()};
}
}

DiscInput::DiscInput(std::atomic<bool>& cancel) : cancel_(cancel) { trace_ = GetEnvironmentVariableW(L"FFF_DISC_TRACE", nullptr, 0) != 0; }
DiscInput::~DiscInput() {
    ResetSubtitleDecoder();
    if (io_) { av_freep(&io_->buffer); avio_context_free(&io_); }
    if (bd_) { bd_register_overlay_proc(bd_, nullptr, nullptr); bd_close(bd_); }
}
bool DiscInput::IsDiscPath(const std::string& path) {
    try {
        const auto p = LocalPath(path);
        auto ext = p.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext == L".bdmv" ||
            std::filesystem::is_directory(p);
    } catch (...) { return false; }
}
bool DiscInput::Open(const std::string& path) {
    auto p = LocalPath(path);
    auto extension = p.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    if (extension == L".bdmv") p = p.parent_path();
    if (p.filename().empty() && p != p.root_path()) p = p.parent_path();
    auto name = p.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), ::towupper);
    if (name == L"BDMV") p = p.parent_path();
    auto source = Utf8Path(p);
    bd_ = bd_open(source.c_str(), nullptr);
    if (bd_) {
        const auto* info = bd_get_disc_info(bd_);
        if (!info || !info->bluray_detected) { bd_close(bd_); bd_ = nullptr; }
        else {
            if (info->aacs_detected && !info->aacs_handled) {
                error_ = "This Blu-ray requires an available AACS decoder."; return false;
            }
            if (info->bdplus_detected && !info->bdplus_handled) {
                error_ = "This Blu-ray requires an available BD+ decoder."; return false;
            }
            if (info->num_bdj_titles && !info->bdj_handled) {
                error_ = "This disc uses BD-J menus, which are not available in this build."; return false;
            }
            titleCount_ = info->num_hdmv_titles;
            startupMenuPending_ = info->top_menu_supported != 0;
            bd_register_overlay_proc(bd_, this, OverlayCallback);
            if (!bd_play(bd_)) { error_ = "The Blu-ray navigation VM could not start."; return false; }
            aspect_ = 16.0 / 9;
            return true;
        }
    }
    error_ = "The source is not a readable Blu-ray disc."; return false;
}
int DiscInput::InterruptCallback(void* context) {
    const auto* self = static_cast<DiscInput*>(context);
    return self->cancel_.load() || (self->opening_ && GetTickCount64() > self->openDeadline_) ? 1 : 0;
}

bool DiscInput::OpenDemux(AVFormatContext** format) {
    if (trace_) std::fprintf(stderr, "DISC demux open held=%d restart=%d title=%d playlist=%u\n", held_, restart_, title_, playlist_);
    if (io_) { av_freep(&io_->buffer); avio_context_free(&io_); }
    auto* buffer = static_cast<unsigned char*>(av_malloc(32768));
    if (!buffer) return false;
    io_ = avio_alloc_context(buffer, 32768, 0, this, ReadCallback, nullptr, nullptr);
    if (!io_) { av_free(buffer); return false; }
    io_->seekable = 0;
    *format = avformat_alloc_context();
    if (!*format) return false;
    (*format)->pb = io_;
    (*format)->flags |= AVFMT_FLAG_CUSTOM_IO;
    (*format)->interrupt_callback = {InterruptCallback, this};
    (*format)->probesize = 256 * 1024;
    (*format)->max_analyze_duration = 500000;
    opening_ = true;
    openDeadline_ = GetTickCount64() + 15000;
    auto result = avformat_open_input(format, nullptr, av_find_input_format(bd_ ? "mpegts" : "mpeg"), nullptr);
    if (result >= 0) result = avformat_find_stream_info(*format, nullptr);
    opening_ = false;
    if (trace_) std::fprintf(stderr, "DISC demux result=%d streams=%u held=%d\n", result, *format ? (*format)->nb_streams : 0, held_);
    if (trace_ && *format) {
        std::fprintf(stderr, "DISC format start=%lld duration=%lld\n", (*format)->start_time, (*format)->duration);
        for (unsigned i = 0; i < (*format)->nb_streams; ++i) {
            const auto* st = (*format)->streams[i];
            std::fprintf(stderr, "DISC stream=%u type=%d id=%x codec=%d size=%dx%d start=%lld tb=%d/%d\n", i,
                st->codecpar->codec_type, st->id, st->codecpar->codec_id, st->codecpar->width, st->codecpar->height,
                st->start_time, st->time_base.num, st->time_base.den);
        }
    }
    restart_ = false;
    if (result < 0) { error_ = "Could not inspect audio/video at the current disc position."; return false; }
    return true;
}
void DiscInput::CloseDemux(AVFormatContext** format) {
    avformat_close_input(format);
    if (io_) { av_freep(&io_->buffer); avio_context_free(&io_); }
    ResetSubtitleDecoder();
}
void DiscInput::Barrier(bool restart) {
    if (!opening_) { held_ = true; restart_ |= restart; }
}

int DiscInput::ReadCallback(void* context, std::uint8_t* buffer, int size) {
    try { return static_cast<DiscInput*>(context)->Read(buffer, size); }
    catch (...) { auto* self = static_cast<DiscInput*>(context); self->failed_ = true;
        self->error_ = "Disc navigation failed."; return AVERROR(EIO); }
}
int DiscInput::Read(std::uint8_t* buffer, int size) {
    if (InterruptCallback(this)) return AVERROR_EXIT;
    if (held_ || ended_) return AVERROR_EOF;
    if (failed_) return AVERROR(EIO);
    if (!pendingRead_.empty()) {
        const auto count = std::min<size_t>(size, pendingRead_.size());
        std::memcpy(buffer, pendingRead_.data(), count);
        pendingRead_.erase(pendingRead_.begin(), pendingRead_.begin() + count);
        return static_cast<int>(count);
    }
    for (int attempt = 0; attempt < 512 && !cancel_; ++attempt) {
        {
            BD_EVENT event{};
            const int result = bd_read_ext(bd_, buffer, size, &event);
            if (trace_ && event.event) std::fprintf(stderr, "DISC bd event=%u param=%u result=%d opening=%d\n", event.event, event.param, result, opening_);
            HandleBluRayEvent(event.event, event.param);
            if (failed_) return AVERROR(EIO);
            if (result > 0) {
                invalidReads_ = 0;
                if (startupMenuPending_ && startupTitleReached_) {
                    // First Play initializes the disc VM. Prefer its top menu once
                    // that sequence completes, before presenting the main title.
                    if (bd_menu_call(bd_, -1)) {
                        startupMenuPending_ = false;
                        Barrier(true);
                        if (held_) return AVERROR_EOF;
                        continue;
                    }
                }
                if (held_) { pendingRead_.assign(buffer, buffer + result); return AVERROR_EOF; }
                return result;
            }
            if (result < 0 && !event.event && ++invalidReads_ > 100) {
                failed_ = true; error_ = "The Blu-ray navigation VM did not select a playable stream."; return AVERROR(EIO);
            }
            if (event.event == BD_EVENT_IDLE || (result < 0 && !event.event)) {
                held_ = true; wait_ = true; break;
            }
        }
        if (held_) return AVERROR_EOF;
    }
    if (cancel_) return AVERROR_EXIT;
    held_ = true; wait_ = true;
    return AVERROR_EOF;
}
void DiscInput::HandleBluRayEvent(unsigned event, unsigned parameter) {
    switch (event) {
    case BD_EVENT_ERROR: case BD_EVENT_ENCRYPTED: case BD_EVENT_READ_ERROR:
        failed_ = true; error_ = "Blu-ray navigation or disc read error: " + std::to_string(event); break;
    case BD_EVENT_TITLE:
        title_ = parameter;
        // Title 1 can be reported while First Play is still running. Title 0
        // is the boundary after which the top-menu call is allowed.
        if (parameter == 0) startupTitleReached_ = true;
        break;
    case BD_EVENT_PLAYLIST:
        if (playlist_ != parameter) Barrier(true);
        playItem_ = 0; UpdateBluRayTitle(parameter); break;
    case BD_EVENT_PLAYITEM: playItem_ = parameter; UpdateBluRayTitle(playlist_); break;
    case BD_EVENT_CHAPTER: chapter_ = parameter; break;
    case BD_EVENT_AUDIO_STREAM: audioId_ = parameter; break;
    case BD_EVENT_PG_TEXTST_STREAM: bluraySubtitle_ = parameter; ClearSubtitle(); break;
    case BD_EVENT_PG_TEXTST: bluraySubtitleEnabled_ = parameter != 0; ClearSubtitle(); break;
    case BD_EVENT_MENU:
        menu_ = parameter != 0;
        if (menu_) startupMenuPending_ = false;
        break;
    case BD_EVENT_STILL_TIME:
        stillSeconds_ = parameter; stillStarted_ = 0; held_ = true; break;
    case BD_EVENT_PLAYLIST_STOP: case BD_EVENT_SEEK: case BD_EVENT_DISCONTINUITY:
        Barrier(true); break;
    }
}
void DiscInput::PollBluRayNavigation() {
    // A zero-length read runs the HDMV VM without consuming media. Page changes
    // and highlight changes can occur while an indefinite still remains active.
    for (unsigned i = 0; i < 128; ++i) {
        BD_EVENT event{};
        const auto result = bd_read_ext(bd_, nullptr, 0, &event);
        if (result < 0) { failed_ = true; error_ = "The Blu-ray navigation VM failed."; break; }
        if (!event.event) break;
        HandleBluRayEvent(event.event, event.param);
        if (failed_) break;
    }
    if (restart_) {
        held_ = ended_ = wait_ = false;
        stillSeconds_ = -1;
        pendingRead_.clear();
        ClearSubtitle();
    }
}
bool DiscInput::PollHold(bool drained) {
    if (!held_ || !drained) return false;
    if (stillSeconds_ >= 0) {
        if (!stillStarted_) stillStarted_ = GetTickCount64();
        if (!stillSeconds_ || GetTickCount64() - stillStarted_ < static_cast<unsigned>(stillSeconds_) * 1000ull)
            return false;
        bd_read_skip_still(bd_);
        stillSeconds_ = -1;
    }
    if (wait_) {
        Sleep(10);
        wait_ = false;
    }
    held_ = false;
    if (trace_) std::fprintf(stderr, "DISC hold released restart=%d\n", restart_);
    return true;
}
void DiscInput::UpdateBluRayTitle(unsigned playlist) {
    playlist_ = playlist;
    auto* info = bd_get_playlist_info(bd_, playlist, 0);
    if (!info) return;
    duration_ = info->duration * 10000000 / 90000;
    chapterCount_ = info->chapter_count;
    audioPids_.clear(); subtitlePids_.clear();
    if (playItem_ < info->clip_count) {
        const auto& clip = info->clips[playItem_];
        for (unsigned i = 0; i < clip.audio_stream_count; ++i) audioPids_.push_back(clip.audio_streams[i].pid);
        for (unsigned i = 0; i < clip.pg_stream_count; ++i) subtitlePids_.push_back(clip.pg_streams[i].pid);
    }
    chapters_.clear();
    for (unsigned i = 0; i < info->chapter_count; ++i) chapters_.push_back(info->chapters[i].start * 10000000 / 90000);
    bd_free_title_info(info);
}
std::int64_t DiscInput::Position() const {
    return bd_ ? bd_tell_time(bd_) * 10000000 / 90000 : 0;
}
int DiscInput::AudioStream(AVFormatContext* format) const {
    if (!format || audioId_ < 0) return -1;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const auto* stream = format->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        if (bd_ && audioId_ > 0 && audioId_ <= static_cast<int>(audioPids_.size()) &&
            stream->id == audioPids_[audioId_ - 1]) return static_cast<int>(i);
    }
    return -1;
}
void DiscInput::SelectAudioStream(AVFormatContext* format, int stream) {
    if (!format || stream < 0 || stream >= static_cast<int>(format->nb_streams)) return;
    (void)format; (void)stream;
}
bool DiscInput::Seek(std::int64_t value) {
    if (menu_ || value < 0) return false;
    bool ok = bd_ && bd_seek_time(bd_, value * 9 / 1000) >= 0;
    if (ok) { held_ = ended_ = wait_ = false; stillSeconds_ = -1; restart_ = true; ClearSubtitle(); }
    return ok;
}
bool DiscInput::Navigate(Command command, int value, int y) {
    if (trace_) std::fprintf(stderr, "DISC command=%d value=%d y=%d\n", static_cast<int>(command), value, y);
    if (command == Command::Subtitle) { subtitleSelection_ = value; ClearSubtitle(); }
    bool ok = false, jump = false;
    if (bd_) {
        int key = -1;
        switch (command) {
        case Command::Up: key = BD_VK_UP; break;
        case Command::Down: key = BD_VK_DOWN; break;
        case Command::Left: key = BD_VK_LEFT; break;
        case Command::Right: key = BD_VK_RIGHT; break;
        case Command::Activate: key = BD_VK_ENTER; jump = true; break;
        case Command::PopupMenu: key = BD_VK_POPUP; break;
        case Command::Back: key = BD_VK_ROOT_MENU; jump = true; break;
        case Command::RootMenu: ok = bd_menu_call(bd_, -1) != 0; jump = true; break;
        case Command::MouseMove: ok = bd_mouse_select(bd_, -1, value, y) >= 0; break;
        case Command::MouseActivate:
            bd_mouse_select(bd_, -1, value, y); key = BD_VK_MOUSE_ACTIVATE; jump = true; break;
        case Command::Title: ok = value > 0 && bd_play_title(bd_, value); jump = true; break;
        case Command::Chapter:
            if (value > 0 && value <= static_cast<int>(chapters_.size())) return Seek(chapters_[value - 1]);
            break;
        case Command::Audio: ok = bd_select_stream(bd_, BLURAY_AUDIO_STREAM, value, 1) >= 0; break;
        case Command::Subtitle: ok = bd_select_stream(bd_, BLURAY_PG_TEXTST_STREAM, std::max(1, value), value > 0) >= 0; break;
        }
        if (key >= 0) ok = bd_user_input(bd_, -1, key) >= 0;
    }
    if (bd_ && ok) {
        PollBluRayNavigation();
        return !failed_;
    }
    if (ok && jump) { held_ = ended_ = wait_ = false; stillSeconds_ = -1; restart_ = true; pendingRead_.clear(); ClearSubtitle(); }
    return ok;
}
std::string DiscInput::StatusJson() const {
    std::ostringstream json;
    json << "{\"kind\":\"bluray\",\"menu\":" << (menu_ ? "true" : "false")
        << ",\"waiting\":" << (held_ ? "true" : "false") << ",\"title\":" << title_
        << ",\"chapter\":" << chapter_ << ",\"titles\":" << titleCount_ << ",\"chapters\":" << chapterCount_
        << ",\"playlist\":" << playlist_ << ",\"width\":" << graphics_.width << ",\"height\":" << graphics_.height
        << ",\"aspect\":" << aspect_ << ",\"overlaySequence\":" << graphics_.sequence << '}';
    return json.str();
}
void DiscInput::OverlayCallback(void* context, const bd_overlay_s* overlay) {
    auto* self = static_cast<DiscInput*>(context);
    try { self->HandleOverlay(overlay); } catch (...) { self->failed_ = true; self->error_ = "Could not retain disc graphics."; }
}
void DiscInput::HandleOverlay(const bd_overlay_s* overlay) {
    if (!overlay) { for (auto& plane : planes_) plane = {}; PublishGraphics(); return; }
    if (overlay->plane > 1) return;
    if (trace_) std::fprintf(stderr, "DISC overlay plane=%u cmd=%u xy=%u,%u size=%u,%u palette=%d img=%d\n",
        overlay->plane, overlay->cmd, overlay->x, overlay->y, overlay->w, overlay->h, overlay->palette != nullptr, overlay->img != nullptr);
    auto& plane = planes_[overlay->plane];
    if (overlay->cmd == BD_OVERLAY_INIT) {
        if (!overlay->w || !overlay->h || overlay->w > 4096 || overlay->h > 2160) return;
        plane = {}; plane.width = overlay->w; plane.height = overlay->h;
        plane.indices.assign(static_cast<size_t>(plane.width) * plane.height, 255);
    } else if (overlay->cmd == BD_OVERLAY_CLOSE) {
        plane = {}; PublishGraphics();
    } else if (overlay->cmd == BD_OVERLAY_CLEAR || overlay->cmd == BD_OVERLAY_HIDE) {
        std::fill(plane.indices.begin(), plane.indices.end(), 255); plane.visible = false;
    } else if (overlay->cmd == BD_OVERLAY_DRAW || overlay->cmd == BD_OVERLAY_WIPE) {
        if (overlay->palette) for (int i = 0; i < 256; ++i) {
            const auto& p = overlay->palette[i]; plane.palette[i] = Color(p.Y, p.Cb, p.Cr, i == 255 ? 0 : p.T, true);
        }
        if (plane.indices.empty()) return;
        const auto* rle = overlay->img;
        unsigned run = 0, index = 255;
        for (int y = 0; y < overlay->h; ++y) for (int x = 0; x < overlay->w; ++x) {
        if (overlay->cmd == BD_OVERLAY_DRAW && rle) {
                if (!run && !rle->len && x == 0) ++rle;
                if (!run) { run = rle->len; index = std::min<unsigned>(255, rle->color); ++rle; }
                if (!run) return;
                --run;
            }
            const int dx = overlay->x + x, dy = overlay->y + y;
            if (dx < plane.width && dy < plane.height && (rle || overlay->cmd == BD_OVERLAY_WIPE))
                plane.indices[static_cast<size_t>(dy) * plane.width + dx] = static_cast<uint8_t>(index);
        }
        plane.visible = true;
    } else if (overlay->cmd == BD_OVERLAY_FLUSH) PublishGraphics();
}
void DiscInput::PublishGraphics() {
    const int width = std::max({planes_[0].width, planes_[1].width, subtitlePixels_.empty() ? 0 : subtitleWidth_});
    const int height = std::max({planes_[0].height, planes_[1].height, subtitlePixels_.empty() ? 0 : subtitleHeight_});
    graphics_.width = width; graphics_.height = height;
    graphics_.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    if (!subtitlePixels_.empty()) for (int y = 0; y < subtitleHeight_; ++y)
        std::memcpy(graphics_.pixels.data() + static_cast<size_t>(y) * width * 4,
            subtitlePixels_.data() + static_cast<size_t>(y) * subtitleWidth_ * 4, static_cast<size_t>(subtitleWidth_) * 4);
    for (const auto& p : planes_) if (p.visible) {
        for (int y = 0; y < p.height; ++y) for (int x = 0; x < p.width; ++x) {
            const auto color = p.palette[p.indices[static_cast<size_t>(y) * p.width + x]];
            auto* out = &graphics_.pixels[(static_cast<size_t>(y) * width + x) * 4];
            const unsigned alpha = color >> 24;
            for (int c = 0; c < 4; ++c) out[c] = static_cast<uint8_t>(
                ((color >> (c * 8)) & 255) + out[c] * (255 - alpha) / 255);
        }
    }
    ++graphics_.sequence;
    if (trace_) {
        size_t visible = 0;
        for (size_t i = 3; i < graphics_.pixels.size(); i += 4) if (graphics_.pixels[i]) ++visible;
        std::fprintf(stderr, "DISC graphics %dx%d visible=%zu sequence=%llu\n", width, height, visible, graphics_.sequence);
    }
}
void DiscInput::ResetSubtitleDecoder() {
    avcodec_free_context(&subtitleDecoder_); subtitleDecoderStream_ = -1;
}
void DiscInput::ClearSubtitle() {
    ResetSubtitleDecoder(); subtitlePixels_.clear();
    if (bd_) PublishGraphics();
}
void DiscInput::DecodeSubtitle(const AVPacket* packet, AVFormatContext* format) {
    if (!packet || packet->stream_index < 0 || packet->stream_index >= static_cast<int>(format->nb_streams)) return;
    const auto* stream = format->streams[packet->stream_index];
    const auto codecId = stream->codecpar->codec_id;
    if (codecId != AV_CODEC_ID_HDMV_PGS_SUBTITLE) return;
    if (!menu_ && subtitleSelection_ == 0) return;
    if (!menu_ && subtitleSelection_ > 0) {
        int ordinal = 0;
        for (unsigned i = 0; i <= static_cast<unsigned>(packet->stream_index); ++i)
            if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) ++ordinal;
        if (ordinal != subtitleSelection_) return;
    }
    else if (bd_ && subtitleSelection_ < 0 && (!bluraySubtitleEnabled_ || bluraySubtitle_ <= 0 ||
        bluraySubtitle_ > static_cast<int>(subtitlePids_.size()) || stream->id != subtitlePids_[bluraySubtitle_ - 1])) return;
    if (subtitleDecoderStream_ != packet->stream_index) {
        ResetSubtitleDecoder();
        subtitleDecoder_ = avcodec_alloc_context3(avcodec_find_decoder(codecId));
        if (!subtitleDecoder_) return;
        avcodec_parameters_to_context(subtitleDecoder_, stream->codecpar);
        if (avcodec_open2(subtitleDecoder_, subtitleDecoder_->codec, nullptr) < 0) { ResetSubtitleDecoder(); return; }
        subtitleDecoderStream_ = packet->stream_index;
    }
    AVSubtitle sub{}; int got = 0;
    const auto result = avcodec_decode_subtitle2(subtitleDecoder_, &sub, &got, const_cast<AVPacket*>(packet));
    if (trace_ && bd_) std::fprintf(stderr, "DISC PGS decoded=%d got=%d rects=%u\n", result, got, sub.num_rects);
    if (result >= 0 && got) {
        if (subtitleDecoder_->width > 0 && subtitleDecoder_->height > 0) {
            subtitleWidth_ = subtitleDecoder_->width; subtitleHeight_ = subtitleDecoder_->height;
        }
        if (subtitleWidth_ <= 0 || subtitleHeight_ <= 0 || subtitleWidth_ > 4096 || subtitleHeight_ > 2160) {
            avsubtitle_free(&sub); return;
        }
        subtitlePixels_.assign(static_cast<size_t>(subtitleWidth_) * subtitleHeight_ * 4, 0);
        for (unsigned i = 0; i < sub.num_rects; ++i) {
            const auto* rect = sub.rects[i];
            if (!rect || !rect->data[0] || !rect->data[1]) continue;
            const auto* palette = reinterpret_cast<const uint32_t*>(rect->data[1]);
            for (int y = 0; y < rect->h; ++y) for (int x = 0; x < rect->w; ++x) {
                const int dx = x + rect->x, dy = y + rect->y;
                if (dx < 0 || dy < 0 || dx >= subtitleWidth_ || dy >= subtitleHeight_) continue;
                const auto index = rect->data[0][y * rect->linesize[0] + x];
                if (index >= rect->nb_colors) continue;
                const auto c = palette[index]; const auto alpha = c >> 24;
                const auto offset = static_cast<size_t>(dy) * subtitleWidth_ + dx;
                auto* out = subtitlePixels_.data() + offset * 4;
                for (int k = 0; k < 3; ++k) out[k] = static_cast<uint8_t>(((c >> (k * 8)) & 255) * alpha / 255);
                out[3] = static_cast<uint8_t>(alpha);
            }
        }
        {
            // PGS is decoded from the same navigation stream; IG remains above it.
            PublishGraphics();
        }
    }
    avsubtitle_free(&sub);
}
