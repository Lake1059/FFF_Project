cbuffer ScalingConstants : register(b0)
{
    uint2 OutputSize;
    uint4 DestinationRect;
    float4 SourceRect;
    float ReferenceWhiteNits;
    float TargetPeakNits;
    float Exposure;
    float HighlightCompression;
    float Saturation;
    uint Rotation;
    uint HighQualityScaling;
    uint Reserved;
};

Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

#include "HighQualitySampling.hlsli"

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
    float4 color = SampleScaledSource(localUv, destinationSize);
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb = saturate(lerp(luminance.xxx, color.rgb, Saturation) * exp2(Exposure));
    OutputTexture[pixel] = color;
}
