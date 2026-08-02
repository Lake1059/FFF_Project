cbuffer HdrConstants : register(b0)
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

float EncodePq(float nits)
{
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 32.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 128.0;
    const float c3 = 2392.0 / 128.0;
    float normalized = saturate(nits / 10000.0);
    float powered = pow(normalized, m1);
    return pow((c1 + c2 * powered) / (1.0 + c3 * powered), m2);
}

// Windows scRGB capture is physically 1.0=80 nit, but OBS maps that canvas value to the
// selected SDR white before PQ encoding. ReferenceWhiteNits carries that selected value;
// the native encoder must preserve the resulting display-referred Rec.2100 PQ signal.

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
    float luminance = dot(linear709, float3(0.2126, 0.7152, 0.0722));
    linear709 = max(lerp(luminance.xxx, linear709, Saturation), 0.0);
    float3 linear2020;
    linear2020.r = dot(linear709, float3(0.6274040, 0.3292820, 0.0433136));
    linear2020.g = dot(linear709, float3(0.0690970, 0.9195400, 0.0113612));
    linear2020.b = dot(linear709, float3(0.0163916, 0.0880132, 0.8955950));
    linear2020 = max(linear2020, 0.0);
    float3 nits = linear2020 * ReferenceWhiteNits * exp2(Exposure);
    OutputTexture[pixel] = float4(EncodePq(nits.r), EncodePq(nits.g), EncodePq(nits.b), 1.0);
}
