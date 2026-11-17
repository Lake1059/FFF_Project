// ICC profile structured decoder / validator (v2.x and v4.x).
//
// Scope: structure decoding plus a read-only colorimetric summary. No colour
// conversion (PCS -> device, or ICC -> display) is performed here; the
// renderer keeps its own transfer/primaries pipeline. Callers get a structural
// description plus the D50 matrix / TRC / white point / chad / CICP values that
// ICC actually carries, so they can validate, display, or later feed a
// transform.
//
// Contract:
//   * Parse() never throws and never reads outside [data, data+size).
//   * Unknown / vendor-private tags are NOT an error: their bounds are
//     validated and the raw bytes are left in the file. Only structural
//     violations (bad header, overflowing tag table, out-of-bounds or
//     overlapping tag data, truncated payloads, malformed LUT/curve, invalid
//     text encoding) make a profile unusable.
//   * A profile that fails Parse() must be treated by callers as "ICC present
//     but not identified", never as "ICC ok".
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fff::icc {

// Header field enumerations. Values are the raw 32-bit signatures where the
// spec defines a four-character code, so comparisons against the file bytes
// stay exact.
enum class ProfileClass : std::uint32_t {
    Unknown = 0u,
    Input = 0x73636E72u,        // 'scnr'
    Display = 0x6D6E7472u,      // 'mntr'
    Output = 0x70727472u,       // 'prtr'
    DeviceLink = 0x6C696E6Bu,   // 'link'
    ColorSpace = 0x73706163u,   // 'spac'
    Abstract = 0x61627374u,     // 'abst'
    NamedColor = 0x6E6D636Cu,   // 'nmcl'
};

enum class ColorSpace : std::uint32_t {
    Unknown = 0u,
    XYZ = 0x58595A20u,          // 'XYZ '
    Lab = 0x4C616220u,          // 'Lab '
    Luv = 0x4C757620u,          // 'Luv '
    YCbCr = 0x59436272u,        // 'YCbr'
    Yxy = 0x59787920u,          // 'Yxy '
    RGB = 0x52474220u,          // 'RGB '
    Gray = 0x47524159u,         // 'GRAY'
    HSV = 0x48535620u,          // 'HSV '
    HLS = 0x484C5320u,          // 'HLS '
    CMYK = 0x434D594Bu,         // 'CMYK'
    CMY = 0x434D5920u,          // 'CMY '
};

enum class Platform : std::uint32_t {
    Unknown = 0u,
    Apple = 0x4150504Cu,        // 'APPL'
    Microsoft = 0x4D534654u,    // 'MSFT'
    SiliconGraphics = 0x53474920u,  // 'SGI '
    Sun = 0x53554E57u,          // 'SUNW'
};

// What the tag table entry's type signature means to us. Decoded tags have
// their payload parsed into the dedicated structures below; RawOnly tags are
// bounds-checked but otherwise opaque (unknown / private / not worth decoding).
enum class TagKind : std::uint32_t {
    Unknown = 0u,
    TextDescription = 0x64657363u,   // 'desc' (v2)
    MultiLocalizedText = 0x6D6C7563u, // 'mluc' (v4)
    Text = 0x74657874u,              // 'text'
    Utf8Text = 0x75746638u,          // 'utf8'
    Signature = 0x73696720u,         // 'sig '
    Curve = 0x63757276u,             // 'curv'
    ParametricCurve = 0x70617261u,   // 'para'
    Xyz = 0x58595A20u,               // 'XYZ '
    Measurement = 0x6D656173u,       // 'meas'
    ViewingConditions = 0x76696577u, // 'view'
    Chromaticity = 0x6368726Du,      // 'chrm'
    S15Fixed16Array = 0x73663332u,   // 'sf32' (chad / lumi)
    Lut8 = 0x6D667431u,              // 'mft1'
    Lut16 = 0x6D667432u,             // 'mft2'
    LutAToB = 0x6D414220u,            // 'mAB '
    LutBToA = 0x6D424120u,           // 'mBA '
    Cicp = 0x63696370u,              // 'cicp' (ICC.1:2022)
    NamedColor2 = 0x6E636C32u,       // 'ncl2'
    ColorantOrder = 0x636C726Fu,     // 'clro'
    ColorantTable = 0x636C7274u,     // 'clrt'
    Data = 0x64617461u,              // 'data'
    DateTime = 0x6474696Du,          // 'dtim'
    Screening = 0x7363726Eu,         // 'scrn'
    UcrBg = 0x62666420u,             // 'bfd '
};

struct CicpInfo {
    std::uint8_t colourPrimaries = 0;
    std::uint8_t transferCharacteristics = 0;
    std::uint8_t matrixCoefficients = 0;
    std::uint8_t videoFullRangeFlag = 0;
};

struct CurveInfo {
    bool parametric = false;
    // 'curv': count == 0 -> identity, count == 1 -> gamma stored as u16Fixed8,
    // otherwise a lookup table of `count` entries.
    std::uint32_t count = 0;
    double gamma = 0.0;
    // 'para': function type 0..4 and its parameters (g, a, b, c, d, e, f).
    std::uint16_t functionType = 0;
    std::vector<double> parameters;
    // 'curv' lookup table, normalized to 0..1. Some legacy writers emit 8-bit
    // entries even though ICC.1 specifies 16-bit; the parser accepts both and
    // records which width it actually saw.
    std::vector<double> table;
    bool table16Bit = false;
    bool identity = false;
};

struct LutInfo {
    bool present = false;
    bool isMatrixShaper = false;   // mft1/mft2
    bool isMab = false;            // mAB / mBA
    std::uint8_t inputChannels = 0;
    std::uint8_t outputChannels = 0;
    std::uint8_t gridPoints = 0;
    bool valid = false;
    std::string problem;
};

struct XyzInfo {
    double x = 0.0, y = 0.0, z = 0.0;
};

// Read-only colorimetric summary derived from the tags. This is not a
// transform: it reports what the profile says (D50 RGB->XYZ matrix, TRC
// curves, white point, chad and CICP) and whether that matrix matches a
// standard space we know how to name.
struct ColorimetryInfo {
    bool hasRgbToXyzD50 = false;
    double rgbToXyzD50[9] = {};       // row-major; columns are rXYZ/gXYZ/bXYZ

    bool hasWhitePoint = false;
    XyzInfo whitePoint;               // wtpt (PCS-relative; normally D50)

    bool hasChad = false;
    double chad[9] = {};              // row-major 3x3, sf32 'chad'

    bool hasRgbCurves = false;
    CurveInfo rCurve, gCurve, bCurve;
    bool curvesIdentical = false;     // all three TRCs byte-equivalent in value

    bool hasCicp = false;
    CicpInfo cicp;

    std::string knownSpaceName;       // "sRGB", "Display P3", "Adobe RGB", ...
    std::string primariesToken;       // "bt709", "smpte432", "bt2020", ...
    std::string transferToken;        // "iec61966-2-1", "bt709", ...
    double knownSpaceError = 0.0;     // max |measured - theoretical| (9 entries)
    std::string note;                 // e.g. "custom matrix", "non-neutral TRC"
};

struct TagInfo {
    std::uint32_t signature = 0;      // e.g. 'wtpt', 'A2B0', vendor code
    std::uint32_t declaredType = 0;   // type signature from the tag data
    std::uint32_t offset = 0;
    std::uint32_t size = 0;
    TagKind kind = TagKind::Unknown;
    bool boundsOk = false;
    bool decoded = false;             // structure parsed into a typed payload
    bool rawOnly = false;             // kept opaque on purpose
    std::string note;                 // failure reason or decode remark
    // Typed payloads, filled only when decoded == true.
    std::string text;                 // desc / mluc / text / utf8 / sig / data
    std::vector<std::string> textAlternates;  // mluc locales
    XyzInfo xyz;                      // XYZ  ("wtpt" etc.)
    CurveInfo curve;                  // curv / para
    LutInfo lut;                      // mft1 / mft2 / mAB / mBA
    CicpInfo cicp;                    // cicp
    std::vector<double> fixed;        // sf32 (chad 3x3, lumi 1, etc.)
    std::vector<std::uint16_t> u16;   // chrm / view / meas / clro / clrt scalars
};

struct ProfileInfo {
    // --- raw header ---
    std::uint32_t declaredSize = 0;
    std::uint32_t cmmType = 0;            // 'ADBE', 'APPL', ... or 0
    std::uint32_t versionRaw = 0;         // full 32-bit version field
    std::uint8_t versionMajor = 0;        // 2 or 4
    std::uint8_t versionMinor = 0;
    ProfileClass klass = ProfileClass::Unknown;
    ColorSpace dataColorSpace = ColorSpace::Unknown;
    ColorSpace pcs = ColorSpace::Unknown;
    std::uint32_t platformSignature = 0;
    std::uint32_t flags = 0;
    std::uint32_t manufacturer = 0;
    std::uint32_t model = 0;
    std::uint64_t attributes = 0;
    std::uint32_t renderingIntent = 0;
    double illuminantX = 0.0, illuminantY = 0.0, illuminantZ = 0.0;
    std::uint32_t creator = 0;
    std::string profileIdHex;             // 32 hex chars, empty when absent

    // --- tag table ---
    std::uint32_t tagCount = 0;
    std::vector<TagInfo> tags;

    // --- derived conveniences (filled when the corresponding tag decoded) ---
    std::string description;              // 'desc' (v2) or 'mluc' (v4)
    std::string copyright;                // 'cprt'
    bool hasMatrixColumnTags = false;     // rXYZ/gXYZ/bXYZ or rTRC/gTRC/bTRC
    bool hasChad = false;
    bool hasWhitepoint = false;
    bool hasLut = false;                  // any A2B*/B2A*/gamt/pre*/psd*
    bool isNamedColorProfile = false;

    // Derived colorimetric summary. Filled only when the corresponding tags
    // decoded successfully; an empty summary is not an error.
    ColorimetryInfo colorimetry;
    bool hasCicp = false;
    CicpInfo cicp;                        // first decoded 'cicp' tag

    std::vector<std::string> warnings;     // non-fatal remarks (duplicates, odd tags)
};

enum class Status {
    Ok = 0,
    NotIcc,               // missing 'acsp' signature at 36..39
    HeaderTooSmall,       // fewer than 128 bytes
    ProfileTooLarge,      // declared size exceeds the caller's byte budget
    SizeMismatch,         // declared size != supplied size
    VersionUnsupported,   // major version outside {2,4}
    TagTableTruncated,    // tag count / table runs past the file
    TagOutOfBounds,       // tag data outside the profile
    TagOverlap,           // two tags claim overlapping payload ranges
    TagDataTruncated,     // declared tag size smaller than its type header
    MalformedTag,         // structural violation inside a decoded tag
};

struct ParseResult {
    Status status = Status::Ok;
    std::string message;
    bool usable = false;   // true only when status == Ok
    std::uint32_t tagDecoded = 0;
    std::uint32_t tagRawOnly = 0;
    std::uint32_t tagFailed = 0;
};

// Byte ceiling guards against a hostile declared size. 8 MiB is far above any
// real-world display profile (sRGB ~ 3 KiB, large CMYK ~ 500 KiB).
constexpr std::size_t kDefaultMaxProfileBytes = 8u * 1024u * 1024u;

// Parse `size` bytes at `data`. Never throws, never reads out of range.
ParseResult Parse(const std::uint8_t* data, std::size_t size, ProfileInfo& out,
                  std::size_t maxBytes = kDefaultMaxProfileBytes);

// Single-line JSON projection used by the appended C export. Includes the
// header summary, the per-tag table and the parse status; strictly ASCII-safe.
std::string ToJson(const ProfileInfo& info, const ParseResult& result);

const char* StatusName(Status status);

}  // namespace fff::icc
