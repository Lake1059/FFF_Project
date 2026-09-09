#include <windows.h>
#ifndef BLURAY_ONLY
#include <dvdnav/dvdnav.h>
#endif
#include <libbluray/bluray.h>
#include <libbluray/overlay.h>
#include <libbluray/keys.h>
#include <dvdread/dvd_reader.h>
#include <dvdread/ifo_read.h>
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

#ifndef BLURAY_ONLY
static int Dvd(const char* path) {
    static_assert(sizeof(playback_type_t) == 1 && sizeof(title_info_t) == 12);
    dvdnav_t* nav = nullptr;
    if (dvdnav_open(&nav, path) != DVDNAV_STATUS_OK) return 10;
    int titles = 0;
    dvdnav_get_number_of_titles(nav, &titles);
    std::printf("DVD titles=%d\n", titles);
    dvdnav_set_readahead_flag(nav, 0);
    std::vector<uint8_t> block(2048);
    bool activated = false, titleAfterActivate = false;
    unsigned blocks = 0, buttons = 0, activatedAtBlock = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    for (unsigned i = 0; i < 300000 && std::chrono::steady_clock::now() < deadline; ++i) {
        int event = 0, length = 0;
        if (dvdnav_get_next_block(nav, block.data(), &event, &length) != DVDNAV_STATUS_OK) {
            std::printf("DVD error=%s\n", dvdnav_err_to_string(nav));
            break;
        }
        if (event == DVDNAV_BLOCK_OK) ++blocks;
        if (event == DVDNAV_STILL_FRAME) dvdnav_still_skip(nav);
        if (event == DVDNAV_WAIT) dvdnav_wait_skip(nav);
        if (event == DVDNAV_STOP) break;
        if (event == DVDNAV_NAV_PACKET) {
            auto* pci = dvdnav_get_current_nav_pci(nav);
            if (pci && pci->hli.hl_gi.btn_ns && !activated) {
                buttons = pci->hli.hl_gi.btn_ns;
                int current = 0;
                dvdnav_get_current_highlight(nav, &current);
                std::printf("DVD menu buttons=%u current=%d blocks=%u\n", buttons, current, blocks);
                dvdnav_button_select(nav, pci, current > 0 ? current : 1);
                activated = dvdnav_button_activate(nav, pci) == DVDNAV_STATUS_OK;
                activatedAtBlock = blocks;
            }
        }
        int title = 0, part = 0;
        dvdnav_current_title_info(nav, &title, &part);
        if (activated && title > 0 && blocks > activatedAtBlock + 200) {
            std::printf("DVD activated title=%d chapter=%d blocks=%u\n", title, part, blocks);
            titleAfterActivate = true;
            break;
        }
    }
    dvdnav_close(nav);
    std::printf("DVD result buttons=%u activated=%d title_after=%d blocks=%u\n", buttons, activated, titleAfterActivate, blocks);
    return buttons && activated && titleAfterActivate ? 0 : 11;
}

static int DvdFirstPlayDump(const char* path, const char* output) {
    dvdnav_t* nav = nullptr;
    if (dvdnav_open(&nav, path) != DVDNAV_STATUS_OK) return 12;
    FILE* file = std::fopen(output, "wb");
    if (!file) { dvdnav_close(nav); return 13; }
    std::vector<uint8_t> block(2048);
    unsigned blocks = 0, stills = 0, navPackets = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    for (unsigned i = 0; i < 100000 && std::chrono::steady_clock::now() < deadline; ++i) {
        int event = 0, length = 0;
        if (dvdnav_get_next_block(nav, block.data(), &event, &length) != DVDNAV_STATUS_OK) break;
        if (event == DVDNAV_BLOCK_OK) { std::fwrite(block.data(), 1, length, file); ++blocks; }
        else if (event == DVDNAV_STILL_FRAME) { ++stills; std::printf("FIRSTPLAY STILL %d seconds\n", reinterpret_cast<dvdnav_still_event_t*>(block.data())->length); dvdnav_still_skip(nav); }
        else if (event == DVDNAV_NAV_PACKET) ++navPackets;
        else if (event == DVDNAV_STOP) break;
    }
    std::fclose(file); dvdnav_close(nav);
    std::printf("FIRSTPLAY blocks=%u nav=%u stills=%u dump=%s\n", blocks, navPackets, stills, output);
    return blocks > 0 ? 0 : 14;
}

