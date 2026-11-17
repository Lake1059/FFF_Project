// ICC profile structured decoder / validator (v2.x and v4.x).
//
// Scope: structure only. No colour transform, no CLUT interpolation.
// Every read is bounds-checked; Parse() never throws and never reads outside
// [data, data+size).
//
// Conventions:
//   * All multi-byte integers are big-endian on disk.
//   * Tag payloads begin with an 8-byte type header (4-byte type signature +
//     4 reserved bytes).
//   * Unknown / vendor-private tags are bounds-checked only and kept rawOnly.

#include "pch.h"

#include "3FP/Core/IccProfileParser.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

namespace fff::icc {
namespace {

// ---------------------------------------------------------------------------
// Bounded big-endian reader. Every access returns false on overflow; the
// caller checks r.ok before trusting the produced value.
// ---------------------------------------------------------------------------
struct R {
    const uint8_t* d;
    size_t n;
    size_t p = 0;
    bool ok = true;

    R(const uint8_t* data, size_t size) : d(data), n(size) {}

    bool Need(size_t b) {
        if (n - p < b) { ok = false; return false; }
        return true;
    }
    uint8_t U8() {
        if (!Need(1)) return 0;
        return d[p++];
    }
    uint16_t U16() {
        if (!Need(2)) return 0;
        uint16_t v = (uint16_t((uint16_t)d[p] << 8)) | uint16_t(d[p + 1]);
        p += 2;
        return v;
    }
    uint32_t U32() {
        if (!Need(4)) return 0;
        uint32_t v = (uint32_t(d[p]) << 24) | (uint32_t(d[p + 1]) << 16) |
                     (uint32_t(d[p + 2]) << 8) | uint32_t(d[p + 3]);
        p += 4;
        return v;
    }
    uint64_t U64() {
        uint64_t hi = U32();
        uint64_t lo = U32();
        if (!ok) return 0;
        return (hi << 32) | lo;
    }
    double S15Fixed16() {
        uint32_t v = U32();
        if (!ok) return 0.0;
        return static_cast<double>(static_cast<int32_t>(v)) / 65536.0;
    }
    double U16Fixed8() {
        uint16_t v = U16();
        if (!ok) return 0.0;
        return static_cast<double>(v) / 256.0;
    }
    void Skip(size_t b) {
        if (!Need(b)) return;
        p += b;
    }
};

// Render a 4CC as 4 printable chars (non-printable -> '?').
std::string Sig(uint32_t s) {
    char b[5];
    b[0] = static_cast<char>((s >> 24) & 0xFF);
    b[1] = static_cast<char>((s >> 16) & 0xFF);
    b[2] = static_cast<char>((s >> 8) & 0xFF);
    b[3] = static_cast<char>(s & 0xFF);
    b[4] = '\0';
    for (int i = 0; i < 4; i++) {
        unsigned char c = static_cast<unsigned char>(b[i]);
        if (c < 32 || c > 126) b[i] = '?';
    }
    return std::string(b, 4);
}

std::string HexId(const uint8_t* b) {
    static const char* h = "0123456789abcdef";
    std::string s;
    s.reserve(32);
    for (int i = 0; i < 16; i++) {
        s.push_back(h[(b[i] >> 4) & 0xF]);
        s.push_back(h[b[i] & 0xF]);
    }
    return s;
}

// Strip trailing NUL / CR / LF / space.
std::string TrimAscii(std::string s) {
    while (!s.empty()) {
        char c = s.back();
        if (c == 0 || c == '\r' || c == '\n' || c == ' ' || c == '\t') s.pop_back();
        else break;
    }
    return s;
}

// Decode UTF-16BE to UTF-8. Rejects lone/invalid surrogates.
bool Utf16BeToUtf8(const uint8_t* d, size_t bytes, std::string& out) {
    if (bytes % 2 != 0) return false;
    size_t n = bytes / 2;
    out.clear();
    for (size_t i = 0; i < n; i++) {
        uint16_t u = (uint16_t(d[i * 2]) << 8) | uint16_t(d[i * 2 + 1]);
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 >= n) return false;
            uint16_t lo = (uint16_t(d[(i + 1) * 2]) << 8) | uint16_t(d[(i + 1) * 2 + 1]);
            if (lo < 0xDC00 || lo > 0xDFFF) return false;
            uint32_t cp = 0x10000u + ((uint32_t(u - 0xD800) << 10) | (lo - 0xDC00));
            i++;
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (u >= 0xDC00 && u <= 0xDFFF) {
            return false;  // lone low surrogate
        } else {
            uint32_t cp = u;
            if (cp < 0x80) {
                out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
                out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        }
    }
    return true;
}

// JSON string escaping. Strictly ASCII-safe: non-ASCII code points are
// emitted as \uXXXX (with surrogate pairs beyond the BMP).
std::string JsonEsc(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"') { o += "\\\""; }
        else if (c == '\\') { o += "\\\\"; }
        else if (c == '\b') { o += "\\b"; }
        else if (c == '\f') { o += "\\f"; }
        else if (c == '\n') { o += "\\n"; }
        else if (c == '\r') { o += "\\r"; }
        else if (c == '\t') { o += "\\t"; }
        else if (c < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            o += buf;
        } else if (c < 0x80) {
            o.push_back(static_cast<char>(c));
        } else {
            // Decode UTF-8 code point (input is produced by our encoder, but
            // be defensive against stray continuation bytes).
            uint32_t cp = 0;
            int extra = 0;
            if ((c & 0xE0) == 0xC0) { cp = c & 0x1Fu; extra = 1; }
            else if ((c & 0xF0) == 0xE0) { cp = c & 0x0Fu; extra = 2; }
            else if ((c & 0xF8) == 0xF0) { cp = c & 0x07u; extra = 3; }
            else {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
                continue;
            }
            if (i + extra >= s.size()) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                o += buf;
                continue;
            }
            for (int k = 1; k <= extra; k++) {
                cp = (cp << 6) | (static_cast<uint32_t>(static_cast<unsigned char>(s[i + k])) & 0x3Fu);
            }
            i += extra;
            if (cp < 0x10000) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
                o += buf;
            } else {
                cp -= 0x10000u;
                uint32_t hi = 0xD800u + (cp >> 10);
                uint32_t lo = 0xDC00u + (cp & 0x3FFu);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "\\u%04x\\u%04x", hi, lo);
                o += buf;
            }
        }
    }
    return o;
}

TagKind KindFromType(uint32_t t) {
    switch (t) {
        case 0x64657363u: return TagKind::TextDescription;        // 'desc'
        case 0x6D6C7563u: return TagKind::MultiLocalizedText;     // 'mluc'
        case 0x74657874u: return TagKind::Text;                   // 'text'
        case 0x75746638u: return TagKind::Utf8Text;               // 'utf8'
        case 0x73696720u: return TagKind::Signature;              // 'sig '
        case 0x63757276u: return TagKind::Curve;                  // 'curv'
        case 0x70617261u: return TagKind::ParametricCurve;        // 'para'
        case 0x58595A20u: return TagKind::Xyz;                    // 'XYZ '
        case 0x6D656173u: return TagKind::Measurement;            // 'meas'
        case 0x76696577u: return TagKind::ViewingConditions;      // 'view'
        case 0x6368726Du: return TagKind::Chromaticity;           // 'chrm'
        case 0x73663332u: return TagKind::S15Fixed16Array;        // 'sf32'
        case 0x6D667431u: return TagKind::Lut8;                   // 'mft1'
        case 0x6D667432u: return TagKind::Lut16;                  // 'mft2'
        case 0x6D414220u: return TagKind::LutAToB;                // 'mAB '
        case 0x6D424120u: return TagKind::LutBToA;                // 'mBA '
        case 0x63696370u: return TagKind::Cicp;                   // 'cicp'
        case 0x6E636C32u: return TagKind::NamedColor2;            // 'ncl2'
        case 0x636C726Fu: return TagKind::ColorantOrder;          // 'clro'
        case 0x636C7274u: return TagKind::ColorantTable;          // 'clrt'
        case 0x64617461u: return TagKind::Data;                   // 'data'
        case 0x6474696Du: return TagKind::DateTime;               // 'dtim'
        case 0x7363726Eu: return TagKind::Screening;              // 'scrn'
        case 0x62666420u: return TagKind::UcrBg;                  // 'bfd '
        default: return TagKind::Unknown;
    }
}

