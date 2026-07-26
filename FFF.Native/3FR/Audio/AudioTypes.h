#pragma once

#include "Api/FFF.Native.Api.h"

#include <cstdint>
#include <functional>

struct WasapiSampleFormat {
    std::uint32_t sampleRate;
    std::uint16_t channelCount;
    std::uint16_t bitsPerSample;
    std::uint16_t validBitsPerSample;
    std::uint16_t blockAlign;
    std::uint32_t channelMask;
    bool floatingPoint;
};

using WasapiPacketCallback = std::function<FFFResult(const std::uint8_t* data,
    std::uint32_t frameCount, std::uint32_t flags, std::uint64_t qpcPosition100ns,
    const WasapiSampleFormat& format)>;

using WasapiFailureCallback = std::function<void(const std::string& message)>;
