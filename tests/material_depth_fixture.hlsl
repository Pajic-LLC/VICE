cbuffer SceneFrame : register(b0) { float4x4 view_projection; float4x4 model; };
cbuffer MaterialParams : register(b1) { float4 base_color; };
float4 depth_test_vs(float3 position : POSITION0) : SV_Position {
    float4 clip = mul(view_projection, mul(model, float4(position, 1)));
    // A legal project vertex transform that a generic native prepass cannot reproduce.
    clip.z += .0007 * clip.w;
    return clip;
}
float4 depth_test_fs() : SV_Target0 { return base_color; }
