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

float4 PSMain(VertexOutput input) : SV_Target
{
    return SourceTexture.SampleLevel(LinearClampSampler, input.UV, 0.0);
}