// Tag signatures that count toward hasLut.
bool IsLutTagSig(uint32_t s) {
    switch (s) {
        case 0x41324230u: case 0x41324231u: case 0x41324232u:  // A2B0/1/2
        case 0x42324130u: case 0x42324131u: case 0x42324132u:  // B2A0/1/2
        case 0x67616D74u:                                       // gamt
        case 0x70726530u: case 0x70726531u: case 0x70726532u:  // pre0/1/2
        case 0x70736430u: case 0x70736431u:
        case 0x70736432u: case 0x70736433u:                    // psd0..3
            return true;
        default: return false;
    }
}

// ---------------------------------------------------------------------------
// Per-type decoders. Each receives a reader positioned at tag payload start
// (i.e. after the 8-byte type header). Returns false on structural violation.
// ---------------------------------------------------------------------------

bool DecodeDesc(R& r, TagInfo& tag) {
    uint32_t asciiCount = r.U32();
    if (!r.ok) return false;
    if (asciiCount == 0) { tag.text.clear(); return true; }
    if (asciiCount > r.n - r.p) return false;
    std::string ascii(reinterpret_cast<const char*>(r.d + r.p), asciiCount);
    r.p += asciiCount;
    tag.text = TrimAscii(ascii);
    return true;
}

bool DecodeMluc(R& r, TagInfo& tag) {
    uint32_t recCount = r.U32();
    uint32_t recSize = r.U32();
    if (!r.ok) return false;
    if (recSize != 0 && recSize < 12) return false;
    if (recCount > 1024) return false;

    struct Rec { uint16_t lang, country; uint32_t len, off; };
    std::vector<Rec> recs;
    recs.reserve(recCount);
    for (uint32_t i = 0; i < recCount; i++) {
        Rec rc;
        rc.lang = r.U16();
        rc.country = r.U16();
        rc.len = r.U32();
        rc.off = r.U32();
        if (!r.ok) return false;
        recs.push_back(rc);
    }

    std::string enText;
    std::string firstText;
    for (size_t i = 0; i < recs.size(); i++) {
        const Rec& rc = recs[i];
        if (rc.len > 65536) return false;
        if (static_cast<uint64_t>(rc.off) + rc.len > r.n) return false;
        std::string s;
        if (!Utf16BeToUtf8(r.d + rc.off, rc.len, s)) return false;
        s = TrimAscii(s);
        tag.textAlternates.push_back(s);
        if (i == 0) firstText = s;
        if (rc.lang == 0x656Eu) enText = s;  // 'en'
    }
    tag.text = !enText.empty() ? enText : firstText;
    return true;
}

bool DecodeText(R& r, TagInfo& tag) {
    // ASCII, NUL-terminated; read to end of tag.
    if (r.n - r.p > 65536) return false;
    std::string s(reinterpret_cast<const char*>(r.d + r.p), r.n - r.p);
    tag.text = TrimAscii(s);
    return true;
}

bool DecodeUtf8(R& r, TagInfo& tag) {
    // utf8Type: 8 reserved bytes after the type header, then UTF-8 string.
    if (r.n - r.p < 4) return false;
    r.Skip(4);  // reserved
    if (!r.ok) return false;
    if (r.n - r.p > 65536) return false;
    std::string s(reinterpret_cast<const char*>(r.d + r.p), r.n - r.p);
    tag.text = TrimAscii(s);
    return true;
}

bool DecodeSig(R& r, TagInfo& tag) {
    uint32_t s = r.U32();
    if (!r.ok) return false;
    tag.text = Sig(s);
    return true;
}

bool DecodeData(R& r, TagInfo& tag) {
    uint32_t flag = r.U32();
    if (!r.ok) return false;
    size_t remain = r.n - r.p;
    if (remain > 65536) return false;
    std::string s(reinterpret_cast<const char*>(r.d + r.p), remain);
    if (flag == 0) {
        tag.text = TrimAscii(s);
    } else {
        // Binary: hex-encode for display.
        static const char* h = "0123456789abcdef";
        std::string hex;
        hex.reserve(remain * 2);
        for (size_t i = 0; i < remain; i++) {
            hex.push_back(h[(static_cast<uint8_t>(s[i]) >> 4) & 0xF]);
            hex.push_back(h[static_cast<uint8_t>(s[i]) & 0xF]);
        }
        tag.text = "hex:" + hex;
    }
    return true;
}

bool DecodeDateTime(R& r, TagInfo& tag) {
    uint16_t year = r.U16();
    uint16_t mon = r.U16();
    uint16_t day = r.U16();
    uint16_t hour = r.U16();
    uint16_t min = r.U16();
    uint16_t sec = r.U16();
    if (!r.ok) return false;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u %02u:%02u:%02u",
                  year, mon, day, hour, min, sec);
    tag.text = buf;
    return true;
}

bool DecodeXyz(R& r, TagInfo& tag) {
    double x = r.S15Fixed16();
    double y = r.S15Fixed16();
    double z = r.S15Fixed16();
    if (!r.ok) return false;
    tag.xyz.x = x;
    tag.xyz.y = y;
    tag.xyz.z = z;
    return true;
}

bool DecodeSf32(R& r, TagInfo& tag) {
    size_t count = (r.n - r.p) / 4;
    if (count > 64) return false;
    tag.fixed.reserve(count);
    for (size_t i = 0; i < count; i++) {
        tag.fixed.push_back(r.S15Fixed16());
    }
    return r.ok;
}

bool DecodeCicp(R& r, TagInfo& tag) {
    if (r.n - r.p < 4) return false;
    tag.cicp.colourPrimaries = r.U8();
    tag.cicp.transferCharacteristics = r.U8();
    tag.cicp.matrixCoefficients = r.U8();
    tag.cicp.videoFullRangeFlag = r.U8();
    return r.ok;
}

bool DecodeCurv(R& r, TagInfo& tag) {
    uint32_t count = r.U32();
    if (!r.ok) return false;
    tag.curve.parametric = false;
    tag.curve.count = count;
    if (count == 0) {
        tag.curve.identity = true;
        return true;
    }
    if (count > 4096) return false;
    if (count == 1) {
        uint16_t g = r.U16();
        if (!r.ok) return false;
        tag.curve.gamma = static_cast<double>(g) / 256.0;
        return true;
    }
    // Lookup table. ICC.1 specifies u16 entries, but some legacy writers emit
    // u8 entries; accept the width the remaining payload actually supports.
    const size_t remain = r.n - r.p;
    const bool sixteen = remain >= static_cast<size_t>(count) * 2;
    if (!sixteen && remain < count) return false;
    tag.curve.table16Bit = sixteen;
    tag.curve.table.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        if (sixteen) {
            uint16_t v = r.U16();
            if (!r.ok) return false;
            tag.curve.table.push_back(static_cast<double>(v) / 65535.0);
        } else {
            uint8_t v = r.U8();
            if (!r.ok) return false;
            tag.curve.table.push_back(static_cast<double>(v) / 255.0);
        }
    }
    return true;
}

