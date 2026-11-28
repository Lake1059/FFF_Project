#include "pch.h"
#include "3FP/Stream/DashManifest.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace FFFPlayer::Stream {
namespace {
struct Tag final {
    std::string name;
    std::map<std::string, std::string> attributes;
    bool closing = false;
    bool selfClosing = false;
};

std::string Trim(const std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

std::string LocalName(const std::string& name) {
    const auto colon = name.rfind(':');
    return colon == std::string::npos ? name : name.substr(colon + 1);
}

bool StartsWith(const std::string_view value, const std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::string DecodeEntities(const std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '&') {
            result.push_back(value[i]);
            continue;
        }
        if (value.substr(i, 5) == "&amp;") { result.push_back('&'); i += 4; }
        else if (value.substr(i, 4) == "&lt;") { result.push_back('<'); i += 3; }
        else if (value.substr(i, 4) == "&gt;") { result.push_back('>'); i += 3; }
        else if (value.substr(i, 6) == "&quot;") { result.push_back('"'); i += 5; }
        else if (value.substr(i, 6) == "&apos;") { result.push_back('\''); i += 5; }
        else result.push_back(value[i]);
    }
    return result;
}

bool ParseAttributes(const std::string_view text, std::map<std::string, std::string>& values) {
    values.clear();
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() && (text[position] == ' ' || text[position] == '\t' ||
            text[position] == '\r' || text[position] == '\n' || text[position] == '/'))
            ++position;
        if (position >= text.size()) break;
        const auto keyStart = position;
        while (position < text.size() && text[position] != '=' && text[position] != ' ' &&
            text[position] != '\t' && text[position] != '\r' && text[position] != '\n')
            ++position;
        if (position >= text.size() || text[position] != '=') return false;
        const std::string key = std::string(text.substr(keyStart, position - keyStart));
        ++position;
        while (position < text.size() && (text[position] == ' ' || text[position] == '\t'))
            ++position;
        std::string value;
        if (position < text.size() && (text[position] == '"' || text[position] == '\'')) {
            const char quote = text[position++];
            const auto end = text.find(quote, position);
            if (end == std::string_view::npos) return false;
            value.assign(text.substr(position, end - position));
            position = end + 1;
        } else {
            const auto start = position;
            while (position < text.size() && text[position] != ' ' && text[position] != '\t' &&
                text[position] != '\r' && text[position] != '\n')
                ++position;
            value.assign(text.substr(start, position - start));
        }
        values[key] = DecodeEntities(value);
    }
    return true;
}

bool NextTag(const std::string_view xml, std::size_t& position, Tag& tag,
    std::string& error) {
    while (position < xml.size()) {
        const auto lt = xml.find('<', position);
        if (lt == std::string_view::npos) return false;
        if (xml.substr(lt, 4) == "<!--") {
            const auto end = xml.find("-->", lt + 4);
            if (end == std::string_view::npos) { error = "DASH: unterminated comment."; return false; }
            position = end + 3;
            continue;
        }
        if (xml.substr(lt, 2) == "<?") {
            const auto end = xml.find("?>", lt + 2);
            if (end == std::string_view::npos) { error = "DASH: unterminated processing instruction."; return false; }
            position = end + 2;
            continue;
        }
        if (xml.substr(lt, 2) == "<!") {
            const auto end = xml.find('>', lt + 2);
            if (end == std::string_view::npos) { error = "DASH: unterminated declaration."; return false; }
            position = end + 1;
            continue;
        }
        std::size_t gt = lt + 1;
        char quote = 0;
        while (gt < xml.size()) {
            const char c = xml[gt];
            if (quote != 0) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            ++gt;
        }
        if (gt >= xml.size()) { error = "DASH: unterminated tag."; return false; }
        std::string inside(xml.substr(lt + 1, gt - lt - 1));
        tag = {};
        tag.closing = !inside.empty() && inside.front() == '/';
        if (tag.closing) inside.erase(inside.begin());
        tag.selfClosing = !inside.empty() && inside.back() == '/';
        if (tag.selfClosing) inside.pop_back();
        const auto nameEnd = inside.find_first_of(" \t\r\n");
        tag.name = LocalName(Trim(nameEnd == std::string::npos ? inside : inside.substr(0, nameEnd)));
        if (tag.name.empty()) { error = "DASH: empty tag name."; return false; }
        if (!ParseAttributes(nameEnd == std::string::npos ? std::string_view{} : std::string_view(inside).substr(nameEnd),
            tag.attributes)) {
            error = "DASH: malformed attributes on <" + tag.name + ">.";
            return false;
        }
        position = gt + 1;
        return true;
    }
    return false;
}

