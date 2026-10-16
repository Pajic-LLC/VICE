cbuffer FrameData : register(b0, space0) {
    column_major float4x4 view_projection;
};

struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
    float4 model_col0 : TEXCOORD0;
    float4 model_col1 : TEXCOORD1;
    float4 model_col2 : TEXCOORD2;
    float4 model_col3 : TEXCOORD3;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_cube_vs(VertexIn input) {
    const float4 world_position =
        input.model_col0 * input.position.x +
        input.model_col1 * input.position.y +
        input.model_col2 * input.position.z +
        input.model_col3;

    VertexOut output;
    output.position = mul(view_projection, world_position);
    output.color = input.color;
    return output;
}

float4 vice_cube_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
