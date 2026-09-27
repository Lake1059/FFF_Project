#pragma once

#include "3FP/Api/FFF.Player.Api.h"

// Probes the subtitle streams of a container file (e.g. an external .mks)
// without opening a playback session or creating a player. The UTF-8 JSON
// output has the top-level shape
//   {"format":"...","formatLongName":"...","startTimeSeconds":<number>,"streams":[...]}
// where startTimeSeconds is the container start time in seconds. Real-world
// subtitle containers may start anywhere (a subtitle track extracted with its
// source timestamps keeps a large offset), so the host needs this to offer a
// correction even though there is no reference stream inside the file.
//
// Every object inside streams uses the same field names and shapes as the
// streams array of FFF3FP_GetMediaInfo, so clients can deserialize both with
// the same types. A file that opens fine but carries no subtitle stream
// reports Success with an empty streams array (not an error).
FFFResult ProbeSubtitleStreams(const char* localPathUtf8, char* outputUtf8,
    std::uint32_t outputSize, std::uint32_t* requiredSize) noexcept;
