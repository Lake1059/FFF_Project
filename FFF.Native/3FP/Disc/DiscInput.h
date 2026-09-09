#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVPacket;
struct AVIOContext;
struct AVCodecContext;
struct dvdnav_s;
struct bluray;
struct bd_overlay_s;
struct DvdNavigationState;

struct DiscBitmap {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
    std::uint64_t sequence = 0;
};

// A navigation VM owns the media order. A temporary EOF drains the demuxer at
// navigation barriers; it is distinct from the end of the disc.
class DiscInput final {
public:
    enum class Command : int { Up, Down, Left, Right, Activate, RootMenu, PopupMenu,
        Back, MouseMove, MouseActivate, Title, Chapter, Audio, Subtitle };
    explicit DiscInput(std::atomic<bool>& cancel);
    ~DiscInput();
    static bool IsDiscPath(const std::string& path);
    bool Open(const std::string& path);
    bool OpenDemux(AVFormatContext** format);
    void CloseDemux(AVFormatContext** format);
    bool Navigate(Command command, int value, int y);
    bool Seek(std::int64_t position100ns);
    bool PollHold(bool drained);
    bool Held() const { return held_; }
    bool Still() const { return stillSeconds_ >= 0; }
    bool Ended() const { return ended_; }
    bool Failed() const { return failed_; }
    bool Menu() const { return menu_; }
    bool IsDvd() const { return dvd_ != nullptr; }
    bool DvdMenuDomain() const;
    bool DvdStillCell() const;
    const DiscBitmap& FirstPlayStill() const { return firstPlayStill_; }
    void RestoreFirstPlayStill();
    bool RestartRequired() const { return restart_; }
    void AcknowledgeRestart() { restart_ = false; }
    std::int64_t Position() const;
    std::int64_t Duration() const { return duration_; }
    double Aspect() const { return aspect_; }
    int AudioId() const { return audioId_; }
    int AudioStream(AVFormatContext* format) const;
    void SelectAudioStream(AVFormatContext* format, int stream);
    int SubtitleId() const { return subtitleId_; }
    std::string StatusJson() const;
    const std::string& Error() const { return error_; }
    const DiscBitmap& Graphics() const { return graphics_; }
    void DecodeSubtitle(const AVPacket* packet, AVFormatContext* format);
    void ClearSubtitle();
private:
    static int ReadCallback(void*, std::uint8_t*, int);
    static int InterruptCallback(void*);
    static void OverlayCallback(void*, const bd_overlay_s*);
    int Read(std::uint8_t* buffer, int size);
    void HandleOverlay(const bd_overlay_s* overlay);
    void PublishGraphics();
    void UpdateDvdHighlight();
    void LoadFirstPlayStill(const std::string& path);
    void ResetSubtitleDecoder();
    void UpdateBluRayTitle(unsigned playlist);
    void HandleBluRayEvent(unsigned event, unsigned parameter);
    void PollBluRayNavigation();
    void Barrier(bool restart);
    std::atomic<bool>& cancel_;
    dvdnav_s* dvd_ = nullptr;
    std::unique_ptr<DvdNavigationState> dvdNavigation_;
    bluray* bd_ = nullptr;
    AVIOContext* io_ = nullptr;
    AVCodecContext* subtitleDecoder_ = nullptr;
    int subtitleDecoderStream_ = -1;
    std::string error_;
    bool opening_ = false, held_ = false, ended_ = false, failed_ = false;
    bool restart_ = false, menu_ = false, wait_ = false;
    int stillSeconds_ = -1;
    std::uint64_t stillStarted_ = 0;
    int title_ = 0, chapter_ = 0, titleCount_ = 0, chapterCount_ = 0;
    int audioId_ = -1, subtitleId_ = -1;
    unsigned playlist_ = 0;
    std::int64_t duration_ = 0;
    double aspect_ = 0;
    std::uint32_t clut_[16]{};
    struct Plane { int width = 0, height = 0; bool visible = false;
        std::vector<std::uint8_t> indices; std::uint32_t palette[256]{}; } planes_[2];
    DiscBitmap graphics_;
    DiscBitmap firstPlayStill_;
    std::vector<std::uint8_t> dvdPixels_;
    std::vector<std::uint8_t> dvdIndices_;
    int dvdWidth_ = 720, dvdHeight_ = 480;
    std::vector<std::int64_t> chapters_;
    int subtitleSelection_ = -1;
    int bluraySubtitle_ = 1;
    bool bluraySubtitleEnabled_ = true;
    unsigned playItem_ = 0;
    bool trace_ = false;
    std::vector<int> audioPids_, subtitlePids_;
    std::vector<std::uint8_t> pendingRead_;
    unsigned invalidReads_ = 0;
    std::uint64_t openDeadline_ = 0;
    bool startupMenuPending_ = false;
    bool startupTitleReached_ = false;
};
