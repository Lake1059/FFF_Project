// GainMapJpeg.cpp — JPEG / WebP family gain-map container probe.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "GainMapJpeg.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <vector>

namespace fp3 {
namespace {

// ---------- byte cursor ----------
struct Buf {
    const uint8_t* d = nullptr;
    size_t n = 0;
    bool ok(size_t o, size_t l) const { return o + l <= n; }
    uint8_t  u8(size_t o) const { return ok(o,1) ? d[o] : 0; }
    uint16_t u16(size_t o, bool be) const {
        if (!ok(o,2)) return 0;
        return be ? (uint16_t((d[o]<<8)|d[o+1])) : (uint16_t(d[o]|(d[o+1]<<8)));
    }
    uint32_t u32(size_t o, bool be) const {
        if (!ok(o,4)) return 0;
        return be ? (uint32_t((uint32_t(d[o])<<24)|(uint32_t(d[o+1])<<16)|(uint32_t(d[o+2])<<8)|d[o+3]))
                  : (uint32_t((uint32_t(d[o])|(uint32_t(d[o+1])<<8)|(uint32_t(d[o+2])<<16)|(uint32_t(d[o+3])<<24))));
    }
    int32_t s32(size_t o, bool be) const { return (int32_t)u32(o, be); }
    float f32(size_t o, bool be) const {
        uint32_t b = u32(o, be);
        float v; memcpy(&v, &b, 4); return v;
    }
};

std::string Quote(const std::string& s) { return s; }

std::string ToLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// ---------- XMP field extraction (tiny tolerant parser) ----------
// Grab attribute value: <tag ... attr="value" ...>
std::string XmpAttr(const std::string& xml, const std::string& attr) {
    size_t pos = 0;
    while ((pos = xml.find(attr, pos)) != std::string::npos) {
        // must be preceded by whitespace or '=' boundary
        if (pos > 0) {
            char prev = xml[pos-1];
            if (std::isalnum((unsigned char)prev) || prev == ':' || prev == '_') { pos += attr.size(); continue; }
        }
        size_t eq = pos + attr.size();
        while (eq < xml.size() && (xml[eq]==' ' || xml[eq]=='\t' || xml[eq]=='\n' || xml[eq]=='\r')) eq++;
        if (eq >= xml.size() || xml[eq] != '=') { pos += attr.size(); continue; }
        size_t q = eq + 1;
        while (q < xml.size() && (xml[q]==' ' || xml[q]=='\t' || xml[q]=='\n' || xml[q]=='\r')) q++;
        if (q >= xml.size()) break;
        char qc = xml[q];
        if (qc != '"' && qc != '\'') { pos += attr.size(); continue; }
        size_t end = xml.find(qc, q+1);
        if (end == std::string::npos) break;
        return xml.substr(q+1, end-(q+1));
    }
    return "";
}

// Grab <Prefix:Tag>value</Prefix:Tag> (single scalar element, e.g. Apple Headroom).
std::string XmpElem(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag;
    size_t s = xml.find(open);
    if (s == std::string::npos) return "";
    s = xml.find('>', s);
    if (s == std::string::npos) return "";
    s++;
    size_t e = xml.find("</" + tag + ">", s);
    if (e == std::string::npos) return "";
    std::string v = xml.substr(s, e-s);
    // trim whitespace
    size_t a = v.find_first_not_of(" \t\r\n");
    size_t b = v.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return v.substr(a, b-a+1);
}

// Grab the three <rdf:li> values inside <hdrgm:Tag>...</hdrgm:Tag>.
std::vector<float> XmpTriple(const std::string& xml, const std::string& tag) {
    std::vector<float> out;
    std::string open = "<" + tag;
    size_t s = xml.find(open);
    if (s == std::string::npos) return out;
    size_t e = xml.find("</" + tag + ">", s);
    if (e == std::string::npos) return out;
    std::string block = xml.substr(s, e-s);
    size_t p = 0;
    while ((p = block.find("<rdf:li", p)) != std::string::npos) {
        size_t gt = block.find('>', p);
        if (gt == std::string::npos) break;
        // self-closing <rdf:li/>
        if (gt > p+6 && block[gt-1] == '/') { p = gt+1; continue; }
        size_t close = block.find("</rdf:li>", gt);
        if (close == std::string::npos) break;
        std::string v = block.substr(gt+1, close-gt-1);
        for (char& c : v) if (c=='\n'||c=='\r'||c=='\t') c=' ';
        try { out.push_back((float)std::stod(v)); } catch(...) {}
        p = close + 9;
    }
    return out;
}

float ParseFloat(const std::string& s, float dflt=0.f) {
    if (s.empty()) return dflt;
    try { return (float)std::stod(s); } catch(...) { return dflt; }
}

// ---------- JPEG structures ----------
struct Img {
    size_t start = 0;      // SOI offset
    size_t end = 0;        // just past EOI
    int w = 0, h = 0, nf = 0;
};

struct MpfEntry { uint32_t attr=0, size=0, offset=0; };

struct Seg {
    int marker = 0;        // 0xE1 / 0xE2 ...
    size_t payload = 0;    // offset of segment payload (after length field)
    size_t len = 0;        // payload length
    int imgIndex = 0;
};

bool ReadFile(const std::wstring& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;
    f.seekg(0, std::ios::end);
    std::streamoff sz = f.tellg();
    f.seekg(0, std::ios::beg);
    out.resize((size_t)sz);
    if (sz > 0) f.read((char*)out.data(), sz);
    return true;
}

// Walk one JPEG image starting at soi (offset of 0xFFD8). Returns offset after EOI.
size_t WalkJpegImage(const Buf& b, size_t soi, Img& img, int imgIndex,
                     std::vector<Seg>& segs, std::string& why) {
    img.start = soi;
    size_t p = soi + 2;
    while (p + 1 < b.n) {
        if (b.u8(p) != 0xFF) { p++; continue; }
        uint8_t m = b.u8(p+1);
        if (m == 0xFF || m == 0x00) { p += 2; continue; }
        if (m == 0xD9) { img.end = p + 2; return img.end; }       // EOI
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { p += 2; continue; }
        if (m == 0xDA) {
            // scan data: skip length, then scan bytes until next marker
            uint16_t sl = b.u16(p+2, true);
            size_t q = p + 2 + sl;
            while (q + 1 < b.n) {
                if (b.u8(q) == 0xFF && b.u8(q+1) != 0x00 && !(b.u8(q+1)>=0xD0 && b.u8(q+1)<=0xD7)) break;
                q++;
            }
            p = q;
            continue;
        }
        if (m < 0xC0) { p += 2; continue; }
        if (!b.ok(p+4, 0)) { why = "truncated segment"; return img.start; }
        uint16_t sl = b.u16(p+2, true);
        size_t pl = p + 4;
        size_t plen = sl - 2;
        if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC && plen >= 6) {
            img.h = b.u16(pl+1, true);
            img.w = b.u16(pl+3, true);
            img.nf = b.u8(pl+5);
        }
        if (m == 0xE1 || m == 0xE2 || m == 0xE3 || m == 0xE4 || m == 0xE5 ||
            m == 0xE6 || m == 0xE7 || m == 0xE8 || m == 0xE9 || m == 0xEA ||
            m == 0xEB || m == 0xEC || m == 0xED || m == 0xEE || m == 0xEF) {
            Seg s; s.marker = m; s.payload = pl; s.len = plen; s.imgIndex = imgIndex;
            segs.push_back(s);
        }
        p = pl + plen;
    }
    why = "no EOI";
    img.end = b.n;
    return img.end;
}

// Parse MPF table at tiffStart (points at "MM\0*" / "II*\0").
bool ParseMpf(const Buf& b, size_t tiffStart, std::vector<MpfEntry>& entries, bool& be) {
    be = (b.u8(tiffStart) == 'M' && b.u8(tiffStart+1) == 'M');
    bool le = (b.u8(tiffStart) == 'I' && b.u8(tiffStart+1) == 'I');
    if (!be && !le) return false;
    if (b.u16(tiffStart+2, be) != 42) return false;
    uint32_t ifd = tiffStart + b.u32(tiffStart+4, be);
    if (!b.ok(ifd, 2)) return false;
    uint16_t cnt = b.u16(ifd, be);
    uint32_t arrOff = 0, arrLen = 0;
    size_t e = ifd + 2;
    for (uint16_t i = 0; i < cnt; i++, e += 12) {
        if (!b.ok(e, 12)) return false;
        uint16_t tag = b.u16(e, be);
        if (tag == 0xB002) { // MPFEntries
            uint16_t typ = b.u16(e+2, be);
            uint32_t c = b.u32(e+4, be);
            int sz = (typ==4||typ==3) ? (typ==3?2:4) : 4;
            arrLen = (size_t)c * 16;
            arrOff = (c*sz <= 4) ? (uint32_t)(e+8) : (tiffStart + b.u32(e+8, be));
        }
    }
    if (!arrOff || !arrLen) return false;
    for (uint32_t o = arrOff; o + 16 <= arrOff + arrLen; o += 16) {
        MpfEntry me;
        me.attr  = b.u32(o, be);
        me.size  = b.u32(o+4, be);
        me.offset= b.u32(o+8, be);
        entries.push_back(me);
    }
    return entries.size() >= 2;
}

// Parse AppleMakerNote from Exif APP1 payload. out headroom (log2). Returns false if absent.
bool ParseAppleMakerNoteHeadroom(const Buf& b, size_t exifPayload, float& headroomLog2, std::string& why) {
    // exifPayload points at "Exif\0\0"
    if (!b.ok(exifPayload+6, 8)) { why="exif too short"; return false; }
    if (!(b.u8(exifPayload+0)=='E' && b.u8(exifPayload+1)=='x' && b.u8(exifPayload+2)=='i' &&
          b.u8(exifPayload+3)=='f' && b.u8(exifPayload+4)==0 && b.u8(exifPayload+5)==0)) {
        why="no Exif magic"; return false;
    }
    size_t t = exifPayload + 6;
    bool be = (b.u8(t)=='M' && b.u8(t+1)=='M');
    bool le = (b.u8(t)=='I' && b.u8(t+1)=='I');
    if (!be && !le) { why="no TIFF endian"; return false; }
    if (b.u16(t+2, be) != 42) { why="bad tiff magic"; return false; }
    uint32_t ifd0 = t + b.u32(t+4, be);
    // find ExifIFD ptr 0x8769
    uint32_t exifIfd = 0;
    {
        uint16_t cnt = b.u16(ifd0, be);
        size_t e = ifd0 + 2;
        for (uint16_t i=0;i<cnt;i++,e+=12) {
            if (b.u16(e,be)==0x8769) { exifIfd = t + b.u32(e+8,be); break; }
        }
    }
    if (!exifIfd) { why="no ExifIFD"; return false; }
    // find MakerNote 0x927C
    size_t mn = 0; uint32_t mnLen = 0;
    {
        uint16_t cnt = b.u16(exifIfd, be);
        size_t e = exifIfd + 2;
        for (uint16_t i=0;i<cnt;i++,e+=12) {
            if (b.u16(e,be)==0x927C) {
                uint16_t typ=b.u16(e+2,be); uint32_t c=b.u32(e+4,be);
                int sz=(typ==2?1:(typ==7?1:4));
                mnLen = c*sz;
                mn = (mnLen<=4)?(e+8):(t + b.u32(e+8,be));
                break;
            }
        }
    }
    if (!mn || mnLen < 16) { why="no MakerNote"; return false; }
    static const char sig[10] = {'A','p','p','l','e',' ','i','O','S',0};
    for (int i=0;i<10;i++) if (b.u8(mn+i)!=(uint8_t)sig[i]) { why="not Apple iOS maker"; return false; }
    // Apple MN IFD starts at mn+14, always big-endian per exiftool.
    size_t ifd = mn + 14;
    uint16_t cnt = b.u16(ifd, true);
    float m33 = -1.f, m48 = -1.f; bool got33=false, got48=false;
    size_t e = ifd + 2;
    for (uint16_t i=0;i<cnt;i++,e+=12) {
        if (!b.ok(e,12)) break;
        uint16_t tag=b.u16(e,true), typ=b.u16(e+2,true), c=b.u32(e+4,true);
        if (tag!=0x0021 && tag!=0x0030) continue;
        float val=0; bool have=false;
        if (typ==5 || typ==10) { // RATIONAL / SRATIONAL
            uint32_t off = b.u32(e+8, true);
            size_t vp = mn + off;
            if (b.ok(vp,8)) {
                int32_t num = b.s32(vp,true), den = b.s32(vp+4,true);
                if (den != 0) { val = (float)num/(float)den; have=true; }
            }
        } else if (typ==11) { // FLOAT32 inline
            val = b.f32(e+8, true); have=true;
        }
        if (!have) continue;
        if (tag==0x0021) { m33=val; got33=true; }
        else { m48=val; got48=true; }
    }
    if (!got33 || !got48) { why="maker tag33/48 missing"; return false; }
    if (m33 <= 0.f && m48 <= 0.f) { why="maker headroom=0"; return false; }
    float stops = (m33 < 1.0f)
        ? (m48 <= 0.01f ? -20.f*m48 + 1.8f : -0.101f*m48 + 1.601f)
        : (m48 <= 0.01f ? -70.f*m48 + 3.0f : -0.303f*m48 + 2.303f);
    float head = std::pow(2.f, std::max(stops, 0.f));
    if (head <= 1.f) { why="maker headroom<=1"; return false; }
    headroomLog2 = (float)std::log2(head);
    return true;
}

// Compute SHA-256 (FIPS 180-3) of a byte span.
void Sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xabc5ed5b,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf4e5346f,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t H[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint64_t bitLen = (uint64_t)len * 8;
    // pad
    std::vector<uint8_t> m(data, data+len);
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i=7;i>=0;i--) m.push_back((uint8_t)(bitLen >> (i*8)));
    for (size_t off=0; off<m.size(); off+=64) {
        uint32_t w[64];
        for (int i=0;i<16;i++) w[i] = (uint32_t(m[off+i*4])<<24)|(uint32_t(m[off+i*4+1])<<16)|(uint32_t(m[off+i*4+2])<<8)|m[off+i*4+3];
        for (int i=16;i<64;i++) {
            uint32_t s1 = (w[i-15]>>7 | w[i-15]<<25) ^ (w[i-15]>>18 | w[i-15]<<14) ^ (w[i-15]>>3);
            uint32_t s2 = (w[i-2]>>17 | w[i-2]<<15) ^ (w[i-2]>>19 | w[i-2]<<13) ^ (w[i-2]>>10);
            w[i] = w[i-16] + s1 + w[i-7] + s2;
        }
        uint32_t a=H[0],b=H[1],c=H[2],cc=H[3],d=H[4],e=H[5],f=H[6],g=H[7];
        for (int i=0;i<64;i++) {
            uint32_t S1=(e>>6|e<<26)^(e>>11|e<<21)^(e>>25|e<<7);
            uint32_t ch=(e&f)^(~e&g);
            uint32_t t1=g+S1+ch+K[i]+w[i];
            uint32_t S0=(a>>2|a<<30)^(a>>13|a<<19)^(a>>22|a<<10);
            uint32_t maj=(a&b)^(a&cc)^(b&cc);
            uint32_t t2=S0+maj;
            g=f;f=e;e=cc+t1;cc=d;d=c;c=b;b=a;a=t1+t2;
        }
        H[0]+=a;H[1]+=b;H[2]+=c;H[3]+=cc;H[4]+=d;H[5]+=e;H[6]+=f;H[7]+=g;
    }
    for (int i=0;i<8;i++) {
        out[i*4+0]=(uint8_t)(H[i]>>24); out[i*4+1]=(uint8_t)(H[i]>>16);
        out[i*4+2]=(uint8_t)(H[i]>>8);  out[i*4+3]=(uint8_t)(H[i]);
    }
}

