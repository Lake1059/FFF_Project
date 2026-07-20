float2 TransformLocalUv(float2 localUv)
{
    if (Rotation == 1) return float2(localUv.y, 1.0 - localUv.x);
    if (Rotation == 2) return 1.0 - localUv;
    if (Rotation == 3) return float2(1.0 - localUv.y, localUv.x);
    return localUv;
}

float2 SourceUv(float2 localUv)
{
    return SourceRect.xy + TransformLocalUv(localUv) * SourceRect.zw;
}

float4 SampleCubicSource(float2 uv, float2 sourceSize)
{
    float2 samplePosition = uv * sourceSize - 0.5;
    float2 texelCenter = floor(samplePosition) + 0.5;
    float2 fraction = samplePosition - floor(samplePosition);

    float2 weight0 = fraction * (-0.5 + fraction * (1.0 - 0.5 * fraction));
    float2 weight1 = 1.0 + fraction * fraction * (-2.5 + 1.5 * fraction);
    float2 weight2 = fraction * (0.5 + fraction * (2.0 - 1.5 * fraction));
    float2 weight3 = fraction * fraction * (-0.5 + 0.5 * fraction);
    float2 weight12 = weight1 + weight2;
    float2 offset12 = weight2 / max(weight12, 0.000001);

    float2 position0 = (texelCenter - 1.0) / sourceSize;
    float2 position12 = (texelCenter + offset12) / sourceSize;
    float2 position3 = (texelCenter + 2.0) / sourceSize;

    float4 result = 0.0;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position0.x, position0.y), 0.0) * weight0.x * weight0.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position12.x, position0.y), 0.0) * weight12.x * weight0.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position3.x, position0.y), 0.0) * weight3.x * weight0.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position0.x, position12.y), 0.0) * weight0.x * weight12.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position12.x, position12.y), 0.0) * weight12.x * weight12.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position3.x, position12.y), 0.0) * weight3.x * weight12.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position0.x, position3.y), 0.0) * weight0.x * weight3.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position12.x, position3.y), 0.0) * weight12.x * weight3.y;
    result += SourceTexture.SampleLevel(LinearClampSampler, float2(position3.x, position3.y), 0.0) * weight3.x * weight3.y;
    return result;
}

float4 SampleScaledSource(float2 localUv, uint2 destinationSize)
{
    float2 uv = SourceUv(localUv);
    float4 result = SourceTexture.SampleLevel(LinearClampSampler, uv, 0.0);
    if (HighQualityScaling != 0)
    {
        uint sourceWidth;
        uint sourceHeight;
        SourceTexture.GetDimensions(sourceWidth, sourceHeight);
        float2 sourceSize = float2(sourceWidth, sourceHeight);
        float2 visibleSourceSize = SourceRect.zw * sourceSize;
        if (Rotation == 1 || Rotation == 3)
            visibleSourceSize = visibleSourceSize.yx;

        float2 sourcePixelsPerOutputPixel = visibleSourceSize / float2(destinationSize);
        if (max(sourcePixelsPerOutputPixel.x, sourcePixelsPerOutputPixel.y) <= 1.0)
        {
            result = SampleCubicSource(uv, sourceSize);
        }
        else
        {
            // Average sixteen evenly distributed samples over the destination pixel footprint.
            // This low-pass filter suppresses the broken thin lines and stair stepping caused by
            // taking a single bilinear sample while shrinking high-resolution game frames.
            static const float offsets[4] = {-0.375, -0.125, 0.125, 0.375};
            float2 localStep = 1.0 / float2(destinationSize);
            float4 color = 0.0;
            [unroll]
            for (uint y = 0; y < 4; ++y)
            {
                [unroll]
                for (uint x = 0; x < 4; ++x)
                {
                    float2 sampleLocalUv = saturate(localUv + float2(offsets[x], offsets[y]) * localStep);
                    color += SourceTexture.SampleLevel(LinearClampSampler, SourceUv(sampleLocalUv), 0.0);
                }
            }
            result = color * (1.0 / 16.0);
        }
    }
    return result;
}