bool DecodePara(R& r, TagInfo& tag) {
    uint16_t func = r.U16();
    uint16_t reserved = r.U16();
    (void)reserved;
    if (!r.ok) return false;
    static const int kParamCount[5] = {1, 3, 4, 5, 7};
    if (func > 4) return false;
    int need = kParamCount[func];
    tag.curve.parametric = true;
    tag.curve.functionType = func;
    tag.curve.count = need;
    tag.curve.parameters.reserve(need);
    for (int i = 0; i < need; i++) {
        tag.curve.parameters.push_back(r.S15Fixed16());
    }
    return r.ok;
}

bool DecodeChrm(R& r, TagInfo& tag) {
    uint16_t channels = r.U16();
    uint16_t vendor = r.U16();
    if (!r.ok) return false;
    if (channels > 16) return false;
    tag.u16.push_back(channels);
    tag.u16.push_back(vendor);
    for (uint16_t i = 0; i < channels; i++) {
        uint16_t x = r.U16();
        uint16_t y = r.U16();
        if (!r.ok) return false;
        tag.u16.push_back(x);
        tag.u16.push_back(y);
    }
    return true;
}

bool DecodeMeas(R& r, TagInfo& tag) {
    uint32_t observer = r.U32();
    double x = r.S15Fixed16();
    double y = r.S15Fixed16();
    double z = r.S15Fixed16();
    uint32_t geometry = r.U32();
    uint32_t flare = r.U32();
    if (!r.ok) return false;
    tag.xyz.x = x; tag.xyz.y = y; tag.xyz.z = z;
    tag.u16.push_back(static_cast<uint16_t>(observer));
    tag.u16.push_back(static_cast<uint16_t>(geometry));
    tag.u16.push_back(static_cast<uint16_t>(flare));
    return true;
}

bool DecodeView(R& r, TagInfo& tag) {
    double wx = r.S15Fixed16();
    double wy = r.S15Fixed16();
    double wz = r.S15Fixed16();
    double sx = r.S15Fixed16();
    double sy = r.S15Fixed16();
    double sz = r.S15Fixed16();
    uint32_t illum = r.U32();
    if (!r.ok) return false;
    tag.xyz.x = wx; tag.xyz.y = wy; tag.xyz.z = wz;
    tag.u16.push_back(static_cast<uint16_t>(illum));
    tag.u16.push_back(static_cast<uint16_t>(sx * 1000));
    tag.u16.push_back(static_cast<uint16_t>(sy * 1000));
    tag.u16.push_back(static_cast<uint16_t>(sz * 1000));
    return true;
}

bool DecodeMft(R& r, TagInfo& tag, bool is16) {
    // Header: 8 (type+reserved) already consumed by caller.
    // At r.p: inCh(u8), outCh(u8), grid(u8), reserved(u8), then 9 s15Fixed16 matrix.
    uint8_t inCh = r.U8();
    uint8_t outCh = r.U8();
    uint8_t grid = r.U8();
    uint8_t pad = r.U8();
    (void)pad;
    if (!r.ok) return false;
    tag.lut.present = true;
    tag.lut.isMatrixShaper = true;
    tag.lut.inputChannels = inCh;
    tag.lut.outputChannels = outCh;
    tag.lut.gridPoints = grid;

    // grid == 1 is legal: matrix-shaper profiles (sRGB, Lab abstract) carry a
    // degenerate 1-entry CLUT and rely on the matrix + TRCs. Reject only 0.
    if (inCh == 0 || outCh == 0 || grid < 1) {
        tag.lut.valid = false;
        tag.lut.problem = "bad channel/grid count";
        return false;
    }
    if (inCh > 16 || outCh > 16) {
        tag.lut.valid = false;
        tag.lut.problem = "channel count out of range";
        return false;
    }
    // Fixed part: 4 channel/grid bytes + 9 s15Fixed16 matrix (36 bytes) = 40
    // bytes after the 8-byte type header. We require at least the fixed
    // header. Variable input tables / CLUT / output tables are NOT byte-counted
    // exactly here: real-world writers (Adobe RSWOP, lcms) lay these out with
    // channel-dependent table widths that we do not interpolate, and the task
    // scope is structural validation only. We do, however, guard against a
    // hostile grid^inCh explosion.
    if (!r.Need(36)) {  // matrix
        tag.lut.valid = false;
        tag.lut.problem = "matrix truncated";
        return false;
    }
    r.p += 36;
    size_t entries = 1;
    for (int i = 0; i < inCh; i++) {
        entries *= grid;
        if (entries > 16u * 1024u * 1024u) {
            tag.lut.valid = false;
            tag.lut.problem = "CLUT grid too large";
            return false;
        }
    }
    (void)is16;
    tag.lut.valid = true;
    return true;
}

bool DecodeMab(R& r, TagInfo& tag, bool /*isAToB*/) {
    // At r.p: inCh(u8), outCh(u8), reserved(u16), then 5 u32 offsets.
    uint8_t inCh = r.U8();
    uint8_t outCh = r.U8();
    uint16_t reserved = r.U16();
    (void)reserved;
    uint32_t offB = r.U32();
    uint32_t offMat = r.U32();
    uint32_t offM = r.U32();
    uint32_t offCLUT = r.U32();
    uint32_t offA = r.U32();
    if (!r.ok) return false;

    tag.lut.present = true;
    tag.lut.isMab = true;
    tag.lut.inputChannels = inCh;
    tag.lut.outputChannels = outCh;

    if (inCh == 0 || outCh == 0 || inCh > 16 || outCh > 16) {
        tag.lut.valid = false;
        tag.lut.problem = "bad channel count";
        return false;
    }
    // Each offset must be 0 (absent) or point within the tag (>= 32).
    uint32_t offsets[5] = {offB, offMat, offM, offCLUT, offA};
    for (int i = 0; i < 5; i++) {
        if (offsets[i] != 0) {
            if (offsets[i] < 32 || static_cast<uint64_t>(offsets[i]) >= r.n) {
                tag.lut.valid = false;
                tag.lut.problem = "LUT offset out of range";
                return false;
            }
        }
    }
    tag.lut.valid = true;
    return true;
}

bool DecodeClro(R& r, TagInfo& tag) {
    uint8_t n = r.U8();
    if (!r.ok) return false;
    if (n > 16) return false;
    tag.u16.push_back(n);
    for (int i = 0; i < n; i++) {
        tag.u16.push_back(r.U8());
    }
    return r.ok;
}

bool DecodeClrt(R& r, TagInfo& tag) {
    uint32_t n = r.U32();
    if (!r.ok) return false;
    if (n > 32) return false;
    tag.u16.push_back(static_cast<uint16_t>(n));
    for (uint32_t i = 0; i < n; i++) {
        // 32-byte name + 3 u16 Lab values.
        if (!r.Need(32 + 6)) return false;
        r.p += 32;  // name (we don't store per-colorant names)
        tag.u16.push_back(r.U16());
        tag.u16.push_back(r.U16());
        tag.u16.push_back(r.U16());
    }
    return true;
}

bool DecodeNcl2(R& r, TagInfo& tag) {
    uint32_t vendorFlags = r.U32();
    uint32_t count = r.U32();
    uint32_t devCoords = r.U32();
    if (!r.ok) return false;
    if (count > 1024 || devCoords > 16) return false;
    tag.u16.push_back(static_cast<uint16_t>(vendorFlags));
    tag.u16.push_back(static_cast<uint16_t>(count));
    tag.u16.push_back(static_cast<uint16_t>(devCoords));
    // Skip prefix(32) + suffix(32).
    if (!r.Need(64)) return false;
    r.p += 64;
    // Each named color: 32-byte root name + devCoords u16.
    for (uint32_t i = 0; i < count; i++) {
        if (!r.Need(32 + static_cast<size_t>(devCoords) * 2)) return false;
        r.p += 32 + static_cast<size_t>(devCoords) * 2;
    }
    return true;
}

