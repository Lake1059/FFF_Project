// gainmap_probe.cpp — command-line probe for private HDR gainmap stills.
#define _CRT_SECURE_NO_WARNINGS
#include "../3FP/Core/GainMapProbe.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

// ── SHA-256 via Windows CNG (guaranteed correct) ────────────────────────────
static std::string Sha256File(const std::string& path) {
    std::string out;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return out;
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0) {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0) {
            uint8_t buf[65536]; DWORD rd = 0;
            while (ReadFile(h, buf, sizeof(buf), &rd, nullptr) && rd > 0)
                BCryptHashData(hash, buf, rd, 0);
            uint8_t dg[32]; ULONG cb = 0;
            if (BCryptFinishHash(hash, dg, 32, 0) >= 0) {
                char b[65]; for (int i = 0; i < 32; i++) sprintf(b + i*2, "%02x", dg[i]);
                out = b;
            }
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    CloseHandle(h);
    return out;
}

static std::string JsonEscape(const std::string& s){
    std::string o; o.reserve(s.size()+8);
    for (char c : s){
        if (c=='"') o+="\\\""; else if (c=='\\') o+="\\\\";
        else if (c=='\n') o+="\\n"; else if (c=='\r') o+="\\r"; else if (c=='\t') o+="\\t";
        else if ((unsigned char)c < 0x20) { char b[8]; sprintf(b,"\\u%04x",(unsigned char)c); o+=b; }
        else o+=c;
    }
    return o;
}

static void WritePfm(const std::string& path, const std::vector<float>& rgba, int w, int h){
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "PF\n%d %d\n-1.0\n", w, h);
    // PFM is bottom-up; write top row last. We'll just write top-down (most tools tolerate;
    // note in sidecar).
    for (int y = h-1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            long i = ((long)y*w + x)*4;
            float r = rgba[i], g = rgba[i+1], b = rgba[i+2];
            fwrite(&r, 4, 1, f); fwrite(&g, 4, 1, f); fwrite(&b, 4, 1, f);
        }
    }
    fclose(f);
}

