#include <windows.h>
#include <libbluray/bluray.h>
#include <libbluray/overlay.h>
#include <libbluray/keys.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

static std::string Utf8(const wchar_t* value) {
    int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), length, nullptr, nullptr);
    result.pop_back();
    return result;
}

struct OverlayStats { unsigned commands = 0; unsigned draws = 0; };
static void Overlay(void* context, const BD_OVERLAY* overlay) {
    if (!overlay) return;
    auto& stats = *static_cast<OverlayStats*>(context);
    ++stats.commands;
    if (overlay->plane == BD_OVERLAY_IG && overlay->cmd == BD_OVERLAY_DRAW) ++stats.draws;
}

static int ProbeBluRay(const char* path) {
    BLURAY* bd = bd_open(path, nullptr);
    if (!bd) return 20;
    const auto* info = bd_get_disc_info(bd);
    std::printf("BD detected=%d hdmv=%u bdj=%u unsupported=%u first=%d top=%d aacs=%d bdplus=%d\n",
        info->bluray_detected, info->num_hdmv_titles, info->num_bdj_titles,
        info->num_unsupported_titles, info->first_play_supported, info->top_menu_supported,
        info->aacs_detected, info->bdplus_detected);
    const auto count = bd_get_titles(bd, TITLES_ALL, 0);
    for (unsigned i = 0; i < count; ++i) {
        auto* title = bd_get_title_info(bd, i, 0);
        if (title) {
            std::printf("BD title=%u playlist=%u duration=%.3f clips=%u chapters=%u\n", i,
                title->playlist, title->duration / 90000.0, title->clip_count, title->chapter_count);
            bd_free_title_info(title);
        }
    }
    OverlayStats overlays;
    bd_register_overlay_proc(bd, &overlays, Overlay);
    const int play = bd_play(bd);
    std::printf("BD play=%d\n", play);
    std::vector<uint8_t> buffer(6144);
    unsigned long long bytes = 0;
    unsigned eventCount = 0;
    bool activated = false;
    unsigned long long lastMenuRequest = 0;
    int activationResult = -1;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned i = 0; play && i < 200000; ++i) {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(45)) break;
        BD_EVENT event{};
        int read = bd_read_ext(bd, buffer.data(), static_cast<int>(buffer.size()), &event);
        if (event.event) {
            ++eventCount;
            if (eventCount < 100) std::printf("BD event=%u param=%u read=%d\n", event.event, event.param, read);
        }
        if (read < 0) {
            if (event.event == BD_EVENT_ERROR || event.event == BD_EVENT_ENCRYPTED) break;
            Sleep(10);
            continue;
        }
        bytes += read;
        if (!activated && bytes > lastMenuRequest + 16 * 1024 * 1024) {
            const int menuResult = bd_menu_call(bd, -1);
            std::printf("BD menu_call=%d\n", menuResult);
            if (!menuResult) std::printf("BD popup=%d\n", bd_user_input(bd, -1, BD_VK_POPUP));
            lastMenuRequest = bytes;
        }
        if (overlays.draws && !activated) {
            activationResult = bd_user_input(bd, -1, BD_VK_ENTER);
            activated = true;
            std::printf("BD menu activation=%d overlay_draws=%u\n", activationResult, overlays.draws);
        }
        if (activated && bytes > 16 * 1024 * 1024) break;
        if (!read) Sleep(1);
    }
    bd_register_overlay_proc(bd, nullptr, nullptr);
    bd_close(bd);
    std::printf("BD result bytes=%llu events=%u overlays=%u draws=%u activation=%d\n",
        bytes, eventCount, overlays.commands, overlays.draws, activationResult);
    return play && bytes && overlays.draws && activationResult >= 0 ? 0 : 21;
}

int wmain(int argc, wchar_t** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 3 || std::wstring(argv[1]) != L"bluray") return 2;
    const auto path = Utf8(argv[2]);
    return ProbeBluRay(path.c_str());
}