bool DecodePseqLike(R& r, TagInfo& tag) {
    uint32_t n = r.U32();
    if (!r.ok) return false;
    if (n > 64) return false;
    tag.u16.push_back(static_cast<uint16_t>(n));
    // Each profile sequence entry is variable length; just bounds-check count.
    return true;
}

// ---------------------------------------------------------------------------
// Colorimetric summary helpers. These are pure functions over already-decoded
// tags; they never touch raw profile bytes again.
// ---------------------------------------------------------------------------

struct KnownSpace {
    const char* name;
    const char* primariesToken;
    double rx, ry, gx, gy, bx, by, wx, wy;
};

struct ParamCurve {
    double g = 1.0, a = 1.0, b = 0.0, c = 0.0, d = 0.0, e = 0.0, f = 0.0;
    bool valid = false;
};

struct NamedCurve {
    const char* token;
    ParamCurve curve;
};

bool Invert3x3(const double m[9], double out[9]) {
    const double a = m[0], b = m[1], c = m[2];
    const double d = m[3], e = m[4], f = m[5];
    const double g = m[6], h = m[7], i = m[8];
    const double A = e * i - f * h;
    const double B = -(d * i - f * g);
    const double C = d * h - e * g;
    const double det = a * A + b * B + c * C;
    if (std::abs(det) < 1e-12) return false;
    const double inv = 1.0 / det;
    out[0] = A * inv;
    out[1] = (c * h - b * i) * inv;
    out[2] = (b * f - c * e) * inv;
    out[3] = B * inv;
    out[4] = (a * i - c * g) * inv;
    out[5] = (c * d - a * f) * inv;
    out[6] = C * inv;
    out[7] = (b * g - a * h) * inv;
    out[8] = (a * e - b * d) * inv;
    return true;
}

void Mul3x3(const double a[9], const double b[9], double out[9]) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            out[i * 3 + j] = a[i * 3 + 0] * b[0 * 3 + j] +
                             a[i * 3 + 1] * b[1 * 3 + j] +
                             a[i * 3 + 2] * b[2 * 3 + j];
        }
    }
}

void Mul3x1(const double m[9], const double v[3], double out[3]) {
    out[0] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2];
    out[1] = m[3] * v[0] + m[4] * v[1] + m[5] * v[2];
    out[2] = m[6] * v[0] + m[7] * v[1] + m[8] * v[2];
}

bool XyzFromXy(double x, double y, double out[3]) {
    if (std::abs(y) < 1e-12) return false;
    out[0] = x / y;
    out[1] = 1.0;
    out[2] = (1.0 - x - y) / y;
    return true;
}

// Bradford cone response, row-major. Values follow lcms2 cmswtpnt.c and
// skcms; the composition must be M^-1 * L * M (not L * M * M^-1).
void BradfordAdapt(const double srcW[3], const double dstW[3], double out[9]) {
    const double M[9] = {
        0.8951, 0.2664, -0.1614,
        -0.7502, 1.7135, 0.0367,
        0.0389, -0.0685, 1.0296
    };
    double coneS[3], coneD[3];
    Mul3x1(M, srcW, coneS);
    Mul3x1(M, dstW, coneD);
    if (std::abs(coneS[0]) < 1e-12 || std::abs(coneS[1]) < 1e-12 ||
        std::abs(coneS[2]) < 1e-12) {
        out[0] = 1.0; out[1] = 0.0; out[2] = 0.0;
        out[3] = 0.0; out[4] = 1.0; out[5] = 0.0;
        out[6] = 0.0; out[7] = 0.0; out[8] = 1.0;
        return;
    }
    const double L[9] = {
        coneD[0] / coneS[0], 0.0, 0.0,
        0.0, coneD[1] / coneS[1], 0.0,
        0.0, 0.0, coneD[2] / coneS[2]
    };
    double LaM[9], Minv[9];
    Mul3x3(L, M, LaM);
    if (!Invert3x3(M, Minv)) {
        out[0] = 1.0; out[1] = 0.0; out[2] = 0.0;
        out[3] = 0.0; out[4] = 1.0; out[5] = 0.0;
        out[6] = 0.0; out[7] = 0.0; out[8] = 1.0;
        return;
    }
    Mul3x3(Minv, LaM, out);
}

// Lindbloom primaries -> XYZ, then Bradford-adapt to ICC PCS D50.
bool PrimariesToXyzD50(const KnownSpace& s, double out[9]) {
    double r[3], g[3], b[3], w[3];
    if (!XyzFromXy(s.rx, s.ry, r) ||
        !XyzFromXy(s.gx, s.gy, g) ||
        !XyzFromXy(s.bx, s.by, b) ||
        !XyzFromXy(s.wx, s.wy, w)) {
        return false;
    }
    const double N[9] = {
        r[0], g[0], b[0],
        r[1], g[1], b[1],
        r[2], g[2], b[2]
    };
    double invN[9];
    if (!Invert3x3(N, invN)) return false;
    const double Sr = invN[0] * w[0] + invN[1] * w[1] + invN[2] * w[2];
    const double Sg = invN[3] * w[0] + invN[4] * w[1] + invN[5] * w[2];
    const double Sb = invN[6] * w[0] + invN[7] * w[1] + invN[8] * w[2];
    const double M[9] = {
        r[0] * Sr, g[0] * Sg, b[0] * Sb,
        r[1] * Sr, g[1] * Sg, b[1] * Sb,
        r[2] * Sr, g[2] * Sg, b[2] * Sb
    };
    const double d50[3] = {0.9642, 1.0, 0.8249};
    double adapt[9];
    BradfordAdapt(w, d50, adapt);
    Mul3x3(adapt, M, out);
    return true;
}

const TagInfo* FindDecodedTag(const ProfileInfo& info, uint32_t sig) {
    for (const TagInfo& t : info.tags) {
        if (t.signature == sig && t.decoded) return &t;
    }
    return nullptr;
}

bool CurveValueEqual(const CurveInfo& a, const CurveInfo& b) {
    if (a.parametric != b.parametric || a.identity != b.identity ||
        a.count != b.count || a.functionType != b.functionType ||
        a.gamma != b.gamma || a.parameters != b.parameters ||
        a.table16Bit != b.table16Bit || a.table != b.table) {
        return false;
    }
    return true;
}

bool CurveToParam(const CurveInfo& c, ParamCurve& p) {
    if (c.parametric) {
        const auto& q = c.parameters;
        switch (c.functionType) {
            case 0:
                if (q.size() < 1) return false;
                p = {q[0], 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, true};
                return true;
            case 1:
                if (q.size() < 3 || std::abs(q[1]) < 1e-12) return false;
                p = {q[0], q[1], q[2], 0.0, -q[2] / q[1], 0.0, 0.0, true};
                return true;
            case 2:
                if (q.size() < 4 || std::abs(q[1]) < 1e-12) return false;
                p = {q[0], q[1], q[2], q[3], -q[2] / q[1], 0.0, 0.0, true};
                return true;
            case 3:
                if (q.size() < 5) return false;
                p = {q[0], q[1], q[2], q[3], q[4], 0.0, 0.0, true};
                return true;
            case 4:
                if (q.size() < 7) return false;
                p = {q[0], q[1], q[2], q[3], q[4], q[5], q[6], true};
                return true;
            default:
                return false;
        }
    }
    if (c.identity) {
        p = {1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, true};
        return true;
    }
    if (c.count == 1 && c.gamma > 0.0) {
        p = {c.gamma, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, true};
        return true;
    }
    return false;
}

