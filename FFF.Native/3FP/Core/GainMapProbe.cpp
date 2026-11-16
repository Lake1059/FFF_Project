#include "pch.h"
#include "GainMapProbe.h"
#include "GainMapAppleMeta.h"
#include "GainMapJpeg.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace {

// ── byte helpers ─────────────────────────────────────────────────────────────
inline uint16_t Be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
inline uint32_t ReadBigN(const uint8_t* p, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v = (v << 8) | p[i];
    return v;
}
inline int ReadBox(const std::vector<uint8_t>& d, long pos, long end,
                   char type[5], long& size, long& hdr) {
    type[0] = type[1] = type[2] = type[3] = 0; type[4] = 0;
    size = 0; hdr = 8;
    if (pos < 0 || pos + 8 > end) return 0;
    long sz = (long)Be32(d.data() + pos);
    memcpy(type, d.data() + pos + 4, 4);
    if (sz == 1) {
        if (pos + 16 > end) return 0;
        sz = 0;
        for (int i = 0; i < 8; i++) sz = (sz << 8) | d[pos + 8 + i];
        hdr = 16;
    } else if (sz == 0) {
        sz = end - pos;
    }
    if (sz < hdr || pos + sz > end) return 0;
    size = sz;
    return 1;
}
inline bool IsCodecItem(const std::string& t) {
    return t == "av01" || t == "hvc1" || t == "hev1";
}

// Convert a UTF-8 path to a UTF-16 wstring for the Win32-only JPEG/WebP probe.
// The old code did std::wstring(pathUtf8.begin(), pathUtf8.end()), which copies
// each UTF-8 BYTE into a wchar_t and garbles any non-ASCII path (the JPEG/WebP
// probe opens via std::ifstream on the wide path, so such a path silently
// failed to open). MultiByteToWideChar is the correct conversion.
inline std::wstring Utf8ToWide(const std::string& u8) {
    if (u8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), nullptr, 0);
    if (n <= 0) return std::wstring();
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), (int)u8.size(), w.data(), n);
    return w;
}

struct Extent { long off = 0; long len = 0; };
struct IlocEntry {
    int method = 0;
    long baseOff = 0;
    std::vector<Extent> extents;
};
struct RefEntry {
    std::string type;
    int from = 0;
    std::vector<int> to;
};
struct PropBox {
    std::string type;
    long off = 0;   // content offset (after box header)
    long size = 0;  // content size
};

struct GrpGroup {
    uint32_t groupId;
    std::vector<int> entities; // item IDs in order
};

struct ParsedContainer {
    std::string majorBrand;
    std::vector<std::string> compatibleBrands;
    bool isStill = false;
    int primary = -1;
    std::map<int, std::string> items;            // id -> infe type
    std::map<int, IlocEntry> locs;
    std::vector<RefEntry> refs;
    std::vector<GrpGroup> groups;                // grpl groups (altr etc)
    std::vector<PropBox> ipco;
    std::map<int, std::vector<int>> ipma;        // item -> 1-based prop indices
    long idatStart = -1, idatEnd = -1;
    bool truncated = false;
    std::string truncReason;
};

// ── parsers ──────────────────────────────────────────────────────────────────
void ParseIinf(const std::vector<uint8_t>& d, long start, long end,
               std::map<int, std::string>& items, bool& itemTypeUnknown) {
    if (start + 4 > end) return;
    int ver = d[start];
    long p = start + 4;
    int count;
    if (ver == 0) {
        if (p + 2 > end) return;
        count = Be16(d.data() + p); p += 2;
    } else {
        if (p + 4 > end) return;
        count = (int)Be32(d.data() + p); p += 4;
    }
    for (int i = 0; i < count && p + 8 <= end; i++) {
        char type[5]; long size, hdr;
        if (!ReadBox(d, p, end, type, size, hdr) || std::string(type) != "infe") break;
        long e = p + hdr, ee = p + size;
        if (e + 4 > ee) break;
        int iver = d[e];
        long q = e + 4;
        int itemId;
        if (iver >= 3) {
            if (q + 4 > ee) break;
            itemId = (int)Be32(d.data() + q); q += 4;
        } else {
            if (q + 2 > ee) break;
            itemId = Be16(d.data() + q); q += 2;
        }
        q += 2; // item_protection_index
        if (iver < 2) { itemTypeUnknown = true; p += size; continue; }
        if (q + 4 > ee) break;
        items[itemId] = std::string((const char*)d.data() + q, 4);
        p += size;
    }
}

void ParseIloc(const std::vector<uint8_t>& d, long start, long end,
               std::map<int, IlocEntry>& locs, bool& incomplete) {
    incomplete = false;
    if (start + 8 > end) { incomplete = true; return; }
    int ver = d[start];
    long p = start + 4;
    uint8_t b0 = d[p], b1 = d[p + 1]; p += 2;
    int offSize = b0 >> 4, lenSize = b0 & 0xF;
    int baseSize = b1 >> 4, idxSize = b1 & 0xF;
    int count, idSize;
    if (ver < 2) { count = Be16(d.data() + p); p += 2; idSize = 2; }
    else { count = (int)Be32(d.data() + p); p += 4; idSize = 4; }
    for (int i = 0; i < count; i++) {
        if (p + idSize + 2 > end) { incomplete = true; break; }
        int itemId = idSize == 4 ? (int)Be32(d.data() + p) : Be16(d.data() + p);
        p += idSize;
        int method = 0;
        if (ver == 1 || ver == 2) { method = Be16(d.data() + p) & 0xF; p += 2; }
        p += 2; // data_reference_index
        if (p + baseSize > end) { incomplete = true; break; }
        long baseOff = baseSize > 0 ? (long)ReadBigN(d.data() + p, baseSize) : 0;
        p += baseSize;
        if (p + 2 > end) { incomplete = true; break; }
        int ec = Be16(d.data() + p); p += 2;
        if (p + (long)ec * (idxSize + offSize + lenSize) > end) { incomplete = true; break; }
        IlocEntry e; e.method = method; e.baseOff = baseOff;
        for (int k = 0; k < ec; k++) {
            p += idxSize;
            long o = offSize > 0 ? (long)ReadBigN(d.data() + p, offSize) : 0; p += offSize;
            long l = lenSize > 0 ? (long)ReadBigN(d.data() + p, lenSize) : 0; p += lenSize;
            e.extents.push_back({o, l});
        }
        locs[itemId] = e;
    }
}

void ParseIref(const std::vector<uint8_t>& d, long start, long end,
               std::vector<RefEntry>& refs) {
    if (start + 4 > end) return;
    int ver = d[start];
    long p = start + 4;
    int idSize = ver >= 1 ? 4 : 2;
    while (p + 8 <= end) {
        char type[5]; long size, hdr;
        if (!ReadBox(d, p, end, type, size, hdr)) break;
        long q = p + hdr, ee = p + size;
        if (q + idSize > ee) break;
        int from = idSize == 4 ? (int)Be32(d.data() + q) : Be16(d.data() + q);
        q += idSize;
        std::vector<int> vals;
        while (q + idSize <= ee) {
            vals.push_back(idSize == 4 ? (int)Be32(d.data() + q) : Be16(d.data() + q));
            q += idSize;
        }
        RefEntry r; r.type = type; r.from = from;
        if (!vals.empty() && vals[0] == (int)vals.size() - 1) {
            for (size_t i = 1; i < vals.size(); i++) r.to.push_back(vals[i]);
        } else {
            r.to = vals;
        }
        refs.push_back(r);
        p += size;
    }
}

// grpl content is a concatenation of group sub-boxes ("altr"). Each sub-box
// is size(4) + type[4] + FullBox(version/flags=4) + group_id(u32) +
// num_entities(u32) + entity_id(u32)[]. The content boundary starts the first
// sub-box directly. The old code skipped 4 bytes then read the "altr" type as
// the first group_id (bogus group 0x616C7472) and only landed on the real
// group by luck on single-group files.
void ParseGrpl(const std::vector<uint8_t>& d, long start, long end,
               std::vector<GrpGroup>& groups) {
    long p = start;
    while (p + 8 <= end) {
        long sz = (long)Be32(d.data() + p);
        // sub-box header(8) + inner FullBox(4) + group_id(4) + count(4) = 20 min.
        if (sz < 20 || p + sz > end) break;
        long q = p + 8 + 4;  // sub-box header 8 bytes, then inner FullBox 4 bytes
        GrpGroup g;
        g.groupId = Be32(d.data() + q); q += 4;
        uint32_t n = Be32(d.data() + q); q += 4;
        for (uint32_t i = 0; i < n && q + 4 <= end; i++) {
            g.entities.push_back((int)Be32(d.data() + q)); q += 4;
        }
        groups.push_back(g);
        p += sz;
    }
}

int ClassifyAuxUrn(const std::string& urn) {
    std::string u; u.reserve(urn.size());
    for (char c : urn) u.push_back((char)tolower((unsigned char)c));
    if (u.find("21496") != std::string::npos) return 1;
    // Exact Apple URN is Apple; any other hdrgainmap-ish URN is an unregistered vendor.
    if (u == "urn:com:apple:photo:2020:aux:hdrgainmap") return 2;
    if (u.find("hdrgainmap") != std::string::npos ||
        u.find("hdr-gain-map") != std::string::npos ||
        u.find("hdr_gain_map") != std::string::npos) return 4;
    if (u.find("auxid:1") != std::string::npos ||
        u.find("auxiliary:alpha") != std::string::npos) return 3;
    if (u.find("depth") != std::string::npos || u.find("disparity") != std::string::npos ||
        u.find("mask") != std::string::npos || u.find("segment") != std::string::npos ||
        u.find("matte") != std::string::npos || u.find("thumbnail") != std::string::npos) return 0;
    return 4;
}

struct AuxCProp { int propIndex; int kind; std::string urn; };

void ParseIprp(const std::vector<uint8_t>& d, long start, long end,
               std::vector<PropBox>& ipco, std::map<int, std::vector<int>>& ipma,
               std::vector<AuxCProp>& auxCProps) {
    long p = start;
    while (p + 8 <= end) {
        char type[5]; long size, hdr;
        if (!ReadBox(d, p, end, type, size, hdr)) return;
        if (std::string(type) == "ipco") {
            long q = p + hdr, ee = p + size;
            while (q + 8 <= ee) {
                char t2[5]; long s2, h2;
                if (!ReadBox(d, q, ee, t2, s2, h2)) break;
                PropBox pb; pb.type = t2; pb.off = q + h2; pb.size = s2 - h2;
                ipco.push_back(pb);
                if (std::string(t2) == "auxC") {
                    long s = q + h2 + 4, e2 = q + s2; // auxC FullBox
                    long z = s;
                    while (z < e2 && d[z] != 0) z++;
                    if (z > s) {
                        std::string urn((const char*)d.data() + s, (size_t)(z - s));
                        int kind = ClassifyAuxUrn(urn);
                        if (kind != 0) auxCProps.push_back({(int)ipco.size(), kind, urn});
                    }
                }
                q += s2;
            }
        } else if (std::string(type) == "ipma") {
            long sp = p + hdr, ep = p + size;
            if (sp + 8 <= ep) {
                int ver = d[sp];
                int flags = (d[sp + 1] << 16) | (d[sp + 2] << 8) | d[sp + 3];
                long q = sp + 4;
                int count = (int)Be32(d.data() + q); q += 4;
                bool wide = (flags & 1) != 0;
                int idSize = ver >= 1 ? 4 : 2;
                for (int i = 0; i < count; i++) {
                    if (q + idSize + 1 > ep) break;
                    int itemId = idSize == 4 ? (int)Be32(d.data() + q) : Be16(d.data() + q);
                    q += idSize;
                    int ac = d[q++];
                    std::vector<int> list;
                    for (int k = 0; k < ac; k++) {
                        if (q + (wide ? 2 : 1) > ep) break;
                        int v = wide ? Be16(d.data() + q) : d[q];
                        q += wide ? 2 : 1;
                        list.push_back(v & (wide ? 0x7FFF : 0x7F));
                    }
                    ipma[itemId] = list;
                }
            }
        }
        p += size;
    }
}

bool SliceItem(const std::vector<uint8_t>& d, const ParsedContainer& c, int itemId,
               std::vector<uint8_t>& out, std::string& why) {
    auto it = c.locs.find(itemId);
    if (it == c.locs.end()) { why = "no iloc entry"; return false; }
    const IlocEntry& l = it->second;
    long ds, de;
    if (l.method == 0) { ds = 0; de = (long)d.size(); }
    else if (l.method == 1) {
        if (c.idatStart < 0 || c.idatEnd <= c.idatStart) { why = "method=1 but no idat"; return false; }
        ds = c.idatStart; de = c.idatEnd;
    } else { why = "unsupported construction_method"; return false; }
    long total = 0;
    for (auto& e : l.extents) total += e.len;
    if (total <= 0 || l.baseOff < 0 || ds + l.baseOff + total > de) { why = "extent out of range"; return false; }
    out.resize((size_t)total);
    long w = 0;
    for (auto& e : l.extents) {
        if (e.off < 0 || e.len < 0 || ds + l.baseOff + e.off + e.len > de) { why = "extent out of range"; return false; }
        memcpy(out.data() + w, d.data() + ds + l.baseOff + e.off, (size_t)e.len);
        w += e.len;
    }
    return true;
}

bool FindHvcC(const ParsedContainer& c, int itemId, long& off, long& len, std::string& why) {
    auto it = c.ipma.find(itemId);
    if (it != c.ipma.end()) {
        for (int ix : it->second) {
            if (ix >= 1 && ix <= (int)c.ipco.size() && c.ipco[ix - 1].type == "hvcC") {
                off = c.ipco[ix - 1].off; len = c.ipco[ix - 1].size; return true;
            }
        }
        why = "ipma does not associate hvcC"; return false;
    }
    int cnt = 0, found = -1;
    for (int i = 0; i < (int)c.ipco.size(); i++) if (c.ipco[i].type == "hvcC") { cnt = i; found = i; }
    if (cnt == 1 && found >= 0) { off = c.ipco[found].off; len = c.ipco[found].size; return true; }
    why = "no unambiguous hvcC"; return false;
}

bool ParseHvcC(const std::vector<uint8_t>& d, long off, long len, int& lengthSize,
               std::vector<std::vector<uint8_t>>& paramSets, std::string& why) {
    lengthSize = 0;
    if (len < 23) { why = "hvcC too short"; return false; }
    if (d[off] != 1) { why = "hvcC version != 1"; return false; }
    lengthSize = (d[off + 21] & 0x03) + 1;
    int numArrays = d[off + 22];
    long p = off + 23, end = off + len;
    for (int a = 0; a < numArrays; a++) {
        if (p + 3 > end) { why = "hvcC array header out of range"; return false; }
        int nalType = d[p] & 0x3F;
        int n = Be16(d.data() + p + 1); p += 3;
        for (int k = 0; k < n; k++) {
            if (p + 2 > end) { why = "hvcC NAL length out of range"; return false; }
            int ln = Be16(d.data() + p); p += 2;
            if (ln <= 0 || p + ln > end) { why = "hvcC NAL out of range"; return false; }
            if (nalType == 32 || nalType == 33 || nalType == 34) {
                std::vector<uint8_t> u((size_t)ln);
                memcpy(u.data(), d.data() + p, (size_t)ln);
                paramSets.push_back(std::move(u));
            }
            p += ln;
        }
    }
    return true;
}

