struct VertexIn {
    float2 position : POSITION0;
    float3 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_triangle_vs(VertexIn input) {
    VertexOut output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}

float4 vice_triangle_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
