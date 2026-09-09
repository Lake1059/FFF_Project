#include "pch.h"
#include "3FP/Disc/DiscInput.h"
#include <dvdnav/dvdnav.h>
#include <libbluray/bluray.h>
#include <libbluray/overlay.h>
#include <libbluray/keys.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>
}
namespace {
struct StillMemory { const uint8_t* data; size_t size; size_t pos; };
int StillRead(void* opaque, uint8_t* buf, int n) { auto* m = static_cast<StillMemory*>(opaque); const auto count = std::min<size_t>(n, m->size - m->pos); if (!count) return AVERROR_EOF; std::memcpy(buf, m->data + m->pos, count); m->pos += count; return static_cast<int>(count); }
int64_t StillSeek(void* opaque, int64_t off, int whence) { auto* m = static_cast<StillMemory*>(opaque); if ((whence & AVSEEK_SIZE) != 0) return static_cast<int64_t>(m->size); whence &= ~AVSEEK_FORCE; int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? static_cast<int64_t>(m->pos) : static_cast<int64_t>(m->size); int64_t next = base + off; if (next < 0 || static_cast<uint64_t>(next) > m->size) return AVERROR(EINVAL); m->pos = static_cast<size_t>(next); return next; }
}
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <iomanip>

static_assert(sizeof(playback_type_t) == 1, "DVD IFO byte bitfields require the patched MSVC dependency.");
static_assert(sizeof(title_info_t) == 12, "DVD title records must match their on-disc size.");

