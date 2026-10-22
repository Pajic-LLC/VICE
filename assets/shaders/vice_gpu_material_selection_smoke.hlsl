struct ViceSmokeMaterialRecord {
    uint texture_index;
    uint sampler_index;
    uint2 reserved;
};

StructuredBuffer<uint> visible_scene_instances : register(t0, space0);
StructuredBuffer<uint4> instance_materials : register(t1, space0);
StructuredBuffer<ViceSmokeMaterialRecord> material_records : register(t2, space0);

Texture2D<float4> material_textures[] : register(t0, space1);
SamplerState material_samplers[] : register(s0, space1);

RWStructuredBuffer<uint> output_values : register(u0, space2);

[numthreads(2, 1, 1)]
void vice_gpu_material_selection_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint compacted_index = dispatch_id.x;
    const uint scene_instance_index = visible_scene_instances[compacted_index];
    const uint material_index = instance_materials[scene_instance_index].x;
    const ViceSmokeMaterialRecord material = material_records[material_index];

    const uint texture_index = NonUniformResourceIndex(material.texture_index);
    const uint sampler_index = NonUniformResourceIndex(material.sampler_index);
    const float4 sampled = material_textures[texture_index].SampleLevel(
        material_samplers[sampler_index],
        float2(0.5, 0.5),
        0.0
    );
    const uint4 rgba = (uint4)round(saturate(sampled) * 255.0);
    output_values[compacted_index] =
        rgba.x | (rgba.y << 8U) | (rgba.z << 16U) | (rgba.w << 24U);
}