bool ParseU64(const std::string& text, std::uint64_t& value) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool ParseInt64(const std::string& text, std::int64_t& value) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool ParseDouble(const std::string& text, double& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

void ParseTemplate(const std::map<std::string, std::string>& attributes,
    DashSegmentTemplate& value) {
    if (auto it = attributes.find("timescale"); it != attributes.end()) ParseU64(it->second, value.timescale);
    if (auto it = attributes.find("duration"); it != attributes.end()) ParseU64(it->second, value.duration);
    if (auto it = attributes.find("startNumber"); it != attributes.end()) ParseU64(it->second, value.startNumber);
    if (auto it = attributes.find("presentationTimeOffset"); it != attributes.end())
        ParseU64(it->second, value.presentationTimeOffset);
    if (auto it = attributes.find("media"); it != attributes.end()) value.media = it->second;
    if (auto it = attributes.find("initialization"); it != attributes.end()) value.initialization = it->second;
    if (auto it = attributes.find("index"); it != attributes.end()) value.index = it->second;
    if (auto it = attributes.find("bitstreamSwitching"); it != attributes.end())
        value.bitstreamSwitching = it->second;
}

bool ParseTimeline(const std::string_view xml, std::size_t& position,
    std::vector<DashTimelineEntry>& timeline, std::string& error) {
    while (position < xml.size()) {
        Tag tag;
        if (!NextTag(xml, position, tag, error)) return false;
        if (tag.closing && tag.name == "SegmentTimeline") return true;
        if (tag.name != "S" || tag.closing) continue;
        DashTimelineEntry entry;
        if (auto it = tag.attributes.find("t"); it != tag.attributes.end() && !ParseInt64(it->second, entry.time))
            return false;
        if (auto it = tag.attributes.find("d"); it != tag.attributes.end() && !ParseU64(it->second, entry.duration))
            return false;
        if (auto it = tag.attributes.find("r"); it != tag.attributes.end()) {
            std::uint64_t repeat = 0;
            if (!ParseU64(it->second, repeat)) return false;
            entry.repeat = static_cast<std::uint32_t>(std::min<std::uint64_t>(repeat, 0xffffffffu));
        }
        if (entry.duration == 0) return false;
        timeline.push_back(entry);
    }
    error = "DASH: unterminated SegmentTimeline.";
    return false;
}

bool ParseSegmentList(const std::string_view xml, std::size_t& position,
    std::vector<DashSegmentUrl>& urls, std::string& error) {
    while (position < xml.size()) {
        Tag tag;
        if (!NextTag(xml, position, tag, error)) return false;
        if (tag.closing && tag.name == "SegmentList") return true;
        if (tag.name != "SegmentURL" || tag.closing) continue;
        DashSegmentUrl url;
        if (auto it = tag.attributes.find("media"); it != tag.attributes.end()) url.media = it->second;
        if (auto it = tag.attributes.find("mediaRange"); it != tag.attributes.end()) url.mediaRange = it->second;
        if (auto it = tag.attributes.find("indexRange"); it != tag.attributes.end()) url.indexRange = it->second;
        urls.push_back(std::move(url));
    }
    error = "DASH: unterminated SegmentList.";
    return false;
}

void NormalizePath(std::vector<std::string>& segments) {
    std::vector<std::string> result;
    for (const auto& segment : segments) {
        if (segment.empty() || segment == ".") continue;
        if (segment == "..") {
            if (!result.empty()) result.pop_back();
            continue;
        }
        result.push_back(segment);
    }
    segments = std::move(result);
}

std::string PadNumber(const std::uint64_t value, const unsigned width) {
    std::string text = std::to_string(value);
    while (text.size() < width) text.insert(text.begin(), '0');
    return text;
}

std::string ExpandToken(const std::string_view token, const std::string_view representationId,
    const std::uint64_t bandwidth, const std::uint64_t number, const std::int64_t time,
    bool& supported) {
    const auto percent = token.find('%');
    const std::string name(token.substr(0, percent == std::string_view::npos
        ? token.size() : percent));
    unsigned width = 0;
    if (percent != std::string_view::npos) {
        std::uint64_t parsed = 0;
        std::size_t position = percent + 1;
        while (position < token.size() && token[position] >= '0' && token[position] <= '9') {
            parsed = parsed * 10 + (token[position] - '0');
            ++position;
        }
        if (position >= token.size() || token[position] != 'd' || parsed == 0 || parsed > 18) {
            supported = false;
            return {};
        }
        width = static_cast<unsigned>(parsed);
    }
    if (name == "RepresentationID") return std::string(representationId);
    if (name == "Bandwidth") return width ? PadNumber(bandwidth, width) : std::to_string(bandwidth);
    if (name == "Number") return width ? PadNumber(number, width) : std::to_string(number);
    if (name == "Time") return width ? PadNumber(static_cast<std::uint64_t>(time), width) : std::to_string(time);
    supported = false;
    return {};
}
} // namespace

