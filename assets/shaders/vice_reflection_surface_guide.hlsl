static const uint VICE_VIRTUAL_TEXTURE_FLAG = 0x80000000u;
static const uint VICE_VIRTUAL_TEXTURE_INDEX_MASK = 0x7fffffffu;
static const uint VICE_VIRTUAL_TEXTURE_TILE_EXTENT = 128u;
static const uint VICE_VIRTUAL_TABLE_WIDTH = 1024u;
static const uint VICE_RESOURCE_UNASSIGNED = 0xffffffffu;

struct ViceGpuMaterialRecord {
    float4 base_color;
    float4 emissive_color;
    float metallic;
    float roughness;
    float normal_scale;
    float emissive_intensity;
    uint base_color_texture;
    uint normal_texture;
    uint orm_texture;
    uint emissive_texture;
    uint sampler_index;
    uint3 reserved;
};

cbuffer SceneFrame : register(b0, space0) {
    column_major float4x4 view_projection;
};

struct ViceInstanceModel { column_major float4x4 transform; };
StructuredBuffer<ViceInstanceModel> scene_models : register(t0, space0);
StructuredBuffer<uint> visible_scene_instances : register(t1, space0);
StructuredBuffer<uint4> instance_materials : register(t2, space0);
StructuredBuffer<ViceGpuMaterialRecord> material_records : register(t3, space0);
Texture2D<float4> material_textures[] : register(t0, space1);
SamplerState material_samplers[] : register(s0, space1);

struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint scene_instance_index : TEXCOORD3;
};

VertexOut vice_reflection_surface_guide_vs(VertexIn input, uint instance_id : SV_InstanceID) {
    VertexOut output;
    const uint scene_instance_index = visible_scene_instances[instance_id];
    const float4x4 model = scene_models[scene_instance_index].transform;
    const float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.world_position = world_position.xyz;
    output.world_normal = normalize(mul((float3x3)model, input.normal));
    output.uv = input.uv;
    output.scene_instance_index = scene_instance_index;
    return output;
}

uint vice_virtual_word(uint table_texture_index, uint word_index) {
    const uint2 coordinate = uint2(
        word_index % VICE_VIRTUAL_TABLE_WIDTH,
        word_index / VICE_VIRTUAL_TABLE_WIDTH
    );
    const float4 encoded = material_textures[
        NonUniformResourceIndex(table_texture_index)
    ].Load(int3(coordinate, 0));
    const uint4 bytes = (uint4)round(saturate(encoded) * 255.0);
    return bytes.x | (bytes.y << 8u) | (bytes.z << 16u) | (bytes.w << 24u);
}

uint4 vice_virtual_record(uint table_texture_index, uint record_index) {
    const uint first_word = record_index * 4u;
    return uint4(
        vice_virtual_word(table_texture_index, first_word + 0u),
        vice_virtual_word(table_texture_index, first_word + 1u),
        vice_virtual_word(table_texture_index, first_word + 2u),
        vice_virtual_word(table_texture_index, first_word + 3u)
    );
}

uint vice_next_mip_extent(uint extent) {
    return extent > 1u ? (extent + 1u) / 2u : 1u;
}

uint2 vice_virtual_mip_extent(uint width, uint height, uint mip_level) {
    uint2 extent = uint2(width, height);
    for (uint mip = 0u; mip < mip_level; ++mip) {
        extent.x = vice_next_mip_extent(extent.x);
        extent.y = vice_next_mip_extent(extent.y);
    }
    return extent;
}

uint vice_virtual_page_index(uint4 descriptor, uint mip_level, uint tile_x, uint tile_y) {
    uint page = descriptor.w;
    uint2 extent = descriptor.xy;
    for (uint mip = 0u; mip < mip_level; ++mip) {
        const uint columns = (extent.x + VICE_VIRTUAL_TEXTURE_TILE_EXTENT - 1u) /
            VICE_VIRTUAL_TEXTURE_TILE_EXTENT;
        const uint rows = (extent.y + VICE_VIRTUAL_TEXTURE_TILE_EXTENT - 1u) /
            VICE_VIRTUAL_TEXTURE_TILE_EXTENT;
        page += columns * rows;
        extent.x = vice_next_mip_extent(extent.x);
        extent.y = vice_next_mip_extent(extent.y);
    }
    const uint columns = (extent.x + VICE_VIRTUAL_TEXTURE_TILE_EXTENT - 1u) /
        VICE_VIRTUAL_TEXTURE_TILE_EXTENT;
    return page + tile_y * columns + tile_x;
}

uint vice_wrap_virtual_texel(int coordinate, uint extent) {
    const int signed_extent = (int)extent;
    int wrapped = coordinate % signed_extent;
    if (wrapped < 0) wrapped += signed_extent;
    return (uint)wrapped;
}

float4 vice_load_virtual_texel(
    uint4 descriptor,
    uint page_table_texture,
    uint desired_mip,
    int2 desired_texel
) {
    const uint2 desired_extent = vice_virtual_mip_extent(descriptor.x, descriptor.y, desired_mip);
    const uint2 wrapped_texel = uint2(
        vice_wrap_virtual_texel(desired_texel.x, desired_extent.x),
        vice_wrap_virtual_texel(desired_texel.y, desired_extent.y)
    );
    const uint2 desired_tile = wrapped_texel / VICE_VIRTUAL_TEXTURE_TILE_EXTENT;
    const uint page_index = vice_virtual_page_index(
        descriptor, desired_mip, desired_tile.x, desired_tile.y
    );
    const uint4 page = vice_virtual_record(page_table_texture, page_index);
    const uint2 resolved_extent = vice_virtual_mip_extent(descriptor.x, descriptor.y, page.y);
    const float2 normalized_center =
        (float2(wrapped_texel) + 0.5) / float2(desired_extent);
    const uint2 resolved_texel = min(
        (uint2)floor(normalized_center * float2(resolved_extent)),
        resolved_extent - 1u
    );
    const uint2 resolved_origin = page.zw * VICE_VIRTUAL_TEXTURE_TILE_EXTENT;
    const uint2 local_texel = resolved_texel - resolved_origin;
    return material_textures[
        NonUniformResourceIndex(page.x)
    ].Load(int3(local_texel, 0));
}

