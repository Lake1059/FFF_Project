cbuffer Blur : register(b0) {
    float2 Size;
    float2 Unused;
    uint Axis;
    uint Pad;
    float Sigma;
};
Texture2D<float4> Source : register(t0);

float4 main(float4 position : SV_Position) : SV_Target {
    const int2 pixel = int2(position.xy);
    const int2 direction = Axis == 0 ? int2(1, 0) : int2(0, 1);
    const int radius = min(64, int(ceil(3.0 * Sigma)));
    float4 total = 0.0;
    float weightTotal = 0.0;
    [loop] for (int i = -radius; i <= radius; ++i) {
        const float x = float(i) / Sigma;
        const float weight = exp(-0.5 * x * x);
        total += Source.Load(int3(clamp(pixel + i * direction, int2(0, 0), int2(Size) - 1), 0)) * weight;
        weightTotal += weight;
    }
    return total / weightTotal;
}