bool DashParser::ParseIsoDuration(const std::string_view text, double& seconds) {
    if (text.empty() || (text.front() != 'P' && text.front() != 'p')) return false;
    double total = 0;
    bool inTime = false;
    bool sawComponent = false;
    std::size_t position = 1;
    while (position < text.size()) {
        if (text[position] == 'T' || text[position] == 't') {
            inTime = true;
            ++position;
            continue;
        }
        const auto numberStart = position;
        while (position < text.size() &&
            ((text[position] >= '0' && text[position] <= '9') || text[position] == '.' ||
                text[position] == ','))
            ++position;
        if (position == numberStart || position >= text.size()) return false;
        double value = 0;
        std::string number(text.substr(numberStart, position - numberStart));
        std::replace(number.begin(), number.end(), ',', '.');
        if (!ParseDouble(number, value)) return false;
        const char unit = text[position++];
        const bool validUnit = inTime ? (unit == 'H' || unit == 'h' || unit == 'M' || unit == 'm' ||
            unit == 'S' || unit == 's') : (unit == 'D' || unit == 'd' || unit == 'Y' || unit == 'y');
        // Months are intentionally rejected: DASH durations in the wild use
        // days/hours, and silently treating months as 30 days hides a bug.
        if (!validUnit) return false;
        sawComponent = true;
        if (unit == 'Y' || unit == 'y') total += value * 365.0 * 24 * 3600;
        else if (unit == 'D' || unit == 'd') total += value * 24 * 3600;
        else if (unit == 'H' || unit == 'h') total += value * 3600;
        else if (unit == 'M' || unit == 'm') total += value * 60;
        else total += value;
    }
    if (!sawComponent) return false;
    seconds = total;
    return true;
}

std::string DashParser::ResolveUri(const std::string_view baseUrl, const std::string_view uri) {
    if (uri.empty()) return {};
    if (uri.find("://") != std::string_view::npos) return std::string(uri);
    const auto scheme = baseUrl.find("://");
    if (scheme == std::string_view::npos) return std::string(uri);
    const auto authorityStart = scheme + 3;
    const auto pathStart = baseUrl.find('/', authorityStart);
    const auto authority = pathStart == std::string_view::npos
        ? baseUrl.substr(authorityStart)
        : baseUrl.substr(authorityStart, pathStart - authorityStart);
    const std::string origin = std::string(baseUrl.substr(0, authorityStart)) + std::string(authority);
    if (StartsWith(uri, "//")) return std::string(baseUrl.substr(0, scheme + 1)) + std::string(uri);
    if (uri.front() == '/') return origin + std::string(uri);

    std::string directory = "/";
    if (pathStart != std::string_view::npos) {
        std::size_t end = baseUrl.size();
        const auto query = baseUrl.find_first_of("?#", pathStart);
        if (query != std::string_view::npos) end = query;
        const auto slash = baseUrl.find_last_of('/', end);
        if (slash >= pathStart)
            directory = std::string(baseUrl.substr(pathStart, slash - pathStart + 1));
    }
    const std::string path = directory + std::string(uri);
    std::vector<std::string> segments;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = path.find('/', start);
        segments.push_back(path.substr(start, slash == std::string::npos
            ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    NormalizePath(segments);
    std::string normalized = "/";
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i != 0) normalized.push_back('/');
        normalized += segments[i];
    }
    return origin + normalized;
}

