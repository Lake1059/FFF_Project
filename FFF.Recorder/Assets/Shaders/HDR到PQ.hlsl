cbuffer HdrConstants : register(b0)
{
    uint2 SourceSize;
    uint2 OutputSize;
    uint4 DestinationRect;
    float4 SourceRect;
    float ReferenceWhiteNits;
    float TargetPeakNits;
    float Exposure;
    float HighlightCompression;
    float Saturation;
    uint Rotation;
    float2 Reserved;
};

Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

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
    if (Rotation == 1) localUv = float2(localUv.y, 1.0 - localUv.x);
    else if (Rotation == 2) localUv = 1.0 - localUv;
    else if (Rotation == 3) localUv = float2(1.0 - localUv.y, localUv.x);
    float2 uv = SourceRect.xy + localUv * SourceRect.zw;
    float3 linear709 = max(SourceTexture.SampleLevel(LinearClampSampler, uv, 0.0).rgb, 0.0);
    float luminance = dot(linear709, float3(0.2126, 0.7152, 0.0722));
    linear709 = max(lerp(luminance.xxx, linear709, Saturation), 0.0);
    float3 linear2020;
    linear2020.r = dot(linear709, float3(0.6274040, 0.3292820, 0.0433136));
    linear2020.g = dot(linear709, float3(0.0690970, 0.9195400, 0.0113612));
    linear2020.b = dot(linear709, float3(0.0163916, 0.0880132, 0.8955950));
    float3 nits = linear2020 * ReferenceWhiteNits * exp2(Exposure);
    float knee = TargetPeakNits * (1.0 - HighlightCompression * 0.5);
    float3 excess = max(nits - knee, 0.0);
    nits = min(nits, knee) + excess / (1.0 + excess / max(TargetPeakNits - knee, 1.0));
    nits = min(nits, TargetPeakNits);
    OutputTexture[pixel] = float4(EncodePq(nits.r), EncodePq(nits.g), EncodePq(nits.b), 1.0);
}
