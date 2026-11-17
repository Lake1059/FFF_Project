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

// Grab a hdrgm channel triple in EITHER Ultra HDR form:
//   (a) element  <hdrgm:Tag><rdf:Seq><rdf:li>..</rdf:li>..</hdrgm:Tag>   (per-channel)
//   (b) attribute hdrgm:Tag="scalar"                                   (broadcast to 3 ch)
// Real writers mix them (spruit uses all-attributes; paris writes only
// GainMapMax as an rdf:Seq). Returns empty only when the tag is absent in
// BOTH forms.
static std::vector<float> XmpTripleBoth(const std::string& xml, const std::string& tag) {
    std::vector<float> v = XmpTriple(xml, tag);
    if (!v.empty()) return v;
    std::string s = XmpAttr(xml, tag);
    if (!s.empty()) {
        float f = ParseFloat(s);
        return {f, f, f};
    }
    return {};
}

// ---------- JPEG structures ----------
struct Img {
    size_t start = 0;      // SOI offset
    size_t end = 0;        // just past EOI
    int w = 0, h = 0, nf = 0;
};

struct MpfEntry { uint32_t attr=0, size=0, offset=0; uint32_t imageType=0; };

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
    const size_t ifd = tiffStart + static_cast<size_t>(b.u32(tiffStart+4, be));
    if (!b.ok(ifd, 2)) return false;
    uint16_t cnt = b.u16(ifd, be);
    size_t arrOff = 0;
    uint32_t arrLen = 0;
    size_t e = ifd + 2;
    for (uint16_t i = 0; i < cnt; i++, e += 12) {
        if (!b.ok(e, 12)) return false;
        uint16_t tag = b.u16(e, be);
        if (tag == 0xB002) { // MPFEntries
            uint16_t typ = b.u16(e+2, be);
            uint32_t c = b.u32(e+4, be);
            int sz = (typ==4||typ==3) ? (typ==3?2:4) : 4;
            arrLen = static_cast<uint32_t>(c) * 16u;
            arrOff = (c*static_cast<uint32_t>(sz) <= 4u)
                ? e + 8
                : tiffStart + static_cast<size_t>(b.u32(e+8, be));
        }
    }
    if (!arrOff || !arrLen) return false;
    for (size_t o = arrOff; o + 16 <= arrOff + arrLen; o += 16) {
        MpfEntry me;
        me.attr  = b.u32(o, be);
        me.size  = b.u32(o+4, be);
        me.offset= b.u32(o+8, be);
        // CIPA DC-007 MPF entry attribute, bits 26-24:
        //   0 = base/representative image
        //   1 = dependent child image
        //   2 = dependent child image of primary image (the gain map)
        me.imageType = (me.attr >> 24) & 0x07;
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
    const size_t ifd0 = t + static_cast<size_t>(b.u32(t+4, be));
    // find ExifIFD ptr 0x8769
    size_t exifIfd = 0;
    {
        uint16_t cnt = b.u16(ifd0, be);
        size_t e = ifd0 + 2;
        for (uint16_t i=0;i<cnt;i++,e+=12) {
            if (b.u16(e,be)==0x8769) { exifIfd = t + static_cast<size_t>(b.u32(e+8,be)); break; }
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

// ---------- WebP probe ----------
bool ProbeWebp(const Buf& b, JpegGainMapHit& out) {
    out.gainCodec = "webp";
    if (b.n < 12 || memcmp(b.d, "RIFF", 4) != 0 || memcmp(b.d+8, "WEBP", 4) != 0) {
        out.reason = "not RIFF/WEBP"; return false;
    }
    bool hasVpx = false, hasExif = false, hasXmp = false, hasAnim = false;
    size_t vp8Off = 0, vp8Len = 0;
    std::string chunks;
    std::string xmpBody;
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
            if (b.ok(body, sz)) xmpBody.assign((const char*)b.d+body, sz);
        }
        p = body + sz + (sz & 1);
    }
    // Real hdr-gain-map detection: scan the XMP packet (and the whole file) for
    // Ultra-HDR / Apple gain-map metadata. The old code set `hasGainMeta=false`
    // as a dead constant and never actually scanned anything.
    bool hasHdrgmMeta = false;
    {
        std::string_view all((const char*)b.d, b.n);
        hasHdrgmMeta = all.find("hdr-gain-map") != std::string_view::npos
                    || all.find("HDRGainMap") != std::string_view::npos
                    || all.find("GainMapMin") != std::string_view::npos
                    || all.find("hdrgm") != std::string_view::npos;
    }
    out.found = false;
    // A WebP that actually carries hdr-gain-map metadata is a real gain-map file
    // we cannot decode -> honest unsupported (present=true upstream).
    if (hasHdrgmMeta) {
        out.containerKind = "webp_gainmap";
        out.family = "iso";
        out.metaSource = "xmp";
        out.reason = "UNSUPPORTED_WEBP_GAINMAP: hdr-gain-map metadata present in WebP "
                     "but WebP gain-map decode not implemented";
        return false;
    }
    // No positive gain-map metadata. Distinguish a bare auxiliary gain layer
    // (recognized as a gain map, cannot apply standalone) from a plain SDR WebP.
    // A gain map is a downscaled auxiliary layer, so a lone small lossy VP8 frame
    // is treated as the bare layer; a large lone VP8 is an ordinary SDR photo.
    if (hasVpx && !hasAnim) {
        out.containerKind = "webp_gainmap";
        out.family = "iso";
        // WebP gain-map metadata lives in an XMP hdrgm chunk; GContainer is a
        // JPEG-only XMP construct and never applies to WebP → source is "xmp".
        out.metaSource = "xmp";
        bool bareLayer = (vp8Len <= 256u * 1024u);
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
            } else if (s.len >= 27 && memcmp(b.d+s.payload, "urn:iso:std:iso:ts:21496:-1", 27) == 0) {
                // ISO 21496-1 APP2 binary metadata box. A file may carry MORE THAN
                // ONE such segment: skia writes a degenerate 32-byte URN-only box
                // (no flags/headroom/channels) in the primary image AND the full
                // metadata box in the gain-map image. Keep the LONGEST segment —
                // that is the one carrying flags/headroom/per-channel data. The
                // degenerate URN-only box is tolerated but yields no headroom and
                // MUST NOT be used to fabricate one.
                if (s.len > isoLen) { isoPayload = s.payload; isoLen = s.len; }
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
    auto gMin = XmpTripleBoth(allXmp, "hdrgm:GainMapMin");
    auto gMax = XmpTripleBoth(allXmp, "hdrgm:GainMapMax");
    auto gam  = XmpTripleBoth(allXmp, "hdrgm:Gamma");
    // Ultra HDR spec: GainMapMin defaults to 0 when a writer omits it while
    // emitting GainMapMax (paris family does this). This is the spec default
    // for the gain-map lower bound, not a fabricated headroom.
    if (gMin.empty() && !gMax.empty()) gMin = {0.f, 0.f, 0.f};
    float offS = ParseFloat(XmpAttr(allXmp,"hdrgm:OffsetSDR"));
    float offH = ParseFloat(XmpAttr(allXmp,"hdrgm:OffsetHDR"));
    // Ultra HDR HDRCapacity: Min = BASE-side headroom (hB), Max = ALTERNATE-side
    // headroom (hA). (The old variable names hdrCapMax* were misleading: the Min
    // attribute is the base side, not a "max".) Units are log2 stops.
    std::string hbXmpStr = XmpAttr(allXmp, "hdrgm:HDRCapacityMin");
    std::string haXmpStr = XmpAttr(allXmp, "hdrgm:HDRCapacityMax");
    bool hasXmpCapacity = !hbXmpStr.empty() && !haXmpStr.empty();
    float xmpBaseHdr = hasXmpCapacity ? ParseFloat(hbXmpStr) : 0.f;
    float xmpAltHdr  = hasXmpCapacity ? ParseFloat(haXmpStr)  : 0.f;
    bool hdrgmParams = !gMax.empty() && gMax[0] != 0.f;

    // ---- Parse the FULL ISO 21496-1 APP2 box (longest URN segment) ----
    // All multi-byte fields BIG-ENDIAN. Layout:
    //   +0..+26  URN "urn:iso:std:iso:ts:21496:-1" (27 bytes)
    //   +27      NUL
    //   +28..+29 min_version u16
    //   +30..+31 writer_version u16
    //   +32      flags u8            (only when box >= 33 bytes)
    //   +33..+40 baseHdrHeadroom   rational: num s32, den u32  (log2 stops)
    //   +41..+48 alternateHdrHeadroom rational: num s32, den u32
    //   +49..    per-channel 40 bytes: min/max/gamma/offSDR/offHDR (num s32, den u32)
    // A degenerate box (len==32, skia primary image) has none of flags/headroom/
    // channels: tolerated, but yields NO headroom (never fabricate).
    float app2BaseHdr = 0.f, app2AltHdr = 0.f;
    bool  app2GotHdr = false;
    float app2Min[3]={0,0,0}, app2Max[3]={0,0,0}, app2Gamma[3]={1,1,1};
    float app2OffSdr[3]={0,0,0}, app2OffHdr[3]={0,0,0};
    int   app2Ch = 0;
    if (isoPayload && isoLen >= 33) {
        out.app2Flags = b.u8(isoPayload + 32);
        out.hasApp2Flags = true;
        // bits 0x3F are reserved: any set bit => consumer must fail-closed.
        out.app2ReservedNonZero = (out.app2Flags & 0x3F) != 0;
        size_t p = 33;
        if (isoLen >= p + 16) {
            int32_t bhn = b.s32(isoPayload+p, true); uint32_t bhd = b.u32(isoPayload+p+4, true); p += 8;
            int32_t ahn = b.s32(isoPayload+p, true); uint32_t ahd = b.u32(isoPayload+p+4, true); p += 8;
            if (bhd != 0 && ahd != 0) {
                app2BaseHdr = (float)bhn / (float)bhd;
                app2AltHdr  = (float)ahn / (float)ahd;
                app2GotHdr = true;
            }
        }
        while (app2Ch < 3 && isoLen >= p + 40) {
            int32_t mn=b.s32(isoPayload+p,true); uint32_t md=b.u32(isoPayload+p+4,true); p+=8;
            int32_t mx=b.s32(isoPayload+p,true); uint32_t mxd=b.u32(isoPayload+p+4,true); p+=8;
            int32_t gm=b.s32(isoPayload+p,true); uint32_t gd=b.u32(isoPayload+p+4,true); p+=8;
            int32_t bo=b.s32(isoPayload+p,true); uint32_t bod=b.u32(isoPayload+p+4,true); p+=8;
            int32_t ao=b.s32(isoPayload+p,true); uint32_t aod=b.u32(isoPayload+p+4,true); p+=8;
            if (md && mxd && gd && bod && aod) {
                app2Min[app2Ch]    = (float)mn/(float)md;
                app2Max[app2Ch]    = (float)mx/(float)mxd;
                app2Gamma[app2Ch]  = (float)gm/(float)gd;
                app2OffSdr[app2Ch] = (float)bo/(float)bod;
                app2OffHdr[app2Ch] = (float)ao/(float)aod;
                app2Ch++;
            } else {
                break; // zero denominator -> malformed; stop rather than divide
            }
        }
    }

    // Apple XMP headroom (v2/v3)
    float appleXmpHeadroom = ParseFloat(XmpElem(allXmp, "HDRGainMap:HDRGainMapHeadroom"));
    int appleVer = (int)ParseFloat(XmpElem(allXmp, "HDRGainMap:HDRGainMapVersion"), 0.f);

    // No MPF at all. If the JPEG still declares HDR gain-map headroom in XMP
    // (Apple HDRGainMapHeadroom or hdr-gain-map), it is an HDR base rendition
    // with no separable gain-map image: route upstream to present=true /
    // form=jpeg_hdr / decoded_not_applied instead of a flat not_gainmap.
    if (!hasMpf || mpf.size() < 2) {
        bool hdrHeadroomDeclared = (appleXmpHeadroom > 0.f) ||
                                   (hdrgmDecl && hdrgmParams);
        if (hdrHeadroomDeclared) {
            char b[160];
            sprintf(b, "JPEG_HDR_BASE_NO_GAINMAP: HDR headroom declared in XMP (headroomLog2=%.3f) "
                       "but no MPF gain-map second image", (double)appleXmpHeadroom);
            out.reason = b;
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

    // ---- Who is base / who is gain: priority order ----
    //
    //  (1) Authoritative container declaration wins outright: Google GContainer
    //      Container:Directory / ISO 21496-1 APP2 URN / Apple HDRGainMap ns.
    //  (2) Without a declaration, an MPF pair is trusted as base+gainmap ONLY
    //      when the secondary XMP ACTUALLY parses GainMapMin AND GainMapMax.
    //  (3) MPF image type (CIPA DC-007 entry attr bits 26-24): a proper gain-map
    //      writer marks the secondary as a dependent child (type 1/2). If the MPF
    //      explicitly types the secondary as a second BASE (type 0) AND there is
    //      neither an authoritative declaration nor real GainMapMin/Max, it is an
    //      MPO photo pair, not a gain map. (Many real writers — skia/libavif/
    //      Apple — leave the type field 0 and rely on (1)/(2), so type 0 alone
    //      never rejects a file that (1) or (2) already confirms.)
    //  (4) Fallback heuristic: equal-size secondary on a large base (>=1024px) is
    //      an MPO twin (02.jpg / 03.jpg: two 3000px camera frames). sein (400x300)
    //      stays accepted: real params, small base.
    bool authoritative = hasGContainer || hasIsoUrn || hasAppleNs;
    bool hasRealParams = !gMin.empty() && !gMax.empty();
    uint32_t secondaryType = (mpf.size() >= 2) ? mpf[1].imageType : 0;

    if (!authoritative) {
        if (!hasRealParams) {
            out.found = false;
            out.containerKind = "jpeg_uhdr";
            out.reason = "hdrgm namespace without GainMapMin/Max (no GContainer/ISO/Apple"
                         " declaration; MPF secondary image type=" + std::to_string(secondaryType) + ")";
            return false;
        }
        // (4) fallback: equal-size secondary on a large base => MPO twin.
        bool fullResTwin = (gainImg->w == baseImg.w && gainImg->h == baseImg.h &&
                            std::max(baseImg.w, baseImg.h) >= 1024);
        if (fullResTwin) {
            out.found = false;
            out.containerKind = "jpeg_uhdr";
            out.reason = "mpf secondary is a full-resolution equal-size photo (" +
                         std::to_string(baseImg.w) + "x" + std::to_string(baseImg.h) +
                         "), not a downsampled gain map (MPO pair; secondary type=" +
                         std::to_string(secondaryType) + ")";
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
        float appleHeadroomLog2 = 0.f;
        if (appleVer > 0 && appleVer < 131072) {
            out.containerKind = "jpeg_apple_v1";
            // v1: headroom MUST come from the Exif AppleMakerNote (tag 33/48).
            // Fail-closed: no/unparseable MakerNote => NOT a gain map. Never assume
            // 8x/log2=3 (this was the P0-4 bug: a missing MakerNote silently
            // produced a fabricated 3.0 stops).
            float hm = 0; std::string w;
            if (exifPayload && ParseAppleMakerNoteHeadroom(b, exifPayload, hm, w)) {
                appleHeadroomLog2 = hm; out.metaSource = "exif";
            } else {
                out.found = false;
                out.reason = "APPLE_V1_NO_MAKERNOTE: HDRGainMapVersion=1 but Exif "
                             "AppleMakerNote headroom unavailable (" + w +
                             "); refusing fixed-multiplier fallback (fail-closed)";
                return false;
            }
        } else if (!hasHdrToneMap) {
            out.containerKind = "jpeg_apple_v2";
            out.metaSource = "xmp";
            if (appleXmpHeadroom > 0.f) {
                appleHeadroomLog2 = appleXmpHeadroom;
            } else {
                out.found = false;
                out.reason = "APPLE_V2_NO_XMP_HEADROOM: HDRGainMapVersion=2 but XMP "
                             "HDRGainMap:HDRGainMapHeadroom missing/<=0; refusing guess (fail-closed)";
                return false;
            }
        } else {
            out.containerKind = "jpeg_apple_v3";
            out.metaSource = "xmp";
            if (appleXmpHeadroom > 0.f) {
                appleHeadroomLog2 = appleXmpHeadroom;
            } else {
                out.found = false;
                out.reason = "APPLE_V3_NO_XMP_HEADROOM: HDRGainMapVersion=3 but XMP "
                             "HDRGainMap:HDRGainMapHeadroom missing/<=0; refusing guess (fail-closed)";
                return false;
            }
        }
        out.headroomLog2 = appleHeadroomLog2;
        // Apple 0-base family: SDR base (hB=0), alternate headroom = hA.
        out.hasHdrCapacity = true;
        out.baseHdrHeadroomLog2 = 0.f;
        out.altHdrHeadroomLog2  = appleHeadroomLog2;
        out.gamma = 1.f;
        out.logBoost[0]=0; out.logBoost[1]=appleHeadroomLog2;
        out.logBoost[2]=0; out.logBoost[3]=appleHeadroomLog2;
        out.logBoost[4]=0; out.logBoost[5]=appleHeadroomLog2;
    } else {
        out.family = "iso";
        // Adobe "GContainer" form requires the ISO 21496-1 APP2 URN. The plain
        // hdrgm Ultra HDR XMP also emits Container:Directory/Item:Semantic (to
        // point at the MPF gainmap image), so hasGContainer alone must NOT be
        // read as the Adobe container; without the URN it is classic Ultra HDR.
        if (hasIsoUrn) {
            out.containerKind = "jpeg_adobe_gcontainer";
            // Adobe GContainer form: the gain-map metadata comes from the XMP
            // Container:Directory (GContainer), never from the APP2 segment itself
            // (APP2 only carries the URN marker). "app2" is the carrier segment,
            // not the metadata source → always "gcontainer".
            out.metaSource = "gcontainer";
        } else {
            out.containerKind = "jpeg_uhdr";
            out.metaSource = "xmp";
        }

        // Resolve hB/hA: XMP hdrgm:HDRCapacityMin/Max wins; else the APP2 box.
        if (hasXmpCapacity) {
            out.hasHdrCapacity = true;
            out.baseHdrHeadroomLog2 = xmpBaseHdr;
            out.altHdrHeadroomLog2  = xmpAltHdr;
        } else if (app2GotHdr) {
            out.hasHdrCapacity = true;
            out.baseHdrHeadroomLog2 = app2BaseHdr;
            out.altHdrHeadroomLog2  = app2AltHdr;
        }

        // Resolve gain min/max/gamma/offsets: XMP hdrgm triples win; else the
        // per-channel rationals from the APP2 box (skia files put the only copy
        // of the gain-map metadata in the APP2 box, not in XMP).
        bool xmpTriples = !gMin.empty() && !gMax.empty();
        if (xmpTriples) {
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
        } else if (app2Ch > 0) {
            for (int c=0;c<3;c++) {
                out.logBoost[c*2+0] = app2Min[c];
                out.logBoost[c*2+1] = app2Max[c];
                out.offsetSdr[c] = app2OffSdr[c];
                out.offsetHdr[c] = app2OffHdr[c];
            }
            out.gamma = app2Gamma[0];
        } else {
            out.found = false;
            out.reason = "NO_GAINMAP_PARAMS: MPF pair but neither XMP hdrgm "
                         "GainMapMin/Max nor APP2 ISO box channel data present";
            return false;
        }
    }

    // ---- Monotonicity / sanity (fail-closed on the APPLY path) ----
    // gainMin[c] <= gainMax[c] and gamma > 0. A violation means the gain map
    // cannot be applied without nonsense math. We only reject when we would
    // actually apply, i.e. the base is SDR (hB <= hA, or headroom unknown). When
    // hB > hA the base rendition is already HDR and the consumer emits
    // decoded_not_applied without touching the gain map, so a malformed gain-map
    // body must NOT force found=false (it would hide the HDR-base verdict).
    bool baseIsAlreadyHdr = out.hasHdrCapacity &&
                            out.baseHdrHeadroomLog2 > out.altHdrHeadroomLog2;
    if (!baseIsAlreadyHdr) {
        for (int c = 0; c < 3; c++) {
            float gmin = out.logBoost[c*2+0], gmax = out.logBoost[c*2+1];
            if (gmin > gmax) {
                out.found = false;
                out.reason = "MALFORMED_GAINMAP: channel " + std::to_string(c) +
                             " GainMapMin(" + std::to_string(gmin) +
                             ") > GainMapMax(" + std::to_string(gmax) + ")";
                return false;
            }
        }
        if (!(out.gamma > 0.f)) {
            out.found = false;
            out.reason = "MALFORMED_GAINMAP: gamma(" + std::to_string(out.gamma) + ") <= 0";
            return false;
        }
    }

    out.found = true;
    if (out.reason.empty()) {
        out.reason = "ok";
    }
    return true;
}

} // namespace fp3