std::string DashParser::ExpandTemplate(const std::string_view templateText,
    const std::string_view representationId, const std::uint64_t bandwidth,
    const std::uint64_t number, const std::int64_t time) {
    std::string result;
    std::size_t position = 0;
    while (position < templateText.size()) {
        if (templateText[position] != '$') {
            result.push_back(templateText[position++]);
            continue;
        }
        const auto end = templateText.find('$', position + 1);
        if (end == std::string_view::npos) {
            result.append(templateText.substr(position));
            break;
        }
        const std::string_view token = templateText.substr(position + 1, end - position - 1);
        if (token.empty()) {
            result.push_back('$');
        } else {
            bool supported = true;
            const auto replacement = ExpandToken(token, representationId, bandwidth, number, time, supported);
            if (supported) result += replacement;
            else result.append(templateText.substr(position, end - position + 1));
        }
        position = end + 1;
    }
    return result;
}

bool DashParser::TemplateIsSupported(const std::string_view templateText) {
    std::size_t position = 0;
    while (position < templateText.size()) {
        const auto start = templateText.find('$', position);
        if (start == std::string_view::npos) return true;
        const auto end = templateText.find('$', start + 1);
        if (end == std::string_view::npos) return false;
        const std::string_view token = templateText.substr(start + 1, end - start - 1);
        if (!token.empty()) {
            bool supported = true;
            ExpandToken(token, {}, 0, 0, 0, supported);
            if (!supported) return false;
        }
        position = end + 1;
    }
    return true;
}

