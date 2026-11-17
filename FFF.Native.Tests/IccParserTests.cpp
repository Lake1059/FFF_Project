// ICC parser fixture-driven test skeleton.
//
// Reads <fixture-dir>/manifest.json, parses every listed profile with
// fff::icc::Parse(), and asserts the expectations recorded by
// FFF.Native.Tests/icc/make_icc_fixtures.py. Prints one PASS/FAIL line per
// check and a final "checks=N failures=M" summary; exits 1 on any failure.
//
// Build (see run_icc_parser_tests.py):
//   cl /std:c++17 /EHsc /I ..\FFF.Native IccParserTests.cpp ^
//      ..\FFF.Native\3FP\Core\IccProfileParser.cpp /Fe:icc_parser_tests.exe
//
// Expectations live in the manifest only -- this file is deliberately generic.

#include "3FP/Core/IccProfileParser.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// minimal JSON reader (enough for the manifest the generator writes)
// ---------------------------------------------------------------------------
struct JVal {
    enum Type { Nul, Bool, Num, Str, Arr, Obj } type = Nul;
    bool b = false;
    double num = 0.0;
    std::string str;
    std::vector<JVal> arr;
    std::map<std::string, JVal> obj;

    const JVal* find(const char* key) const {
        auto it = obj.find(key);
        return it == obj.end() ? nullptr : &it->second;
    }
};

struct JParser {
    const char* p = nullptr;
    const char* end = nullptr;
    bool fail = false;

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    char peek() { ws(); return p < end ? *p : '\0'; }

    std::string parseString() {
        ws();
        if (p < end && *p == '"') ++p;
        std::string out;
        while (p < end && *p != '"') {
            if (*p == '\\' && p + 1 < end) {
                ++p;
                if (*p == 'n') out += '\n';
                else if (*p == 't') out += '\t';
                else out += *p;
                ++p;
            } else {
                out += *p++;
            }
        }
        if (p < end) ++p;
        return out;
    }

    JVal parse() {
        char c = peek();
        JVal v;
        if (c == '{') {
            v.type = JVal::Obj;
            ++p;
            while (peek() != '}' && !fail) {
                std::string key = parseString();
                if (fail) break;
                if (peek() == ':') ++p;
                v.obj[key] = parse();
                if (fail) break;
                if (peek() == ',') { ++p; continue; }
                break;
            }
            if (peek() == '}') ++p;
            return v;
        }
        if (c == '[') {
            v.type = JVal::Arr;
            ++p;
            while (peek() != ']' && !fail) {
                v.arr.push_back(parse());
                if (fail) break;
                if (peek() == ',') { ++p; continue; }
                break;
            }
            if (peek() == ']') ++p;
            return v;
        }
        if (c == '"') { v.type = JVal::Str; v.str = parseString(); return v; }
        if (c == 't') { v.type = JVal::Bool; v.b = true; p += 4; return v; }
        if (c == 'f') { v.type = JVal::Bool; v.b = false; p += 5; return v; }
        if (c == 'n') { p += 4; return v; }
        v.type = JVal::Num;
        const char* s = p;
        while (p < end && *p != '}' && *p != ',' && *p != ']' &&
               *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') ++p;
        v.num = std::strtod(std::string(s, p).c_str(), nullptr);
        return v;
    }
};

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::uint32_t sig4(const std::string& s) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4 && i < (int)s.size(); ++i) v = (v << 8) | (unsigned char)s[i];
    return v;
}

// TagInfo fields hold raw big-endian fourcc values; render them big-endian.
std::string sigStr(std::uint32_t v) {
    char buf[5];
    buf[0] = static_cast<char>((v >> 24) & 0xFF);
    buf[1] = static_cast<char>((v >> 16) & 0xFF);
    buf[2] = static_cast<char>((v >> 8) & 0xFF);
    buf[3] = static_cast<char>(v & 0xFF);
    buf[4] = '\0';
    return buf;
}

int g_checks = 0;
int g_failures = 0;

void fail(const std::string& file, const std::string& what,
          const std::string& expected, const std::string& actual) {
    ++g_failures;
    std::printf("FAIL %s %s expected=%s actual=%s\n",
                file.c_str(), what.c_str(), expected.c_str(), actual.c_str());
}