double CurveEval(const ParamCurve& p, double x) {
    if (x < p.d) return p.c * x + p.f;
    const double base = p.a * x + p.b;
    if (base < 0.0) return 0.0;
    return std::pow(base, p.g) + p.e;
}

bool ClaimNamedCurve(const ParamCurve& p, std::string& token) {
    static const NamedCurve kNamed[] = {
        // ICC parametric sRGB (IEC 61966-2-1).
        {"iec61966-2-1", {2.4, 1.0 / 1.055, 0.055 / 1.055,
                          1.0 / 12.92, 0.04045, 0.0, 0.0, true}},
        // Rec. ITU-R BT.709-6 OETF.
        {"bt709", {1.0 / 0.45, 1.0 / 1.099, 0.099 / 1.099,
                   1.0 / 4.5, 0.08125, 0.0, 0.0, true}},
        {"bt470m", {2.2, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, true}},
        {"bt470bg", {2.8, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, true}},
    };
    static const double kSamples[5] = {0.02, 0.05, 0.18, 0.5, 0.9};
    for (const NamedCurve& n : kNamed) {
        if (std::abs(p.g - n.curve.g) > 2e-4 ||
            std::abs(p.a - n.curve.a) > 2e-4 ||
            std::abs(p.b - n.curve.b) > 2e-4 ||
            std::abs(p.c - n.curve.c) > 2e-4 ||
            std::abs(p.d - n.curve.d) > 2e-4 ||
            std::abs(p.e - n.curve.e) > 2e-4 ||
            std::abs(p.f - n.curve.f) > 2e-4) {
            continue;
        }
        bool same = true;
        for (double x : kSamples) {
            const double a = CurveEval(p, x);
            const double b = CurveEval(n.curve, x);
            if (!std::isfinite(a) || !std::isfinite(b) || std::abs(a - b) > 5e-4) {
                same = false;
                break;
            }
        }
        if (same) {
            token = n.token;
            return true;
        }
    }
    return false;
}

void MatchKnownSpace(ColorimetryInfo& c) {
    static const KnownSpace kKnown[] = {
        {"sRGB",            "bt709",    0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290},
        {"BT.2020",         "bt2020",   0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290},
        {"Display P3",      "smpte432", 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290},
        {"DCI-P3",          "smpte431", 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3140, 0.3510},
        {"Adobe RGB",       "",         0.640, 0.330, 0.210, 0.710, 0.150, 0.060, 0.3127, 0.3290},
        {"ProPhoto RGB",    "",         0.7347, 0.2653, 0.1596, 0.8404, 0.0366, 0.0001, 0.3457, 0.3585},
        {"ColorMatch RGB",  "",         0.630, 0.340, 0.295, 0.605, 0.155, 0.077, 0.3457, 0.3585},
        {"BT.601-625",      "bt470bg",  0.640, 0.330, 0.290, 0.600, 0.150, 0.060, 0.3127, 0.3290},
        {"SMPTE 170M",      "smpte170m",0.630, 0.340, 0.310, 0.595, 0.155, 0.070, 0.3127, 0.3290},
    };
    double best = 1e30;
    const KnownSpace* bestSpace = nullptr;
    for (const KnownSpace& s : kKnown) {
        double ref[9];
        if (!PrimariesToXyzD50(s, ref)) continue;
        double err = 0.0;
        for (int i = 0; i < 9; i++) {
            const double d = std::abs(c.rgbToXyzD50[i] - ref[i]);
            if (d > err) err = d;
        }
        if (err < best) {
            best = err;
            bestSpace = &s;
        }
    }
    c.knownSpaceError = std::isfinite(best) ? best : 0.0;
    if (bestSpace && best <= 2e-3) {
        c.knownSpaceName = bestSpace->name;
        c.primariesToken = bestSpace->primariesToken;
    } else {
        c.note = "custom matrix";
    }
}

const char* PrimariesTokenFromCicp(uint8_t v) {
    switch (v) {
        case 1:  return "bt709";
        case 4:  return "bt470m";
        case 5:  return "bt470bg";
        case 6:  return "smpte170m";
        case 7:  return "smpte240m";
        case 9:  return "bt2020";
        case 11: return "smpte431";
        case 12: return "smpte432";
        default: return "";
    }
}

const char* TransferTokenFromCicp(uint8_t v) {
    switch (v) {
        case 1:  return "bt709";
        case 4:  return "bt470m";
        case 5:  return "bt470bg";
        case 6:  return "smpte170m";
        case 7:  return "smpte240m";
        case 8:  return "linear";
        case 13: return "iec61966-2-1";
        case 14: return "bt2020-10";
        case 15: return "bt2020-12";
        case 16: return "smpte2084";
        case 17: return "smpte428";
        case 18: return "arib-std-b67";
        default: return "";
    }
}

