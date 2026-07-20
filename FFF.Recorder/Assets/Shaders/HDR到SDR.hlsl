cbuffer ToneMapConstants : register(b0)
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
};

Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearClampSampler : register(s0);
RWTexture2D<float4> OutputTexture : register(u0);

float Bt709Encode(float value)
{
    value = max(value, 0.0);
    return value < 0.018 ? 4.5 * value : 1.099 * pow(value, 0.45) - 0.099;
}

float Bt2390Eetf(float normalizedLuminance, float compression)
{
    float knee = lerp(0.8, 0.55, compression);
    if (normalizedLuminance <= knee)
        return normalizedLuminance;
    float t = saturate((normalizedLuminance - knee) / max(1.0 - knee, 0.0001));
    float shoulder = t / (1.0 + t);
    return knee + (1.0 - knee) * shoulder * 2.0;
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
    linear709 *= exp2(Exposure);
    float luminance = max(dot(linear709, float3(0.2126, 0.7152, 0.0722)), 0.000001);
    float sourceNits = luminance * ReferenceWhiteNits;
    float mapped = Bt2390Eetf(saturate(sourceNits / TargetPeakNits), HighlightCompression);
    float targetLinear = mapped * TargetPeakNits / 100.0;
    float3 toneMapped = linear709 * (targetLinear / luminance);
    float mappedLuminance = dot(toneMapped, float3(0.2126, 0.7152, 0.0722));
    toneMapped = max(lerp(mappedLuminance.xxx, toneMapped, Saturation), 0.0);
    float maxOutput = max(max(toneMapped.r, toneMapped.g), toneMapped.b);
    float outputLimit = max(TargetPeakNits / 100.0, 0.0001);
    toneMapped *= min(1.0, outputLimit / max(maxOutput, 0.0001));
    OutputTexture[pixel] = float4(Bt709Encode(toneMapped.r), Bt709Encode(toneMapped.g), Bt709Encode(toneMapped.b), 1.0);
}
