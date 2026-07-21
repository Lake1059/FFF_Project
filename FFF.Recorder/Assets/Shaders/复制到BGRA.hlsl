Texture2D<float4> SourceTexture : register(t0);
SamplerState LinearClampSampler : register(s0);

struct VertexOutput
{
    float4 Position : SV_Position;
    float2 UV : TEXCOORD0;
};

VertexOutput VSMain(uint vertexId : SV_VertexID)
{
    VertexOutput output;
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.UV = uv;
    output.Position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return output;
}

float4 SampleCubic(float2 uv, float2 sourceSize)
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

float4 PSMain(VertexOutput input) : SV_Target
{
    uint sourceWidth;
    uint sourceHeight;
    SourceTexture.GetDimensions(sourceWidth, sourceHeight);
    float2 sourceSize = float2(sourceWidth, sourceHeight);
    float2 uvDx = ddx(input.UV);
    float2 uvDy = ddy(input.UV);
    float sourcePixelsPerOutputPixel = max(length(uvDx * sourceSize), length(uvDy * sourceSize));

    if (sourcePixelsPerOutputPixel <= 1.0)
        return SampleCubic(input.UV, sourceSize);

    static const float offsets[4] = {-0.375, -0.125, 0.125, 0.375};
    float4 color = 0.0;
    [unroll]
    for (uint y = 0; y < 4; ++y)
    {
        [unroll]
        for (uint x = 0; x < 4; ++x)
        {
            float2 sampleUv = saturate(input.UV + offsets[x] * uvDx + offsets[y] * uvDy);
            color += SourceTexture.SampleLevel(LinearClampSampler, sampleUv, 0.0);
        }
    }
    return color * (1.0 / 16.0);
}