void BuildColorimetry(ProfileInfo& out) {
    ColorimetryInfo& c = out.colorimetry;
    c.hasCicp = out.hasCicp;
    c.cicp = out.cicp;

    const TagInfo* rx = FindDecodedTag(out, 0x7258595Au);  // rXYZ
    const TagInfo* gx = FindDecodedTag(out, 0x6758595Au);  // gXYZ
    const TagInfo* bx = FindDecodedTag(out, 0x6258595Au);  // bXYZ
    if (rx && gx && bx && rx->kind == TagKind::Xyz &&
        gx->kind == TagKind::Xyz && bx->kind == TagKind::Xyz) {
        c.rgbToXyzD50[0] = rx->xyz.x; c.rgbToXyzD50[1] = gx->xyz.x; c.rgbToXyzD50[2] = bx->xyz.x;
        c.rgbToXyzD50[3] = rx->xyz.y; c.rgbToXyzD50[4] = gx->xyz.y; c.rgbToXyzD50[5] = bx->xyz.y;
        c.rgbToXyzD50[6] = rx->xyz.z; c.rgbToXyzD50[7] = gx->xyz.z; c.rgbToXyzD50[8] = bx->xyz.z;
        c.hasRgbToXyzD50 = true;
        MatchKnownSpace(c);
    }

    const TagInfo* wtpt = FindDecodedTag(out, 0x77747074u);  // wtpt
    if (wtpt && wtpt->kind == TagKind::Xyz) {
        c.hasWhitePoint = true;
        c.whitePoint = wtpt->xyz;
    }

    const TagInfo* chad = FindDecodedTag(out, 0x63686164u);  // chad
    if (chad && chad->fixed.size() >= 9) {
        c.hasChad = true;
        for (int i = 0; i < 9; i++) c.chad[i] = chad->fixed[i];
    }

    const TagInfo* rt = FindDecodedTag(out, 0x72545243u);  // rTRC
    const TagInfo* gt = FindDecodedTag(out, 0x67545243u);  // gTRC
    const TagInfo* bt = FindDecodedTag(out, 0x62545243u);  // bTRC
    if (rt && gt && bt && (rt->kind == TagKind::Curve ||
                           rt->kind == TagKind::ParametricCurve) &&
        (gt->kind == TagKind::Curve || gt->kind == TagKind::ParametricCurve) &&
        (bt->kind == TagKind::Curve || bt->kind == TagKind::ParametricCurve)) {
        c.hasRgbCurves = true;
        c.rCurve = rt->curve;
        c.gCurve = gt->curve;
        c.bCurve = bt->curve;
        c.curvesIdentical = CurveValueEqual(c.rCurve, c.gCurve) &&
                            CurveValueEqual(c.gCurve, c.bCurve);
        if (c.curvesIdentical) {
            ParamCurve p;
            if (CurveToParam(c.rCurve, p)) {
                if (!ClaimNamedCurve(p, c.transferToken)) {
                    if (c.rCurve.identity) {
                        c.transferToken = "linear";
                    } else if (c.rCurve.count == 1 && c.rCurve.gamma > 0.0) {
                        // curv count==1 is u8Fixed8; 2.2 quantizes to 2.19921875.
                        const double q = 1.0 / 256.0;
                        if (std::abs(c.rCurve.gamma - 2.2) <= q) c.transferToken = "bt470m";
                        else if (std::abs(c.rCurve.gamma - 2.8) <= q) c.transferToken = "bt470bg";
                    }
                }
            } else if (!c.rCurve.table.empty()) {
                c.note = "table TRC";
            }
        } else {
            c.note = "non-neutral TRC";
        }
    }

    // ICC metrics win over CICP when both exist; CICP is the fallback.
    if (c.primariesToken.empty() && c.hasCicp) {
        c.primariesToken = PrimariesTokenFromCicp(c.cicp.colourPrimaries);
    }
    if (!c.hasRgbCurves && c.transferToken.empty() && c.hasCicp) {
        c.transferToken = TransferTokenFromCicp(c.cicp.transferCharacteristics);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

ParseResult Parse(const uint8_t* data, size_t size, ProfileInfo& out, size_t maxBytes) {
    ParseResult res;
    out = ProfileInfo{};

    try {
        // --- size guards ---
        if (size < 128) {
            res.status = Status::HeaderTooSmall;
            res.message = "profile smaller than 128-byte header";
            return res;
        }
        R h(data, size);
        uint32_t declaredSize = h.U32();
        out.declaredSize = declaredSize;
        out.cmmType = h.U32();
        uint32_t versionRaw = h.U32();
        out.versionRaw = versionRaw;
        out.versionMajor = static_cast<uint8_t>((versionRaw >> 24) & 0xFF);
        // ICC stores version as BCD: byte 8 major, high nibble of byte 9 minor,
        // low nibble revision. 04 40 00 00 must read 4.4, not 4.64.
        out.versionMinor = static_cast<uint8_t>(((versionRaw >> 16) & 0xF0) >> 4);
        out.klass = static_cast<ProfileClass>(h.U32());
        out.dataColorSpace = static_cast<ColorSpace>(h.U32());
        out.pcs = static_cast<ColorSpace>(h.U32());
        h.Skip(12);  // date y/m/d/h/m/s
        uint32_t acsp = h.U32();
        out.platformSignature = h.U32();
        out.flags = h.U32();
        out.manufacturer = h.U32();
        out.model = h.U32();
        out.attributes = h.U64();
        out.renderingIntent = h.U32();
        out.illuminantX = h.S15Fixed16();
        out.illuminantY = h.S15Fixed16();
        out.illuminantZ = h.S15Fixed16();
        out.creator = h.U32();

        uint8_t profileId[16];
        for (int i = 0; i < 16; i++) profileId[i] = h.U8();
        bool idAllZero = true;
        for (int i = 0; i < 16; i++) if (profileId[i] != 0) { idAllZero = false; break; }
        if (!idAllZero) out.profileIdHex = HexId(profileId);

        if (!h.ok) {
            res.status = Status::HeaderTooSmall;
            res.message = "header read overflow";
            return res;
        }
        if (acsp != 0x61637370u) {  // 'acsp'
            res.status = Status::NotIcc;
            res.message = "missing 'acsp' signature";
            return res;
        }
        if (declaredSize > maxBytes) {
            res.status = Status::ProfileTooLarge;
            res.message = "declared profile size exceeds byte budget";
            return res;
        }
        if (declaredSize != size) {
            res.status = Status::SizeMismatch;
            res.message = "declared size does not match supplied size";
            return res;
        }
        if (out.versionMajor != 2 && out.versionMajor != 4) {
            res.status = Status::VersionUnsupported;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "unsupported ICC major version %u",
                          out.versionMajor);
            res.message = buf;
            return res;
        }

        // --- tag table ---
        R tr(data, size);
        tr.p = 128;
        uint32_t tagCount = tr.U32();
        out.tagCount = tagCount;
        if (!tr.ok) {
            res.status = Status::TagTableTruncated;
            res.message = "cannot read tag count";
            return res;
        }
        if (tagCount > 0) {
            // Each entry is 12 bytes. Bounds-check the whole table.
            uint64_t tableBytes = static_cast<uint64_t>(tagCount) * 12u;
            if (static_cast<uint64_t>(128) + 4 + tableBytes > size) {
                res.status = Status::TagTableTruncated;
                res.message = "tag table overruns profile";
                return res;
            }
            out.tags.reserve(tagCount);
            for (uint32_t i = 0; i < tagCount; i++) {
                TagInfo ti;
                ti.signature = tr.U32();
                ti.offset = tr.U32();
                ti.size = tr.U32();
                if (!tr.ok) {
                    res.status = Status::TagTableTruncated;
                    res.message = "tag entry read overflow";
                    return res;
                }
                out.tags.push_back(ti);
            }
        }

        // --- bounds + overlap check ---
        for (size_t i = 0; i < out.tags.size(); i++) {
            TagInfo& ti = out.tags[i];
            if (static_cast<uint64_t>(ti.offset) + ti.size > size) {
                res.status = Status::TagOutOfBounds;
                char buf[80];
                std::snprintf(buf, sizeof(buf), "tag '%s' data outside profile",
                              Sig(ti.signature).c_str());
                res.message = buf;
                return res;
            }
            for (size_t j = i + 1; j < out.tags.size(); j++) {
                TagInfo& tj = out.tags[j];
                bool overlap =
                    static_cast<uint64_t>(ti.offset) < static_cast<uint64_t>(tj.offset) + tj.size &&
                    static_cast<uint64_t>(tj.offset) < static_cast<uint64_t>(ti.offset) + ti.size;
                if (!overlap) continue;
                // ICC allows two tags to share the exact same data range
                // (identical offset AND size, e.g. rTRC/gTRC/bTRC pointing at
                // one gamma curve). That is legal sharing, not a conflict.
                bool identical = (ti.offset == tj.offset) && (ti.size == tj.size);
                if (identical) {
                    char wbuf[96];
                    std::snprintf(wbuf, sizeof(wbuf),
                                  "tags '%s' and '%s' share payload",
                                  Sig(ti.signature).c_str(), Sig(tj.signature).c_str());
                    out.warnings.push_back(wbuf);
                    continue;
                }
                // Partial overlap (ranges intersect but are not identical) is
                // a genuine structural conflict.
                res.status = Status::TagOverlap;
                char buf[96];
                std::snprintf(buf, sizeof(buf), "tags '%s' and '%s' overlap",
                              Sig(ti.signature).c_str(), Sig(tj.signature).c_str());
                res.message = buf;
                return res;
            }
        }

        // --- duplicate signature warning ---
        {
            std::vector<uint32_t> seen;
            for (size_t i = 0; i < out.tags.size(); i++) {
                uint32_t s = out.tags[i].signature;
                for (uint32_t k : seen) {
                    if (k == s) {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "duplicate tag signature '%s'",
                                      Sig(s).c_str());
                        out.warnings.push_back(buf);
                        break;
                    }
                }
                seen.push_back(s);
            }
        }

        // --- per-tag decode ---
        Status pending = Status::Ok;
        for (size_t i = 0; i < out.tags.size(); i++) {
            TagInfo& ti = out.tags[i];
            ti.boundsOk = true;

            if (ti.size < 8) {
                ti.decoded = false;
                ti.rawOnly = false;
                ti.note = "tag smaller than 8-byte type header";
                if (pending == Status::Ok) pending = Status::TagDataTruncated;
                res.tagFailed++;
                continue;
            }

            R tr2(data + ti.offset, ti.size);
            uint32_t typeSig = tr2.U32();
            (void)tr2.U32();  // reserved
            ti.declaredType = typeSig;
            ti.kind = KindFromType(typeSig);

            // 'pseq' / 'psid' have no TagKind enum entry (frozen header) but are
            // well-known v4 profile-sequence types; decode them lightly.
            if (typeSig == 0x70736571u || typeSig == 0x70736964u) {  // pseq / psid
                if (DecodePseqLike(tr2, ti)) {
                    ti.decoded = true;
                    ti.rawOnly = false;
                    res.tagDecoded++;
                    continue;
                }
                ti.decoded = false;
                ti.note = "malformed profile sequence";
                if (pending == Status::Ok) pending = Status::MalformedTag;
                res.tagFailed++;
                continue;
            }

            if (ti.kind == TagKind::Unknown) {
                ti.rawOnly = true;
                ti.decoded = false;
                res.tagRawOnly++;
                continue;
            }

            bool ok = false;
            switch (ti.kind) {
                case TagKind::TextDescription:     ok = DecodeDesc(tr2, ti); break;
                case TagKind::MultiLocalizedText:  ok = DecodeMluc(tr2, ti); break;
                case TagKind::Text:                ok = DecodeText(tr2, ti); break;
                case TagKind::Utf8Text:            ok = DecodeUtf8(tr2, ti); break;
                case TagKind::Signature:           ok = DecodeSig(tr2, ti); break;
                case TagKind::Data:                ok = DecodeData(tr2, ti); break;
                case TagKind::DateTime:            ok = DecodeDateTime(tr2, ti); break;
                case TagKind::Xyz:                 ok = DecodeXyz(tr2, ti); break;
                case TagKind::S15Fixed16Array:     ok = DecodeSf32(tr2, ti); break;
                case TagKind::Curve:               ok = DecodeCurv(tr2, ti); break;
                case TagKind::ParametricCurve:     ok = DecodePara(tr2, ti); break;
                case TagKind::Chromaticity:        ok = DecodeChrm(tr2, ti); break;
                case TagKind::Measurement:         ok = DecodeMeas(tr2, ti); break;
                case TagKind::ViewingConditions:   ok = DecodeView(tr2, ti); break;
                case TagKind::Lut8:                ok = DecodeMft(tr2, ti, false); break;
                case TagKind::Lut16:               ok = DecodeMft(tr2, ti, true); break;
                case TagKind::LutAToB:             ok = DecodeMab(tr2, ti, true); break;
                case TagKind::LutBToA:             ok = DecodeMab(tr2, ti, false); break;
                case TagKind::Cicp:                 ok = DecodeCicp(tr2, ti); break;
                case TagKind::ColorantOrder:       ok = DecodeClro(tr2, ti); break;
                case TagKind::ColorantTable:       ok = DecodeClrt(tr2, ti); break;
                case TagKind::NamedColor2:         ok = DecodeNcl2(tr2, ti); break;
                case TagKind::Screening:
                case TagKind::UcrBg:
                    // Recognised but not deep-parsed; keep rawOnly.
                    ti.rawOnly = true;
                    ti.decoded = false;
                    res.tagRawOnly++;
                    ok = true;
                    break;
                default:
                    ti.rawOnly = true;
                    ti.decoded = false;
                    res.tagRawOnly++;
                    ok = true;
                    break;
            }

            if (!ok) {
                ti.decoded = false;
                ti.rawOnly = false;
                if (ti.note.empty()) ti.note = "malformed tag payload";
                if (pending == Status::Ok) pending = Status::MalformedTag;
                res.tagFailed++;
            } else if (!ti.rawOnly) {
                ti.decoded = true;
                res.tagDecoded++;
            }
        }

        // --- derived conveniences ---
        for (const TagInfo& ti : out.tags) {
            // Profile description: v2 uses tag signature 'desc' (type desc);
            // v4 uses tag signature 'desc' with type mluc, and some writers
            // (and this suite) label the v4 description tag 'mluc' directly.
            // Accept either signature, taking the first successfully decoded.
            if (out.description.empty() && ti.decoded &&
                (ti.signature == 0x64657363u || ti.signature == 0x6D6C7563u)) {
                out.description = ti.text;
            }
            if (out.copyright.empty() && ti.signature == 0x63707274u && ti.decoded) {
                out.copyright = ti.text;  // 'cprt'
            }
            if (ti.signature == 0x63686164u && ti.decoded) out.hasChad = true;       // 'chad'
            if (ti.signature == 0x77747074u && ti.decoded) out.hasWhitepoint = true; // 'wtpt'
            if (ti.signature == 0x63696370u && ti.decoded && !out.hasCicp) {        // 'cicp'
                out.hasCicp = true;
                out.cicp = ti.cicp;
            }
            if (IsLutTagSig(ti.signature) && ti.decoded) out.hasLut = true;
            if ((ti.signature == 0x7258595Au || ti.signature == 0x6758595Au ||
                 ti.signature == 0x6258595Au) && ti.decoded) {
                out.hasMatrixColumnTags = true;  // rXYZ/gXYZ/bXYZ
            }
            if ((ti.signature == 0x72545243u || ti.signature == 0x67545243u ||
                 ti.signature == 0x62545243u || ti.signature == 0x6B545243u) && ti.decoded) {
                out.hasMatrixColumnTags = true;  // rTRC/gTRC/bTRC/kTRC
            }
        }
        if (out.klass == ProfileClass::NamedColor) out.isNamedColorProfile = true;
        BuildColorimetry(out);

        // --- final status ---
        if (pending != Status::Ok) {
            res.status = pending;
            if (pending == Status::TagDataTruncated) res.message = "tag payload truncated";
            else res.message = "malformed tag payload";
        } else {
            res.status = Status::Ok;
            res.message = "ok";
        }
        res.usable = (res.status == Status::Ok);
        return res;
    } catch (...) {
        res.status = Status::MalformedTag;
        res.message = "unexpected exception during parse";
        res.usable = false;
        return res;
    }
}