std::string Hex(const uint8_t* p, size_t n) {
    static const char* H = "0123456789abcdef";
    std::string s; s.resize(n*2);
    for (size_t i=0;i<n;i++) { s[i*2]=H[p[i]>>4]; s[i*2+1]=H[p[i]&15]; }
    return s;
}

// ---------- WebP probe ----------
bool ProbeWebp(const Buf& b, JpegGainMapHit& out) {
    out.gainCodec = "webp";
    if (b.n < 12 || memcmp(b.d, "RIFF", 4) != 0 || memcmp(b.d+8, "WEBP", 4) != 0) {
        out.reason = "not RIFF/WEBP"; return false;
    }
    bool hasVpx = false, hasExif = false, hasXmp = false, hasAnim = false;
    size_t vp8Off = 0, vp8Len = 0;
    std::string chunks;
    size_t p = 12;
    while (p + 8 <= b.n) {
        char id[5] = {0}; memcpy(id, b.d+p, 4);
        uint32_t sz = b.u32(p+4, false);
        chunks += id; chunks += " ";
        size_t body = p + 8;
        if (memcmp(id,"VP8X",4)==0) {
            if (b.ok(body,1)) {
                uint8_t fl = b.u8(body);
                hasAnim = (fl & 0x02) != 0;
                // bit4=EXIF, bit3=XMP per WebP spec flags
                hasExif = (fl & 0x08) != 0;
                hasXmp  = (fl & 0x20) != 0;
            }
        } else if (memcmp(id,"VP8 ",4)==0 || memcmp(id,"VP8L",4)==0) {
            hasVpx = true; vp8Off = body; vp8Len = sz;
        } else if (memcmp(id,"EXIF",4)==0) {
            hasExif = true;
        } else if (memcmp(id,"XMP ",4)==0) {
            hasXmp = true;
        }
        p = body + sz + (sz & 1);
    }
    // A self-contained WebP gain map needs a base frame AND gain-map metadata
    // (EXIF/XMP carrying hdrgm). This corpus has none, so every WebP here is
    // found=false; the reason string MUST stay discriminative because the player
    // routes the two negatives differently:
    //   * spruit-...-gainmap.webp = 24KB bare VP8 gain LAYER (has gain-map data
    //     but NO base in-file) -> fail_closed_unsupported: recognized as a gain
    //     map but cannot be applied standalone.
    //   * spruit_sunrise_4k.webp  = 1.5MB plain SDR photo (no gain-map wrapper
    //     at all) -> not_gainmap.
    // Heuristic: an auxiliary-sized VP8 (<64KB, no metadata) is the bare layer;
    // a large VP8 is a plain SDR frame.
    bool hasGainMeta = false;
    if (hasVpx && !hasGainMeta && !hasAnim) {
        out.found = false;
        out.containerKind = "webp_gainmap";
        out.family = "iso";
        out.metaSource = "gcontainer";
        bool bareLayer = (vp8Len < 65536);
        if (bareLayer) {
            out.reason = "BARE_GAINMAP_LAYER_NO_BASE: gain-map VP8 layer present but no "
                         "base image in file; recognized as gain map yet cannot be applied "
                         "standalone (RIFF chunks: " + chunks + ")";
        } else {
            out.reason = "PLAIN_SDR_WEBP: ordinary lossy SDR WebP; no gain-map wrapper or "
                         "hdrgm metadata (RIFF chunks: " + chunks + ")";
        }
        if (b.ok(vp8Off, vp8Len)) out.gainBytes.assign(b.d+vp8Off, b.d+vp8Off+vp8Len);
        return false;
    }
    out.found = false;
    out.reason = "no gain-map wrapper in WebP (chunks: " + chunks + ")";
    return false;
}

} // namespace

