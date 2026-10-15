cbuffer FrameData : register(b2, space1)
{
    uint output_index;
    float gain;
};

Texture2D<float4> InputTexture : register(t4, space2);
SamplerState LinearSampler : register(s3, space1);
RWStructuredBuffer<float4> OutputBuffer : register(u7, space3);

[numthreads(1, 1, 1)]
void vice_reflection_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    const float2 uv = float2(0.5, 0.5);
    const float4 sample_value = InputTexture.SampleLevel(LinearSampler, uv, 0.0);
    OutputBuffer[output_index + dispatch_id.x] = sample_value * gain;
}