bool ToAnnexB(const std::vector<uint8_t>& item, int lengthSize,
              const std::vector<std::vector<uint8_t>>& paramSets,
              std::vector<uint8_t>& out, std::string& why) {
    if (lengthSize != 1 && lengthSize != 2 && lengthSize != 4) { why = "bad lengthSize"; return false; }
    std::vector<std::pair<long, long>> nals;
    long total = 0;
    for (auto& ps : paramSets) total += 4 + (long)ps.size();
    long p = 0;
    while (p + lengthSize <= (long)item.size()) {
        long head = p;
        long ln = (long)ReadBigN(item.data() + p, lengthSize);
        p += lengthSize;
        if (ln <= 0 || p + ln > (long)item.size()) {
            char b[128]; sprintf(b, "length-prefix chain broken at %ld remain=%ld", head, (long)item.size() - p);
            why = b; return false;
        }
        nals.push_back({p, ln});
        total += 4 + ln;
        p += ln;
    }
    if (p != (long)item.size()) { why = "trailing bytes after last NAL"; return false; }
    if (nals.empty()) { why = "no NAL unit"; return false; }
    out.resize((size_t)total);
    long w = 0;
    auto emitStart = [&]() { out[w]=0; out[w+1]=0; out[w+2]=0; out[w+3]=1; w += 4; };
    for (auto& ps : paramSets) {
        emitStart(); memcpy(out.data() + w, ps.data(), ps.size()); w += (long)ps.size();
    }
    for (auto& n : nals) {
        emitStart(); memcpy(out.data() + w, item.data() + n.first, (size_t)n.second); w += n.second;
    }
    return true;
}

// Extract a coded image item (hvc1/av01) into a decodable byte stream:
// AV1 OBU items pass through raw; HEVC items are converted to Annex-B.
// Returns false with a readable reason on any structural problem.
bool ExtractCodedStream(const std::vector<uint8_t>& d, const ParsedContainer& c,
                        int itemId, std::vector<uint8_t>& out, std::string& why,
                        std::string* outCodec = nullptr) {
    auto it = c.items.find(itemId);
    if (it == c.items.end()) { why = "item not found"; return false; }
    const std::string& codec = it->second;
    if (!IsCodecItem(codec)) { why = "not a codec item: " + codec; return false; }
    if (outCodec) *outCodec = codec;
    std::vector<uint8_t> raw;
    if (!SliceItem(d, c, itemId, raw, why)) return false;
    if (codec == "av01") { out = std::move(raw); return true; }
    long ho, hl; std::string hw;
    if (!FindHvcC(c, itemId, ho, hl, hw)) { why = "hvcC: " + hw; return false; }
    int ls; std::vector<std::vector<uint8_t>> ps; std::string pw;
    if (!ParseHvcC(d, ho, hl, ls, ps, pw)) { why = "hvcC parse: " + pw; return false; }
    std::vector<uint8_t> ann; std::string cw;
    if (!ToAnnexB(raw, ls, ps, ann, cw)) { why = "annexB: " + cw; return false; }
    out = std::move(ann);
    return true;
}

bool FindIspe(const ParsedContainer& c, const std::vector<uint8_t>& d, int itemId,
              int& w, int& h) {
    w = h = 0;
    auto it = c.ipma.find(itemId);
    if (it == c.ipma.end()) return false;
    for (int ix : it->second) {
        if (ix < 1 || ix > (int)c.ipco.size() || c.ipco[ix - 1].type != "ispe") continue;
        long off = c.ipco[ix - 1].off; long size = c.ipco[ix - 1].size;
        if (size < 12) continue;
        w = (int)Be32(d.data() + off + 4);
        h = (int)Be32(d.data() + off + 8);
        return w > 0 && h > 0;
    }
    return false;
}

// ── ISO 21496-1 GainMapMetadata ──────────────────────────────────────────────
struct IsoMeta {
    int channels = 1;
    uint8_t rawFlags = 0; // tmap gain-map flags byte: 0x80=is_multichannel, 0x40=use_base_colour_space, 0x3F=reserved
    bool useBaseColorSpace = false;
    // Headroom of the base (SDR/HDR) and alternate sides, in log2 stops.
    // These are the ONLY basis for the HDR-base / apply-weight decision.
    float baseHdrHeadroomLog2 = 0.f, altHdrHeadroomLog2 = 0.f;
    float gainMin[3] = {0,0,0}, gainMax[3] = {0,0,0};
    float gammaC[3] = {1,1,1};
    float offSdr[3] = {0,0,0}, offHdr[3] = {0,0,0};
};

// Result of parsing the ISO 21496-1 metadata box.
enum class IsoParse { Ok, Malformed, Unsupported };

IsoParse ParseIso21496(const uint8_t* d, long off, long len, IsoMeta& m, long& consumed,
                   std::string& err) {
    consumed = 0; err.clear();
    long end = off + len, p = off;
    if (p + 5 > end) { err = "tmap metadata too short"; return IsoParse::Malformed; }
    int minVersion = (Be16(d + p)); p += 2;
    if (minVersion != 0) { err = "tmap minVersion != 0"; return IsoParse::Unsupported; }
    p += 2; // writer_version
    uint8_t flags = d[p++];
    m.rawFlags = flags;
    m.useBaseColorSpace = (flags & 0x40) != 0;
    // Plan §2.4.4: bits 0x3F are reserved. Only 0x80 (is_multichannel) and
    // 0x40 (use_base_colour_space) are defined. Any reserved bit set ⇒ reject.
    if (flags & 0x3F) {
        char b[96]; sprintf(b, "tmap flags reserved bits set: 0x%02X (only 0x80/0x40 defined)", flags);
        err = b; return IsoParse::Unsupported;
    }
    // Always use the per-channel-denominator layout (40 bytes/channel):
    //   16 bytes baseHdrHeadroom (num/den) + 16 bytes altHdrHeadroom (num/den)
    //   + channels * (5 rationals * 8 bytes).
    // The old shared-denominator layout was selected by reserved bit 0x08,
    // which is now rejected above (dead branch removed, plan §2.4).
    long remaining = end - p;
    if (remaining < 16) { err = "tmap metadata shorter than hdrMin+hdrMax"; return IsoParse::Malformed; }
    long chBytes = remaining - 16;
    int channels = (int)(chBytes / 40);
    consumed = p - off + 16 + (long)channels * 40;
    if (channels < 1) { err = "tmap metadata has no channel block"; return IsoParse::Malformed; }
    if (channels > 3) channels = 3;  // cap at 3 channels
    m.channels = channels;

    // Read a signed 32-bit numerator / unsigned 32-bit denominator rational.
    // Denominator must be > 0 (plan §2.4.3): zero/negative ⇒ malformed.
    auto readRat = [&](float& out, const char* what, int ch) -> bool {
        int num = (int)Be32(d + p);
        uint32_t den = Be32(d + p + 4);
        p += 8;
        if (den == 0) {
            char b[128]; sprintf(b, "tmap %s[ch%d] denominator == 0", what, ch);
            err = b; return false;
        }
        out = (float)((double)num / (double)den);
        return true;
    };
    if (!readRat(m.baseHdrHeadroomLog2, "baseHdrHeadroom", 0)) return IsoParse::Malformed;
    if (!readRat(m.altHdrHeadroomLog2, "altHdrHeadroom", 0)) return IsoParse::Malformed;
    for (int c = 0; c < channels; c++) {
        if (!readRat(m.gainMin[c], "gainMin", c)) return IsoParse::Malformed;
        if (!readRat(m.gainMax[c], "gainMax", c)) return IsoParse::Malformed;
        if (!readRat(m.gammaC[c], "gamma", c)) return IsoParse::Malformed;
        if (!readRat(m.offSdr[c], "offSdr", c)) return IsoParse::Malformed;
        if (!readRat(m.offHdr[c], "offHdr", c)) return IsoParse::Malformed;
    }
    if (channels == 1) {
        // Mono gainmap: channel 0 parameters applied to all RGB channels.
        for (int c = 1; c < 3; c++) {
            m.gainMin[c] = m.gainMin[0]; m.gainMax[c] = m.gainMax[0];
            m.gammaC[c] = m.gammaC[0]; m.offSdr[c] = m.offSdr[0]; m.offHdr[c] = m.offHdr[0];
        }
    }
    // Plan §2.4.1/2: per-channel gainMin <= gainMax and gamma > 0.
    for (int c = 0; c < 3; c++) {
        if (m.gainMin[c] > m.gainMax[c]) {
            char b[128]; sprintf(b, "tmap gainMin[ch%d]=%.6g > gainMax[ch%d]=%.6g", c, m.gainMin[c], c, m.gainMax[c]);
            err = b; return IsoParse::Malformed;
        }
        if (!(m.gammaC[c] > 0.f)) {
            char b[96]; sprintf(b, "tmap gamma[ch%d]=%.6g must be > 0", c, m.gammaC[c]);
            err = b; return IsoParse::Malformed;
        }
    }
    consumed = p - off;
    return IsoParse::Ok;
}

// ── Apple MakerNote tag33/48 ─────────────────────────────────────────────────
bool MnU16(const std::vector<uint8_t>& d, long o, bool be, int& v) {
    if (o + 2 > (long)d.size()) return false;
    v = be ? (d[o] << 8) | d[o+1] : d[o] | (d[o+1] << 8);
    return true;
}
bool MnU32(const std::vector<uint8_t>& d, long o, bool be, int& v) {
    if (o + 4 > (long)d.size()) return false;
    uint32_t x = be ? ((uint32_t)d[o]<<24)|((uint32_t)d[o+1]<<16)|((uint32_t)d[o+2]<<8)|d[o+3]
                    : ((uint32_t)d[o])|((uint32_t)d[o+1]<<8)|((uint32_t)d[o+2]<<16)|((uint32_t)d[o+3]<<24);
    v = (int)x; return true;
}

bool AppleMakerNote(const std::vector<uint8_t>& exif, float& m33, float& m48, std::string& why) {
    m33 = m48 = 0;
    if (exif.size() < 12) { why = "exif too short"; return false; }
    int tiffHeaderOffset = (exif[0] << 24) | (exif[1] << 16) | (exif[2] << 8) | exif[3];
    long tiff = 4 + tiffHeaderOffset;
    if (tiff + 8 > (long)exif.size()) { why = "tiff offset out of range"; return false; }
    bool be;
    if (exif[tiff]=='M' && exif[tiff+1]=='M') be = true;
    else if (exif[tiff]=='I' && exif[tiff+1]=='I') be = false;
    else { why = "no MM/II"; return false; }
    int magic; if (!MnU16(exif, tiff+2, be, magic) || magic != 42) { why = "magic != 42"; return false; }
    int ifd0Off; if (!MnU32(exif, tiff+4, be, ifd0Off)) { why = "no ifd0"; return false; }
    // Find 0x8769 in IFD0
    long ifd = tiff + ifd0Off;
    int count; if (!MnU16(exif, ifd, be, count)) { why = "ifd0 count"; return false; }
    long p = ifd + 2;
    int exifIfdOff = -1;
    for (int i = 0; i < count; i++, p += 12) {
        int tag, type, cnt;
        if (!MnU16(exif, p, be, tag)) { why = "ifd0 entry"; return false; }
        MnU16(exif, p+2, be, type); MnU32(exif, p+4, be, cnt);
        if (tag == 0x8769 && type == 4 && cnt >= 1) { MnU32(exif, p+8, be, exifIfdOff); break; }
    }
    if (exifIfdOff < 0) { why = "no ExifIFD"; return false; }
    // Find 0x927C in ExifIFD
    ifd = tiff + exifIfdOff;
    if (!MnU16(exif, ifd, be, count)) { why = "exififd count"; return false; }
    p = ifd + 2;
    int mnAbs = -1, mnLen = 0;
    for (int i = 0; i < count; i++, p += 12) {
        int tag, type, cnt;
        if (!MnU16(exif, p, be, tag)) { why = "exififd entry"; return false; }
        MnU16(exif, p+2, be, type); MnU32(exif, p+4, be, cnt);
        if (tag == 0x927C && cnt > 0) {
            int off; MnU32(exif, p+8, be, off);
            mnAbs = (int)(tiff + off); mnLen = cnt; break;
        }
    }
    if (mnAbs < 0 || mnLen < 16) { why = "no MakerNote"; return false; }
    const char* sig = "Apple iOS";
    for (int i = 0; i < 9; i++) if (exif[mnAbs + i] != (uint8_t)sig[i]) { why = "MakerNote signature"; return false; }
    long mnIfd = mnAbs + 14;
    if (!MnU16(exif, mnIfd, be, count)) { why = "mn ifd count"; return false; }
    p = mnIfd + 2;
    auto readTag = [&](int tagWant, float& outv) -> bool {
        for (int i = 0; i < count; i++, p += 12) {
            int tag, type, cnt;
            if (!MnU16(exif, p, be, tag)) return false;
            MnU16(exif, p+2, be, type); MnU32(exif, p+4, be, cnt);
            if (tag != tagWant) continue;
            if (type == 5 || type == 10) {
                int off; MnU32(exif, p+8, be, off);
                long v = mnAbs + off;
                if (v + 8 > (long)exif.size()) return false;
                int num, den;
                MnU32(exif, v, be, num); MnU32(exif, v+4, be, den);
                if (den == 0) return false;
                outv = (float)((double)num / den); return true;
            }
            if (type == 9 || type == 11) {
                int bits; MnU32(exif, p+8, be, bits);
                float f; memcpy(&f, &bits, 4);
                outv = f; return true;
            }
            return false;
        }
        return false;
    };
    if (!readTag(0x0021, m33)) { why = "tag33"; return false; }
    if (!readTag(0x0030, m48)) { why = "tag48"; return false; }
    return true;
}