const char* StatusName(Status s) {
    switch (s) {
        case Status::Ok:                  return "Ok";
        case Status::NotIcc:              return "NotIcc";
        case Status::HeaderTooSmall:      return "HeaderTooSmall";
        case Status::ProfileTooLarge:     return "ProfileTooLarge";
        case Status::SizeMismatch:        return "SizeMismatch";
        case Status::VersionUnsupported:  return "VersionUnsupported";
        case Status::TagTableTruncated:   return "TagTableTruncated";
        case Status::TagOutOfBounds:      return "TagOutOfBounds";
        case Status::TagOverlap:          return "TagOverlap";
        case Status::TagDataTruncated:    return "TagDataTruncated";
        case Status::MalformedTag:        return "MalformedTag";
        default:                          return "Unknown";
    }
}

static std::string JNum(double v) {
    if (!std::isfinite(v)) return "0";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    if (std::strcmp(buf, "-0") == 0) return "0";
    return buf;
}

static void AppendCurveJson(std::string& o, const CurveInfo& c) {
    o += "{";
    o += "\"parametric\":"; o += (c.parametric ? "true" : "false");
    o += ",\"identity\":"; o += (c.identity ? "true" : "false");
    o += ",\"count\":"; o += std::to_string(c.count);
    o += ",\"functionType\":"; o += std::to_string(c.functionType);
    o += ",\"gamma\":"; o += JNum(c.gamma);
    o += ",\"parameters\":[";
    for (size_t i = 0; i < c.parameters.size(); i++) {
        if (i) o += ",";
        o += JNum(c.parameters[i]);
    }
    o += "]";
    o += ",\"tableCount\":"; o += std::to_string(c.table.size());
    o += ",\"table16Bit\":"; o += (c.table16Bit ? "true" : "false");
    if (!c.table.empty()) {
        o += ",\"tableFirst\":"; o += JNum(c.table.front());
        o += ",\"tableLast\":"; o += JNum(c.table.back());
        if (c.table.size() <= 64) {
            o += ",\"table\":[";
            for (size_t i = 0; i < c.table.size(); i++) {
                if (i) o += ",";
                o += JNum(c.table[i]);
            }
            o += "]";
        }
    }
    o += "}";
}

