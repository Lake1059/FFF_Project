#include "pch.h"
#include "3FP/Stream/HlsPlaylist.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <map>
#include <sstream>

namespace FFFPlayer::Stream {
namespace {
std::string Trim(const std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

bool StartsWith(const std::string_view value, const std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool ParseAttributes(const std::string_view text, std::map<std::string, std::string>& values,
    std::string& error) {
    values.clear();
    std::size_t position = 0;
    while (position < text.size()) {
        while (position < text.size() &&
            (text[position] == ' ' || text[position] == '\t' || text[position] == ','))
            ++position;
        if (position >= text.size()) break;
        const auto keyStart = position;
        while (position < text.size() && text[position] != '=' && text[position] != ',')
            ++position;
        std::string key = Trim(text.substr(keyStart, position - keyStart));
        if (key.empty()) {
            error = "HLS attribute has no name.";
            return false;
        }
        if (position >= text.size() || text[position] != '=') {
            error = "HLS attribute has no '='.";
            return false;
        }
        ++position;
        std::string value;
        if (position < text.size() && text[position] == '"') {
            ++position;
            const auto quote = text.find('"', position);
            if (quote == std::string_view::npos) {
                error = "HLS quoted attribute is not terminated.";
                return false;
            }
            value.assign(text.substr(position, quote - position));
            position = quote + 1;
        } else {
            const auto comma = text.find(',', position);
            value = Trim(text.substr(position, comma == std::string_view::npos
                ? text.size() - position : comma - position));
            position = comma == std::string_view::npos ? text.size() : comma;
        }
        values[std::move(key)] = std::move(value);
    }
    return true;
}

bool ParseDouble(const std::string& text, double& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(parsed) || parsed < 0) return false;
    value = parsed;
    return true;
}

bool ParseU64(const std::string& text, std::uint64_t& value) {
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool ParseResolution(const std::string& text, std::uint32_t& width, std::uint32_t& height) {
    const auto x = text.find('x');
    if (x == std::string::npos) return false;
    std::uint64_t w = 0;
    std::uint64_t h = 0;
    if (!ParseU64(text.substr(0, x), w) || !ParseU64(text.substr(x + 1), h) ||
        w == 0 || h == 0 || w > 16384 || h > 16384) return false;
    width = static_cast<std::uint32_t>(w);
    height = static_cast<std::uint32_t>(h);
    return true;
}

bool ParseByteRange(const std::string& text, HlsByteRange& range) {
    const auto at = text.find('@');
    if (at == std::string::npos) {
        std::uint64_t length = 0;
        if (!ParseU64(text, length)) return false;
        range.length = length;
        range.offset = 0;
        range.offsetExplicit = false;
        range.hasRange = true;
        return true;
    }
    std::uint64_t length = 0;
    std::uint64_t offset = 0;
    if (!ParseU64(text.substr(0, at), length) || !ParseU64(text.substr(at + 1), offset))
        return false;
    range.length = length;
    range.offset = offset;
    range.offsetExplicit = true;
    range.hasRange = true;
    return true;
}

void NormalizePathSegments(std::vector<std::string>& segments) {
    std::vector<std::string> result;
    result.reserve(segments.size());
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
} // namespace

std::string HlsParser::ResolveUri(const std::string_view baseUri, const std::string_view uri) {
    if (uri.empty()) return {};
    if (uri.find("://") != std::string_view::npos) return std::string(uri);
    const auto scheme = baseUri.find("://");
    if (scheme == std::string_view::npos) return std::string(uri);

    const auto authorityStart = scheme + 3;
    const auto pathStart = baseUri.find('/', authorityStart);
    const auto authority = pathStart == std::string_view::npos
        ? baseUri.substr(authorityStart)
        : baseUri.substr(authorityStart, pathStart - authorityStart);
    const std::string origin = std::string(baseUri.substr(0, authorityStart)) +
        std::string(authority);

    if (StartsWith(uri, "//")) {
        return std::string(baseUri.substr(0, scheme + 1)) + std::string(uri);
    }
    if (uri.front() == '/') {
        return origin + std::string(uri);
    }

    std::string directory = "/";
    if (pathStart != std::string_view::npos) {
        std::size_t end = baseUri.size();
        const auto query = baseUri.find_first_of("?#", pathStart);
        if (query != std::string_view::npos) end = query;
        const auto slash = baseUri.find_last_of('/', end);
        if (slash >= pathStart)
            directory = std::string(baseUri.substr(pathStart, slash - pathStart + 1));
    }

    std::string relative = std::string(uri);
    std::string suffix;
    const auto suffixStart = relative.find_first_of("?#");
    if (suffixStart != std::string::npos) {
        suffix = relative.substr(suffixStart);
        relative.resize(suffixStart);
    }
    std::vector<std::string> segments;
    const std::string path = directory + relative;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = path.find('/', start);
        segments.push_back(path.substr(start, slash == std::string::npos
            ? std::string::npos : slash - start));
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    NormalizePathSegments(segments);
    std::string normalized = "/";
    for (std::size_t i = 0; i < segments.size(); ++i) {
        if (i != 0) normalized.push_back('/');
        normalized += segments[i];
    }
    return origin + normalized + suffix;
}

bool HlsParser::Parse(const std::string_view text, const std::string_view baseUri,
    HlsPlaylist& playlist, std::string& error) {
    playlist = {};
    if (text.empty()) {
        error = "HLS playlist is empty.";
        return false;
    }
    std::vector<std::string> lines;
    std::string current;
    for (const char c : text) {
        if (c == '\r') continue;
        if (c == '\n') {
            lines.push_back(Trim(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) lines.push_back(Trim(current));
    bool sawHeader = false;
    HlsKey currentKey;
    HlsByteRange pendingRange;
    std::uint64_t previousRangeEnd = 0;
    bool pendingDiscontinuity = false;
    bool haveExtInf = false;
    double pendingDuration = 0;
    bool wantVariantUri = false;
    HlsVariant pendingVariant;
    bool sawMediaTag = false;

    for (const auto& line : lines) {
        if (line.empty()) continue;
        if (!sawHeader) {
            if (line != "#EXTM3U") {
                error = "HLS playlist does not start with #EXTM3U.";
                return false;
            }
            sawHeader = true;
            continue;
        }
        if (wantVariantUri) {
            if (!line.starts_with("#")) {
                pendingVariant.uri = ResolveUri(baseUri, line);
                playlist.variants.push_back(std::move(pendingVariant));
                pendingVariant = {};
                wantVariantUri = false;
            }
            continue;
        }
        if (line.starts_with("#EXT-X-STREAM-INF:")) {
            std::map<std::string, std::string> attributes;
            if (!ParseAttributes(std::string_view(line).substr(18), attributes, error)) return false;
            pendingVariant = {};
            if (auto it = attributes.find("BANDWIDTH"); it != attributes.end())
                ParseU64(it->second, pendingVariant.bandwidth);
            if (auto it = attributes.find("RESOLUTION"); it != attributes.end())
                ParseResolution(it->second, pendingVariant.width, pendingVariant.height);
            if (auto it = attributes.find("CODECS"); it != attributes.end())
                pendingVariant.codecs = it->second;
            wantVariantUri = true;
            playlist.master = true;
            continue;
        }
        if (line.starts_with("#EXT-X-TARGETDURATION:")) {
            std::uint64_t value = 0;
            if (!ParseU64(line.substr(22), value)) {
                error = "HLS target duration is malformed.";
                return false;
            }
            playlist.targetDurationSeconds = static_cast<std::uint32_t>(value);
            sawMediaTag = true;
            continue;
        }
        if (line.starts_with("#EXT-X-MEDIA-SEQUENCE:")) {
            if (!ParseU64(line.substr(22), playlist.mediaSequence)) {
                error = "HLS media sequence is malformed.";
                return false;
            }
            sawMediaTag = true;
            continue;
        }
        if (line == "#EXT-X-ENDLIST") {
            playlist.endList = true;
            sawMediaTag = true;
            continue;
        }
        if (line.starts_with("#EXT-X-PLAYLIST-TYPE:")) {
            playlist.playlistType = Trim(line.substr(21));
            playlist.event = playlist.playlistType == "EVENT";
            sawMediaTag = true;
            continue;
        }
        if (line.starts_with("#EXT-X-MAP:")) {
            std::map<std::string, std::string> attributes;
            if (!ParseAttributes(std::string_view(line).substr(11), attributes, error)) return false;
            if (auto it = attributes.find("URI"); it != attributes.end())
                playlist.mapUri = ResolveUri(baseUri, it->second);
            sawMediaTag = true;
            continue;
        }
        if (line.starts_with("#EXT-X-KEY:")) {
            std::map<std::string, std::string> attributes;
            if (!ParseAttributes(std::string_view(line).substr(11), attributes, error)) return false;
            currentKey = {};
            if (auto it = attributes.find("METHOD"); it != attributes.end())
                currentKey.method = it->second;
            if (currentKey.method == "NONE") {
                currentKey = {};
            } else if (!currentKey.method.empty()) {
                currentKey.valid = true;
                if (auto it = attributes.find("URI"); it != attributes.end())
                    currentKey.uri = ResolveUri(baseUri, it->second);
                if (auto it = attributes.find("IV"); it != attributes.end())
                    currentKey.iv = it->second;
            }
            sawMediaTag = true;
            continue;
        }
        if (line.starts_with("#EXT-X-BYTERANGE:")) {
            if (!ParseByteRange(line.substr(17), pendingRange)) {
                error = "HLS byte range is malformed.";
                return false;
            }
            if (!pendingRange.offsetExplicit && previousRangeEnd != 0)
                pendingRange.offset = previousRangeEnd;
            sawMediaTag = true;
            continue;
        }
        if (line == "#EXT-X-DISCONTINUITY") {
            pendingDiscontinuity = true;
            pendingRange = {};
            continue;
        }
        if (line.starts_with("#EXTINF:")) {
            const auto comma = line.find(',', 8);
            if (!ParseDouble(line.substr(8, comma == std::string::npos
                ? std::string::npos : comma - 8), pendingDuration)) {
                error = "HLS EXTINF duration is malformed.";
                return false;
            }
            haveExtInf = true;
            sawMediaTag = true;
            continue;
        }
        if (line.starts_with("#")) continue;

        if (haveExtInf) {
            HlsSegment segment;
            segment.durationSeconds = pendingDuration;
            segment.uri = ResolveUri(baseUri, line);
            segment.sequence = playlist.mediaSequence + playlist.segments.size();
            segment.range = pendingRange;
            segment.key = currentKey;
            segment.discontinuity = pendingDiscontinuity;
            if (segment.range.hasRange)
                previousRangeEnd = segment.range.offset + segment.range.length;
            else
                previousRangeEnd = 0;
            playlist.segments.push_back(std::move(segment));
            haveExtInf = false;
            pendingRange = {};
            pendingDiscontinuity = false;
        }
    }
    if (!sawHeader) {
        error = "HLS playlist has no #EXTM3U header.";
        return false;
    }
    if (wantVariantUri) {
        error = "HLS master playlist has a variant with no URI.";
        return false;
    }
    if (!playlist.master && !sawMediaTag) {
        error = "HLS playlist has neither variants nor media tags.";
        return false;
    }
    playlist.valid = true;
    return true;
}

} // namespace FFFPlayer::Stream