// XMP helpers: search raw file bytes for tag content.
bool XmpFindFloat(const std::vector<uint8_t>& d, const std::string& tag, float& outv) {
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    for (size_t i = 0; i + open.size() < d.size(); i++) {
        if (memcmp(d.data() + i, open.data(), open.size()) != 0) continue;
        size_t j = i + open.size();
        while (j < d.size() && (d[j] == ' ' || d[j] == '\n' || d[j] == '\t' || d[j] == '\r')) j++;
        size_t k = j;
        while (k < d.size() && d[k] != '<' && d[k] != ' ') k++;
        std::string val((const char*)d.data() + j, k - j);
        // find close
        if (k + close.size() <= d.size() && memcmp(d.data() + k, close.data(), close.size()) == 0) {
            outv = (float)atof(val.c_str());
            return true;
        }
    }
    return false;
}
bool XmpContains(const std::vector<uint8_t>& d, const std::string& needle) {
    if (needle.size() > d.size()) return false;
    for (size_t i = 0; i + needle.size() <= d.size(); i++)
        if (memcmp(d.data() + i, needle.data(), needle.size()) == 0) return true;
    return false;
}

// ── FFmpeg decode helpers ───────────────────────────────────────────────────
struct MemCtx {
    const uint8_t* data;
    size_t size;
    size_t pos;
};
static int MemRead(void* opaque, uint8_t* buf, int sz) {
    MemCtx* c = (MemCtx*)opaque;
    if (c->pos >= c->size) return AVERROR_EOF;
    int n = (int)std::min<size_t>(sz, c->size - c->pos);
    memcpy(buf, c->data + c->pos, n);
    c->pos += n;
    return n;
}
static int64_t MemSeek(void* opaque, int64_t off, int whence) {
    MemCtx* c = (MemCtx*)opaque;
    if (whence == AVSEEK_SIZE) return (int64_t)c->size;
    if (whence == SEEK_SET) c->pos = (size_t)off;
    else if (whence == SEEK_CUR) c->pos += off;
    else if (whence == SEEK_END) c->pos = c->size + off;
    else return -1;
    return (int64_t)c->pos;
}

// Convert a decoded AVFrame to packed RGBA float (0..1) via RGB24 intermediate.
// Using RGB24 (not GBRPF32) avoids swscale float-path quirks on this build.
static bool FrameToRgbaFloat(const AVFrame* frame, std::vector<float>& rgbaOut,
                             int w, int h, std::string& why) {
    SwsContext* sws = sws_getContext(w, h, (AVPixelFormat)frame->format,
                                     w, h, AV_PIX_FMT_RGB24,
                                     SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) { why = "sws_getContext"; return false; }
    std::vector<uint8_t> rgb((size_t)w * h * 3);
    uint8_t* dst[4] = { rgb.data(), nullptr, nullptr, nullptr };
    int dstStride[4] = { w * 3, 0, 0, 0 };
    sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
    sws_freeContext(sws);
    rgbaOut.resize((size_t)w * h * 4);
    for (long i = 0; i < (long)w * h; i++) {
        rgbaOut[i*4+0] = rgb[i*3+0] / 255.f;
        rgbaOut[i*4+1] = rgb[i*3+1] / 255.f;
        rgbaOut[i*4+2] = rgb[i*3+2] / 255.f;
        rgbaOut[i*4+3] = 1.f;
    }
    return true;
}

// Decode one frame from a memory buffer. Returns AVFrame* (caller frees) or null.
// `streamHint` selects stream index if >=0; otherwise picks first video stream.
// Puts decoded frame into packed RGBA float (0..1) in rgbaOut, with w/h.
bool DecodeBufferToRgba(const std::vector<uint8_t>& buf, int streamIdx,
                        std::vector<float>& rgbaOut, int& w, int& h, std::string& why,
                        const char* fmtHint = nullptr, int* outColorTrc = nullptr) {
    rgbaOut.clear(); w = h = 0;
    if (buf.empty()) { why = "empty buffer"; return false; }
    MemCtx mc{buf.data(), buf.size(), 0};
    const int bufSize = 4096;
    uint8_t* avioBuf = (uint8_t*)av_malloc(bufSize);
    if (!avioBuf) { why = "av_malloc"; return false; }
    AVIOContext* avio = avio_alloc_context(avioBuf, bufSize, 0, &mc, MemRead, nullptr, MemSeek);
    if (!avio) { av_free(avioBuf); why = "avio"; return false; }
    AVFormatContext* fmt = avformat_alloc_context();
    fmt->pb = avio;
    fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    fmt->probesize = 1 << 20;
    fmt->max_analyze_duration = AV_TIME_BASE;
    // AVFMT_FLAG_CUSTOM_IO means avformat_close_input() must NOT be expected to
    // free pb/buffer; the context and its av_malloc'd buffer stay ours. Player-
    // Session calls this probe for every gainmap still, so the old short-lived
    // "leak is fine" assumption leaked a context + 4 KiB per open.
    auto releaseCustomIo = [&] {
        if (fmt) avformat_close_input(&fmt);
        if (avio) avio_context_free(&avio);
    };
    const AVInputFormat* fmtH = fmtHint ? av_find_input_format(fmtHint) : nullptr;
    int rc = avformat_open_input(&fmt, nullptr, fmtH, nullptr);
    if (rc < 0 && fmtH) {
        // forced demuxer rejected (e.g. wrong codec); retry auto.
        // Keep `avio` alive for the retry; free only the rejected format shell.
        if (fmt) { avformat_free_context(fmt); fmt = nullptr; }
        avio_seek(avio, 0, SEEK_SET); mc.pos = 0;
        fmt = avformat_alloc_context(); fmt->pb = avio;
        fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        fmt->probesize = 1 << 20;
        rc = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
    }
    if (rc < 0) {
        char b[80]; sprintf(b, "avformat_open_input failed rc=%d", rc);
        why = b;
        releaseCustomIo();
        return false;
    }
    // For small raw buffers, skip find_stream_info: it consumes the whole buffer
    // and re-reading after a seek double-feeds the demuxer. The hinted demuxer
    // already exposes a stream with codecpar populated at open time.
    avio_seek(avio, 0, SEEK_SET);
    int vstream = -1;
    if (streamIdx >= 0 && streamIdx < (int)fmt->nb_streams) vstream = streamIdx;
    else {
        for (unsigned i = 0; i < fmt->nb_streams; i++)
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vstream = i; break; }
    }
    if (vstream < 0) { why = "no video stream"; releaseCustomIo(); return false; }
    AVStream* st = fmt->streams[vstream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { why = "no decoder"; releaseCustomIo(); return false; }
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, st->codecpar);
    cc->pkt_timebase = st->time_base;
    if (avcodec_open2(cc, codec, nullptr) < 0) { why = "avcodec_open2"; avcodec_free_context(&cc); releaseCustomIo(); return false; }
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool got = false;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != vstream) { av_packet_unref(pkt); continue; }
        int sr = avcodec_send_packet(cc, pkt);
        int rr = avcodec_receive_frame(cc, frame);
        if (sr == 0 && rr == 0) { got = true; av_packet_unref(pkt); break; }
        av_packet_unref(pkt);
    }
    // Flush the decoder; loop until it drains.
    if (!got) {
        avcodec_send_packet(cc, nullptr);
        while (avcodec_receive_frame(cc, frame) == 0) { got = true; break; }
    }
    if (!got) { why = "no frame decoded"; av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc); releaseCustomIo(); return false; }
    w = frame->width; h = frame->height;
    if (outColorTrc) *outColorTrc = frame->color_trc;
    if (!FrameToRgbaFloat(frame, rgbaOut, w, h, why)) { av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc); releaseCustomIo(); return false; }
    av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc);
    releaseCustomIo();
    return true;
}

// Decode base image by opening the file directly.
bool DecodeFileBaseToRgba(const std::string& path, std::vector<float>& rgbaOut, int& w, int& h, std::string& why,
                          int* outColorTrc = nullptr) {
    rgbaOut.clear(); w = h = 0;
    AVFormatContext* fmt = avformat_alloc_context();
    if (avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) { why = "open_input"; return false; }
    if (avformat_find_stream_info(fmt, nullptr) < 0) { why = "find_stream_info"; avformat_close_input(&fmt); return false; }
    int vstream = -1;
    for (unsigned i = 0; i < fmt->nb_streams; i++)
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) { vstream = i; break; }
    if (vstream < 0) { why = "no video stream"; avformat_close_input(&fmt); return false; }
    AVStream* st = fmt->streams[vstream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) { why = "no decoder"; avformat_close_input(&fmt); return false; }
    AVCodecContext* cc = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(cc, st->codecpar);
    cc->pkt_timebase = st->time_base;
    if (avcodec_open2(cc, codec, nullptr) < 0) { why = "avcodec_open2"; avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    bool got = false;
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index != vstream) { av_packet_unref(pkt); continue; }
        int sr = avcodec_send_packet(cc, pkt);
        int rr = avcodec_receive_frame(cc, frame);
        if (sr == 0 && rr == 0) { got = true; av_packet_unref(pkt); break; }
        av_packet_unref(pkt);
    }
    if (!got) { avcodec_send_packet(cc, nullptr); if (avcodec_receive_frame(cc, frame) == 0) got = true; }
    if (!got) { why = "no frame"; av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    w = frame->width; h = frame->height;
    if (outColorTrc) *outColorTrc = frame->color_trc;
    if (!FrameToRgbaFloat(frame, rgbaOut, w, h, why)) { av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    return true;
}

// ── math ─────────────────────────────────────────────────────────────────────
float SrgbToLinear(float v) {
    if (v <= 0.04045f) return v / 12.92f;
    return powf((v + 0.055f) / 1.055f, 2.4f);
}
float Rec709Inverse(float v) {
    if (v < 0) v = 0; if (v > 1) v = 1;
    return v < 0.081f ? v / 4.5f : powf((v + 0.099f) / 1.099f, 1.f / 0.45f);
}
float Luma(float r, float g, float b) { return 0.2126f*r + 0.7152f*g + 0.0722f*b; }

// ISO CICP transfer characteristics that denote an already-HDR base:
//   16 = SMPTE ST 2084 (PQ), 18 = ARIB STD-B67 (HLG).
// We cannot lift such a base with sRGB-linear gain math, so the probe fails
// closed instead of emitting identical pixels. This single predicate backs
// BOTH the JPEG base gate and the BMFF base gate, and is exercised directly
// by GainMapProbe::RunSelftest() so the gate cannot silently regress.
inline bool IsHdrBaseRejected(int trc, GainMapProbeResult& R, const char* where) {
    if (trc != 16 && trc != 18) return false;
    R.verdict = "fail_closed_unsupported";
    R.reason = std::string(where) + " HDR base (PQ/HLG) gain map not implemented; cannot lift an already-HDR base";
    return true;
}

struct GainApplyParams {
    bool isAppleFamily = false;      // Apple 0-base formula
    bool isVendor128 = false;        // 128-base (uses ISO math with min=-h,max=+h)
    float headroomLinear = 1.f;      // for apple 0-base (display multiplier)
    float headroomLog2 = 0.f;        // apple alternate-side headroom (log2 stops)
    IsoMeta iso;                     // for iso / vendor128
};

// Plan §2.1 — single entry point for the HDR-base / apply-weight decision.
// Both the ISO-BMFF (tmap/HEIC/AVIF) path and the JPEG/WebP APP2 path call
// THIS function; the verdict never depends on flags bytes.
//   hB = base-side headroom (log2 stops), hA = alternate-side headroom (log2 stops).
//   target = player target headroom (log2 stops).
// Returns true  -> caller should apply; *weight is set in [0,1].
// Returns false -> base is already HDR (hB > hA); *weight=0, *reason filled and
//                  caller must display base as-is (verdict decoded_not_applied).
// On hA == hB the weight is forced to 1.0 and a note is appended.
bool DecideApplyWeight(double hB, double hA, double target,
                       double& weight, std::string& reason,
                       std::vector<std::string>* notes) {
    if (hB > hA) {
        weight = 0.0;
        char b[160];
        sprintf(b, "base HDR (baseHdrHeadroomLog2=%.6g > altHdrHeadroomLog2=%.6g); base displayed as-is, gain not applied",
                hB, hA);
        reason = b;
        return false;
    }
    if (hA == hB) {
        weight = 1.0;
        if (notes) notes->push_back("altHdrHeadroomLog2 == baseHdrHeadroomLog2; applyWeight forced to 1.0");
    } else {
        double w = (target - hB) / (hA - hB);
        if (w < 0.0) w = 0.0;
        if (w > 1.0) w = 1.0;
        weight = w;
    }
    return true;
}

// Plan §2.2 — weighted ISO-family apply (single/three channel, incl. vendor128).
//   g        = gainNormalized^(1/gamma)          (gamma>0 already validated)
//   log2Gain = gainMin[c] + (gainMax[c]-gainMin[c])*g   (log2 stops, signed)
//   mult     = pow2(weight * log2Gain)
//   out      = (base + weight*offSdr[c])*mult - weight*offHdr[c], clamped >= 0
// weight==0 ⇒ identity (returns base). gainMax is NEVER used as a raw multiplier.
float ApplyChannelIso(float gainNorm, int c, float baseLinear, const IsoMeta& m, float weight,
                      bool* pClamped = nullptr) {
    if (weight <= 0.f) return baseLinear;
    float gamma = m.gammaC[c];
    float g = (gamma != 1.f && gamma > 0.f) ? powf(gainNorm, 1.f / gamma) : gainNorm;
    float log2Gain = m.gainMin[c] * (1.f - g) + m.gainMax[c] * g;
    // Physical headroom cap: the gain map must not push a channel beyond the
    // declared alternate-side headroom. A malformed/inconsistent box can carry a
    // gainMax far larger than altHdrHeadroomLog2 (e.g. gainMax=11 vs hA=1.3);
    // trust the headroom endpoint, not the per-channel range, for the ceiling.
    if (log2Gain > m.altHdrHeadroomLog2) {
        log2Gain = m.altHdrHeadroomLog2;
        if (pClamped) *pClamped = true;
    }
    float mult = powf(2.f, weight * log2Gain);
    float out = (baseLinear + weight * m.offSdr[c]) * mult - weight * m.offHdr[c];
    if (out < 0.f) out = 0.f;
    return out;
}

bool HasDistinctChannels(const std::vector<float>& gm, int px) {
    int stride = std::max(1, px / 200000);
    for (int i = 0; i < px; i += stride) {
        long o = (long)i * 4;
        if (fabsf(gm[o] - gm[o+1]) > 1e-4f || fabsf(gm[o+1] - gm[o+2]) > 1e-4f) return true;
    }
    return false;
}

float P05Percentile(const std::vector<float>& gm, int px) {
    int stride = std::max(1, px / 1000000);
    int hist[256] = {0};
    long n = 0;
    for (int i = 0; i < px; i += stride) {
        long o = (long)i * 4;
        float l = Luma(gm[o], gm[o+1], gm[o+2]);
        int b = (int)roundf(fmaxf(0.f, fminf(1.f, l)) * 255.f);
        if (b > 255) b = 255;
        hist[b]++; n++;
    }
    long target = std::max(1L, (long)ceil(n * 0.005));
    long acc = 0;
    for (int b = 0; b < 256; b++) { acc += hist[b]; if (acc >= target) return b / 255.f; }
    return 1.f;
}

} // namespace

