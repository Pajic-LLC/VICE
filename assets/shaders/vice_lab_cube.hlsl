cbuffer FrameData : register(b0, space0) {
    column_major float4x4 mvp;
};

struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_cube_vs(VertexIn input) {
    VertexOut output;
    output.position = mul(mvp, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 vice_cube_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
