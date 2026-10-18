cbuffer SceneFrame : register(b0, space0) {
    column_major float4x4 view_projection;
    column_major float4x4 model;
};

cbuffer MaterialParams : register(b1, space0) {
    float4 base_color;
};

struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_surface_vs(VertexIn input) {
    VertexOut output;
    const float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.color = input.color;
    return output;
}

float4 vice_surface_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0) * base_color;
}