// End-to-end JXL container gate check, using a hand-built 16-byte file.
// Layout: box size(BE32)=16 | type "JXL " | body "jhgm" + zero pad. This is
// enough for the probe's JXL detection (which returns before any decode) and
// needs no real media / no ffmpeg.
static int RunSelftestJxlE2E() {
    // Write the hand-built fixture into a directory we control — NEVER the
    // process cwd (that would pollute whoever invoked us, e.g. a repo checkout).
    // Preference: system TEMP; if that is not writable (some locked-down hosts
    // deny child writes to %TEMP%), fall back to the directory the EXE lives in
    // (a build-output dir, not the user's cwd). PID suffix avoids collisions.
    // The file is unlinked as soon as Probe() returns, on every path.
    std::string path;
    {
        char tmpDir[MAX_PATH] = {0};
        DWORD tn = GetTempPathA(MAX_PATH, tmpDir);
        std::string dir = (tn > 0 && tn < MAX_PATH) ? tmpDir : "";
        if (!dir.empty() && dir.back() != '\\') dir.push_back('\\');
        std::string candidate = dir + "gainmap_probe_selftest_" + std::to_string(GetCurrentProcessId()) + ".jxl";
        FILE* probe = fopen(candidate.c_str(), "wb");
        if (probe) { fclose(probe); path = candidate; }
    }
    if (path.empty()) {
        char exe[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string d = exe;
        size_t slash = d.find_last_of('\\');
        if (slash != std::string::npos) d = d.substr(0, slash + 1); else d = "";
        path = d + "gainmap_probe_selftest_" + std::to_string(GetCurrentProcessId()) + ".jxl";
    }

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { printf("[FAIL] C0.jxl_cannot_write_tempfile (path=%s errno=%d:%s)\n",
                     path.c_str(), errno, strerror(errno)); return 1; }
    unsigned char hdr[16] = { 0,0,0,16, 'J','X','L',' ', 'j','h','g','m', 0,0,0,0 };
    fwrite(hdr, 1, 16, f);
    fclose(f);

    GainMapProbeResult R = GainMapProbe::Probe(path, false);
    // Cleanup regardless of assertion outcome. Retry briefly in case on-access
    // AV holds the file; report the result so a persistent failure is visible.
    int rc = -1;
    for (int attempt = 0; attempt < 10; ++attempt) {
        rc = remove(path.c_str());
        if (rc == 0) break;
        Sleep(150);
    }
    if (rc != 0) printf("  [warn] fixture cleanup failed rc=%d errno=%d:%s (%s)\n",
                        rc, errno, strerror(errno), path.c_str());

    int fails = 0;
    char d[220];
    sprintf(d, "present=%d form=\"%s\" verdict=\"%s\" reason=\"%s\" fixture=%s",
            (int)R.present, R.form.c_str(), R.verdict.c_str(), R.reason.c_str(), path.c_str());
    auto chk = [&](bool c, const char* n){ if (c) printf("[PASS] %s\n", n); else { fails++; printf("[FAIL] %s (%s)\n", n, d); } };
    chk(R.present == true,                       "C1.jxl_present");
    chk(R.form == "jhgm",                        "C2.jxl_form_jhgm");
    chk(R.verdict == "fail_closed_unsupported",  "C3.jxl_verdict_fail_closed");
    chk(R.reason == "jxl_gainmap_not_implemented","C4.jxl_reason_stable");
    return fails;
}

int main(int argc, char** argv){
    if (argc >= 2 && std::string(argv[1]) == "--selftest") {
        printf("== gainmap_probe --selftest ==\n");
        int fails = 0;
        printf("-- internal helper checks (tmap trailing / PQ-HLG gate / baseRgba linearize) --\n");
        fails += GainMapProbe::RunSelftest();
        printf("-- end-to-end JXL container check (hand-built 16-byte file) --\n");
        fails += RunSelftestJxlE2E();
        printf("== selftest total failures=%d ==\n", fails);
        return fails == 0 ? 0 : 1;
    }
    if (argc < 2) {
        fprintf(stderr, "usage: gainmap_probe <file> [--dump <dir>] [--base-only] [--target-headroom <stops>]\n"
                        "       gainmap_probe --selftest\n");
        return 2;
    }
    std::string path = argv[1];
    std::string dumpDir;
    bool baseOnly = false;
    // Player target headroom in log2 stops. 4.9 ≈ HDR10 reference viewing
    // condition (≈1000-nit display vs ≈120-nit SDR base; 2^4.9 ≈ 29.5×).
    double targetHeadroom = GainMapProbe::kDefaultTargetHeadroomLog2;
    for (int i = 2; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--base-only") baseOnly = true;
        else if (a == "--dump" && i+1 < argc) { dumpDir = argv[++i]; }
        else if (a == "--target-headroom" && i+1 < argc) { targetHeadroom = atof(argv[++i]); }
    }

    // sha256 of file (Windows CNG)
    std::string sha = Sha256File(path);

    GainMapProbeResult R = GainMapProbe::Probe(path, baseOnly, targetHeadroom);

    // dump
    if (!dumpDir.empty()) {
        if (dumpDir.back() != '/' && dumpDir.back() != '\\') dumpDir += "\\";
        if (!R.appliedRgba.empty()) WritePfm(dumpDir + "applied.pfm", R.appliedRgba, R.outW, R.outH);
        if (!R.baseRgba.empty()) WritePfm(dumpDir + "base.pfm", R.baseRgba, R.outW, R.outH);
        std::ofstream note(dumpDir + "note.txt");
        note << "verdict=" << R.verdict << "\nreason=" << R.reason << "\n";
        note << "form=" << R.form << " family=" << R.metaFamily << " source=" << R.metaSource << "\n";
        note << "decode " << R.decodeW << "x" << R.decodeH << " ch=" << R.decodeChannels << " codec=" << R.decodeCodec << "\n";
        note << "apply.ok=" << R.applyOk << " mode=" << R.applyMode << "\n";
        note << "stats p50=" << R.statsP50 << " maxLinear=" << R.statsMaxLinear << " over1Pct=" << R.statsOver1Pct << "\n";
        note << "headroom: baseLog2=" << R.baseHdrHeadroomLog2 << " altLog2=" << R.altHdrHeadroomLog2
             << " linear=" << R.headroomLinear << " targetLog2=" << R.targetHeadroomLog2
             << " weight=" << R.applyWeight << "\n";
        note << "applied.pfm = linear RGB float after gain apply (top-down in file; PFM convention is bottom-up)\n";
        note << "base.pfm    = linear RGB float SDR base (after sRGB->linear), no gain\n";
    }

    // JSON
    std::ostringstream j;
    j << "{\"schema\":\"3fp-gainmap-probe/1\"";
    j << ",\"file\":\"" << JsonEscape(path) << "\"";
    j << ",\"sha256\":\"" << sha << "\"";
    j << ",\"container\":{\"majorBrand\":\"" << JsonEscape(R.majorBrand) << "\"";
    j << ",\"compatibleBrands\":[";
    for (size_t i = 0; i < R.compatibleBrands.size(); i++) {
        if (i) j << ",";
        j << "\"" << JsonEscape(R.compatibleBrands[i]) << "\"";
    }
    j << "],\"isStill\":" << (R.isStill ? "true" : "false") << "}";
    j << ",\"gainmap\":{\"present\":" << (R.present ? "true" : "false");
    j << ",\"form\":\"" << JsonEscape(R.form) << "\"";
    j << ",\"auxCUrn\":\"" << JsonEscape(R.auxCUrn) << "\"";
    j << ",\"grid\":{\"present\":" << (R.gridPresent ? "true" : "false")
      << ",\"rows\":" << R.gridRows << ",\"cols\":" << R.gridCols
      << ",\"flags\":" << R.gridFlags
      << ",\"tailPresent\":" << (R.gridTailPresent ? "true" : "false")
      << ",\"tailW\":" << R.gridTailW << ",\"tailH\":" << R.gridTailH << "}";
    j << ",\"items\":[";
    for (size_t i = 0; i < R.items.size(); i++) {
        if (i) j << ",";
        j << "{\"id\":" << R.items[i].id << ",\"type\":\"" << JsonEscape(R.items[i].type)
          << "\",\"role\":\"" << JsonEscape(R.items[i].role) << "\"}";
    }
    j << "]}";
    j << ",\"decode\":{\"ok\":" << (R.decodeOk ? "true" : "false")
      << ",\"codec\":\"" << JsonEscape(R.decodeCodec) << "\""
      << ",\"width\":" << R.decodeW << ",\"height\":" << R.decodeH
      << ",\"channels\":" << R.decodeChannels << ",\"bitDepth\":" << R.decodeBitDepth
      << ",\"reason\":\"" << JsonEscape(R.decodeReason) << "\"}";
    j << ",\"meta\":{\"family\":\"" << JsonEscape(R.metaFamily) << "\""
      << ",\"source\":\"" << JsonEscape(R.metaSource) << "\""
      << ",\"offsetSdr\":[" << R.offsetSdr[0] << "," << R.offsetSdr[1] << "," << R.offsetSdr[2] << "]"
      << ",\"offsetHdr\":[" << R.offsetHdr[0] << "," << R.offsetHdr[1] << "," << R.offsetHdr[2] << "]"
      << ",\"logBoost\":[" << R.logBoostMin[0] << "," << R.logBoostMax[0] << "," << R.logBoostMin[1] << "," << R.logBoostMax[1] << "," << R.logBoostMin[2] << "," << R.logBoostMax[2] << "]"
      << ",\"baseHdrHeadroomLog2\":" << R.baseHdrHeadroomLog2
      << ",\"altHdrHeadroomLog2\":" << R.altHdrHeadroomLog2
      << ",\"headroomLinear\":" << R.headroomLinear
      << ",\"targetHeadroomLog2\":" << R.targetHeadroomLog2
      << ",\"applyWeight\":" << R.applyWeight
      << ",\"gamma\":" << R.gamma << "}";
    j << ",\"apply\":{\"ok\":" << (R.applyOk ? "true" : "false")
      << ",\"mode\":\"" << JsonEscape(R.applyMode) << "\""
      << ",\"stats\":{\"p50\":" << R.statsP50 << ",\"maxLinear\":" << R.statsMaxLinear
      << ",\"over1Pct\":" << R.statsOver1Pct << "}"
      << ",\"reason\":\"" << JsonEscape(R.reason) << "\"}";
    j << ",\"verdict\":\"" << R.verdict << "\"";
    j << ",\"trailingBytesIgnored\":" << R.trailingBytesIgnored;
    j << ",\"notes\":[";
    for (size_t i = 0; i < R.notes.size(); i++) {
        if (i) j << ",";
        j << "\"" << JsonEscape(R.notes[i]) << "\"";
    }
    j << "]}";
    printf("%s\n", j.str().c_str());
    return 0;
}
