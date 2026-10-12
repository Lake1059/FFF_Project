cbuffer ToneMapConstants : register(b0)
{
    uint2 OutputSize;
    uint4 DestinationRect;
    float4 SourceRect;
    float ReferenceWhiteNits;
    float TargetPeakNits;
    float SourcePeakNits;
    float Exposure;
    float ReservedToneMap;
    float Saturation;
    uint Rotation;
    uint HighQualityScaling;
};

Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

#include "HighQualitySampling.hlsli"

float Bt709Encode(float value)
{
    value = max(value, 0.0);
    return value < 0.018 ? 4.5 * value : 1.099 * pow(value, 0.45) - 0.099;
}

float PqToNits(float value)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float v = pow(saturate(value), 1.0 / m2);
    return 10000.0 * pow(max(v - c1, 0.0) / max(c2 - c3 * v, 0.000001), 1.0 / m1);
}

float NitsToPq(float nits)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float powered = pow(saturate(nits / 10000.0), m1);
    return pow((c1 + c2 * powered) / (1.0 + c3 * powered), m2);
}

float Bt2390HdrToSdrNits(float nits, float sourcePeak, float targetPeak)
{
    float sourceMaximum = max(sourcePeak, 1.0);
    float targetMaximum = clamp(targetPeak, 1.0, sourceMaximum);
    float sourcePq = NitsToPq(sourceMaximum);
    float targetPq = NitsToPq(targetMaximum);
    if (targetPq >= sourcePq)
        return clamp(nits, 0.0, targetMaximum);

    float normalizedTarget = targetPq / sourcePq;
    float knee = clamp(1.5 * normalizedTarget - 0.5, 0.0, 1.0);
    float signal = clamp(NitsToPq(max(nits, 0.0)) / sourcePq, 0.0, 1.0);
    if (signal <= knee || knee >= 1.0)
        return clamp(nits, 0.0, targetMaximum);

    float t = (signal - knee) / (1.0 - knee);
    float t2 = t * t;
    float t3 = t2 * t;
    float mapped = (2.0 * t3 - 3.0 * t2 + 1.0) * knee +
        (t3 - 2.0 * t2 + t) * (1.0 - knee) +
        (-2.0 * t3 + 3.0 * t2) * normalizedTarget;
    return min(PqToNits(mapped * sourcePq), targetMaximum);
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 pixel = dispatchThreadId.xy;
    if (pixel.x >= OutputSize.x || pixel.y >= OutputSize.y)
        return;

    uint2 destinationStart = DestinationRect.xy;
    uint2 destinationSize = DestinationRect.zw;
    bool outside = pixel.x < destinationStart.x || pixel.y < destinationStart.y ||
        pixel.x >= destinationStart.x + destinationSize.x ||
        pixel.y >= destinationStart.y + destinationSize.y;
    if (outside)
    {
        OutputTexture[pixel] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float2 localPixel = float2(pixel - destinationStart) + 0.5;
    float2 localUv = localPixel / float2(destinationSize);
    float3 linear709 = max(SampleScaledSource(localUv, destinationSize).rgb, 0.0);
    linear709 *= exp2(Exposure);
    float luminance = max(max(linear709.r, linear709.g), linear709.b);
    float exposureScale = exp2(Exposure);
    float sourceNits = luminance * ReferenceWhiteNits;
    float mappedNits = Bt2390HdrToSdrNits(sourceNits,
        SourcePeakNits * exposureScale, TargetPeakNits);
    float targetLinear = mappedNits / max(TargetPeakNits, 1.0);
    float scale = luminance > 0.000001 ? targetLinear / luminance : 0.0;
    float3 toneMapped = linear709 * scale;
    float mappedLuminance = dot(toneMapped, float3(0.2126, 0.7152, 0.0722));
    toneMapped = max(lerp(mappedLuminance.xxx, toneMapped, Saturation), 0.0);
    float maxOutput = max(max(toneMapped.r, toneMapped.g), toneMapped.b);
    float outputLimit = 1.0;
    toneMapped *= min(1.0, outputLimit / max(maxOutput, 0.0001));
    OutputTexture[pixel] = float4(Bt709Encode(toneMapped.r), Bt709Encode(toneMapped.g), Bt709Encode(toneMapped.b), 1.0);
}