struct DvdNavigationState {
    pci_t pci{};
    bool valid = false;
};

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
bool DiscInput::DvdMenuDomain() const { return dvd_ && dvdnav_is_domain_vts(dvd_) == 0; }
bool DiscInput::DvdStillCell() const { return dvd_ && dvdnav_get_next_still_flag(dvd_) != 0; }
void DiscInput::RestoreFirstPlayStill() {
    if (firstPlayStill_.pixels.empty()) return;
    graphics_ = firstPlayStill_;
    ++graphics_.sequence;
}
DiscInput::~DiscInput() {
    ResetSubtitleDecoder();
    if (io_) { av_freep(&io_->buffer); avio_context_free(&io_); }
    if (bd_) { bd_register_overlay_proc(bd_, nullptr, nullptr); bd_close(bd_); }
    if (dvd_) dvdnav_close(dvd_);
}
bool DiscInput::IsDiscPath(const std::string& path) {
    try {
        const auto p = LocalPath(path);
        auto ext = p.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
        return ext == L".iso" || ext == L".bdmv" || ext == L".ifo" ||
            std::filesystem::is_directory(p);
    } catch (...) { return false; }
}
bool DiscInput::Open(const std::string& path) {
    auto p = LocalPath(path);
    auto extension = p.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
    if (extension == L".bdmv" || extension == L".ifo") p = p.parent_path();
    if (p.filename().empty() && p != p.root_path()) p = p.parent_path();
    auto name = p.filename().wstring();
    std::transform(name.begin(), name.end(), name.begin(), ::towupper);
    if (name == L"BDMV" || name == L"VIDEO_TS") p = p.parent_path();
    auto source = Utf8Path(p);
    const bool dvdDirectory = std::filesystem::is_directory(p) &&
        std::filesystem::exists(p / L"VIDEO_TS" / L"VIDEO_TS.IFO");
    if (!dvdDirectory) bd_ = bd_open(source.c_str(), nullptr);
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
    if (dvdnav_open(&dvd_, source.c_str()) != DVDNAV_STATUS_OK) {
        error_ = "The source is not a readable DVD-Video or Blu-ray disc."; return false;
    }
    dvdnav_set_readahead_flag(dvd_, 0);
    dvdNavigation_ = std::make_unique<DvdNavigationState>();
    dvdnav_set_PGC_positioning_flag(dvd_, 1);
    dvdnav_get_number_of_titles(dvd_, &titleCount_);
    aspect_ = 4.0 / 3;
    try { LoadFirstPlayStill(source); } catch (...) { }
    return true;
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
void DiscInput::LoadFirstPlayStill(const std::string& path) {
    if (!dvd_) return;
    if (trace_) std::fprintf(stderr, "DISC first-play probe path=%s\n", path.c_str());
    dvd_reader_t* reader = DVDOpen(path.c_str());
    std::vector<uint8_t> data;
    dvd_file_t* file = nullptr; ifo_handle_t* ifo = nullptr;
    if (!reader) {
        dvdnav_t* nav = nullptr;
        if (dvdnav_open(&nav, path.c_str()) == DVDNAV_STATUS_OK) {
            std::vector<uint8_t> block(2048); int event = 0, length = 0; bool cell2 = false;
            for (int i = 0; i < 20000; ++i) {
                if (dvdnav_get_next_block(nav, block.data(), &event, &length) != DVDNAV_STATUS_OK) break;
                if (event == DVDNAV_CELL_CHANGE) { auto* c = reinterpret_cast<dvdnav_cell_change_event_t*>(block.data()); cell2 = c->cellN == 2; if (c->cellN > 2) break; }
                else if (cell2 && event == DVDNAV_BLOCK_OK && length > 0) data.insert(data.end(), block.begin(), block.begin() + std::min(length, 2048));
                if (cell2 && event == DVDNAV_STILL_FRAME) break;
            }
            dvdnav_close(nav);
        }
        if (trace_) std::fprintf(stderr, "DISC first-play dvdnav fallback bytes=%zu\n", data.size());
    } else {
        file = DVDOpenFile(reader, 0, DVD_READ_MENU_VOBS); ifo = ifoOpen(reader, 0);
    }
    const auto closeDvd = [&]() {
        if (ifo) { ifoClose(ifo); ifo = nullptr; }
        if (file) { DVDCloseFile(file); file = nullptr; }
        if (reader) { DVDClose(reader); reader = nullptr; }
    };
    pgc_t* stillPgc = reader && ifo ? ifo->first_play_pgc : nullptr;
    if (reader && ifo && (!stillPgc || stillPgc->nr_of_cells < 2) && ifo->pgci_ut) {
        for (unsigned lu = 0; lu < ifo->pgci_ut->nr_of_lus && (!stillPgc || stillPgc->nr_of_cells < 2); ++lu) {
            auto* table = ifo->pgci_ut->lu[lu].pgcit;
            if (!table) continue;
            for (unsigned item = 0; item < table->nr_of_pgci_srp; ++item) {
                auto* candidate = table->pgci_srp[item].pgc;
                if (candidate && candidate->nr_of_cells >= 2) { stillPgc = candidate; break; }
            }
        }
    }
    if ((reader && (!file || !ifo || !stillPgc || stillPgc->nr_of_cells < 2)) || (!reader && data.empty())) {
        if (trace_) std::fprintf(stderr, "DISC first-play unavailable file=%p ifo=%p pgc=%p cells=%u\n", file, ifo, stillPgc, stillPgc ? stillPgc->nr_of_cells : 0);
        closeDvd(); return;
    }
    int blocks = 0;
    if (reader) {
        const auto& cell = stillPgc->cell_playback[1]; blocks = static_cast<int>(cell.last_sector - cell.first_sector + 1);
        if (blocks <= 0 || blocks > 20000) { closeDvd(); return; }
        data.resize(static_cast<size_t>(blocks) * 2048);
        if (DVDReadBlocks(file, cell.first_sector, blocks, data.data()) != blocks) { closeDvd(); return; }
    } else blocks = static_cast<int>(data.size() / 2048);
    AVFormatContext* fmt = avformat_alloc_context();
    if (!fmt) { closeDvd(); return; }
    auto* ioBuf = static_cast<unsigned char*>(av_malloc(32768));
    if (!ioBuf) { avformat_free_context(fmt); closeDvd(); return; }
    StillMemory memory{data.data(), data.size(), 0};
    AVIOContext* mem = avio_alloc_context(ioBuf, 32768, 0, &memory, StillRead, nullptr, StillSeek);
    fmt->pb = mem; fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    const AVInputFormat* input = av_find_input_format("mpeg");
    const int openResult = avformat_open_input(&fmt, nullptr, input, nullptr);
    const int infoResult = openResult < 0 ? openResult : avformat_find_stream_info(fmt, nullptr);
    if (trace_) std::fprintf(stderr, "DISC first-play demux open=%d info=%d bytes=%zu sectors=%d\n", openResult, infoResult, data.size(), blocks);
    if (infoResult < 0) {
        avformat_close_input(&fmt); if (mem) av_freep(&mem->buffer); if (mem) avio_context_free(&mem); closeDvd(); return;
    }
    int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream >= 0) {
        AVCodecContext* dec = avcodec_alloc_context3(nullptr);
        avcodec_parameters_to_context(dec, fmt->streams[stream]->codecpar);
        if (avcodec_open2(dec, avcodec_find_decoder(dec->codec_id), nullptr) >= 0) {
            AVPacket* pkt = av_packet_alloc(); AVFrame* frame = av_frame_alloc();
            const auto storeFrame = [&]() {
                firstPlayStill_.width = frame->width; firstPlayStill_.height = frame->height;
                firstPlayStill_.pixels.resize(static_cast<size_t>(frame->width) * frame->height * 4);
                SwsContext* sws = sws_getContext(frame->width, frame->height,
                    static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
                    AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!sws) { firstPlayStill_.pixels.clear(); return false; }
                uint8_t* dst[] = { firstPlayStill_.pixels.data() }; int stride[] = { frame->width * 4 };
                sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst, stride);
                sws_freeContext(sws);
                firstPlayStill_.sequence = 1; graphics_ = firstPlayStill_;
                if (trace_) std::fprintf(stderr, "DISC first-play decoded %dx%d\n", frame->width, frame->height);
                return true;
            };
            bool decoded = false;
            while (av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == stream && avcodec_send_packet(dec, pkt) >= 0) {
                    while (avcodec_receive_frame(dec, frame) >= 0) {
                        decoded = storeFrame(); av_frame_unref(frame);
                        if (decoded) break;
                    }
                }
                av_packet_unref(pkt);
                if (decoded) break;
            }
            if (!decoded && avcodec_send_packet(dec, nullptr) >= 0) {
                while (avcodec_receive_frame(dec, frame) >= 0) {
                    decoded = storeFrame(); av_frame_unref(frame);
                    if (decoded) break;
                }
            }
            av_frame_free(&frame); av_packet_free(&pkt);
        }
        avcodec_free_context(&dec);
    }
    avformat_close_input(&fmt); if (mem) av_freep(&mem->buffer); if (mem) avio_context_free(&mem); closeDvd();
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
        if (dvd_) {
            if (size < 2048) return AVERROR(EINVAL);
            int event = 0, length = 0;
            if (dvdnav_get_next_block(dvd_, buffer, &event, &length) != DVDNAV_STATUS_OK) {
                failed_ = true; error_ = dvdnav_err_to_string(dvd_); return AVERROR(EIO);
            }
            if (trace_ && event != DVDNAV_BLOCK_OK && event != DVDNAV_NAV_PACKET)
                std::fprintf(stderr, "DISC dvd event=%d held=%d opening=%d\n", event, held_, opening_);
            switch (event) {
            case DVDNAV_BLOCK_OK: return length;
            case DVDNAV_STOP: ended_ = true; return AVERROR_EOF;
            case DVDNAV_STILL_FRAME:
                stillSeconds_ = reinterpret_cast<dvdnav_still_event_t*>(buffer)->length;
                if (trace_) std::fprintf(stderr, "DISC still seconds=%d\n", stillSeconds_);
                if (stillSeconds_ == 255) stillSeconds_ = 0;
                // DVDNAV_STILL_FRAME keeps the last decoded frame visible.
                // Let the playback worker present it and release the VM only
                // after the reported duration, as VLC/Kodi do.
                stillStarted_ = GetTickCount64();
                held_ = true;
                return AVERROR_EOF;
            case DVDNAV_WAIT: wait_ = true; held_ = true; return AVERROR_EOF;
            case DVDNAV_HOP_CHANNEL: Barrier(false); break;
            case DVDNAV_VTS_CHANGE: {
                aspect_ = dvdnav_get_video_aspect(dvd_) == 3 ? 16.0 / 9 : 4.0 / 3;
                uint32_t width = 0, height = 0;
                dvdnav_get_video_resolution(dvd_, &width, &height);
                if (width && height) { dvdWidth_ = width; dvdHeight_ = height; }
                Barrier(false); break;
            }
            case DVDNAV_CELL_CHANGE: {
                const auto* cell = reinterpret_cast<dvdnav_cell_change_event_t*>(buffer);
                duration_ = cell->pgc_length * 10000000 / 90000;
                dvdnav_current_title_info(dvd_, &title_, &chapter_);
                if (trace_) std::fprintf(stderr, "DISC dvd cell=%d pg=%d title=%d part=%d duration=%lld\n", cell->cellN, cell->pgN, title_, chapter_, cell->pgc_length);
                menu_ = false;
                chapterCount_ = 0;
                if (title_ > 0) dvdnav_get_number_of_parts(dvd_, title_, &chapterCount_);
                if (title_ > 0 && !graphics_.pixels.empty()) {
                    graphics_.pixels.clear(); graphics_.width = graphics_.height = 0; ++graphics_.sequence;
                    firstPlayStill_.pixels.clear();
                }
                break;
            }
            case DVDNAV_AUDIO_STREAM_CHANGE:
                audioId_ = reinterpret_cast<dvdnav_audio_stream_change_event_t*>(buffer)->physical; break;
            case DVDNAV_SPU_STREAM_CHANGE:
                subtitleId_ = reinterpret_cast<dvdnav_spu_stream_change_event_t*>(buffer)->physical_wide;
                if (trace_) std::fprintf(stderr, "DISC dvd spu=%d\n", subtitleId_);
                break;
            case DVDNAV_SPU_CLUT_CHANGE:
                std::memcpy(clut_, buffer, sizeof(clut_)); ResetSubtitleDecoder(); break;
            case DVDNAV_NAV_PACKET:
                dvdNavigation_->pci = *dvdnav_get_current_nav_pci(dvd_);
                dvdNavigation_->valid = true;
                menu_ = DvdMenuDomain() && dvdNavigation_->pci.hli.hl_gi.btn_ns > 0;
                if (menu_ && !firstPlayStill_.pixels.empty()) firstPlayStill_.pixels.clear();
                UpdateDvdHighlight(); break;
            case DVDNAV_HIGHLIGHT: UpdateDvdHighlight(); break;
            }
        } else {
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
                    startupMenuPending_ = false;
                    if (bd_menu_call(bd_, -1)) {
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
        if (parameter == 0) startupMenuPending_ = false;
        else if (parameter != 65535) startupTitleReached_ = true;
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
        if (dvd_) dvdnav_still_skip(dvd_); else bd_read_skip_still(bd_);
        stillSeconds_ = -1;
    }
    if (wait_) {
        if (dvd_) dvdnav_wait_skip(dvd_);
        else Sleep(10);
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
    if (bd_) return bd_tell_time(bd_) * 10000000 / 90000;
    if (dvd_) return std::max<int64_t>(0, dvdnav_get_current_time(dvd_)) * 10000000 / 90000;
    return 0;
}
int DiscInput::AudioStream(AVFormatContext* format) const {
    if (!format || audioId_ < 0) return -1;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        const auto* stream = format->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        if (dvd_ && (stream->id & 7) == audioId_) return static_cast<int>(i);
        if (bd_ && audioId_ > 0 && audioId_ <= static_cast<int>(audioPids_.size()) &&
            stream->id == audioPids_[audioId_ - 1]) return static_cast<int>(i);
    }
    return -1;
}
void DiscInput::SelectAudioStream(AVFormatContext* format, int stream) {
    if (!format || stream < 0 || stream >= static_cast<int>(format->nb_streams)) return;
    if (dvd_) audioId_ = format->streams[stream]->id & 7;
}
bool DiscInput::Seek(std::int64_t value) {
    if (menu_ || value < 0) return false;
    bool ok = bd_ ? bd_seek_time(bd_, value * 9 / 1000) >= 0 :
        dvdnav_time_search(dvd_, value * 9 / 1000) == DVDNAV_STATUS_OK;
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
    } else if (dvd_) {
        auto* pci = dvdNavigation_->valid ? &dvdNavigation_->pci : dvdnav_get_current_nav_pci(dvd_);
        if (command == Command::Title || command == Command::Chapter || command == Command::RootMenu) {
            if (stillSeconds_ >= 0) dvdnav_still_skip(dvd_);
            if (wait_) dvdnav_wait_skip(dvd_);
        }
        switch (command) {
        case Command::Up: ok = dvdnav_upper_button_select(dvd_, pci); break;
        case Command::Down: ok = dvdnav_lower_button_select(dvd_, pci); break;
        case Command::Left: ok = dvdnav_left_button_select(dvd_, pci); break;
        case Command::Right: ok = dvdnav_right_button_select(dvd_, pci); break;
        case Command::Activate: ok = dvdnav_button_activate(dvd_, pci); jump = true; break;
        case Command::RootMenu: case Command::PopupMenu:
            ok = dvdnav_menu_call(dvd_, DVD_MENU_Root);
            if (!ok && titleCount_ > 0 && dvdnav_title_play(dvd_, 1) == DVDNAV_STATUS_OK)
                ok = dvdnav_menu_call(dvd_, DVD_MENU_Root);
            if (!ok) ok = dvdnav_menu_call(dvd_, DVD_MENU_Title);
            jump = true; break;
        case Command::Back: ok = dvdnav_go_up(dvd_); jump = true; break;
        case Command::MouseMove: ok = dvdnav_mouse_select(dvd_, pci, value, y); break;
        case Command::MouseActivate: ok = dvdnav_mouse_activate(dvd_, pci, value, y); jump = true; break;
        case Command::Title: ok = dvdnav_title_play(dvd_, value); jump = true; break;
        case Command::Chapter: ok = dvdnav_part_play(dvd_, title_, value); jump = true; break;
        case Command::Subtitle: ok = true; break;
        default: break;
        }
        UpdateDvdHighlight();
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
    json << "{\"kind\":\"" << (dvd_ ? "dvd" : "bluray") << "\",\"menu\":" << (menu_ ? "true" : "false")
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
    const int width = std::max({planes_[0].width, planes_[1].width, dvdPixels_.empty() ? 0 : dvdWidth_});
    const int height = std::max({planes_[0].height, planes_[1].height, dvdPixels_.empty() ? 0 : dvdHeight_});
    graphics_.width = width; graphics_.height = height;
    graphics_.pixels.assign(static_cast<size_t>(width) * height * 4, 0);
    if (!dvdPixels_.empty()) for (int y = 0; y < dvdHeight_; ++y)
        std::memcpy(graphics_.pixels.data() + static_cast<size_t>(y) * width * 4,
            dvdPixels_.data() + static_cast<size_t>(y) * dvdWidth_ * 4, static_cast<size_t>(dvdWidth_) * 4);
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
    ResetSubtitleDecoder(); dvdPixels_.clear(); dvdIndices_.clear();
    if (dvd_) { if (!firstPlayStill_.pixels.empty() && title_ == 0) graphics_ = firstPlayStill_; else { graphics_.pixels.clear(); ++graphics_.sequence; } }
    else if (bd_) PublishGraphics();
}
void DiscInput::DecodeSubtitle(const AVPacket* packet, AVFormatContext* format) {
    if (!packet || packet->stream_index < 0 || packet->stream_index >= static_cast<int>(format->nb_streams)) return;
    const auto* stream = format->streams[packet->stream_index];
    const auto codecId = stream->codecpar->codec_id;
    if (codecId != AV_CODEC_ID_DVD_SUBTITLE && codecId != AV_CODEC_ID_HDMV_PGS_SUBTITLE) return;
    if (trace_ && dvd_) std::fprintf(stderr, "DISC sub packet stream=%d id=%x selected=%d size=%d\n", packet->stream_index, stream->id, subtitleId_, packet->size);
    if (!menu_ && subtitleSelection_ == 0) return;
    if (!menu_ && subtitleSelection_ > 0) {
        int ordinal = 0;
        for (unsigned i = 0; i <= static_cast<unsigned>(packet->stream_index); ++i)
            if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) ++ordinal;
        if (ordinal != subtitleSelection_) return;
    } else if (dvd_ && subtitleId_ >= 0 && (stream->id & 31) != (subtitleId_ & 31)) return;
    else if (bd_ && subtitleSelection_ < 0 && (!bluraySubtitleEnabled_ || bluraySubtitle_ <= 0 ||
        bluraySubtitle_ > static_cast<int>(subtitlePids_.size()) || stream->id != subtitlePids_[bluraySubtitle_ - 1])) return;
    if (subtitleDecoderStream_ != packet->stream_index) {
        ResetSubtitleDecoder();
        subtitleDecoder_ = avcodec_alloc_context3(avcodec_find_decoder(codecId));
        if (!subtitleDecoder_) return;
        avcodec_parameters_to_context(subtitleDecoder_, stream->codecpar);
        if (dvd_) { subtitleDecoder_->width = dvdWidth_; subtitleDecoder_->height = dvdHeight_; }
        std::ostringstream palette;
        for (int i = 0; i < 16; ++i) {
            const auto c = clut_[i];
            if (i) palette << ',';
            palette << std::hex << std::setw(6) << std::setfill('0') << (Color((c >> 16) & 255, c & 255, (c >> 8) & 255, 255, false) & 0xffffff);
        }
        if (dvd_) av_opt_set(subtitleDecoder_->priv_data, "palette", palette.str().c_str(), 0);
        if (avcodec_open2(subtitleDecoder_, subtitleDecoder_->codec, nullptr) < 0) { ResetSubtitleDecoder(); return; }
        subtitleDecoderStream_ = packet->stream_index;
    }
    AVSubtitle sub{}; int got = 0;
    const auto result = avcodec_decode_subtitle2(subtitleDecoder_, &sub, &got, const_cast<AVPacket*>(packet));
    if (trace_ && dvd_) std::fprintf(stderr, "DISC sub decoded=%d got=%d rects=%u\n", result, got, sub.num_rects);
    if (result >= 0 && got) {
        if (subtitleDecoder_->width > 0 && subtitleDecoder_->height > 0) {
            dvdWidth_ = subtitleDecoder_->width; dvdHeight_ = subtitleDecoder_->height;
        }
        if (dvdWidth_ <= 0 || dvdHeight_ <= 0 || dvdWidth_ > 4096 || dvdHeight_ > 2160) {
            avsubtitle_free(&sub); return;
        }
        dvdPixels_.assign(static_cast<size_t>(dvdWidth_) * dvdHeight_ * 4, 0);
        dvdIndices_.assign(static_cast<size_t>(dvdWidth_) * dvdHeight_, 255);
        for (unsigned i = 0; i < sub.num_rects; ++i) {
            const auto* rect = sub.rects[i];
            if (!rect || !rect->data[0] || !rect->data[1]) continue;
            const auto* palette = reinterpret_cast<const uint32_t*>(rect->data[1]);
            for (int y = 0; y < rect->h; ++y) for (int x = 0; x < rect->w; ++x) {
                const int dx = x + rect->x, dy = y + rect->y;
                if (dx < 0 || dy < 0 || dx >= dvdWidth_ || dy >= dvdHeight_) continue;
                const auto index = rect->data[0][y * rect->linesize[0] + x];
                if (index >= rect->nb_colors) continue;
                const auto c = palette[index]; const auto alpha = c >> 24;
                const auto offset = static_cast<size_t>(dy) * dvdWidth_ + dx;
                dvdIndices_[offset] = index;
                auto* out = dvdPixels_.data() + offset * 4;
                for (int k = 0; k < 3; ++k) out[k] = static_cast<uint8_t>(((c >> (k * 8)) & 255) * alpha / 255);
                out[3] = static_cast<uint8_t>(alpha);
            }
        }
        if (dvd_) UpdateDvdHighlight();
        else {
            // PGS is decoded from the same navigation stream; IG remains above it.
            PublishGraphics();
        }
    }
    avsubtitle_free(&sub);
}
void DiscInput::UpdateDvdHighlight() {
    if (!dvd_) return;
    auto* pci = dvdNavigation_ && dvdNavigation_->valid ? &dvdNavigation_->pci : nullptr;
    graphics_.width = dvdPixels_.empty() && !firstPlayStill_.pixels.empty() ? firstPlayStill_.width : dvdWidth_;
    graphics_.height = dvdPixels_.empty() && !firstPlayStill_.pixels.empty() ? firstPlayStill_.height : dvdHeight_;
    if (!dvdPixels_.empty()) graphics_.pixels = dvdPixels_;
    int button = 0; dvdnav_get_current_highlight(dvd_, &button);
    if (trace_ && menu_) std::fprintf(stderr, "DISC buttons=%d selected=%d forced=%d auto=%d video=%dx%d\n",
        pci->hli.hl_gi.btn_ns, button, pci->hli.hl_gi.fosl_btnn, pci->hli.hl_gi.foac_btnn, dvdWidth_, dvdHeight_);
    dvdnav_highlight_area_t area{};
    if (menu_ && pci && button > 0 && dvdnav_get_highlight_area(pci, button, 1, &area) == DVDNAV_STATUS_OK && !dvdIndices_.empty()) {
        for (int y = area.sy; y <= area.ey && y < dvdHeight_; ++y)
            for (int x = area.sx; x <= area.ex && x < dvdWidth_; ++x) {
                const auto offset = static_cast<size_t>(y) * dvdWidth_ + x;
                const auto index = dvdIndices_[offset]; if (index > 3) continue;
                const auto entry = clut_[(area.palette >> (16 + index * 4)) & 15];
                const auto alpha = ((area.palette >> (index * 4)) & 15) * 17;
                const auto color = Color((entry >> 16) & 255, entry & 255, (entry >> 8) & 255, alpha, false);
                std::memcpy(graphics_.pixels.data() + offset * 4, &color, 4);
            }
    }
    ++graphics_.sequence;
}