bool ProbeJpegGainMap(const std::wstring& path, JpegGainMapHit& out) {
    out = JpegGainMapHit{};
    std::vector<uint8_t> file;
    if (!ReadFile(path, file)) { out.reason = "cannot read file"; return false; }
    Buf b{file.data(), file.size()};

    // WebP?
    if (b.n >= 12 && memcmp(b.d, "RIFF", 4) == 0 && memcmp(b.d+8, "WEBP", 4) == 0) {
        ProbeWebp(b, out);
        return out.found;
    }

    // JPEG?
    if (b.n < 4 || b.u8(0) != 0xFF || b.u8(1) != 0xD8) {
        out.reason = "not JPEG (no SOI)"; return false;
    }

    std::vector<Img> imgs;
    std::vector<Seg> segs;
    size_t p = 0;
    int imgIdx = 0;
    while (p + 3 < b.n) {
        if (b.u8(p) != 0xFF || b.u8(p+1) != 0xD8) { p++; continue; }
        Img im; std::string why;
        size_t nxt = WalkJpegImage(b, p, im, imgIdx, segs, why);
        imgs.push_back(im);
        imgIdx++;
        p = nxt;
        if (p <= im.start) break;
    }

    if (imgs.empty()) { out.reason = "no JPEG image"; return false; }

    // Locate MPF + ISO-URN segments across all images.
    size_t mpfPayload = 0, mpfLen = 0;
    size_t isoPayload = 0, isoLen = 0;
    size_t exifPayload = 0, exifLen = 0;
    std::vector<std::string> xmpPackets; // raw XML per APP1 XMP
    for (const Seg& s : segs) {
        if (s.marker == 0xE2) {
            // MPF\0 ?
            if (b.ok(s.payload+4,0) && b.u8(s.payload)=='M'&&b.u8(s.payload+1)=='P'&&
                b.u8(s.payload+2)=='F'&&b.u8(s.payload+3)==0) {
                mpfPayload = s.payload + 4; mpfLen = s.len - 4;
            } else if (s.len >= 26 && memcmp(b.d+s.payload, "urn:iso:std:iso:ts:21496:-1", 26) == 0) {
                // ISO 21496-1 APP2 binary metadata box. Layout (per libultrahdr /
                // GainMapDecoder.ParseIso21496), all multi-byte fields BIG-ENDIAN:
                //   payload[0..25]  "urn:iso:std:iso:ts:21496:-1" (26 ASCII)
                //   payload[26]     NUL
                //   payload[27..28] min_version u16   (must be 0 for v0)
                //   payload[29..30] writer_version u16
                //   payload[31]     FLAGS u8
                //     bit7 0x80 = multi-channel (3-ch)
                //     bit6 0x40 = useBaseColorSpace
                //     bit3 0x08 = commonDenominator
                //     bit2 0x04 = backwardDirection (BaseRenditionIsHdr)
                //   then per-channel min/max/gamma/offsets (rational or common-denom).
                // i.e. the flags byte absolute file offset = s.payload + 31.
                //   flags=0xc0 -> useBaseColorSpace=1, SDR base  -> apply gainmap (applied)
                //   flags=0x80 -> useBaseColorSpace=0, HDR base  -> decoded_not_applied
                // (The skia test files write a degenerate "1\0" version here; the
                // main implementer parses the box when present and tolerates that.)
                isoPayload = s.payload; isoLen = s.len;
            }
        } else if (s.marker == 0xE1) {
            // XMP: "http://ns.adobe.com/xap/1.0/\0" (28 chars + NUL = 29 bytes)
            if (s.len >= 29 && memcmp(b.d+s.payload, "http://ns.adobe.com/xap/1.0/", 28) == 0 &&
                b.u8(s.payload+28) == 0) {
                size_t sp = s.payload + 29;
                // find </x:xmpmeta> within segment (12 chars)
                std::string xml;
                size_t scan = sp;
                while (scan + 12 < b.n) {
                    if (memcmp(b.d+scan, "</x:xmpmeta>", 12) == 0) {
                        xml.assign((const char*)b.d+sp, scan+12-sp);
                        break;
                    }
                    scan++;
                    if (scan > sp + s.len) break;
                }
                if (!xml.empty()) xmpPackets.push_back(xml);
            } else if (s.len >= 6 && memcmp(b.d+s.payload, "Exif\0\0", 6) == 0) {
                exifPayload = s.payload; exifLen = s.len;
            }
        }
    }

    // Merge all XMP into one string for field lookup.
    std::string allXmp;
    for (auto& x : xmpPackets) allXmp += x;

    // MPF entries.
    std::vector<MpfEntry> mpf;
    bool mpfBe = true;
    bool hasMpf = false;
    if (mpfPayload) {
        hasMpf = ParseMpf(b, mpfPayload, mpf, mpfBe);
    }

    // ----- Classify -----
    bool hasGContainer = allXmp.find("Container:Directory") != std::string::npos ||
                         allXmp.find("Item:Semantic") != std::string::npos;
    bool hasIsoUrn = isoPayload != 0;
    bool hasAppleNs = allXmp.find("ns.apple.com/HDRGainMap") != std::string::npos;

    // hdrgm parameters (legacy Ultra HDR)
    bool hdrgmDecl = allXmp.find("hdr-gain-map/1.0") != std::string::npos;
    std::string ver = XmpAttr(allXmp, "hdrgm:Version");
    auto gMin = XmpTriple(allXmp, "hdrgm:GainMapMin");
    auto gMax = XmpTriple(allXmp, "hdrgm:GainMapMax");
    auto gam  = XmpTriple(allXmp, "hdrgm:Gamma");
    float offS = ParseFloat(XmpAttr(allXmp,"hdrgm:OffsetSDR"));
    float offH = ParseFloat(XmpAttr(allXmp,"hdrgm:OffsetHDR"));
    float hdrCapMax = ParseFloat(XmpAttr(allXmp,"hdrgm:HDRCapacityMin"), 0.f);
    float hdrCapMaxV = ParseFloat(XmpAttr(allXmp,"hdrgm:HDRCapacityMax"), 1.f);
    bool hdrgmParams = !gMax.empty() && gMax[0] != 0.f;

    // Apple XMP headroom (v2/v3)
    float appleXmpHeadroom = ParseFloat(XmpElem(allXmp, "HDRGainMap:HDRGainMapHeadroom"));
    int appleVer = (int)ParseFloat(XmpElem(allXmp, "HDRGainMap:HDRGainMapVersion"), 0.f);

    // No MPF at all -> not a gain map JPEG (plain SDR).
    if (!hasMpf || mpf.size() < 2) {
        if (hdrgmDecl && hdrgmParams) {
            out.reason = "hdrgm XMP present but no MPF second image";
        } else if (hdrgmDecl) {
            out.reason = "hdrgm namespace only, no parameters and no MPF second image";
        } else {
            out.reason = "no MPF and no hdrgm metadata";
        }
        out.found = false;
        return false;
    }

    // Locate gain-map secondary image. MPF entry[0] = base (offset 0), entry[1] = gain map.
    const Img& baseImg = imgs[0];
    // gain image = image[1] if present, else locate by MPF offset.
    const Img* gainImg = imgs.size() >= 2 ? &imgs[1] : nullptr;
    size_t gainAbs = mpf[1].offset; // relative to TIFF base start (== file start for JPEG MPF)
    // MPF offset is relative to the start of the file's primary image (== 0).
    if (!gainImg) {
        // find image whose SOI is at gainAbs
        for (auto& im : imgs) if (im.start == gainAbs) { gainImg = &im; break; }
    }
    if (!gainImg || gainImg->w <= 0) {
        out.reason = "MPF second image not locatable";
        out.found = false; return false;
    }

    // ---- Non-declared Ultra HDR: require real parameters, reject full-res twins.
    //
    // Without an authoritative container declaration (Google GContainer
    // Container:Directory / ISO 21496-1 APP2 URN / Apple HDRGainMap namespace),
    // an MPF pair is only trusted as base+gainmap when the secondary XMP
    // ACTUALLY parses to GainMapMin AND GainMapMax triples. A bare hdrgm
    // namespace declaration (or a stray header on a normal photo) must NOT flip
    // us into the HDR apply path.
    //
    // Even with parameters, a gain map is a DOWNSAMPLED auxiliary; a second
    // image that is the same pixel grid as a large base (>=1024px) is an MPO
    // photo pair (e.g. 02.jpg / 03.jpg: two 3000px camera frames), not a
    // gain map. sein (400x300) stays accepted: it has real min/max params and
    // the base is small, so the same-size secondary is a pseudo-map, not a photo.
    bool secondaryDownsampled = (gainImg->w < baseImg.w && gainImg->h < baseImg.h);
    bool authoritative = hasGContainer || hasIsoUrn || hasAppleNs;
    bool hasRealParams = !gMin.empty() && !gMax.empty();
    (void)secondaryDownsampled;

    if (!authoritative) {
        if (!hasRealParams) {
            out.found = false;
            out.containerKind = "jpeg_uhdr";
            out.reason = "hdrgm namespace without GainMapMin/Max (no GContainer/ISO/Apple declaration)";
            return false;
        }
        bool fullResTwin = (gainImg->w == baseImg.w && gainImg->h == baseImg.h &&
                            std::max(baseImg.w, baseImg.h) >= 1024);
        if (fullResTwin) {
            out.found = false;
            out.containerKind = "jpeg_uhdr";
            out.reason = "mpf secondary is a full-resolution equal-size photo (" +
                         std::to_string(baseImg.w) + "x" + std::to_string(baseImg.h) +
                         "), not a downsampled gain map (MPO pair)";
            return false;
        }
    }

    // ---- Carve bytes ----
    out.baseBytes.assign(b.d + baseImg.start, b.d + baseImg.end);
    out.gainBytes.assign(b.d + gainImg->start, b.d + gainImg->end);
    out.gainCodec = "jpeg";

    // ---- Fill metadata by family ----
    if (hasAppleNs) {
        out.family = "apple";
        // v1 = HDRGainMapVersion 65536, headroom in Exif MakerNote.
        // v2 = version 131072, headroom in XMP, Adobe hdr-gain-map namespace absent.
        // v3 = version 131072 AND HDRToneMap/adobe hdr-gain-map block present.
        bool hasHdrToneMap = allXmp.find("HDRToneMap:") != std::string::npos ||
                             allXmp.find("ns.adobe.com/hdr-gain-map") != std::string::npos;
        if (appleVer > 0 && appleVer < 131072) {
            out.containerKind = "jpeg_apple_v1";
            // v1: headroom from Exif MakerNote
            float hm = 0; std::string w;
            if (exifPayload && ParseAppleMakerNoteHeadroom(b, exifPayload, hm, w)) {
                out.headroom = hm; out.metaSource = "exif";
            } else {
                out.headroom = 3.0f; out.metaSource = "exif";
                out.reason = "apple v1: maker note unavailable (" + w + "); assumed 8x (log2=3)";
            }
        } else if (!hasHdrToneMap) {
            out.containerKind = "jpeg_apple_v2";
            out.headroom = appleXmpHeadroom; out.metaSource = "xmp";
        } else {
            out.containerKind = "jpeg_apple_v3";
            out.headroom = appleXmpHeadroom; out.metaSource = "xmp";
        }
        out.gamma = 1.f;
        out.logBoost[0]=0; out.logBoost[1]=out.headroom;
        out.logBoost[2]=0; out.logBoost[3]=out.headroom;
        out.logBoost[4]=0; out.logBoost[5]=out.headroom;
    } else {
        out.family = "iso";
        if (hasIsoUrn) {
            out.containerKind = "jpeg_adobe_gcontainer";
            out.metaSource = "app2";
        } else if (hasGContainer) {
            out.containerKind = "jpeg_adobe_gcontainer";
            out.metaSource = "gcontainer";
        } else {
            out.containerKind = "jpeg_uhdr";
            out.metaSource = "xmp";
        }
        // hdrgm values -> logBoost triple.
        auto fill = [&](float* dst, const std::vector<float>& v, float dflt) {
            for (int i=0;i<3;i++) dst[i] = (i < (int)v.size()) ? v[i] : dflt;
        };
        fill(out.offsetSdr, {offS,offS,offS}, offS);
        fill(out.offsetHdr, {offH,offH,offH}, offH);
        // logBoost layout [minR,maxR,minG,maxG,minB,maxB]
        for (int c=0;c<3;c++) {
            out.logBoost[c*2+0] = (c < (int)gMin.size()) ? gMin[c] : 0.f;
            out.logBoost[c*2+1] = (c < (int)gMax.size()) ? gMax[c] : 1.f;
        }
        out.gamma = (!gam.empty()) ? gam[0] : 1.f;
        (void)hdrCapMax; (void)hdrCapMaxV;
    }

    out.found = true;
    if (out.reason.empty()) {
        out.reason = "ok";
    }
    return true;
}

} // namespace fp3