GainMapProbeResult GainMapProbe::Probe(const std::string& pathUtf8, bool baseOnly,
                                       double targetHeadroomLog2) {
    GainMapProbeResult R;
    R.verdict = "not_gainmap";
    R.form = "none";            // vocab: never emit an empty form
    R.targetHeadroomLog2 = targetHeadroomLog2;
    // read file
    FILE* f = fopen(pathUtf8.c_str(), "rb");
    if (!f) { R.verdict = "fail_closed_malformed"; R.reason = "cannot open file"; return R; }
    // A gainmap carrier is a still image, not a multi-gigabyte archive. The
    // previous long/ftell path silently wrapped to a negative size above 2 GiB
    // and then constructed an unbounded vector; fail closed before allocating.
    constexpr std::int64_t MaxProbeBytes = 256LL * 1024 * 1024;
    if (_fseeki64(f, 0, SEEK_END) != 0) {
        fclose(f);
        R.verdict = "fail_closed_malformed";
        R.reason = "cannot seek to end";
        return R;
    }
    const auto fileSize = _ftelli64(f);
    if (fileSize < 0) {
        fclose(f);
        R.verdict = "fail_closed_malformed";
        R.reason = "cannot determine file size";
        return R;
    }
    if (fileSize > MaxProbeBytes) {
        fclose(f);
        R.verdict = "fail_closed_malformed";
        R.reason = "file too large for gainmap probe";
        return R;
    }
    if (_fseeki64(f, 0, SEEK_SET) != 0) {
        fclose(f);
        R.verdict = "fail_closed_malformed";
        R.reason = "cannot rewind file";
        return R;
    }
    std::vector<uint8_t> d(static_cast<size_t>(fileSize));
    if (fileSize > 0 && fread(d.data(), 1, static_cast<size_t>(fileSize), f) != static_cast<size_t>(fileSize)) {
        fclose(f);
        R.verdict = "fail_closed_malformed";
        R.reason = "short read";
        return R;
    }
    fclose(f);
    if (fileSize < 16) { R.reason = "file too small"; return R; }
    // The rest of this parser predates the 64-bit guard and uses a signed long
    // size. It is safe here because the guard above caps the file at 256 MiB.
    const long sz = static_cast<long>(fileSize);

    // ── JPEG / WebP gainmap containers ──
    bool isJpeg = (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8);
    bool isWebp = (d.size() >= 12 && memcmp(d.data(), "RIFF", 4) == 0 && memcmp(d.data() + 8, "WEBP", 4) == 0);
    if (isJpeg || isWebp) {
        fp3::JpegGainMapHit jh;
        ProbeJpegGainMap(Utf8ToWide(pathUtf8), jh);
        if (!jh.found) {
            // Route by reason prefix (JPEG side emits these):
            //   BARE_GAINMAP_LAYER_NO_BASE:      -> unsupported, but it IS a gainmap layer (present=true)
            //   MALFORMED_GAINMAP:               -> malformed (e.g. min>max in APP2/XMP)
            //   APPLE_V1_NO_MAKERNOTE:           -> unsupported (cannot derive headroom)
            //   APPLE_V2/V3_NO_XMP_HEADROOM:     -> unsupported (no headroom metadata)
            //   JPEG_HDR_BASE_NO_GAINMAP:        -> recognized HDR JPEG base, no separable gainmap
            //   otherwise                        -> not_gainmap (plain SDR / unrelated)
            if (jh.reason.rfind("BARE_GAINMAP_LAYER_NO_BASE:", 0) == 0 ||
                jh.reason.rfind("UNSUPPORTED_WEBP_GAINMAP:", 0) == 0) {
                // A standalone gain-map layer (or a WebP carrying hdrgm metadata
                // we cannot decode) is still a gainmap-shaped file; we just
                // cannot reconstruct/apply it. Propagate the carrier fields the
                // parser already classified (form/family/metaSource).
                R.present = true;
                if (!jh.containerKind.empty()) R.form = jh.containerKind;
                if (!jh.family.empty()) R.metaFamily = jh.family;
                if (!jh.metaSource.empty()) R.metaSource = jh.metaSource;
                R.verdict = "fail_closed_unsupported";
                R.reason = jh.reason;
                return R;
            }
            if (jh.reason.rfind("MALFORMED_GAINMAP:", 0) == 0) {
                // Malformed but RECOGNIZED gain-map carrier: fill the carrier
                // fields (form/family/metaSource) so the verdict isn't reported
                // as an unrecognized file (present=false/form="").
                R.present = true;
                if (!jh.containerKind.empty()) R.form = jh.containerKind;
                if (!jh.family.empty()) R.metaFamily = jh.family;
                if (!jh.metaSource.empty()) R.metaSource = jh.metaSource;
                R.verdict = "fail_closed_malformed";
            } else if (jh.reason.rfind("APPLE_V1_NO_MAKERNOTE:", 0) == 0) {
                R.verdict = "fail_closed_unsupported";
            } else if (jh.reason.rfind("APPLE_V2/V3_NO_XMP_HEADROOM:", 0) == 0) {
                R.verdict = "fail_closed_unsupported";
            } else if (jh.reason.rfind("JPEG_HDR_BASE_NO_GAINMAP:", 0) == 0) {
                // Ultra-HDR JPEG that declares HDR headroom in XMP but carries no
                // separable MPF gain-map image. It IS an HDR gainmap-family JPEG
                // (present=true, form=jpeg_hdr); decode the single base frame and
                // report decoded_not_applied (there is no gain layer to apply).
                std::string why;
                std::vector<float> baseRgba; int baseW = 0, baseH = 0;
                if (!DecodeFileBaseToRgba(pathUtf8, baseRgba, baseW, baseH, why)) {
                    R.verdict = "fail_closed_malformed";
                    R.reason = "jpeg hdr base decode: " + why;
                    return R;
                }
                R.present = true;
                R.form = "jpeg_hdr";
                R.metaFamily = "iso";
                R.metaSource = "xmp";
                R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
                R.decodeCodec = "mjpeg";
                for (size_t i = 0; i < baseRgba.size(); i += 4) {
                    baseRgba[i]   = SrgbToLinear(baseRgba[i]);
                    baseRgba[i+1] = SrgbToLinear(baseRgba[i+1]);
                    baseRgba[i+2] = SrgbToLinear(baseRgba[i+2]);
                }
                R.baseRgba = baseRgba; R.outW = baseW; R.outH = baseH;
                R.appliedRgba = baseRgba;
                R.applyOk = false; R.applyMode = "none";
                R.verdict = "decoded_not_applied";
                R.reason = "jpeg HDR base rendition; no separable gain-map layer to apply";
                return R;
            } else {
                R.verdict = "not_gainmap";
            }
            R.reason = jh.reason.empty() ? "no gainmap" : jh.reason;
            return R;
        }
        R.present = true;
        R.form = jh.containerKind;
        R.metaFamily = jh.family;
        R.metaSource = jh.metaSource;
        // Decode gainmap to verify it's valid
        std::string why;
        std::vector<float> gmRgba; int gmW=0, gmH=0;
        if (!DecodeBufferToRgba(jh.gainBytes, -1, gmRgba, gmW, gmH, why)) {
            R.verdict = "fail_closed_malformed"; R.reason = "jpeg gain decode: " + why; return R;
        }
        // Decode base. Prefer the file path here: the custom-AVIO buffer route
        // could select the wrong jpeg_mpf member and hand the gain-map image to
        // the base decoder (observed as a green base under the DLL build).
        std::vector<float> baseRgba; int baseW=0, baseH=0;
        int baseTrc = 2;
        if (!DecodeFileBaseToRgba(pathUtf8, baseRgba, baseW, baseH, why, &baseTrc)) {
            if (!DecodeBufferToRgba(jh.baseBytes, -1, baseRgba, baseW, baseH, why, nullptr, &baseTrc)) {
                R.verdict = "fail_closed_malformed"; R.reason = "jpeg base decode: " + why; return R;
            }
        }
        R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
        R.decodeCodec = "mjpeg";
        // baseTrc fed back by DecodeBufferToRgba via outColorTrc; the PQ/HLG gate
        // is applied after the headroom decision (only refuse when we would lift).
        // Channels: APP2 flags bit7 (0x80) declares multichannel; else fall back
        // to pixel-distinct detection. The old code hardcoded 1 and fed the same
        // luma sample to all three channels, dropping per-channel gain.
        bool jpegMulti = ((jh.app2Flags & 0x80) != 0) || HasDistinctChannels(gmRgba, gmW * gmH);
        R.decodeChannels = jpegMulti ? 3 : 1; // 1 or 3

        // ── Headroom decision (plan §2.1/§3): hB/hA drive the verdict ──
        // We call the SAME DecideApplyWeight as the tmap/HEIC path. The §3
        // contract fields are owned by Impl-Jpeg in GainMapJpeg.h.
        bool isApple = (jh.family == "apple");
        if (jh.app2ReservedNonZero) {
            R.verdict = "fail_closed_unsupported";
            R.reason = "jpeg app2 reserved flags (0x3F) non-zero";
            return R;
        }
        float jB, jA, jhHrLog2;
        if (isApple) {
            // Apple v1/v2/v3: SDR base, alternate headroom = headroomLog2.
            jB = 0.f; jA = jh.headroomLog2; jhHrLog2 = jh.headroomLog2;
            if (!(jA > 0.f)) {
                R.verdict = "fail_closed_unsupported";
                R.reason = "jpeg apple gainmap without credible headroom (headroomLog2<=0)";
                return R;
            }
        } else {
            // ISO / Ultra HDR JPEG: hB/hA must come from HDRCapacity (APP2/XMP).
            if (!jh.hasHdrCapacity) {
                R.verdict = "fail_closed_unsupported";
                R.reason = "jpeg gainmap located but no HDRCapacity headroom declared";
                return R;
            }
            jB = jh.baseHdrHeadroomLog2;
            jA = jh.altHdrHeadroomLog2;
            jhHrLog2 = 0.f;
        }
        R.baseHdrHeadroomLog2 = jB;
        R.altHdrHeadroomLog2 = jA;
        // echo per-channel params into the result
        if (isApple) {
            R.gamma = 1.f;
        } else {
            for (int c = 0; c < 3; c++) {
                R.logBoostMin[c] = jh.logBoost[c*2];
                R.logBoostMax[c] = jh.logBoost[c*2+1];
                R.offsetSdr[c] = jh.offsetSdr[c];
                R.offsetHdr[c] = jh.offsetHdr[c];
            }
            R.gamma = jh.gamma;
        }
        R.headroomLinear = pow(2.0, (double)fmaxf(jB, jA));

        // sRGB-linearize the base once (used for both display and apply).
        std::vector<float> lin = baseRgba;
        {
            long px = (long)baseW * baseH * 4;
            for (long i = 0; i < px; i += 4) {
                lin[i]   = SrgbToLinear(lin[i]);
                lin[i+1] = SrgbToLinear(lin[i+1]);
                lin[i+2] = SrgbToLinear(lin[i+2]);
            }
        }

        std::string decideReason;
        double jw = 0.0;
        bool apply = DecideApplyWeight(jB, jA, targetHeadroomLog2, jw, decideReason, &R.notes);
        R.applyWeight = jw;
        // PQ/HLG base: fail closed only when we would actually lift it (apply=true).
        if (apply && IsHdrBaseRejected(baseTrc, R, "jpeg")) {
            R.decodeOk = true;
            return R;
        }
        R.baseRgba = lin; R.outW = baseW; R.outH = baseH;

        auto statsOn = [&](const std::vector<float>& px) {
            int n = baseW * baseH;
            double maxLin = 0; long over1 = 0;
            std::vector<float> lum;
            for (int i = 0; i < n; i++) {
                float l = Luma(px[i*4], px[i*4+1], px[i*4+2]);
                lum.push_back(l);
                if (l > maxLin) maxLin = l;
                if (l > 1.0) over1++;
            }
            std::sort(lum.begin(), lum.end());
            R.statsP50 = n > 0 ? lum[n/2] : 0;
            R.statsMaxLinear = maxLin;
            R.statsOver1Pct = n > 0 ? (double)over1 * 100.0 / (double)n : 0;
        };

        if (baseOnly) {
            // --base-only honored for the JPEG/WebP family too: emit the linear
            // SDR base and never claim applyOk. (Previously this flag was silently
            // ignored here and A/B dumps got the HDR rendition.)
            R.appliedRgba = lin;
            R.applyOk = false; R.applyMode = "none";
            R.verdict = "decoded_not_applied";
            R.reason = "base-only requested";
            statsOn(lin);
            return R;
        }

        if (!apply) {
            // Base is already HDR: display base as-is, no gain applied.
            R.appliedRgba = lin;
            R.applyOk = false; R.applyMode = "none";
            R.verdict = "decoded_not_applied";
            R.reason = decideReason;
            statsOn(lin);
            return R;
        }

        // Build an IsoMeta for the ISO-family weighted apply (dedup math).
        IsoMeta jm;
        jm.baseHdrHeadroomLog2 = jB;
        jm.altHdrHeadroomLog2 = jA;
        if (!isApple) {
            for (int c = 0; c < 3; c++) {
                jm.gainMin[c] = jh.logBoost[c*2];
                jm.gainMax[c] = jh.logBoost[c*2+1];
                jm.gammaC[c] = jh.gamma;
                jm.offSdr[c] = jh.offsetSdr[c];
                jm.offHdr[c] = jh.offsetHdr[c];
            }
        }
        float fw = (float)jw;
        // Apply gainmap pixels (weighted, plan §2.2)
        std::vector<float> out((long)baseW * baseH * 4, 0.f);
        bool jClampFired = false; int jClampCh = -1; float jClampGainMax = 0.f;
        for (int y = 0; y < baseH; y++) {
            float gy = (baseH <= 1) ? 0.f : (float)y * (float)(gmH - 1) / (float)(baseH - 1);
            int y0 = (int)gy, y1 = std::min(y0 + 1, gmH - 1);
            float fy = gy - y0;
            for (int x = 0; x < baseW; x++) {
                float gx = (baseW <= 1) ? 0.f : (float)x * (float)(gmW - 1) / (float)(baseW - 1);
                int x0 = (int)gx, x1 = std::min(x0 + 1, gmW - 1);
                float fx = gx - x0;
                long a = ((long)y0*gmW + x0)*4, b = ((long)y0*gmW + x1)*4;
                long c = ((long)y1*gmW + x0)*4, e = ((long)y1*gmW + x1)*4;
                float gv = Luma(gmRgba[a], gmRgba[a+1], gmRgba[a+2]) * (1-fx)*(1-fy)
                         + Luma(gmRgba[b], gmRgba[b+1], gmRgba[b+2]) * fx*(1-fy)
                         + Luma(gmRgba[c], gmRgba[c+1], gmRgba[c+2]) * (1-fx)*fy
                         + Luma(gmRgba[e], gmRgba[e+1], gmRgba[e+2]) * fx*fy;
                long o = ((long)y*baseW + x)*4;
                if (isApple) {
                    // Apple 0-base: out = base * (1 + (2^(w*headroomLog2)-1) * gApple)
                    float gl = Rec709Inverse(gv);
                    float linMult = powf(2.f, fw * jhHrLog2);
                    float scale = 1.f + (linMult - 1.f) * gl;
                    out[o]   = lin[o]   * scale;
                    out[o+1] = lin[o+1] * scale;
                    out[o+2] = lin[o+2] * scale;
                } else if (R.decodeChannels <= 1) {
                    // Mono gainmap: luma sample applied to all three channels.
                    for (int ch = 0; ch < 3; ch++) {
                        bool cl=false;
                        out[o+ch] = ApplyChannelIso(gv, ch, lin[o+ch], jm, fw, &cl);
                        if(cl && jm.gainMax[ch]>jClampGainMax){jClampFired=true;jClampCh=ch;jClampGainMax=jm.gainMax[ch];}
                    }
                } else {
                    // Per-channel gainmap: bilinear-sample each gain-map channel
                    // directly instead of reusing the luma sample for every channel.
                    for (int ch = 0; ch < 3; ch++) {
                        float gvc = gmRgba[a+ch]*(1-fx)*(1-fy)
                                  + gmRgba[b+ch]*fx*(1-fy)
                                  + gmRgba[c+ch]*(1-fx)*fy
                                  + gmRgba[e+ch]*fx*fy;
                        bool cl=false;
                        out[o+ch] = ApplyChannelIso(gvc, ch, lin[o+ch], jm, fw, &cl);
                        if(cl && jm.gainMax[ch]>jClampGainMax){jClampFired=true;jClampCh=ch;jClampGainMax=jm.gainMax[ch];}
                    }
                }
                out[o+3] = 1.f;
            }
        }
        if (jClampFired) { char cn[160]; sprintf(cn, "gain clamped to alternate headroom (gainMax=%.4f > hA=%.4f, ch=%d)", jClampGainMax, jA, jClampCh); R.notes.push_back(cn); }
        // The JPEG branch used to publish only appliedRgba; the toggle worker
        // then had no base picture and no output dimensions for BuildGainMap-
        // PresentationFrame. Keep both buffers and the geometry with the same
        // contract as the ISO/HEIC path.
        R.baseRgba = std::move(lin);
        R.outW = baseW;
        R.outH = baseH;
        R.applyOk = true; R.applyMode = "jpeg";
        R.verdict = "applied";
        statsOn(out);
        R.appliedRgba = std::move(out);
        return R;
    }
    // JPEG XL container: first box type "JXL " (signature box, followed by the
    // JXL codestream magic, ftyp, jxlc/jxll and optionally a jhgm box). We do NOT
    // parse JXL, so we cannot tell a gain-map container from a plain base: any
    // JXL container is reported as present=true/form=jhgm/fail_closed_unsupported
    // with a stable reason. family/metaSource stay within the fixed vocab
    // (family: iso|apple|vendor128|none; metaSource: tmap|makenote|xmp|default):
    // an unparsed JXL is family=none/metaSource=default.
    if (d.size() >= 12 && memcmp(d.data() + 4, "JXL ", 4) == 0) {
        R.present = true;
        R.form = "jhgm";
        R.metaFamily = "none";
        R.metaSource = "default";
        R.verdict = "fail_closed_unsupported";
        R.reason = "jxl_gainmap_not_implemented";
        return R;
    }

    // The probe does not link a crypto library; sha256 is intentionally empty.
    // Callers use it as a display field only, never as content identity.
    // parse ftyp
    {
        char type[5]; long bsize, bhdr;
        if (!ReadBox(d, 0, sz, type, bsize, bhdr) || std::string(type) != "ftyp") {
            R.reason = "not ISO-BMFF (no ftyp)"; return R;
        }
        R.majorBrand = std::string((const char*)d.data() + 8, 4);
        for (long p = 16; p + 4 <= bsize; p += 4)
            R.compatibleBrands.push_back(std::string((const char*)d.data() + p, 4));
        // JXL container (ISOBMFF-based JPEG XL): jhgm gain map is detected but
        // not parsed in this build. Keep fail_closed_unsupported with a stable
        // reason; do not claim "applied" and do not leave the reason empty.
        if (R.majorBrand == "JXL ") {
            R.present = true;
            R.form = "jhgm";
            R.metaFamily = "none";
            R.metaSource = "default";
            R.verdict = "fail_closed_unsupported";
            R.reason = "jxl_gainmap_not_implemented";
            return R;
        }
        // still brand heuristic
        static const char* stillBrands[] = {"heic","heix","heif","avif","avis","mif1","msf1","MiHE","hevc","hevx"};
        for (auto& b : R.compatibleBrands) for (auto s : stillBrands) if (b == s) R.isStill = true;
        if (R.majorBrand == "heic" || R.majorBrand == "avif" || R.majorBrand == "mif1") R.isStill = true;
    }

    ParsedContainer C;
    // walk top-level
    long metaOff = -1, metaSize = 0;
    for (long p = 0; p + 8 <= sz; ) {
        char type[5]; long bsize, bhdr;
        if (!ReadBox(d, p, sz, type, bsize, bhdr)) break;
        if (std::string(type) == "meta") { metaOff = p; metaSize = bsize; }
        p += bsize;
    }
    if (metaOff < 0) { R.reason = "no meta box"; return R; }
    long bodyStart = metaOff + 8 + 4, bodyEnd = metaOff + metaSize;
    bool itemTypeUnknown = false, ilocIncomplete = false;
    for (long p = bodyStart; p + 8 <= bodyEnd; ) {
        char type[5]; long bsize, bhdr;
        if (!ReadBox(d, p, bodyEnd, type, bsize, bhdr)) break;
        long cs = p + bhdr, ce = p + bsize;
        std::string t(type);
        if (t == "pitm") {
            int ver = d[cs]; long q = cs + 4;
            if (ver == 0) { if (q+2<=ce) C.primary = Be16(d.data()+q); }
            else { if (q+4<=ce) C.primary = (int)Be32(d.data()+q); }
        } else if (t == "iinf") ParseIinf(d, cs, ce, C.items, itemTypeUnknown);
        else if (t == "iloc") ParseIloc(d, cs, ce, C.locs, ilocIncomplete);
        else if (t == "iref") ParseIref(d, cs, ce, C.refs);
        else if (t == "grpl") ParseGrpl(d, cs, ce, C.groups);
        else if (t == "iprp") {
            // ipco/ipma are what we need here; auxC URNs are re-derived below in
            // the dedicated ipro scan, so no need to keep the out vector alive.
            std::vector<AuxCProp> ignoredAuxC;
            ParseIprp(d, cs, ce, C.ipco, C.ipma, ignoredAuxC);
        } else if (t == "idat") { C.idatStart = cs; C.idatEnd = ce; }
        p += bsize;
    }
    if (itemTypeUnknown) {
        R.verdict = "fail_closed_malformed";
        R.reason = "[IsoBmff] infe version <2 (no item_type)";
        return R;
    }
    // populate items list
    for (auto& kv : C.items) {
        GainMapItemInfo ii; ii.id = kv.first; ii.type = kv.second; ii.role = "unknown";
        if (kv.first == C.primary) ii.role = "base";
        R.items.push_back(ii);
    }

    // locate auxC props (re-parse ipco for auxC URN list)
    std::vector<AuxCProp> auxCProps;
    {
        // find iprp again
        for (long p = bodyStart; p + 8 <= bodyEnd; ) {
            char type[5]; long bsize, bhdr;
            if (!ReadBox(d, p, bodyEnd, type, bsize, bhdr)) break;
            if (std::string(type) == "iprp") {
                long cs = p + bhdr, ce = p + bsize;
                // ipco
                for (long q = cs; q + 8 <= ce; ) {
                    char t2[5]; long s2, h2;
                    if (!ReadBox(d, q, ce, t2, s2, h2)) break;
                    if (std::string(t2) == "ipco") {
                        long q2 = q + h2, e2 = q + s2;
                        long propIdx = 0;
                        while (q2 + 8 <= e2) {
                            char t3[5]; long s3, h3;
                            if (!ReadBox(d, q2, e2, t3, s3, h3)) break;
                            propIdx++;
                            if (std::string(t3) == "auxC") {
                                long s = q2 + h3 + 4, e3 = q2 + s3;
                                long z = s; while (z < e3 && d[z] != 0) z++;
                                if (z > s) {
                                    std::string urn((const char*)d.data()+s, (size_t)(z-s));
                                    int kind = ClassifyAuxUrn(urn);
                                    if (kind != 0) auxCProps.push_back({(int)propIdx, kind, urn});
                                }
                            }
                            q2 += s3;
                        }
                    }
                    q += s2;
                }
            }
            p += bsize;
        }
    }

    // Determine form
    int tmapId = -1;
    // Parse colr nclx for the BASE (primary) item only: detect PQ/HLG transfer.
    // Scanning every colr in ipco would pick up the gain-map's or an attached
    // ICC/prof colr and mis-reject an SDR base (e.g. *_srgb_icc.avif).
    int baseColorTrc = 2;
    auto pPrimary = C.ipma.find(C.primary);
    if (pPrimary != C.ipma.end()) {
        for (int ix : pPrimary->second) {
            if (ix < 1 || ix > (int)C.ipco.size() || C.ipco[ix-1].type != "colr") continue;
            long cs = C.ipco[ix-1].off;
            if (d[cs]=='n'&&d[cs+1]=='c'&&d[cs+2]=='l'&&d[cs+3]=='x' && C.ipco[ix-1].size >= 11) {
                baseColorTrc = (d[cs+6] << 8) | d[cs+7];
            }
        }
    }
    for (auto& kv : C.items) if (kv.second == "tmap") { tmapId = kv.first; break; }
    if (tmapId >= 0) { R.present = true; R.metaFamily = "iso"; R.metaSource = "tmap"; }

    // Helper: find item ids that own an auxC of given kind
    auto findAuxOwner = [&](int kind, int& owner) -> int {
        owner = -1; int owners = 0;
        for (auto& kv : C.ipma) {
            bool hit = false;
            for (int ix : kv.second)
                for (auto& ap : auxCProps) if (ap.kind == kind && ix == ap.propIndex) { hit = true; break; }
            if (!hit) continue;
            owners++; if (owner < 0) owner = kv.first;
        }
        return owners;
    };

    bool hasXmpAppleHeadroom = XmpContains(d, "ns.apple.com/HDRGainMap") && XmpContains(d, "HDRGainMapHeadroom");

    enum class Form { None, TmapSingle, TmapGrid, AuxApple, AuxVendor, AuxIso };
    Form form = Form::None;
    int gainmapItemId = -1;
    bool gainmapIsGrid = false;
    std::vector<std::vector<uint8_t>> gainmapStreams; // per-tile Annex-B/OBU
    std::vector<uint8_t> tmapPayload;
    std::vector<uint8_t> exifData;
    std::string codecType;
    int gridRows = 0, gridCols = 0, gridFlags = 0;
    int gridW = 0, gridH = 0, tileW = 0, tileH = 0;
    bool gridTail = false;
    int gridTailW = 0, gridTailH = 0;
    std::string containerWhy;
    bool containerWhyUnsupported = false;

    if (tmapId >= 0) {
        // libavif: without "tmap" brand in ftyp, ignore gainmap even if tmap item exists.
        bool hasTmapBrand = false;
        for (auto& b : R.compatibleBrands) if (b == "tmap") { hasTmapBrand = true; break; }
        if (!hasTmapBrand) {
            R.notes.push_back("tmap item present but no tmap brand in ftyp; ignoring gainmap");
            tmapId = -1;
        }
    }
    if (tmapId >= 0) {
        R.present = true;
        // dimg targets: collect coded/grid items referenced alongside tmap.
        // Two layouts exist: (a) tmap -> its gainmap image; (b) primary -> [tmap, gainmap].
        // Union both directions.
        std::vector<int> codecCands, gridCands;
        auto collectCandidates = [&](int refFrom, int excludeTo) {
            for (auto& rf : C.refs) {
                if (rf.type != "dimg" || rf.from != refFrom) continue;
                for (int to : rf.to) {
                    if (to == C.primary || to == excludeTo) continue;
                    auto it = C.items.find(to);
                    if (it == C.items.end()) continue;
                    if (IsCodecItem(it->second)) { if (std::find(codecCands.begin(), codecCands.end(), to) == codecCands.end()) codecCands.push_back(to); }
                    else if (it->second == "grid") { if (std::find(gridCands.begin(), gridCands.end(), to) == gridCands.end()) gridCands.push_back(to); }
                }
            }
        };
        collectCandidates(tmapId, -1);            // layout (a): tmap -> gainmap
        for (auto& rf : C.refs) {                  // layout (b): primary -> [tmap, gainmap]
            if (rf.type != "dimg") continue;
            bool pointsAtTmap = false; for (int to : rf.to) if (to == tmapId) pointsAtTmap = true;
            if (pointsAtTmap) collectCandidates(rf.from, tmapId);
        }
        // Layout (c): no dimg links tmap to its gainmap image. The gainmap image
        // is the grid/codec item that is neither primary nor the tmap itself.
        if (codecCands.empty() && gridCands.empty()) {
            for (auto& kv : C.items) {
                int id = kv.first; const std::string& t = kv.second;
                if (id == C.primary || id == tmapId) continue;
                if (t == "grid") { if (std::find(gridCands.begin(), gridCands.end(), id) == gridCands.end()) gridCands.push_back(id); }
            }
            // Only fall back to a single coded item if there is no grid.
            if (gridCands.empty()) {
                for (auto& kv : C.items) {
                    int id = kv.first; const std::string& t = kv.second;
                    if (id == C.primary || id == tmapId) continue;
                    if (IsCodecItem(t)) { if (std::find(codecCands.begin(), codecCands.end(), id) == codecCands.end()) codecCands.push_back(id); }
                }
            }
        }
        if (codecCands.size() == 1 && gridCands.empty()) {
            form = Form::TmapSingle;
            gainmapItemId = codecCands[0];
            codecType = C.items[gainmapItemId];
            std::string why;
            std::vector<uint8_t> raw;
            if (!SliceItem(d, C, gainmapItemId, raw, why)) { containerWhy = "tmap gainmap slice: " + why; }
            else {
                if (codecType == "av01") { gainmapStreams.push_back(std::move(raw)); }
                else {
                    long ho, hl; std::string hw;
                    if (!FindHvcC(C, gainmapItemId, ho, hl, hw)) containerWhy = "hvcC: " + hw;
                    else {
                        int ls; std::vector<std::vector<uint8_t>> ps; std::string pw;
                        if (!ParseHvcC(d, ho, hl, ls, ps, pw)) containerWhy = "hvcC parse: " + pw;
                        else {
                            std::vector<uint8_t> ann; std::string cw;
                            if (!ToAnnexB(raw, ls, ps, ann, cw)) containerWhy = "annexB: " + cw;
                            else gainmapStreams.push_back(std::move(ann));
                        }
                    }
                }
            }
        } else if (codecCands.empty() && gridCands.size() == 1) {
            form = Form::TmapGrid;
            gainmapIsGrid = true;
            gainmapItemId = gridCands[0];
        } else {
            char b[160]; sprintf(b, "tmap dimg candidates codec=%zu grid=%zu", codecCands.size(), gridCands.size());
            containerWhy = b;
        }
        std::string why;
        if (!SliceItem(d, C, tmapId, tmapPayload, why)) containerWhy = "tmap payload: " + why;
        else if (!tmapPayload.empty() && tmapPayload[0] != 0) {
            // Unsupported tmap version: ignore gainmap, display base as-is.
            // Per libavif / ISO: an unreadable gain-map version is treated as
            // "no gain map" -> verdict not_gainmap, meta reset to defaults.
            tmapPayload.clear();
            R.present = false;
            R.form = "none";
            R.metaFamily = "none";
            R.metaSource = "default";
        }
        else if (tmapPayload.size() >= 3 && tmapPayload[0] == 0) {
            // Check ISO minVersion (u16 at offset 1): if non-zero, gainmap is too new -> ignore.
            int isoMinVer = Be16(tmapPayload.data() + 1);
            if (isoMinVer != 0) {
                tmapPayload.clear();
                R.present = false;
                R.form = "none";
                R.metaFamily = "none";
                R.metaSource = "default";
            }
        }
    } else {
        // auxC paths
        int appleOwner = -1, vendorOwner = -1, isoOwner = -1;
        int nApple = findAuxOwner(2, appleOwner);
        int nVendor = findAuxOwner(4, vendorOwner);
        int nIso = findAuxOwner(1, isoOwner);
        if (nIso == 1) {
            form = Form::AuxIso; gainmapItemId = isoOwner;
            R.present = true;
        } else if (nApple == 1) {
            form = Form::AuxApple; gainmapItemId = appleOwner;
            R.present = true;
            R.metaFamily = "apple";
        } else if (nVendor == 1 && hasXmpAppleHeadroom) {
            form = Form::AuxVendor; gainmapItemId = vendorOwner;
            R.present = true;
            R.metaFamily = "vendor128";
        } else {
            // no gainmap
            if (nApple == 1) { R.auxCUrn = "hdrgainmap-but-no-auxl?"; }
            R.present = false;
        }
        if (R.present && gainmapItemId >= 0) {
            // auxl direction check
            bool auxlOk = false;
            for (auto& rf : C.refs) {
                if (rf.type != "auxl" || rf.from != gainmapItemId) continue;
                for (int to : rf.to) if (to == C.primary) { auxlOk = true; break; }
            }
            if (!auxlOk) { containerWhy = "no auxl aux->primary"; R.present = true; }
            // Exif item
            int exifId = -1;
            for (auto& kv : C.items) {
                for (auto& rf : C.refs) {
                    if (rf.type != "cdsc" || rf.from != kv.first) continue;
                    bool toPrimary = false; for (int t : rf.to) if (t == C.primary) toPrimary = true;
                    if (!toPrimary) continue;
                    if (kv.second == "Exif") exifId = kv.first;
                }
            }
            if (exifId >= 0) {
                std::string why;
                SliceItem(d, C, exifId, exifData, why);
            }
            // extract gainmap
            auto it = C.items.find(gainmapItemId);
            if (it != C.items.end()) {
                codecType = it->second;
                if (codecType == "grid") {
                    gainmapIsGrid = true;
                    R.form = "auxc_grid";
                } else if (IsCodecItem(codecType)) {
                    std::string why; std::vector<uint8_t> raw;
                    if (!SliceItem(d, C, gainmapItemId, raw, why)) containerWhy = "aux slice: " + why;
                    else {
                        if (codecType == "av01") { gainmapStreams.push_back(std::move(raw)); }
                        else {
                            long ho, hl; std::string hw;
                            if (!FindHvcC(C, gainmapItemId, ho, hl, hw)) containerWhy = "hvcC: " + hw;
                            else {
                                int ls; std::vector<std::vector<uint8_t>> ps; std::string pw;
                                if (!ParseHvcC(d, ho, hl, ls, ps, pw)) containerWhy = "hvcC parse: " + pw;
                                else {
                                    std::vector<uint8_t> ann; std::string cw;
                                    if (!ToAnnexB(raw, ls, ps, ann, cw)) containerWhy = "annexB: " + cw;
                                    else gainmapStreams.push_back(std::move(ann));
                                }
                            }
                        }
                    }
                } else {
                    containerWhy = "unsupported: aux item type not codec/grid: " + codecType;
                    containerWhyUnsupported = true;
                }
            }
            // set form string
            if (form == Form::AuxApple) R.form = "auxc_apple";
            else if (form == Form::AuxVendor) R.form = "auxc_vendor";
            else if (form == Form::AuxIso) R.form = "tmap"; // treat as iso aux
            // auxC URN
            for (auto& ap : auxCProps) {
                bool mine = false;
                auto itm = C.ipma.find(gainmapItemId);
                if (itm != C.ipma.end()) for (int ix : itm->second) if (ix == ap.propIndex) mine = true;
                if (mine) { R.auxCUrn = ap.urn; break; }
            }
        }
    }

    // If grid gainmap, parse grid payload + tiles
    if (R.present && gainmapIsGrid && gainmapItemId >= 0) {
        std::vector<uint8_t> gp; std::string why;
        if (!SliceItem(d, C, gainmapItemId, gp, why)) { containerWhy = "grid slice: " + why; }
        else {
            // Grid payload layout (corpus convention): version(1) + flags(1) +
            // rows_minus1(1) + cols_minus1(1) = 4 bytes minimum. A flags=0 grid
            // without tail w/h is exactly these 4 bytes; the old <6 gate rejected
            // it before rows/cols were ever read (reported 0/0).
            if (gp.size() < 4) containerWhy = "grid payload too short";
            else if (gp[0] != 0) containerWhy = "grid version != 0";
            else {
                gridFlags = gp[1];
                gridRows = gp[2] + 1; gridCols = gp[3] + 1;
                R.gridPresent = true; R.gridRows = gridRows; R.gridCols = gridCols; R.gridFlags = gridFlags;
                long q = 4;
                if (gridFlags & 1) {
                    if ((long)gp.size() < q + 8) { containerWhy = "grid flags=1 missing w/h"; }
                    else { gridW = Be32(gp.data()+q); gridH = Be32(gp.data()+q+4); }
                } else {
                    if ((long)gp.size() >= q + 4) {
                        gridTailW = Be16(gp.data()+q); gridTailH = Be16(gp.data()+q+2);
                        gridTail = (gridTailW > 0 && gridTailH > 0);
                    }
                    if (gridTail) { gridW = gridTailW; gridH = gridTailH; }
                    else {
                        containerWhy = "grid flags=0 but no tail w/h";
                    }
                }
                // tile ids from dimg of grid
                std::vector<int> tileIds;
                for (auto& rf : C.refs) {
                    if (rf.type != "dimg") continue;
                    bool pointsAtGrid = false; for (int to : rf.to) if (to == gainmapItemId) pointsAtGrid = true;
                    if (rf.from != gainmapItemId && !pointsAtGrid) continue;
                    for (int to : rf.to) {
                        if (to == gainmapItemId || to == C.primary) continue;
                        if (std::find(tileIds.begin(), tileIds.end(), to) == tileIds.end()) tileIds.push_back(to);
                    }
                }
                int expect = gridRows * gridCols;
                if ((int)tileIds.size() != expect) {
                    char b[120]; sprintf(b, "grid tiles=%zu expected=%d", tileIds.size(), expect);
                    containerWhy = b;
                } else {
                    // tile ispe
                    if (!FindIspe(C, d, tileIds[0], tileW, tileH)) containerWhy = "no tile ispe";
                    else {
                        // Validate canvas: tail w/h must fit within the tiled extent.
                        if (gridW <= 0 || gridH <= 0) {
                            gridW = gridCols*tileW; gridH = gridRows*tileH;
                        } else if (gridW > gridCols*tileW || gridH > gridRows*tileH) {
                            containerWhy = "grid tail w/h exceeds tiled extent";
                        }
                        if (containerWhy.empty()) {
                        R.gridPresent = true; R.gridRows = gridRows; R.gridCols = gridCols;
                        R.gridFlags = gridFlags; R.gridTailPresent = gridTail;
                        R.gridTailW = gridTailW; R.gridTailH = gridTailH;
                        // extract each tile
                        codecType = C.items[tileIds[0]];
                        bool okAll = true;
                        for (int tid : tileIds) {
                            std::vector<uint8_t> raw;
                            if (!SliceItem(d, C, tid, raw, why)) { containerWhy = "tile slice: " + why; okAll = false; break; }
                            if (codecType == "av01") { gainmapStreams.push_back(std::move(raw)); }
                            else {
                                long ho, hl; std::string hw;
                                if (!FindHvcC(C, tid, ho, hl, hw)) { containerWhy = "tile hvcC: " + hw; okAll = false; break; }
                                int ls; std::vector<std::vector<uint8_t>> ps; std::string pw;
                                if (!ParseHvcC(d, ho, hl, ls, ps, pw)) { containerWhy = "tile hvcC: " + pw; okAll = false; break; }
                                std::vector<uint8_t> ann; std::string cw;
                                if (!ToAnnexB(raw, ls, ps, ann, cw)) { containerWhy = "tile annexB: " + cw; okAll = false; break; }
                                gainmapStreams.push_back(std::move(ann));
                            }
                        }
                        if (okAll && form == Form::TmapGrid) R.form = "tmap_grid";
                        else if (okAll) R.form = "auxc_grid";
                        }
                    }
                }
            }
        }
    }

    // Only stamp a concrete form when we actually detected a gainmap. A rejected
    // tmap (bad version, present=false) must keep form="none" / meta reset.
    if (R.present) {
        if (form == Form::TmapSingle) R.form = "tmap";
        if (form == Form::AuxIso) { R.form = "tmap"; } // ISO aux treated as tmap-family
        // If the gainmap item is a grid (even if tile parsing failed), report the grid form.
        if (gainmapIsGrid && (R.form == "auxc_apple" || R.form == "auxc_vendor" || R.form == "tmap"))
            R.form = "auxc_grid";
    }

    // Not gainmap?
    if (!R.present) {
        R.verdict = "not_gainmap";
        R.form = "none";
        R.metaFamily = "none";
        R.metaSource = "default";
        return R;
    }

    // Malformed / unsupported container
    if (!containerWhy.empty()) {
        bool isUnsupported = containerWhyUnsupported ||
            containerWhy.rfind("unsupported:", 0) == 0;
        R.verdict = isUnsupported ? "fail_closed_unsupported" : "fail_closed_malformed";
        R.reason = "[container] " + containerWhy;
        // Even when the container is malformed, surface XMP metadata source if present.
        if (!isUnsupported && R.metaSource == "default" && XmpContains(d, "HDRGainMapHeadroom")) {
            R.metaSource = "xmp";
            if (R.metaFamily == "none") R.metaFamily = "vendor128";
        }
        if (!isUnsupported && R.metaFamily == "none") { R.metaSource = "default"; }
        return R;
    }

    // grpl/altr order check: if an altr group contains both primary and gainmap items,
    // the first entity MUST be the gainmap side. If primary is first → wrong order → ignore gainmap.
    if (gainmapItemId > 0 && R.present && !C.groups.empty()) {
        for (const auto& g : C.groups) {
            bool hasPrimary = false, hasGainSide = false;
            int primaryIdx = -1, gainSideIdx = -1;
            for (size_t i = 0; i < g.entities.size(); i++) {
                if (g.entities[i] == C.primary) { hasPrimary = true; primaryIdx = (int)i; }
                // Gain side = tmap item OR gainmap coded image
                if (g.entities[i] == gainmapItemId || g.entities[i] == tmapId) { hasGainSide = true; gainSideIdx = (int)i; }
            }
            if (hasPrimary && hasGainSide && primaryIdx >= 0 && gainSideIdx >= 0) {
                // Both in group: if primary comes before gain side → wrong order → not_gainmap
                if (primaryIdx < gainSideIdx) {
                    R.present = false;
                    R.form = "none";
                    R.metaFamily = "none";
                    R.metaSource = "default";
                    R.verdict = "not_gainmap";
                    R.reason = "altr group has primary before gainmap (wrong order); gainmap ignored";
                    return R;
                }
            }
        }
    }

    // Decode base
    std::vector<float> baseRgba;
    int baseW = 0, baseH = 0;
    bool baseIsGrid = (C.items.count(C.primary) && C.items[C.primary] == "grid");
    if (baseIsGrid) {
        // Primary item type is "grid": the item's own data IS the grid payload.
        std::string why;
        std::vector<uint8_t> gridPayload;
        if (!SliceItem(d, C, C.primary, gridPayload, why)) {
            R.verdict="fail_closed_malformed"; R.reason="base grid slice: " + why; return R;
        }
        int gRows=0, gCols=0, gW=0, gH=0, gFlags=0;
        if (gridPayload.size() < 6) { R.verdict="fail_closed_malformed"; R.reason="base grid payload too short"; return R; }
        // Grid item payload: version(1) + flags(1) + rows_minus1(1) + cols_minus1(1) + dims
        gFlags = gridPayload[1];
        gRows = gridPayload[2] + 1; gCols = gridPayload[3] + 1;
        long q = 4;
        if (gFlags & 1) {
            if (gridPayload.size() < 12) { R.verdict="fail_closed_malformed"; R.reason="base grid large: payload < 12 bytes"; return R; }
            gW = Be32(gridPayload.data()+q); gH = Be32(gridPayload.data()+q+4);
        } else {
            if (gridPayload.size() < 8) { R.verdict="fail_closed_malformed"; R.reason="base grid: payload < 8 bytes"; return R; }
            gW = Be16(gridPayload.data()+q); gH = Be16(gridPayload.data()+q+2);
        }
        // Find tile IDs via dimg from primary (only codec items, skip alpha/aux)
        std::vector<int> tileIds;
        for (auto& rf : C.refs) {
            if (rf.type != "dimg" || rf.from != C.primary) continue;
            for (int to : rf.to) {
                auto it = C.items.find(to);
                if (it == C.items.end()) continue;
                if (!IsCodecItem(it->second)) continue; // skip alpha/aux
                if (std::find(tileIds.begin(), tileIds.end(), to) == tileIds.end()) tileIds.push_back(to);
            }
        }
        int expect = gRows * gCols;
        if ((int)tileIds.size() < expect) {
            char b[120]; sprintf(b, "base grid tiles=%zu expected>=%d (codec-only)", tileIds.size(), expect);
            R.verdict="fail_closed_malformed"; R.reason=b; return R;
        }
        tileIds.resize(expect);
        // Validate all tile IDs exist as codec items
        for (int tid : tileIds) {
            if (!C.items.count(tid) || !IsCodecItem(C.items[tid])) {
                R.verdict="fail_closed_malformed"; R.reason="base grid: tile not codec"; return R;
            }
        }
        // Find tile dimensions from first tile's ispe
        int tileW=0, tileH=0;
        FindIspe(C, d, tileIds[0], tileW, tileH);
        if (tileW <= 0 || tileH <= 0) { R.verdict="fail_closed_malformed"; R.reason="base grid: no tile ispe"; return R; }
        if (gW <= 0 || gH <= 0) { gW = gCols*tileW; gH = gRows*tileH; }
        if (gW <= 0 || gH <= 0) { R.verdict="fail_closed_malformed"; R.reason="base grid: invalid dimensions"; return R; }
        // Decode and compose tiles
        std::string tileCodec = C.items[tileIds[0]];
        const char* tileHint = (tileCodec == "av01") ? "obu" : "hevc";
        std::vector<float> canvas((size_t)gW*gH*4, 0.f);
        int decTrc = 2;
        for (int ti = 0; ti < expect; ti++) {
            std::vector<uint8_t> tStream; std::string tWhy;
            if (!ExtractCodedStream(d, C, tileIds[ti], tStream, tWhy, nullptr)) {
                char b[120]; sprintf(b, "base tile %d extract: %s", ti, tWhy.c_str());
                R.verdict="fail_closed_malformed"; R.reason=b; return R;
            }
            if (tStream.empty() || tStream.size() < 4) {
                char b[120]; sprintf(b, "base tile %d: stream too small (%zu)", ti, tStream.size());
                R.verdict="fail_closed_malformed"; R.reason=b; return R;
            }
            std::vector<float> tile; int tw=0, th=0;
            if (!DecodeBufferToRgba(tStream, -1, tile, tw, th, tWhy, tileHint, &decTrc)) {
                char b[120]; sprintf(b, "base tile %d decode: %s", ti, tWhy.c_str());
                R.verdict="fail_closed_malformed"; R.reason=b; return R;
            }
            int ty = ti / gCols, tx = ti % gCols;
            int copyW = (tw < tileW) ? tw : tileW;
            int copyH = (th < tileH) ? th : tileH;
            for (int y = 0; y < copyH; y++) {
                int dy = ty*tileH + y; if (dy < 0 || dy >= gH) continue;
                for (int x = 0; x < copyW; x++) {
                    int dx = tx*tileW + x; if (dx < 0 || dx >= gW) continue;
                    long si = ((long)y*tw + x)*4;
                    long di = ((long)dy*gW + dx)*4;
                    if (si+3 >= (long)tile.size() || di+3 >= (long)canvas.size()) continue;
                    canvas[di] = tile[si]; canvas[di+1] = tile[si+1]; canvas[di+2] = tile[si+2]; canvas[di+3] = 1.f;
                }
            }
        }
        baseRgba = std::move(canvas); baseW = gW; baseH = gH;
        R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
        R.decodeCodec = tileCodec;
    } else {
        std::string why;
        std::vector<uint8_t> baseStream;
        std::string baseCodec;
        if (!ExtractCodedStream(d, C, C.primary, baseStream, why, &baseCodec)) {
            R.verdict = "fail_closed_malformed";
            R.reason = "base extract: " + why;
            return R;
        }
        const char* fmtHint = (baseCodec == "av01") ? "obu" : "hevc";
        int decTrc = 2;
        if (!DecodeBufferToRgba(baseStream, -1, baseRgba, baseW, baseH, why, fmtHint, &decTrc)) {
            R.verdict = "fail_closed_malformed";
            R.reason = "base decode: " + why;
            R.decodeOk = false;
            R.decodeReason = why;
            R.decodeCodec = (baseCodec == "av01") ? "av1" : "hevc";
            return R;
        }
        FindIspe(C, d, C.primary, baseW, baseH); // prefer declared ispe over decoded dims
    }
    R.baseRgba = baseRgba; R.outW = baseW; R.outH = baseH;
    // Gainmap was ignored (no brand / unsupported version / etc.) but base decoded:
    if (!R.present) {
        R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
        R.verdict = "not_gainmap"; R.reason = "gainmap not recognized for this file";
        return R;
    }

    // HDR-base (PQ/HLG) gate: we cannot LIFT an HDR base with sRGB-linear gain
    // math, so fail closed only when we would actually apply (hB<=hA). When the
    // gainmap itself declares the base already HDR (hB>hA → apply=false), we
    // report decoded_not_applied: the data is valid and we did no wrong math.
    // baseTrc is the BASE (primary) item's own colr nclx transfer only.
    int baseTrc = baseColorTrc;

    // Decode gainmap
    std::vector<float> gmRgba;
    int gmW = 0, gmH = 0;
    if (gainmapStreams.size() == 1) {
        std::string why;
        if (!DecodeBufferToRgba(gainmapStreams[0], -1, gmRgba, gmW, gmH, why)) {
            R.verdict = "fail_closed_malformed"; R.reason = "gainmap decode: " + why; return R;
        }
    } else if (gainmapStreams.size() > 1 && gainmapIsGrid) {
        // decode each tile and stitch
        int totalTiles = gridRows * gridCols;
        if (totalTiles <= 0 || (int)gainmapStreams.size() < totalTiles) {
            R.verdict = "fail_closed_malformed"; R.reason = "gainmap grid: tile count mismatch"; return R;
        }
        if (gridW <= 0 || gridH <= 0 || tileH <= 0) {
            R.verdict = "fail_closed_malformed"; R.reason = "gainmap grid: bad dimensions"; return R;
        }
        std::vector<float> canvas((size_t)gridW * gridH * 4, 0.f);
        int idx = 0;
        for (int ty = 0; ty < gridRows; ty++) for (int tx = 0; tx < gridCols; tx++) {
            if (idx >= (int)gainmapStreams.size()) break;
            if (gainmapStreams[idx].empty() || gainmapStreams[idx].size() < 4) {
                char b[120]; sprintf(b, "gm tile %d: stream too small (%zu)", idx, gainmapStreams[idx].size());
                R.verdict = "fail_closed_malformed"; R.reason = b; return R;
            }
            std::vector<float> tile; int tw=0, th=0; std::string why;
            if (!DecodeBufferToRgba(gainmapStreams[idx], -1, tile, tw, th, why)) {
                char b[120]; sprintf(b, "gm tile %d decode: %s", idx, why.c_str());
                R.verdict = "fail_closed_malformed"; R.reason = b; return R;
            }
            int copyW = (tw < tileW) ? tw : tileW;
            int copyH = (th < tileH) ? th : tileH;
            for (int y = 0; y < copyH; y++) {
                int dy = ty*tileH + y; if (dy < 0 || dy >= gridH) continue;
                for (int x = 0; x < copyW; x++) {
                    int dx = tx*tileW + x; if (dx < 0 || dx >= gridW) continue;
                    long si = ((long)y*tw + x)*4, di = ((long)dy*gridW + dx)*4;
                    if (si+3 >= (long)tile.size() || di+3 >= (long)canvas.size()) continue;
                    canvas[di] = tile[si]; canvas[di+1] = tile[si+1]; canvas[di+2] = tile[si+2]; canvas[di+3] = 1.f;
                }
            }
            idx++;
        }
        gmRgba = std::move(canvas); gmW = gridW; gmH = gridH;
    } else {
        R.verdict = "fail_closed_unsupported"; R.reason = "no gainmap stream"; return R;
    }
    R.decodeOk = true;
    R.decodeCodec = (codecType == "av01") ? "av1" : "hevc";
    R.decodeW = gmW; R.decodeH = gmH;
    R.decodeBitDepth = 8;

    // Determine channels
    int px = gmW * gmH;
    bool distinct = HasDistinctChannels(gmRgba, px);
    R.decodeChannels = distinct ? 3 : 1;

    // Metadata resolution order: tmap(ISO) -> makenote -> xmp
    GainApplyParams ap;
    bool metaOk = false;
    // 1) tmap payload ISO 21496
    if (!tmapPayload.empty() && tmapPayload.size() >= 2 && tmapPayload[0] == 0) {
        long consumed; IsoMeta m; std::string perr;
        IsoParse pr = ParseIso21496(tmapPayload.data(), 1, (long)tmapPayload.size() - 1, m, consumed, perr);
        if (pr != IsoParse::Ok) {
            R.verdict = (pr == IsoParse::Unsupported) ? "fail_closed_unsupported" : "fail_closed_malformed";
            R.reason = "tmap: " + perr;
            R.decodeOk = false; R.applyOk = false;
            return R;
        }
        {
            long used = consumed + 1;  // +1 for the version byte at tmapPayload[0]
            long trailing = (long)tmapPayload.size() - used;
            if (trailing < 0) trailing = 0;
            R.trailingBytesIgnored = (int)trailing;  // first-class field (sibling of verdict)
            if (trailing > 0) {
                char tn[96]; sprintf(tn, "trailingBytesIgnored=%ld", trailing);
                R.notes.push_back(tn);
                // A tmap that carries bytes beyond the canonical v0 layout uses a
                // writer extension this build does not implement. The canonical
                // part parsed fine (not malformed), but we cannot trust the full
                // output → fail_closed_unsupported (capability gap), distinct from
                // a clean tmap whose base/grid simply does not need applying.
                R.verdict = "fail_closed_unsupported";
                R.reason = "tmap writer extension beyond canonical v0 layout (" +
                            std::to_string(trailing) + " trailing bytes) not implemented";
                R.decodeOk = true;
                return R;
            }
        }
        // Consume the use_base_colour_space flag (0x40): the probe applies gain in
        // the base colour space, so surface it as a note rather than dropping it.
        if (m.useBaseColorSpace) R.notes.push_back("tmap useBaseColorSpace=1");
        ap.iso = m; ap.isAppleFamily = false;
        R.metaFamily = "iso"; R.metaSource = "tmap";
        for (int c = 0; c < 3; c++) {
            R.offsetSdr[c] = m.offSdr[c]; R.offsetHdr[c] = m.offHdr[c];
            R.logBoostMin[c] = m.gainMin[c]; R.logBoostMax[c] = m.gainMax[c];
        }
        R.gamma = m.gammaC[0];
        metaOk = true;
    }
    // 2) Apple MakerNote tag33/48
    if (!metaOk && !exifData.empty()) {
        float m33, m48; std::string why;
        if (AppleMakerNote(exifData, m33, m48, why)) {
            auto hr = gainmap_apple::ComputeHeadroomFromMakerNote(m33, m48, 0);
            if (hr.ok) {
                float hrLog2 = hr.headroomLog2;
                float headroomLin = hr.headroom;
                // decide 0-base vs 128-base by p0.5
                float p05 = P05Percentile(gmRgba, px);
                if (p05 * 255.f >= 16.f) {
                    ap.iso = IsoMeta{};
                    for (int c = 0; c < 3; c++) { ap.iso.gainMin[c] = -hrLog2; ap.iso.gainMax[c] = hrLog2; ap.iso.gammaC[c] = 1.f; }
                    ap.iso.channels = distinct ? 3 : 1;
                    ap.isVendor128 = true; ap.isAppleFamily = false;
                    R.metaFamily = "vendor128";
                } else {
                    ap.isAppleFamily = true; ap.headroomLinear = headroomLin; ap.headroomLog2 = hrLog2;
                    R.metaFamily = "apple";
                }
                R.metaSource = "makenote";
                R.gamma = 1.f;
                metaOk = true;
            } else {
                R.notes.push_back("makenote unusable: " + hr.why);
            }
        } else {
            R.notes.push_back("makenote parse: " + why);
        }
    }
    // 3) XMP Apple HDRGainMapHeadroom
    if (!metaOk) {
        float hr;
        if (XmpFindFloat(d, "HDRGainMap:HDRGainMapHeadroom", hr)) {
            float p05 = P05Percentile(gmRgba, px);
            if (p05 * 255.f >= 16.f) {
                ap.iso = IsoMeta{};
                for (int c = 0; c < 3; c++) { ap.iso.gainMin[c] = -hr; ap.iso.gainMax[c] = hr; ap.iso.gammaC[c] = 1.f; }
                ap.iso.channels = distinct ? 3 : 1;
                ap.isVendor128 = true; ap.isAppleFamily = false;
                R.metaFamily = "vendor128";
            } else {
                ap.isAppleFamily = true; ap.headroomLinear = powf(2.f, hr); ap.headroomLog2 = hr;
                R.metaFamily = "apple";
            }
            R.metaSource = "xmp";
            R.gamma = 1.f;
            metaOk = true;
        }
    }
    if (!metaOk) {
        // If we have a tmap form but metadata parsing failed → malformed (bad data),
        // not unsupported (missing feature).
        bool isTmapForm = (R.form == "tmap" || R.form == "tmap_grid");
        R.verdict = isTmapForm ? "fail_closed_malformed" : "fail_closed_unsupported";
        R.reason = "gain-map carrier recognized but no credible gain metadata (tmap/makenote/xmp all absent or invalid)";
        // Family follows the container URN only when we actually recognised a real
        // Apple gain-map carrier (grid tiles / usable headroom). A single auxc_apple
        // item with no usable metadata is a negative sample → leave family=none.
        bool realAppleCarrier = (form == Form::AuxApple) &&
                                (R.form == "auxc_grid");  // grid Apple gainmap present
        R.metaFamily = realAppleCarrier ? "apple" : "none";
        R.metaSource = "default";
        return R;
    }

    // Family label follows the container's auxC URN, not the pixel p0.5 heuristic:
    // an Apple auxC URN with usable headroom is always family=apple, even when the
    // gain pixels happen to look 128-base (which only changes apply math).
    if (form == Form::AuxApple) { R.metaFamily = "apple"; }

    // ── Plan §2.1: the ONLY HDR-base / weight decision, shared with JPEG path.
    // hB/hA in log2 stops. tmap carries them explicitly; apple/vendor128 are
    // SDR-base (hB=0) with alternate headroom = headroomLog2.
    double hB, hA;
    if (R.metaSource == "tmap") {
        hB = ap.iso.baseHdrHeadroomLog2;
        hA = ap.iso.altHdrHeadroomLog2;
    } else {
        hB = 0.0;
        hA = (double)ap.headroomLog2;
    }
    R.baseHdrHeadroomLog2 = hB;
    R.altHdrHeadroomLog2 = hA;
    R.headroomLinear = pow(2.0, (hB > hA ? hB : hA));
    std::string decideReason;
    double w = 0.0;
    bool apply = DecideApplyWeight(hB, hA, targetHeadroomLog2, w, decideReason, &R.notes);
    R.applyWeight = w;
    // Fail closed only when we would actually lift a PQ/HLG base (apply=true).
    // An HDR base the gainmap says is already HDR (apply=false) falls through to
    // decoded_not_applied below.
    if (apply && IsHdrBaseRejected(baseTrc, R, "[container]")) {
        R.decodeOk = true;
        return R;
    }
    if (!apply) {
        // Base is already HDR: display base as-is, no gain applied.
        for (size_t i = 0; i < baseRgba.size(); i += 4) {
            baseRgba[i]   = SrgbToLinear(baseRgba[i]);
            baseRgba[i+1] = SrgbToLinear(baseRgba[i+1]);
            baseRgba[i+2] = SrgbToLinear(baseRgba[i+2]);
        }
        R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
        R.appliedRgba = baseRgba; R.baseRgba = baseRgba;
        R.outW = baseW; R.outH = baseH;
        R.applyOk = false; R.applyMode = "none";
        R.verdict = "decoded_not_applied";
        R.reason = decideReason;
        {
            int n = baseW * baseH;
            std::vector<float> lum; lum.reserve(n);
            double maxLin = 0; long over1 = 0;
            for (int i = 0; i < n; i++) {
                long o = (long)i*4;
                float l = Luma(baseRgba[o], baseRgba[o+1], baseRgba[o+2]);
                lum.push_back(l);
                if (l > maxLin) maxLin = l;
                if (l > 1.0) over1++;
            }
            std::sort(lum.begin(), lum.end());
            R.statsP50 = lum.empty() ? 0.0 : (double)lum[n/2];
            R.statsMaxLinear = maxLin;
            R.statsOver1Pct = n > 0 ? (double)over1 * 100.0 / n : 0.0;
        }
        return R;
    }

    if (baseOnly) {
        // sRGB-linearize the base for a fair linear-space comparison.
        for (size_t i = 0; i < baseRgba.size(); i += 4) {
            baseRgba[i] = SrgbToLinear(baseRgba[i]);
            baseRgba[i+1] = SrgbToLinear(baseRgba[i+1]);
            baseRgba[i+2] = SrgbToLinear(baseRgba[i+2]);
        }
        R.applyOk = false; R.applyMode = "none";
        R.reason = "base-only requested";
        R.verdict = "decoded_not_applied";
        R.appliedRgba = baseRgba;
        R.baseRgba = baseRgba;
        R.outW = baseW; R.outH = baseH;
        // stats on base
        {
            int n = baseW * baseH;
            std::vector<float> lum; lum.reserve(n);
            double maxLin = 0; long over1 = 0;
            for (int i = 0; i < n; i++) {
                long o = (long)i*4;
                float l = Luma(baseRgba[o], baseRgba[o+1], baseRgba[o+2]);
                lum.push_back(l);
                if (l > maxLin) maxLin = l;
                if (l > 1.0) over1++;
            }
            std::sort(lum.begin(), lum.end());
            R.statsP50 = lum.empty() ? 0.0 : (double)lum[n/2];
            R.statsMaxLinear = maxLin;
            R.statsOver1Pct = n > 0 ? (double)over1 * 100.0 / n : 0.0;
        }
        return R;
    }

    // Apply: sRGB-linearize base
    float baseMax = 0;
    for (size_t i = 0; i < baseRgba.size(); i += 4) {
        baseRgba[i] = SrgbToLinear(baseRgba[i]);
        baseRgba[i+1] = SrgbToLinear(baseRgba[i+1]);
        baseRgba[i+2] = SrgbToLinear(baseRgba[i+2]);
        float m = fmaxf(baseRgba[i], fmaxf(baseRgba[i+1], baseRgba[i+2]));
        if (m > baseMax) baseMax = m;
    }
    // R.baseRgba must carry the LINEAR base, matching the not-apply and JPEG
    // paths. It is moved into the result after the apply loop, so the peak does
    // not hold a third full-size copy while `out` is being built.
    float fw = (float)w;
    std::vector<float> out((size_t)baseW * baseH * 4, 0.f);
    bool clampFired = false; int clampCh = -1; float clampGainMax = 0.f;

    for (int y = 0; y < baseH; y++) for (int x = 0; x < baseW; x++) {
        long o = ((long)y*baseW + x) * 4;
        // bilinear sample gainmap (coords normalized by base size)
        float gx = ((float)x + 0.5f) * gmW / baseW - 0.5f;
        float gy = ((float)y + 0.5f) * gmH / baseH - 0.5f;
        gx = fmaxf(0.f, fminf((float)(gmW - 1), gx));
        gy = fmaxf(0.f, fminf((float)(gmH - 1), gy));
        int x0 = (int)floorf(gx), y0 = (int)floorf(gy);
        int x1 = std::min(x0 + 1, gmW - 1), y1 = std::min(y0 + 1, gmH - 1);
        float fx = gx - x0, fy = gy - y0;
        auto sampleGray = [&]() -> float {
            long a = ((long)y0*gmW + x0)*4, b = ((long)y0*gmW + x1)*4;
            long c = ((long)y1*gmW + x0)*4, e = ((long)y1*gmW + x1)*4;
            float v00 = Luma(gmRgba[a], gmRgba[a+1], gmRgba[a+2]);
            float v01 = Luma(gmRgba[b], gmRgba[b+1], gmRgba[b+2]);
            float v10 = Luma(gmRgba[c], gmRgba[c+1], gmRgba[c+2]);
            float v11 = Luma(gmRgba[e], gmRgba[e+1], gmRgba[e+2]);
            return v00*(1-fx)*(1-fy) + v01*fx*(1-fy) + v10*(1-fx)*fy + v11*fx*fy;
        };
        auto sampleCh = [&](int ch) -> float {
            long a = ((long)y0*gmW + x0)*4, b = ((long)y0*gmW + x1)*4;
            long c = ((long)y1*gmW + x0)*4, e = ((long)y1*gmW + x1)*4;
            float v00 = gmRgba[a+ch], v01 = gmRgba[b+ch], v10 = gmRgba[c+ch], v11 = gmRgba[e+ch];
            return v00*(1-fx)*(1-fy) + v01*fx*(1-fy) + v10*(1-fx)*fy + v11*fx*fy;
        };
        if (ap.isAppleFamily) {
            float gv = sampleGray();
            float gl = Rec709Inverse(gv);
            // Plan §2.2: out = base * (1 + (2^(w*headroomLog2)-1) * gApple)
            float linMult = powf(2.f, fw * ap.headroomLog2);
            float scale = 1.f + (linMult - 1.f) * gl;
            out[o] = baseRgba[o] * scale;
            out[o+1] = baseRgba[o+1] * scale;
            out[o+2] = baseRgba[o+2] * scale;
        } else {
            // ISO family (incl. vendor128) — weight applied inside ApplyChannelIso
            if (R.decodeChannels <= 1) {
                float gv = sampleGray();
                for (int ch = 0; ch < 3; ch++) { bool cl=false; out[o+ch] = ApplyChannelIso(gv, ch, baseRgba[o+ch], ap.iso, fw, &cl); if(cl && ap.iso.gainMax[ch]>clampGainMax){clampFired=true;clampCh=ch;clampGainMax=ap.iso.gainMax[ch];} }
            } else {
                for (int ch = 0; ch < 3; ch++) { bool cl=false; out[o+ch] = ApplyChannelIso(sampleCh(ch), ch, baseRgba[o+ch], ap.iso, fw, &cl); if(cl && ap.iso.gainMax[ch]>clampGainMax){clampFired=true;clampCh=ch;clampGainMax=ap.iso.gainMax[ch];} }
            }
        }
        out[o+3] = 1.f;
    }
    if (clampFired) { char cn[160]; sprintf(cn, "gain clamped to alternate headroom (gainMax=%.4f > hA=%.4f, ch=%d)", clampGainMax, hA, clampCh); R.notes.push_back(cn); }

    R.applyOk = true;
    R.applyMode = "rgb_f32";

    // stats
    {
        int n = baseW * baseH;
        std::vector<float> lum; lum.reserve(n);
        double maxLin = 0; long over1 = 0;
        for (int i = 0; i < n; i++) {
            long o = (long)i*4;
            float l = Luma(out[o], out[o+1], out[o+2]);
            lum.push_back(l);
            if (l > maxLin) maxLin = l;
            if (l > 1.0) over1++;
        }
        std::sort(lum.begin(), lum.end());
        R.statsP50 = lum.empty() ? 0.0 : (double)lum[n/2];
        R.statsMaxLinear = maxLin;
        R.statsOver1Pct = n > 0 ? (100.0 * over1 / n) : 0.0;
    }
    // Publish both buffers without a second full-size copy. The values are
    // already in their final form; only ownership moves.
    R.baseRgba = std::move(baseRgba);
    R.appliedRgba = std::move(out);
    R.verdict = "applied";
    return R;
}