float4 vice_sample_material_texture(
    uint encoded_texture_index,
    uint sampler_index,
    uint descriptor_table_texture,
    uint page_table_texture,
    float2 uv,
    float2 uv_dx,
    float2 uv_dy
) {
    if (encoded_texture_index == VICE_RESOURCE_UNASSIGNED ||
        (encoded_texture_index & VICE_VIRTUAL_TEXTURE_FLAG) == 0u) {
        return material_textures[
            NonUniformResourceIndex(encoded_texture_index)
        ].SampleGrad(
            material_samplers[NonUniformResourceIndex(sampler_index)],
            uv,
            uv_dx,
            uv_dy
        );
    }

    const uint virtual_texture_index =
        encoded_texture_index & VICE_VIRTUAL_TEXTURE_INDEX_MASK;
    const uint4 descriptor = vice_virtual_record(
        descriptor_table_texture, virtual_texture_index
    );
    const float2 base_extent = float2(descriptor.xy);
    const float rho = max(
        length(uv_dx * base_extent),
        length(uv_dy * base_extent)
    );
    const float lod = max(log2(max(rho, 1.0)), 0.0);
    const uint desired_mip = min((uint)floor(lod), descriptor.z - 1u);
    const uint2 desired_extent = vice_virtual_mip_extent(
        descriptor.x, descriptor.y, desired_mip
    );
    const float2 texel_position = frac(uv) * float2(desired_extent) - 0.5;
    const int2 first_texel = (int2)floor(texel_position);
    const float2 blend = frac(texel_position);

    const float4 top_left = vice_load_virtual_texel(
        descriptor, page_table_texture, desired_mip, first_texel
    );
    const float4 top_right = vice_load_virtual_texel(
        descriptor, page_table_texture, desired_mip, first_texel + int2(1, 0)
    );
    const float4 bottom_left = vice_load_virtual_texel(
        descriptor, page_table_texture, desired_mip, first_texel + int2(0, 1)
    );
    const float4 bottom_right = vice_load_virtual_texel(
        descriptor, page_table_texture, desired_mip, first_texel + int2(1, 1)
    );
    return lerp(
        lerp(top_left, top_right, blend.x),
        lerp(bottom_left, bottom_right, blend.x),
        blend.y
    );
}

float3 vice_surface_normal(
    VertexOut input,
    ViceGpuMaterialRecord material,
    float2 uv_dx,
    float2 uv_dy
) {
    const float3 geometric_normal = normalize(input.world_normal);
    const float3 sampled_normal = vice_sample_material_texture(
        material.normal_texture,
        material.sampler_index,
        material.reserved.x,
        material.reserved.y,
        input.uv,
        uv_dx,
        uv_dy
    ).xyz * 2.0 - 1.0;
    const float3 position_dx = ddx(input.world_position);
    const float3 position_dy = ddy(input.world_position);
    const float determinant = uv_dx.x * uv_dy.y - uv_dx.y * uv_dy.x;
    if (abs(determinant) <= 0.000001) return geometric_normal;
    const float inverse = 1.0 / determinant;
    const float3 tangent = normalize((position_dx * uv_dy.y - position_dy * uv_dx.y) * inverse);
    const float3 bitangent = normalize((-position_dx * uv_dy.x + position_dy * uv_dx.x) * inverse);
    const float3 mapped = normalize(float3(sampled_normal.xy * material.normal_scale, sampled_normal.z));
    return normalize(tangent * mapped.x + bitangent * mapped.y + geometric_normal * mapped.z);
}

float2 vice_oct_encode(float3 normal) {
    normal = normalize(normal);
    normal /= max(abs(normal.x) + abs(normal.y) + abs(normal.z), 0.000001);
    float2 encoded = normal.xy;
    if (normal.z < 0.0) {
        const float2 sign_value = float2(
            encoded.x >= 0.0 ? 1.0 : -1.0,
            encoded.y >= 0.0 ? 1.0 : -1.0
        );
        encoded = (1.0 - abs(encoded.yx)) * sign_value;
    }
    return encoded * 0.5 + 0.5;
}

float4 vice_reflection_surface_guide_fs(VertexOut input) : SV_Target0 {
    const uint material_index = instance_materials[input.scene_instance_index].x;
    const ViceGpuMaterialRecord material = material_records[material_index];
    const float2 uv_dx = ddx(input.uv);
    const float2 uv_dy = ddy(input.uv);
    const float4 orm = vice_sample_material_texture(
        material.orm_texture,
        material.sampler_index,
        material.reserved.x,
        material.reserved.y,
        input.uv,
        uv_dx,
        uv_dy
    );
    const float3 normal = vice_surface_normal(input, material, uv_dx, uv_dy);
    const float roughness = saturate(material.roughness * orm.g);
    const float metallic = saturate(material.metallic * orm.b);
    return float4(vice_oct_encode(normal), roughness, metallic);
}