bool DashParser::Parse(const std::string_view xml, const std::string_view manifestUrl,
    DashManifest& manifest, std::string& error) {
    manifest = {};
    manifest.baseUrl = std::string(manifestUrl);
    bool sawMpd = false;
    DashPeriod* period = nullptr;
    DashAdaptationSet* adaptation = nullptr;
    DashRepresentation* representation = nullptr;
    std::size_t position = 0;
    while (position < xml.size()) {
        Tag tag;
        if (!NextTag(xml, position, tag, error)) {
            if (error.empty()) break;
            return false;
        }
        if (tag.closing) {
            if (tag.name == "Period") {
                period = nullptr;
                adaptation = nullptr;
                representation = nullptr;
            } else if (tag.name == "AdaptationSet") {
                adaptation = nullptr;
                representation = nullptr;
            } else if (tag.name == "Representation") {
                representation = nullptr;
            }
            continue;
        }
        if (tag.name == "MPD") {
            sawMpd = true;
            if (auto it = tag.attributes.find("type"); it != tag.attributes.end())
                manifest.dynamic = it->second == "dynamic";
            if (auto it = tag.attributes.find("mediaPresentationDuration"); it != tag.attributes.end())
                ParseIsoDuration(it->second, manifest.durationSeconds);
            if (auto it = tag.attributes.find("minimumUpdatePeriod"); it != tag.attributes.end())
                ParseIsoDuration(it->second, manifest.minimumUpdatePeriodSeconds);
            if (auto it = tag.attributes.find("availabilityStartTime"); it != tag.attributes.end())
                manifest.availabilityStartTime = it->second;
            if (auto it = tag.attributes.find("timeShiftBufferDepth"); it != tag.attributes.end())
                ParseIsoDuration(it->second, manifest.timeShiftBufferDepthSeconds);
            if (auto it = tag.attributes.find("suggestedPresentationDelay"); it != tag.attributes.end())
                ParseIsoDuration(it->second, manifest.suggestedPresentationDelaySeconds);
            continue;
        }
        if (tag.name == "BaseURL") {
            const auto close = xml.find("</BaseURL", position);
            if (close == std::string_view::npos) {
                error = "DASH: unterminated BaseURL.";
                return false;
            }
            const std::string value = Trim(xml.substr(position, close - position));
            const std::string resolved = ResolveUri(manifest.baseUrl, value);
            if (representation != nullptr) representation->baseUrl = resolved;
            else manifest.baseUrl = resolved;
            const auto closeEnd = xml.find('>', close);
            position = closeEnd == std::string_view::npos ? xml.size() : closeEnd + 1;
            continue;
        }
        if (tag.name == "Period") {
            DashPeriod value;
            if (auto it = tag.attributes.find("id"); it != tag.attributes.end()) value.id = it->second;
            if (auto it = tag.attributes.find("start"); it != tag.attributes.end())
                ParseIsoDuration(it->second, value.startSeconds);
            if (auto it = tag.attributes.find("duration"); it != tag.attributes.end())
                ParseIsoDuration(it->second, value.durationSeconds);
            manifest.periods.push_back(std::move(value));
            period = &manifest.periods.back();
            adaptation = nullptr;
            representation = nullptr;
            continue;
        }
        if (tag.name == "AdaptationSet") {
            if (period == nullptr) {
                error = "DASH: AdaptationSet outside Period.";
                return false;
            }
            DashAdaptationSet value;
            if (auto it = tag.attributes.find("mimeType"); it != tag.attributes.end()) value.mimeType = it->second;
            if (auto it = tag.attributes.find("codecs"); it != tag.attributes.end()) value.codecs = it->second;
            ParseTemplate(tag.attributes, value.segmentTemplate);
            period->adaptationSets.push_back(std::move(value));
            adaptation = &period->adaptationSets.back();
            representation = nullptr;
            continue;
        }
        if (tag.name == "Representation") {
            if (adaptation == nullptr) {
                error = "DASH: Representation outside AdaptationSet.";
                return false;
            }
            DashRepresentation value;
            value.segmentTemplate = adaptation->segmentTemplate;
            value.timeline = adaptation->timeline;
            if (auto it = tag.attributes.find("id"); it != tag.attributes.end()) value.id = it->second;
            if (auto it = tag.attributes.find("bandwidth"); it != tag.attributes.end())
                ParseU64(it->second, value.bandwidth);
            if (auto it = tag.attributes.find("width"); it != tag.attributes.end()) {
                std::uint64_t width = 0;
                if (ParseU64(it->second, width)) value.width = static_cast<std::uint32_t>(width);
            }
            if (auto it = tag.attributes.find("height"); it != tag.attributes.end()) {
                std::uint64_t height = 0;
                if (ParseU64(it->second, height)) value.height = static_cast<std::uint32_t>(height);
            }
            if (auto it = tag.attributes.find("codecs"); it != tag.attributes.end()) value.codecs = it->second;
            if (auto it = tag.attributes.find("mimeType"); it != tag.attributes.end()) value.mimeType = it->second;
            ParseTemplate(tag.attributes, value.segmentTemplate);
            adaptation->representations.push_back(std::move(value));
            representation = tag.selfClosing ? nullptr : &adaptation->representations.back();
            continue;
        }
        if (tag.name == "SegmentTemplate") {
            if (representation != nullptr)
                ParseTemplate(tag.attributes, representation->segmentTemplate);
            else if (adaptation != nullptr)
                ParseTemplate(tag.attributes, adaptation->segmentTemplate);
            continue;
        }
        if (tag.name == "SegmentTimeline") {
            std::vector<DashTimelineEntry>* timeline = representation != nullptr
                ? &representation->timeline : adaptation != nullptr
                ? &adaptation->timeline : nullptr;
            if (timeline == nullptr || tag.selfClosing) continue;
            if (!ParseTimeline(xml, position, *timeline, error)) return false;
            continue;
        }
        if (tag.name == "SegmentList") {
            if (representation != nullptr && !tag.selfClosing) {
                if (!ParseSegmentList(xml, position, representation->segmentUrls, error)) return false;
            }
            continue;
        }
        if (tag.name == "SegmentURL" && representation != nullptr) {
            DashSegmentUrl url;
            if (auto it = tag.attributes.find("media"); it != tag.attributes.end()) url.media = it->second;
            if (auto it = tag.attributes.find("mediaRange"); it != tag.attributes.end()) url.mediaRange = it->second;
            if (auto it = tag.attributes.find("indexRange"); it != tag.attributes.end()) url.indexRange = it->second;
            representation->segmentUrls.push_back(std::move(url));
            continue;
        }
    }
    if (!sawMpd) {
        error = "DASH: no MPD element.";
        return false;
    }
    if (manifest.periods.empty()) {
        error = "DASH: no Period element.";
        return false;
    }
    bool hasRepresentation = false;
    for (const auto& value : manifest.periods)
        for (const auto& set : value.adaptationSets)
            if (!set.representations.empty()) hasRepresentation = true;
    if (!hasRepresentation) {
        error = "DASH: no Representation element.";
        return false;
    }
    manifest.valid = true;
    return true;
}

} // namespace FFFPlayer::Stream