std::string b2s(bool v) { return v ? "true" : "false"; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: IccParserTests <fixture-dir>\n");
        return 2;
    }
    const std::string dir = argv[1];

    const std::string manifestText = readFile(dir + "/manifest.json");
    JParser jp{manifestText.data(), manifestText.data() + manifestText.size()};
    JVal root = jp.parse();
    if (jp.fail || root.type != JVal::Obj || !root.find("samples")) {
        std::printf("FAIL manifest unreadable in %s\n", dir.c_str());
        return 2;
    }

    for (const JVal& sample : root.find("samples")->arr) {
        const std::string file = sample.find("file")->str;
        const std::string path = dir + "/" + file;
        const std::string bytes = readFile(path);

        fff::icc::ProfileInfo info;
        const fff::icc::ParseResult result = fff::icc::Parse(
            reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), info);

        const JVal* expect = sample.find("expect");

        if (const JVal* st = expect->find("status")) {
            ++g_checks;
            const std::string actual = fff::icc::StatusName(result.status);
            if (actual != st->str) fail(file, "status", st->str, actual);
        }
        if (const JVal* us = expect->find("usable")) {
            ++g_checks;
            if (result.usable != us->b) fail(file, "usable", b2s(us->b), b2s(result.usable));
        }
        if (const JVal* dn = expect->find("descriptionNonEmpty")) {
            ++g_checks;
            const bool actual = !info.description.empty();
            if (actual != dn->b) fail(file, "descriptionNonEmpty", b2s(dn->b), b2s(actual));
        }
        if (const JVal* hl = expect->find("hasLut")) {
            ++g_checks;
            if (info.hasLut != hl->b) fail(file, "hasLut", b2s(hl->b), b2s(info.hasLut));
        }
        if (const JVal* hc = expect->find("hasChad")) {
            ++g_checks;
            if (info.hasChad != hc->b) fail(file, "hasChad", b2s(hc->b), b2s(info.hasChad));
        }
        if (const JVal* hm = expect->find("hasRgbMatrix")) {
            ++g_checks;
            if (info.colorimetry.hasRgbToXyzD50 != hm->b)
                fail(file, "hasRgbMatrix", b2s(hm->b), b2s(info.colorimetry.hasRgbToXyzD50));
        }
        if (const JVal* hw = expect->find("hasWhitePoint")) {
            ++g_checks;
            if (info.colorimetry.hasWhitePoint != hw->b)
                fail(file, "hasWhitePoint", b2s(hw->b), b2s(info.colorimetry.hasWhitePoint));
        }
        if (const JVal* hr = expect->find("hasRgbCurves")) {
            ++g_checks;
            if (info.colorimetry.hasRgbCurves != hr->b)
                fail(file, "hasRgbCurves", b2s(hr->b), b2s(info.colorimetry.hasRgbCurves));
        }
        if (const JVal* ci = expect->find("curvesIdentical")) {
            ++g_checks;
            if (info.colorimetry.curvesIdentical != ci->b)
                fail(file, "curvesIdentical", b2s(ci->b), b2s(info.colorimetry.curvesIdentical));
        }
        if (const JVal* hcp = expect->find("hasCicp")) {
            ++g_checks;
            if (info.hasCicp != hcp->b) fail(file, "hasCicp", b2s(hcp->b), b2s(info.hasCicp));
        }
        if (const JVal* ks = expect->find("knownSpace")) {
            ++g_checks;
            if (info.colorimetry.knownSpaceName != ks->str)
                fail(file, "knownSpace", ks->str, info.colorimetry.knownSpaceName);
        }
        if (const JVal* pt = expect->find("primariesToken")) {
            ++g_checks;
            if (info.colorimetry.primariesToken != pt->str)
                fail(file, "primariesToken", pt->str, info.colorimetry.primariesToken);
        }
        if (const JVal* tt = expect->find("transferToken")) {
            ++g_checks;
            if (info.colorimetry.transferToken != tt->str)
                fail(file, "transferToken", tt->str, info.colorimetry.transferToken);
        }
        if (const JVal* ke = expect->find("knownSpaceErrorMax")) {
            ++g_checks;
            if (!(info.colorimetry.knownSpaceError <= ke->num)) {
                char actual[64];
                std::snprintf(actual, sizeof(actual), "%.9g", info.colorimetry.knownSpaceError);
                fail(file, "knownSpaceErrorMax", "<= " + std::to_string(ke->num), actual);
            }
        }
        if (const JVal* cn = expect->find("colorimetryNoteContains")) {
            ++g_checks;
            if (info.colorimetry.note.find(cn->str) == std::string::npos) {
                fail(file, "colorimetryNoteContains", cn->str, info.colorimetry.note);
            }
        }
        if (const JVal* ver = expect->find("version")) {
            ++g_checks;
            char actual[16];
            std::snprintf(actual, sizeof(actual), "%u.%u",
                          info.versionMajor, info.versionMinor);
            if (ver->str != actual) fail(file, "version", ver->str, actual);
        }
        if (const JVal* cicp = expect->find("cicp")) {
            if (const JVal* cp = cicp->find("colourPrimaries")) {
                ++g_checks;
                if (info.cicp.colourPrimaries != static_cast<std::uint8_t>(cp->num))
                    fail(file, "cicp.colourPrimaries", std::to_string((int)cp->num),
                         std::to_string((int)info.cicp.colourPrimaries));
            }
            if (const JVal* ct = cicp->find("transferCharacteristics")) {
                ++g_checks;
                if (info.cicp.transferCharacteristics != static_cast<std::uint8_t>(ct->num))
                    fail(file, "cicp.transferCharacteristics", std::to_string((int)ct->num),
                         std::to_string((int)info.cicp.transferCharacteristics));
            }
            if (const JVal* cm = cicp->find("matrixCoefficients")) {
                ++g_checks;
                if (info.cicp.matrixCoefficients != static_cast<std::uint8_t>(cm->num))
                    fail(file, "cicp.matrixCoefficients", std::to_string((int)cm->num),
                         std::to_string((int)info.cicp.matrixCoefficients));
            }
            if (const JVal* cf = cicp->find("videoFullRangeFlag")) {
                ++g_checks;
                if (info.cicp.videoFullRangeFlag != static_cast<std::uint8_t>(cf->num))
                    fail(file, "cicp.videoFullRangeFlag", std::to_string((int)cf->num),
                         std::to_string((int)info.cicp.videoFullRangeFlag));
            }
        }
        if (const JVal* md = expect->find("minTagDecoded")) {
            ++g_checks;
            if (result.tagDecoded < static_cast<std::uint32_t>(md->num)) {
                std::printf("FAIL %s tagDecoded expected>=%d actual=%d\n",
                            file.c_str(), (int)md->num, (int)result.tagDecoded);
                ++g_failures;
            }
        }
        if (const JVal* wc = expect->find("warningsContain")) {
            ++g_checks;
            bool found = false;
            for (const auto& w : info.warnings)
                if (w.find(wc->str) != std::string::npos) found = true;
            if (!found) fail(file, "warningsContain", wc->str, "(no matching warning)");
        }

        if (const JVal* tags = sample.find("tags")) {
            for (const auto& kv : tags->obj) {
                const std::uint32_t wantSig = sig4(kv.first);
                const fff::icc::TagInfo* tag = nullptr;
                for (const auto& t : info.tags) {
                    if (t.signature == wantSig) { tag = &t; break; }
                }
                if (!tag) {
                    fail(file, "tag:" + kv.first, "present", "missing");
                    continue;
                }
                const JVal& exp = kv.second;
                if (const JVal* d = exp.find("decoded")) {
                    ++g_checks;
                    if (tag->decoded != d->b)
                        fail(file, "tag:" + kv.first + ".decoded", b2s(d->b), b2s(tag->decoded));
                }
                if (const JVal* ty = exp.find("type")) {
                    ++g_checks;
                    const std::string actual = sigStr(tag->declaredType);
                    if (actual != ty->str)
                        fail(file, "tag:" + kv.first + ".type", ty->str, actual);
                }
                if (const JVal* lv = exp.find("lutValid")) {
                    ++g_checks;
                    if (tag->lut.valid != lv->b)
                        fail(file, "tag:" + kv.first + ".lut.valid",
                             b2s(lv->b), b2s(tag->lut.valid));
                }
                if (const JVal* pa = exp.find("parametric")) {
                    ++g_checks;
                    if (tag->curve.parametric != pa->b)
                        fail(file, "tag:" + kv.first + ".curve.parametric",
                             b2s(pa->b), b2s(tag->curve.parametric));
                }
                if (const JVal* tc = exp.find("curveTableCount")) {
                    ++g_checks;
                    if (tag->curve.table.size() != static_cast<size_t>(tc->num)) {
                        fail(file, "tag:" + kv.first + ".curveTableCount",
                             std::to_string((int)tc->num),
                             std::to_string((int)tag->curve.table.size()));
                    }
                }
                if (const JVal* tb = exp.find("curveTable16Bit")) {
                    ++g_checks;
                    if (tag->curve.table16Bit != tb->b) {
                        fail(file, "tag:" + kv.first + ".curveTable16Bit",
                             b2s(tb->b), b2s(tag->curve.table16Bit));
                    }
                }
            }
        }
        if (g_failures == 0) std::printf("PASS %s\n", file.c_str());
    }

    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    return g_failures > 0 ? 1 : 0;
}