// ── Self-test: locked-in regression assertions (declared in GainMapProbe.h) ──
//
// These exercise internal helpers that cannot be reached end-to-end through
// Probe() without real compressed HEVC/AV1 media (which --selftest must not
// depend on). The CLI driver (tools/gainmap_probe.cpp --selftest) adds the one
// check that IS reachable on a hand-built in-memory file: the JXL container
// gate. Returns the number of failed assertions (0 == all passed).
int GainMapProbe::RunSelftest() {
    int failures = 0, checks = 0;
    auto ok = [&](bool cond, const char* name, const char* detail) {
        checks++;
        if (cond) printf("[PASS] %s\n", name);
        else      { failures++; printf("[FAIL] %s  (%s)\n", name, detail); }
    };

    // ---- (A) ISO 21496-1 tmap reader tolerates trailing bytes ----------------
    // Build a canonical 1-channel v0 tmap payload, then append 4 trailing pad
    // bytes. A compliant reader parses only the `consumed` canonical layout and
    // IGNORES the trailing bytes (ISO 21496-1); it must NOT reject the file.
    {
        std::vector<uint8_t> b;
        auto u8   = [&](uint8_t v){ b.push_back(v); };
        auto be16 = [&](uint16_t v){ b.push_back((uint8_t)(v>>8)); b.push_back((uint8_t)(v&0xff)); };
        auto rat  = [&](int32_t num, uint32_t den){
            for (int s = 24; s >= 0; s -= 8) b.push_back((uint8_t)((num >> s) & 0xff));
            for (int s = 24; s >= 0; s -= 8) b.push_back((uint8_t)((den >> s) & 0xff));
        };
        u8(0x00);                 // version byte at [0] (Probe requires tmapPayload[0]==0)
        be16(0);                  // minVersion
        be16(0);                  // writerVersion
        u8(0x00);                 // flags
        rat(0,1);                 // baseHdrHeadroomLog2 = 0.0
        rat(3,1);                 // altHdrHeadroomLog2  = 3.0
        rat(0,1);                 // gainMin[0]
        rat(3,1);                 // gainMax[0]
        rat(1,1);                 // gamma[0]
        rat(0,1);                 // offSdr[0]
        rat(0,1);                 // offHdr[0]
        u8(0xDE); u8(0xAD); u8(0xBE); u8(0xEF);   // 4 trailing pad bytes

        GainMapProbeResult fresh;
        ok(fresh.trailingBytesIgnored == 0,
           "A0.trailingBytesIgnored_default_zero",
           "first-class field must default to 0");

        IsoMeta m; long consumed = 0; std::string err;
        IsoParse pr = ParseIso21496(b.data(), 1, (long)b.size()-1, m, consumed, err);
        char d1[192];
        sprintf(d1, "pr=%d err=\"%s\" consumed=%ld size=%zu", (int)pr, err.c_str(), consumed, b.size());
        ok(pr == IsoParse::Ok, "A1.tmap_trailing4_parses_ok", d1);

        long used = consumed + 1;                 // +1 for leading version byte
        long trailing = (long)b.size() - used;
        ok(trailing == 4, "A2.trailingBytes_count_is_4", d1);

        GainMapProbeResult R;
        R.trailingBytesIgnored = (int)(trailing > 0 ? trailing : 0);
        ok(R.trailingBytesIgnored == 4, "A3.trailingBytesIgnored_field_eq_4", d1);
    }

    // ---- (B) PQ/HLG base transfer gate (shared by JPEG + BMFF paths) ---------
    {
        GainMapProbeResult r16; bool rej16 = IsHdrBaseRejected(16, r16, "test");
        ok(rej16 && r16.verdict == "fail_closed_unsupported",
           "B1.trc16_PQ_rejected_fail_closed", "PQ base must fail closed");
        GainMapProbeResult r18; bool rej18 = IsHdrBaseRejected(18, r18, "test");
        ok(rej18 && r18.verdict == "fail_closed_unsupported",
           "B2.trc18_HLG_rejected_fail_closed", "HLG base must fail closed");
        GainMapProbeResult r2;  bool rej2  = IsHdrBaseRejected(2,  r2,  "test");
        ok(!rej2 && r2.verdict.empty(),
           "B3.trc2_srgb_NOT_rejected", "sRGB base must pass the gate");
        GainMapProbeResult r13; bool rej13 = IsHdrBaseRejected(13, r13, "test");
        ok(!rej13, "B4.trc13_sRGBish_NOT_rejected", "sRGB-ish trc must pass the gate");
    }

    // ---- (D) baseRgba linearize-then-assign contract ------------------------
    // The BMFF success path linearizes the base IN PLACE and only then assigns
    // R.baseRgba = baseRgba. Previously R.baseRgba kept the encoded sRGB values
    // (double-gamma). Mirror that exact loop + assign here and assert the result
    // carries LINEAR values; also assert linear != encoded so the check can't
    // pass vacuously.
    {
        const float enc0 = 0.5f, enc1 = 0.25f;
        std::vector<float> baseRgba = { enc0, enc0, enc0, 1.f,
                                        enc1, enc1, enc1, 1.f };
        for (size_t i = 0; i < baseRgba.size(); i += 4) {
            baseRgba[i]   = SrgbToLinear(baseRgba[i]);
            baseRgba[i+1] = SrgbToLinear(baseRgba[i+1]);
            baseRgba[i+2] = SrgbToLinear(baseRgba[i+2]);
        }
        GainMapProbeResult R;
        R.baseRgba = baseRgba;                 // mirrors R.baseRgba = baseRgba;
        R.outW = 2; R.outH = 1;
        float expect0 = SrgbToLinear(enc0), expect1 = SrgbToLinear(enc1);
        ok(R.baseRgba.size() == 8 &&
           R.baseRgba[0] == expect0 && R.baseRgba[4] == expect1,
           "D1.baseRgba_carries_linear_values",
           "R.baseRgba must be sRGB-linearized after the apply-path loop");
        ok(expect0 != enc0 && expect1 != enc1,
           "D2.linear_differs_from_encoded",
           "sanity: SrgbToLinear must change the value (else D1 is vacuous)");
    }

    printf("selftest: checks=%d failures=%d\n", checks, failures);
    return failures;
}
