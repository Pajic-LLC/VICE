struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_triangle_vs(uint vertex_id : SV_VertexID) {
    static const float2 positions[3] = {
        float2(0.0, 0.72),
        float2(-0.68, -0.58),
        float2(0.68, -0.58)
    };
    static const float3 colors[3] = {
        float3(0.20, 0.84, 1.00),
        float3(1.00, 0.26, 0.62),
        float3(0.98, 0.82, 0.18)
    };

    VertexOut output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.color = colors[vertex_id];
    return output;
}

float4 vice_triangle_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
