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

void ParseGrpl(const std::vector<uint8_t>& d, long start, long end,
               std::vector<GrpGroup>& groups) {
    if (start + 12 > end) return;
    // FullBox: version(1) + flags(3) = 4 bytes, then group_id u32, num_entities u32, entities u32[]
    long p = start + 4; // skip FullBox header
    while (p + 8 <= end) {
        GrpGroup g;
        g.groupId = Be32(d.data() + p); p += 4;
        uint32_t n = Be32(d.data() + p); p += 4;
        for (uint32_t i = 0; i < n && p + 4 <= end; i++) {
            g.entities.push_back((int)Be32(d.data() + p)); p += 4;
        }
        groups.push_back(g);
        // Next group starts here (grpl contains multiple group boxes concatenated)
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
    uint8_t rawFlags = 0; // tmap gain-map flags byte: 0xc0=SDR base(apply), 0x80=HDR base
    bool useBaseColorSpace = false;
    float hdrCapacityMin = 0.f, hdrCapacityMax = 0.f;
    float gainMin[3] = {0,0,0}, gainMax[3] = {0,0,0};
    float gammaC[3] = {1,1,1};
    float offSdr[3] = {0,0,0}, offHdr[3] = {0,0,0};
};

bool ParseIso21496(const uint8_t* d, long off, long len, IsoMeta& m, long& consumed) {
    consumed = 0;
    long end = off + len, p = off;
    if (p + 5 > end) return false;
    int minVersion = (Be16(d + p)); p += 2;
    if (minVersion != 0) return false;
    p += 2; // writer_version
    uint8_t flags = d[p++];
    m.rawFlags = flags;
    int channels = 1;
    bool commonDenom = (flags & 0x08) != 0;
    m.channels = channels;
    auto rd = [](int n, int dd) -> float { return dd == 0 ? 0.f : (float)((double)n / dd); };
    if (commonDenom) {
        if (p + 12 + channels * 20 > end) return false;
        int dn = (int)Be32(d + p); p += 4;
        int bhn = (int)Be32(d + p); p += 4;
        int ahn = (int)Be32(d + p); p += 4;
        m.hdrCapacityMin = rd(bhn, dn);
        m.hdrCapacityMax = rd(ahn, dn);
        for (int c = 0; c < channels; c++) {
            int mn = (int)Be32(d + p); p += 4;
            int xn = (int)Be32(d + p); p += 4;
            int gn = (int)Be32(d + p); p += 4;
            int sn = (int)Be32(d + p); p += 4;
            int tn = (int)Be32(d + p); p += 4;
            m.gainMin[c] = rd(mn, dn);
            m.gainMax[c] = rd(xn, dn);
            float g = rd(gn, dn); m.gammaC[c] = g; // gamma<=0 checked after loop
            m.offSdr[c] = rd(sn, dn);
            m.offHdr[c] = rd(tn, dn);
        }
    } else {
        if (p + 16 + channels * 40 > end) return false;
        int bhn = (int)Be32(d + p), bhd = (int)Be32(d + p + 4); p += 8;
        int ahn = (int)Be32(d + p), ahd = (int)Be32(d + p + 4); p += 8;
        m.hdrCapacityMax = rd(ahn, ahd);
        m.hdrCapacityMin = rd(bhn, bhd);
        for (int c = 0; c < channels; c++) {
            int mn = (int)Be32(d + p), md = (int)Be32(d + p + 4); p += 8;
            int xn = (int)Be32(d + p), xd = (int)Be32(d + p + 4); p += 8;
            int gn = (int)Be32(d + p), gd = (int)Be32(d + p + 4); p += 8;
            int sn = (int)Be32(d + p), sd = (int)Be32(d + p + 4); p += 8;
            int tn = (int)Be32(d + p), td = (int)Be32(d + p + 4); p += 8;
            m.gainMin[c] = rd(mn, md);
            m.gainMax[c] = rd(xn, xd);
            float g = rd(gn, gd); m.gammaC[c] = g; // gamma<=0 checked after loop
            m.offSdr[c] = rd(sn, sd);
            m.offHdr[c] = rd(tn, td);
        }
    }
    if (channels == 1) {
        for (int c = 1; c < 3; c++) {
            m.gainMin[c] = m.gainMin[0]; m.gainMax[c] = m.gainMax[0];
            m.gammaC[c] = m.gammaC[0]; m.offSdr[c] = m.offSdr[0]; m.offHdr[c] = m.offHdr[0];
        }
    }
    for (int c = 0; c < 3; c++) if (m.gammaC[c] <= 0) return false; // gamma=0 invalid metadata
    consumed = p - off;
    return true;
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

// Build Apple metadata from tag33/48. Returns false when headroom<=1 (no HDR).
bool BuildAppleMeta(float m33, float m48, float& headroomLog2, std::string& why) {
    if (m33 <= 0.f && m48 <= 0.f) { why = "tag33/48 both zero"; return false; }
    float stops = m33 < 1.0f
        ? (m48 <= 0.01f ? -20.f * m48 + 1.8f : -0.101f * m48 + 1.601f)
        : (m48 <= 0.01f ? -70.f * m48 + 3.0f : -0.303f * m48 + 2.303f);
    float headroom = powf(2.f, fmaxf(stops, 0.f));
    if (headroom <= 1.f) { why = "headroom<=1"; return false; }
    headroomLog2 = log2f(headroom);
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
    const AVInputFormat* fmtH = fmtHint ? av_find_input_format(fmtHint) : nullptr;
    int rc = avformat_open_input(&fmt, nullptr, fmtH, nullptr);
    if (rc < 0 && fmtH) {
        // forced demuxer rejected (e.g. wrong codec); retry auto.
        avio_seek(avio, 0, SEEK_SET); mc.pos = 0;
        fmt = avformat_alloc_context(); fmt->pb = avio;
        fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
        fmt->probesize = 1 << 20;
        rc = avformat_open_input(&fmt, nullptr, nullptr, nullptr);
    }
    if (rc < 0) {
        char b[80]; sprintf(b, "avformat_open_input failed rc=%d", rc);
        why = b;
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
    // Flush the decoder; loop until it drains.
    if (!got) {
        avcodec_send_packet(cc, nullptr);
        while (avcodec_receive_frame(cc, frame) == 0) { got = true; break; }
    }
    if (!got) { why = "no frame decoded"; av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    w = frame->width; h = frame->height;
    if (outColorTrc) *outColorTrc = frame->color_trc;
    if (!FrameToRgbaFloat(frame, rgbaOut, w, h, why)) { av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc); avformat_close_input(&fmt); return false; }
    av_frame_free(&frame); av_packet_free(&pkt); avcodec_free_context(&cc);
    avformat_close_input(&fmt);
    // Custom-IO avio cleanup: this FFmpeg build's close_input may already have
    // torn down pb; double-freeing here corrupts the heap. Leak is fine in a
    // short-lived probe (process exits immediately after).
    return true;
}

// Decode base image by opening the file directly.
bool DecodeFileBaseToRgba(const std::string& path, std::vector<float>& rgbaOut, int& w, int& h, std::string& why) {
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

struct GainApplyParams {
    bool isAppleFamily = false;      // Apple 0-base formula
    bool isVendor128 = false;        // 128-base (uses ISO math with min=-h,max=+h)
    float headroomLinear = 1.f;      // for apple 0-base
    IsoMeta iso;                     // for iso / vendor128
};

float ApplyChannelIso(float gainNorm, int c, float baseLinear, const IsoMeta& m) {
    float gamma = m.gammaC[c];
    float g = (gamma != 1.f && gamma > 0.f) ? powf(gainNorm, 1.f / gamma) : gainNorm;
    float logBoost = m.gainMin[c] * (1.f - g) + m.gainMax[c] * g;
    return (baseLinear + m.offSdr[c]) * powf(2.f, logBoost) - m.offHdr[c];
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

GainMapProbeResult GainMapProbe::Probe(const std::string& pathUtf8, bool baseOnly) {
    GainMapProbeResult R;
    R.verdict = "not_gainmap";
    // read file
    FILE* f = fopen(pathUtf8.c_str(), "rb");
    if (!f) { R.verdict = "fail_closed_malformed"; R.reason = "cannot open file"; return R; }
    fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> d(sz);
    if (sz > 0 && fread(d.data(), 1, sz, f) != (size_t)sz) { fclose(f); R.verdict = "fail_closed_malformed"; R.reason = "short read"; return R; }
    fclose(f);
    if (sz < 16) { R.reason = "file too small"; return R; }

    // ── JPEG / WebP gainmap containers ──
    bool isJpeg = (d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8);
    bool isWebp = (d.size() >= 12 && memcmp(d.data(), "RIFF", 4) == 0 && memcmp(d.data() + 8, "WEBP", 4) == 0);
    if (isJpeg || isWebp) {
        fp3::JpegGainMapHit jh;
        ProbeJpegGainMap(std::wstring(pathUtf8.begin(), pathUtf8.end()), jh);
        if (!jh.found) {
            // Route by reason prefix: bare gain layer → unsupported, plain SDR → not_gainmap
            if (jh.reason.rfind("BARE_GAINMAP_LAYER_NO_BASE:", 0) == 0) {
                R.verdict = "fail_closed_unsupported";
                R.reason = jh.reason;
            } else {
                R.verdict = "not_gainmap";
                R.reason = jh.reason.empty() ? "no gainmap" : jh.reason;
            }
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
        // Decode base
        std::vector<float> baseRgba; int baseW=0, baseH=0;
        int baseTrc = 2;
        if (!DecodeBufferToRgba(jh.baseBytes, -1, baseRgba, baseW, baseH, why, nullptr, &baseTrc)) {
            R.verdict = "fail_closed_malformed"; R.reason = "jpeg base decode: " + why; return R;
        }
        R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
        R.decodeCodec = "mjpeg";
        R.baseRgba = baseRgba; R.outW = baseW; R.outH = baseH;
        // HDR base check: from APP2 flags (bit6=0x40 = useBaseColorSpace)
        // If flags present and 0x40 NOT set → HDR base → decoded_not_applied
        // If flags present and 0x40 IS set → SDR base → applied
        // If flags not present → trust module's found=true (it already validated the container)
        if (jh.metaSource == "app2" && jh.hasApp2Flags && !(jh.app2Flags & 0x40)) {
            R.verdict = "decoded_not_applied"; R.applyOk = false;
            R.reason = "hdr base displayed as-is; gain map not applied (APP2 flags 0x80)"; return R;
        } else if (baseTrc == 16 || baseTrc == 18) {
            R.verdict = "decoded_not_applied"; R.applyOk = false;
            R.reason = "hdr base displayed as-is; gain map not applied"; return R;
        }
        // Set metadata
        bool isApple = (jh.family == "apple");
        if (isApple) {
            R.headroom = powf(2.f, jh.headroom);
            R.gamma = 1.f;
        } else {
            for (int c = 0; c < 3; c++) {
                R.logBoostMin[c] = jh.logBoost[c*2];
                R.logBoostMax[c] = jh.logBoost[c*2+1];
                R.offsetSdr[c] = jh.offsetSdr[c];
                R.offsetHdr[c] = jh.offsetHdr[c];
            }
            R.headroom = jh.headroom > 0 ? powf(2.f, jh.headroom) : 1.f;
            R.gamma = jh.gamma;
        }
        R.decodeChannels = 1; // JPEG gainmaps are 1-channel
        // sRGB-linearize base
        {
            long px = (long)baseW * baseH * 4;
            for (long i = 0; i < px; i += 4) {
                baseRgba[i] = SrgbToLinear(baseRgba[i]);
                baseRgba[i+1] = SrgbToLinear(baseRgba[i+1]);
                baseRgba[i+2] = SrgbToLinear(baseRgba[i+2]);
            }
        }
        // Apply gainmap pixels
        std::vector<float> out((long)baseW * baseH * 4, 0.f);
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
                    float gl = Rec709Inverse(gv);
                    float scale = 1.f + (powf(2.f, jh.headroom) - 1.f) * gl;
                    out[o] = baseRgba[o] * scale;
                    out[o+1] = baseRgba[o+1] * scale;
                    out[o+2] = baseRgba[o+2] * scale;
                } else {
                    // ISO family: ApplyChannelIso with jh.logBoost/gamma
                    for (int ch = 0; ch < 3; ch++) {
                        float t = powf(gv, 1.f / jh.gamma);
                        float lb = jh.logBoost[ch*2] * (1-t) + jh.logBoost[ch*2+1] * t;
                        out[o+ch] = (baseRgba[o+ch] + jh.offsetSdr[ch]) * powf(2.f, lb) - jh.offsetHdr[ch];
                    }
                }
                out[o+3] = 1.f;
            }
        }
        R.appliedRgba = out;
        R.applyOk = true; R.applyMode = "jpeg";
        R.verdict = "applied";
        // stats
        {
            int n = baseW * baseH;
            double maxLin = 0; long over1 = 0;
            std::vector<float> lum;
            for (int i = 0; i < n; i++) {
                float l = Luma(out[i*4], out[i*4+1], out[i*4+2]);
                lum.push_back(l);
                if (l > maxLin) maxLin = l;
                if (l > 1.0) over1++;
            }
            std::sort(lum.begin(), lum.end());
            R.applyStats.p50 = n > 0 ? lum[n/2] : 0;
            R.applyStats.maxLinear = (float)maxLin;
            R.applyStats.over1Pct = n > 0 ? (float)over1 * 100.f / (float)n : 0;
        }
        return R;
    }
    // JPEG XL container: first box type "JXL ". Only flag as gainmap if jhgm box present.
    if (d.size() >= 8 && memcmp(d.data() + 4, "JXL ", 4) == 0) {
        std::string_view sv((const char*)d.data(), d.size());
        bool hasJhgm = sv.find("jhgm") != std::string_view::npos;
        if (hasJhgm) {
            R.present = true;
            R.form = "jhgm";
            R.metaFamily = "iso";
            R.metaSource = "tmap";
            R.verdict = "fail_closed_unsupported";
            R.reason = "[container] JPEG XL (jhgm) not supported in this build";
            return R;
        }
        R.reason = "JPEG XL container (no jhgm gainmap box)";
        return R;
    }

    // sha256 placeholder (we don't link crypto; emit empty string)
    // parse ftyp
    {
        char type[5]; long bsize, bhdr;
        if (!ReadBox(d, 0, sz, type, bsize, bhdr) || std::string(type) != "ftyp") {
            R.reason = "not ISO-BMFF (no ftyp)"; return R;
        }
        R.majorBrand = std::string((const char*)d.data() + 8, 4);
        for (long p = 16; p + 4 <= bsize; p += 4)
            R.compatibleBrands.push_back(std::string((const char*)d.data() + p, 4));
        // JXL container (ISOBMFF-based JPEG XL): unsupported in this build.
        if (R.majorBrand == "JXL ") {
            R.present = true;
            R.form = "jhgm";
            R.metaFamily = "iso";
            R.metaSource = "tmap";
            R.verdict = "fail_closed_unsupported";
            R.reason = "[container] JPEG XL (jhgm) not supported in this build";
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
            std::vector<AuxCProp> auxC;
            ParseIprp(d, cs, ce, C.ipco, C.ipma, auxC);
            // stash auxC on R via notes? We'll re-derive below.
            // Store in a thread-local-ish map via a static:
            static std::vector<AuxCProp> s_auxC; s_auxC = auxC;
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
    // Parse colr nclx from already-parsed ipco: detect PQ/HLG transfer.
    int baseColorTrc = 2;
    for (auto& pb : C.ipco) {
        if (pb.type == "colr" && pb.size >= 11) {
            long cs = pb.off;
            if (d[cs]=='n'&&d[cs+1]=='c'&&d[cs+2]=='l'&&d[cs+3]=='x') {
                // nclx: type(4) + primaries(2 BE) + transfer(2 BE) + matrix(2 BE) + range(1)
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
            tmapPayload.clear();
            R.present = false;
            R.form = "none";
        }
        else if (tmapPayload.size() >= 3 && tmapPayload[0] == 0) {
            // Check ISO minVersion (u16 at offset 1): if non-zero, gainmap is too new → ignore.
            int isoMinVer = Be16(tmapPayload.data() + 1);
            if (isoMinVer != 0) {
                tmapPayload.clear();
                R.present = false;
                R.form = "none";
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
            if (gp.size() < 6) containerWhy = "grid payload too short";
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

    if (form == Form::TmapSingle) R.form = "tmap";
    if (form == Form::AuxIso) { R.form = "tmap"; } // ISO aux treated as tmap-family
    // If the gainmap item is a grid (even if tile parsing failed), report the grid form.
    if (gainmapIsGrid && (R.form == "auxc_apple" || R.form == "auxc_vendor" || R.form == "tmap"))
        R.form = "auxc_grid";

    // Not gainmap?
    if (!R.present) {
        R.verdict = "not_gainmap";
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
    {
        char dbg[64]; sprintf(dbg, "groups count=%d", (int)C.groups.size()); R.notes.push_back(dbg);
        for (size_t gi = 0; gi < C.groups.size(); gi++) {
            const auto& g = C.groups[gi];
            std::string ents; for (int e : g.entities) { ents += std::to_string(e) + " "; }
            R.notes.push_back("grp id=" + std::to_string(g.groupId) + " ent=[" + ents + "]");
        }
    }
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
    int decTrcOuter = 2;
    bool baseIsGrid = (C.items.count(C.primary) && C.items[C.primary] == "grid");
    if (baseIsGrid) {
        // Primary item type is "grid": the item's own data IS the grid payload.
        std::string why;
        std::vector<uint8_t> gridPayload;
        if (!SliceItem(d, C, C.primary, gridPayload, why)) {
            R.verdict="fail_closed_malformed"; R.reason="base grid slice: " + why; return R;
        }
        { char dbg[128]; std::string hx; for(int i=0;i<(int)gridPayload.size()&&i<12;i++){char h[4];sprintf(h,"%02x ",gridPayload[i]);hx+=h;} R.notes.push_back("gridPayload: "+hx); }
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
        decTrcOuter = decTrc;
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
        decTrcOuter = decTrc;
    }
    R.baseRgba = baseRgba; R.outW = baseW; R.outH = baseH;
    // Gainmap was ignored (no brand / unsupported version / etc.) but base decoded:
    if (!R.present) {
        R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
        R.verdict = "not_gainmap"; R.reason = "gainmap not recognized for this file";
        return R;
    }

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
        long consumed; IsoMeta m;
        { char dbg[80]; std::string hx; for(int i=0;i<(int)tmapPayload.size()&&i<12;i++){char h[4];sprintf(h,"%02x ",tmapPayload[i]);hx+=h;} R.notes.push_back("tmapPayload: "+hx); }
        if (ParseIso21496(tmapPayload.data(), 1, (long)tmapPayload.size() - 1, m, consumed)) {
            // Verify payload length matches canonical v0 layout (detect writer with extra bytes)
            {
                uint8_t fl = tmapPayload[5]; // flags byte at offset 5 (after version+minVer+writerVer)
                int ch = (fl & 0x80) ? 3 : 1;
                bool cd = (fl & 0x08) != 0;
                long expected = 6 + (cd ? (12 + ch * 20) : (16 + ch * 40));
                if ((long)tmapPayload.size() > expected) {
                    R.verdict = "fail_closed_unsupported";
                    R.reason = "tmap: " + std::to_string((long)tmapPayload.size() - expected) + " trailing bytes beyond v0 layout";
                    R.decodeOk = false; R.applyOk = false;
                    return R;
                }
            }
            // gamma must be > 0
            if (m.gammaC[0] <= 0.f) {
                R.verdict = "fail_closed_malformed";
                R.reason = "tmap: gamma=" + std::to_string(m.gammaC[0]) + " (must be > 0)";
                R.decodeOk = false; R.applyOk = false;
                return R;
            }
            ap.iso = m; ap.isAppleFamily = false;
            R.metaFamily = "iso"; R.metaSource = "tmap";
            for (int c = 0; c < 3; c++) {
                R.offsetSdr[c] = m.offSdr[c]; R.offsetHdr[c] = m.offHdr[c];
                R.logBoostMin[c] = m.gainMin[c]; R.logBoostMax[c] = m.gainMax[c];
            }
            R.gamma = m.gammaC[0];
            R.headroom = powf(2.f, m.hdrCapacityMax);
            metaOk = true;
        }
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
                    ap.isAppleFamily = true; ap.headroomLinear = headroomLin;
                    R.metaFamily = "apple";
                }
                R.metaSource = "makenote";
                R.headroom = headroomLin;
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
                ap.isAppleFamily = true; ap.headroomLinear = powf(2.f, hr);
                R.metaFamily = "apple";
            }
            R.metaSource = "xmp";
            R.headroom = powf(2.f, hr);
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
        R.metaFamily = "none"; R.metaSource = "default";
        return R;
    }
    // HDR base: tmap flags bit 0x40 clear AND bit 0x80 set (0x80=HDR base, 0xc0=SDR base).
    // Only apply this check when 0x80 bit is present (validated tmap format);
    // flags=0x00 means unrecognized format, don't assume HDR base.
    if (metaOk && R.metaSource == "tmap" && (ap.iso.rawFlags & 0x80) && !(ap.iso.rawFlags & 0x40)) {
        R.decodeOk = true; R.decodeW = baseW; R.decodeH = baseH;
        R.verdict = "decoded_not_applied";
        R.applyOk = false; R.applyMode = "none";
        R.reason = "hdr base displayed as-is; gain map not applied (tmap flags 0x80)";
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
    for (size_t i = 0; i < baseRgba.size(); i += 4) {
        baseRgba[i] = SrgbToLinear(baseRgba[i]);
        baseRgba[i+1] = SrgbToLinear(baseRgba[i+1]);
        baseRgba[i+2] = SrgbToLinear(baseRgba[i+2]);
    }
    std::vector<float> out((size_t)baseW * baseH * 4, 0.f);

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
            float scale = 1.f + (ap.headroomLinear - 1.f) * gl;
            out[o] = baseRgba[o] * scale;
            out[o+1] = baseRgba[o+1] * scale;
            out[o+2] = baseRgba[o+2] * scale;
        } else {
            // ISO family (incl. vendor128)
            if (R.decodeChannels <= 1) {
                float gv = sampleGray();
                for (int ch = 0; ch < 3; ch++) out[o+ch] = ApplyChannelIso(gv, ch, baseRgba[o+ch], ap.iso);
            } else {
                for (int ch = 0; ch < 3; ch++) out[o+ch] = ApplyChannelIso(sampleCh(ch), ch, baseRgba[o+ch], ap.iso);
            }
        }
        out[o+3] = 1.f;
    }

    R.appliedRgba = out;
    R.applyOk = true;
    R.applyMode = "rgb16";

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
    R.verdict = "applied";
    return R;
}