static int DvdIfoProbe(const char* path) {
    auto* dvd = DVDOpen(path);
    if (!dvd) return 30;
    auto* ifo = ifoOpen(dvd, 0);
    if (!ifo) { std::printf("IFO title0 unavailable\n"); DVDClose(dvd); return 31; }
    if (!ifo->pgci_ut) { std::printf("IFO pgci missing first_play=%d\n", ifo->first_play_pgc != nullptr); ifoClose(ifo); DVDClose(dvd); return 32; }
    std::printf("IFO VMG menu_lus=%u first_play=%d\n", ifo->pgci_ut->nr_of_lus, ifo->first_play_pgc != nullptr);
    for (unsigned lu = 0; lu < ifo->pgci_ut->nr_of_lus; ++lu) {
        auto* table = ifo->pgci_ut->lu[lu].pgcit;
        if (!table) continue;
        std::printf("IFO LU=%u pgcs=%u\n", lu, table->nr_of_pgci_srp);
        for (unsigned i = 0; i < table->nr_of_pgci_srp; ++i) {
            auto* pgc = table->pgci_srp[i].pgc;
            if (!pgc) continue;
            std::printf("IFO PGC=%u programs=%u cells=%u still=%u first=%u last=%u\n", i + 1,
                pgc->nr_of_programs, pgc->nr_of_cells, pgc->still_time,
                pgc->nr_of_cells ? pgc->cell_playback[0].first_sector : 0,
                pgc->nr_of_cells ? pgc->cell_playback[pgc->nr_of_cells - 1].last_sector : 0);
            for (unsigned c = 0; c < pgc->nr_of_cells; ++c) {
                const auto& cell = pgc->cell_playback[c];
                std::printf("IFO CELL=%u time=%u:%02u:%02u.%02u still=%u sectors=%u-%u\n", c + 1,
                    cell.playback_time.hour, cell.playback_time.minute, cell.playback_time.second,
                    cell.playback_time.frame_u, cell.still_time, cell.first_sector, cell.last_sector);
            }
        }
    }
    ifoClose(ifo); DVDClose(dvd); return 0;
}

static int DvdMenuVobDump(const char* path, const char* output) {
    auto* dvd = DVDOpen(path); if (!dvd) return 40;
    auto* file = DVDOpenFile(dvd, 1, DVD_READ_MENU_VOBS); if (!file) { DVDClose(dvd); return 41; }
    const auto blocks = DVDFileSize(file);
    std::vector<unsigned char> buffer(2048 * 256);
    FILE* out = std::fopen(output, "wb"); if (!out) { DVDCloseFile(file); DVDClose(dvd); return 42; }
    for (int offset = 0; offset < blocks; offset += 256) {
        const auto count = std::min<int64_t>(256, blocks - offset);
        const auto read = DVDReadBlocks(file, offset, static_cast<size_t>(count), buffer.data());
        if (read <= 0) break;
        std::fwrite(buffer.data(), 2048, static_cast<size_t>(read), out);
    }
    std::fclose(out); DVDCloseFile(file); DVDClose(dvd);
    std::printf("MENU VOB blocks=%d output=%s\n", blocks, output); return 0;
}

static int DvdVmgCellDump(const char* path, const char* output, int first, int count) {
    auto* dvd = DVDOpen(path); if (!dvd) return 50;
    auto* file = DVDOpenFile(dvd, 0, DVD_READ_MENU_VOBS); if (!file) { DVDClose(dvd); return 51; }
    std::vector<unsigned char> buffer(2048 * 256); FILE* out = std::fopen(output, "wb"); if (!out) return 52;
    auto read = DVDReadBlocks(file, first, count, buffer.data());
    if (read > 0) std::fwrite(buffer.data(), 2048, static_cast<size_t>(read), out);
    std::fclose(out); DVDCloseFile(file); DVDClose(dvd);
    std::printf("VMG cell first=%d count=%d read=%Id output=%s\n", first, count, read, output); return read > 0 ? 0 : 53;
}
#endif

static int Bluray(const char* path) {
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
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds(45)) break;
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
    if (argc < 3 || argc > 6) return 2;
    const auto path = Utf8(argv[2]);
#ifndef BLURAY_ONLY
    if (std::wstring(argv[1]) == L"dvd-menu-vob") return DvdMenuVobDump(path.c_str(), Utf8(argv[3]).c_str());
    if (std::wstring(argv[1]) == L"dvd-vmg-cell") return DvdVmgCellDump(path.c_str(), Utf8(argv[3]).c_str(), _wtoi(argv[4]), _wtoi(argv[5]));
    if (std::wstring(argv[1]) == L"dvd-ifo") return DvdIfoProbe(path.c_str());
    if (std::wstring(argv[1]) == L"dvd") return Dvd(path.c_str());
    if (std::wstring(argv[1]) == L"dvd-firstplay") return DvdFirstPlayDump(path.c_str(), Utf8(argv[3]).c_str());
#endif
    return Bluray(path.c_str());
}