static void AppendCicpJson(std::string& o, const CicpInfo& c) {
    o += "{\"colourPrimaries\":"; o += std::to_string(c.colourPrimaries);
    o += ",\"transferCharacteristics\":"; o += std::to_string(c.transferCharacteristics);
    o += ",\"matrixCoefficients\":"; o += std::to_string(c.matrixCoefficients);
    o += ",\"videoFullRangeFlag\":"; o += std::to_string(c.videoFullRangeFlag);
    o += ",\"primariesToken\":\""; o += PrimariesTokenFromCicp(c.colourPrimaries); o += "\"";
    o += ",\"transferToken\":\""; o += TransferTokenFromCicp(c.transferCharacteristics); o += "\"";
    o += "}";
}

static void AppendArr9(std::string& o, const double v[9]) {
    o += "[";
    for (int i = 0; i < 9; i++) {
        if (i) o += ",";
        o += JNum(v[i]);
    }
    o += "]";
}

std::string ToJson(const ProfileInfo& info, const ParseResult& result) {
    std::string o;
    o += "{";
    o += "\"status\":\""; o += StatusName(result.status); o += "\",";
    o += "\"message\":\""; o += JsonEsc(result.message); o += "\",";
    o += "\"usable\":"; o += (result.usable ? "true" : "false"); o += ",";

    char ver[32];
    std::snprintf(ver, sizeof(ver), "\"version\":\"%u.%u\"",
                  info.versionMajor, info.versionMinor);
    o += ver; o += ",";

    o += "\"class\":\""; o += Sig(static_cast<uint32_t>(info.klass)); o += "\",";
    o += "\"dataSpace\":\""; o += Sig(static_cast<uint32_t>(info.dataColorSpace)); o += "\",";
    o += "\"pcs\":\""; o += Sig(static_cast<uint32_t>(info.pcs)); o += "\",";

    char buf[64];
    std::snprintf(buf, sizeof(buf), "\"tagCount\":%u", info.tagCount);
    o += buf; o += ",";
    std::snprintf(buf, sizeof(buf), "\"tagDecoded\":%u", result.tagDecoded);
    o += buf; o += ",";
    std::snprintf(buf, sizeof(buf), "\"tagRawOnly\":%u", result.tagRawOnly);
    o += buf; o += ",";
    std::snprintf(buf, sizeof(buf), "\"tagFailed\":%u", result.tagFailed);
    o += buf; o += ",";

    o += "\"description\":\""; o += JsonEsc(info.description); o += "\",";
    o += "\"copyright\":\""; o += JsonEsc(info.copyright); o += "\",";
    o += "\"hasLut\":"; o += (info.hasLut ? "true" : "false"); o += ",";
    o += "\"hasChad\":"; o += (info.hasChad ? "true" : "false"); o += ",";
    o += "\"hasCicp\":"; o += (info.hasCicp ? "true" : "false"); o += ",";
    o += "\"cicp\":"; AppendCicpJson(o, info.cicp); o += ",";

    const ColorimetryInfo& c = info.colorimetry;
    o += "\"colorimetry\":{";
    o += "\"hasRgbMatrix\":"; o += (c.hasRgbToXyzD50 ? "true" : "false");
    o += ",\"rgbToXyzD50\":"; AppendArr9(o, c.rgbToXyzD50);
    o += ",\"hasWhitePoint\":"; o += (c.hasWhitePoint ? "true" : "false");
    o += ",\"whitePoint\":["; o += JNum(c.whitePoint.x); o += ",";
    o += JNum(c.whitePoint.y); o += ","; o += JNum(c.whitePoint.z); o += "]";
    o += ",\"hasChad\":"; o += (c.hasChad ? "true" : "false");
    o += ",\"chad\":"; AppendArr9(o, c.chad);
    o += ",\"hasRgbCurves\":"; o += (c.hasRgbCurves ? "true" : "false");
    o += ",\"curvesIdentical\":"; o += (c.curvesIdentical ? "true" : "false");
    o += ",\"rCurve\":"; AppendCurveJson(o, c.rCurve);
    o += ",\"gCurve\":"; AppendCurveJson(o, c.gCurve);
    o += ",\"bCurve\":"; AppendCurveJson(o, c.bCurve);
    o += ",\"knownSpace\":\""; o += JsonEsc(c.knownSpaceName); o += "\"";
    o += ",\"primariesToken\":\""; o += JsonEsc(c.primariesToken); o += "\"";
    o += ",\"transferToken\":\""; o += JsonEsc(c.transferToken); o += "\"";
    o += ",\"knownSpaceError\":"; o += JNum(c.knownSpaceError);
    o += ",\"note\":\""; o += JsonEsc(c.note); o += "\"";
    o += "}";

    o += ",\"tags\":[";
    for (size_t i = 0; i < info.tags.size(); i++) {
        const TagInfo& t = info.tags[i];
        if (i) o += ",";
        o += "{";
        o += "\"sig\":\""; o += Sig(t.signature); o += "\",";
        o += "\"type\":\""; o += Sig(t.declaredType); o += "\",";
        std::snprintf(buf, sizeof(buf), "\"off\":%u,\"size\":%u", t.offset, t.size);
        o += buf; o += ",";
        o += "\"kind\":\""; o += Sig(static_cast<uint32_t>(t.kind)); o += "\",";
        o += "\"ok\":"; o += (t.boundsOk && (t.decoded || t.rawOnly) ? "true" : "false"); o += ",";
        o += "\"decoded\":"; o += (t.decoded ? "true" : "false"); o += ",";
        o += "\"rawOnly\":"; o += (t.rawOnly ? "true" : "false"); o += ",";
        o += "\"note\":\""; o += JsonEsc(t.note); o += "\"";
        if (t.decoded) {
            if (t.kind == TagKind::Xyz) {
                o += ",\"xyz\":["; o += JNum(t.xyz.x); o += ",";
                o += JNum(t.xyz.y); o += ","; o += JNum(t.xyz.z); o += "]";
            } else if (t.kind == TagKind::Curve || t.kind == TagKind::ParametricCurve) {
                o += ",\"curve\":"; AppendCurveJson(o, t.curve);
            } else if (t.kind == TagKind::Cicp) {
                o += ",\"cicp\":"; AppendCicpJson(o, t.cicp);
            } else if (!t.fixed.empty() && t.fixed.size() <= 16) {
                o += ",\"fixed\":[";
                for (size_t k = 0; k < t.fixed.size(); k++) {
                    if (k) o += ",";
                    o += JNum(t.fixed[k]);
                }
                o += "]";
            }
        }
        o += "}";
    }
    o += "]}";
    return o;
}

}  // namespace fff::icc
