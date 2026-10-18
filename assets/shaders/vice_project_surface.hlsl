static const uint VICE_MAX_LIGHTS = 8;
static const uint VICE_SHADOW_CASCADE_COUNT = 4;

struct ViceLight {
    float4 position_or_direction;
    float4 color_intensity;
    float4 parameters;
};

cbuffer SceneFrame : register(b0, space0) {
    column_major float4x4 view_projection;
    column_major float4x4 model;
    float4 camera_position;
    float4 light_metadata;
    ViceLight lights[VICE_MAX_LIGHTS];
    column_major float4x4 shadow_view_projection;
    float4 shadow_metadata;
    column_major float4x4 shadow_view_projection_1;
    column_major float4x4 shadow_view_projection_2;
    column_major float4x4 shadow_view_projection_3;
    float4 shadow_cascade_splits;
    float4 shadow_camera_forward;
};

cbuffer MaterialParams : register(b1, space0) {
    float4 base_color;
    float metallic;
    float roughness;
    float2 material_padding;
};

Texture2D<float4> base_color_texture : register(t0, space0);
Texture2D<float> directional_shadow_map : register(t1, space0);
Texture2D<float> directional_shadow_map_1 : register(t2, space0);
Texture2D<float> directional_shadow_map_2 : register(t3, space0);
Texture2D<float> directional_shadow_map_3 : register(t4, space0);
SamplerState base_color_sampler : register(s0, space0);
SamplerState directional_shadow_sampler : register(s1, space0);

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
    float3 color : COLOR0;
    float2 uv : TEXCOORD2;
};

VertexOut vice_surface_vs(VertexIn input) {
    VertexOut output;
    const float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.world_position = world_position.xyz;
    output.world_normal = normalize(mul((float3x3)model, input.normal));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float distribution_ggx(float3 normal, float3 half_vector, float alpha) {
    const float alpha_squared = alpha * alpha;
    const float normal_half = saturate(dot(normal, half_vector));
    const float denominator = normal_half * normal_half * (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(3.14159265 * denominator * denominator, 0.0001);
}

float geometry_schlick_ggx(float normal_view, float k) {
    return normal_view / max(normal_view * (1.0 - k) + k, 0.0001);
}

float3 fresnel_schlick(float cosine, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosine), 5.0);
}

float4 directional_shadow_position(uint cascade_index, float3 world_position) {
    const float4 position = float4(world_position, 1.0);
    if (cascade_index == 0) return mul(shadow_view_projection, position);
    if (cascade_index == 1) return mul(shadow_view_projection_1, position);
    if (cascade_index == 2) return mul(shadow_view_projection_2, position);
    return mul(shadow_view_projection_3, position);
}

float directional_shadow_depth(uint cascade_index, float2 uv) {
    if (cascade_index == 0) {
        return directional_shadow_map.Sample(directional_shadow_sampler, uv);
    }
    if (cascade_index == 1) {
        return directional_shadow_map_1.Sample(directional_shadow_sampler, uv);
    }
    if (cascade_index == 2) {
        return directional_shadow_map_2.Sample(directional_shadow_sampler, uv);
    }
    return directional_shadow_map_3.Sample(directional_shadow_sampler, uv);
}

uint directional_shadow_cascade(float world_depth) {
    if (world_depth <= shadow_cascade_splits.x) return 0;
    if (world_depth <= shadow_cascade_splits.y) return 1;
    if (world_depth <= shadow_cascade_splits.z) return 2;
    return 3;
}

float directional_shadow_visibility(float3 world_position) {
    if (shadow_metadata.x < 0.5) return 1.0;

    const float world_depth = max(
        dot(world_position - camera_position.xyz, shadow_camera_forward.xyz),
        0.0
    );
    if (world_depth > shadow_cascade_splits.w) return 1.0;

    const uint cascade_index = directional_shadow_cascade(world_depth);
    const float4 shadow_position = directional_shadow_position(cascade_index, world_position);
    if (abs(shadow_position.w) < 0.000001) return 1.0;

    const float3 projected = shadow_position.xyz / shadow_position.w;
    if (projected.z < 0.0 || projected.z > 1.0) return 1.0;

    const float2 uv = float2(
        projected.x * 0.5 + 0.5,
        projected.y * -0.5 + 0.5
    );
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) return 1.0;

    const float texel = shadow_metadata.z;
    const float compare_depth = projected.z - shadow_metadata.w * (1.0 + 0.35 * cascade_index);
    float visibility = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float stored_depth = directional_shadow_depth(
                cascade_index,
                uv + float2((float)x, (float)y) * texel
            );
            visibility += compare_depth <= stored_depth ? 1.0 : 0.0;
        }
    }
    return visibility / 9.0;
}

float3 evaluate_light(
    ViceLight light,
    float3 world_position,
    float3 normal,
    float3 view_direction,
    float3 albedo,
    float metallic_value,
    float roughness_value
) {
    float3 to_light;
    float attenuation = 1.0;
    if (light.position_or_direction.w < 0.5) {
        to_light = normalize(-light.position_or_direction.xyz);
    } else {
        const float3 delta = light.position_or_direction.xyz - world_position;
        const float distance = max(length(delta), 0.0001);
        to_light = delta / distance;
        const float range = max(light.parameters.x, 0.0001);
        const float smooth_range = saturate(1.0 - distance / range);
        attenuation = smooth_range * smooth_range;
    }

    const float normal_light = saturate(dot(normal, to_light));
    if (normal_light <= 0.0 || attenuation <= 0.0) return float3(0.0, 0.0, 0.0);

    const float3 half_vector = normalize(view_direction + to_light);
    const float normal_view = saturate(dot(normal, view_direction));
    const float half_view = saturate(dot(half_vector, view_direction));
    const float clamped_roughness = clamp(roughness_value, 0.04, 1.0);
    const float alpha = clamped_roughness * clamped_roughness;
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, saturate(metallic_value));
    const float3 fresnel = fresnel_schlick(half_view, f0);
    const float distribution = distribution_ggx(normal, half_vector, alpha);
    const float geometry_k = ((clamped_roughness + 1.0) * (clamped_roughness + 1.0)) / 8.0;
    const float geometry = geometry_schlick_ggx(normal_view, geometry_k) *
        geometry_schlick_ggx(normal_light, geometry_k);
    const float3 specular = distribution * geometry * fresnel /
        max(4.0 * normal_view * normal_light, 0.0001);
    const float3 kd = (1.0 - fresnel) * (1.0 - saturate(metallic_value));
    const float3 diffuse = kd * albedo / 3.14159265;
    const float3 radiance = light.color_intensity.rgb * light.color_intensity.w * attenuation;
    return (diffuse + specular) * radiance * normal_light;
}

float4 vice_surface_fs(VertexOut input) : SV_Target0 {
    const float4 sampled = base_color_texture.Sample(base_color_sampler, input.uv);
    const float4 albedo_sample = sampled * base_color * float4(input.color, 1.0);
    const float3 albedo = albedo_sample.rgb;
    const float3 normal = normalize(input.world_normal);
    const float3 view_direction = normalize(camera_position.xyz - input.world_position);

    float3 direct = float3(0.0, 0.0, 0.0);
    const uint light_count = min((uint)light_metadata.x, VICE_MAX_LIGHTS);
    const uint shadow_light_index = (uint)shadow_metadata.y;
    for (uint index = 0; index < light_count; ++index) {
        float visibility = 1.0;
        if (shadow_metadata.x >= 0.5 && index == shadow_light_index &&
            lights[index].position_or_direction.w < 0.5) {
            visibility = directional_shadow_visibility(input.world_position);
        }
        direct += visibility * evaluate_light(
            lights[index],
            input.world_position,
            normal,
            view_direction,
            albedo,
            metallic,
            roughness
        );
    }

    const float3 ambient = albedo * 0.035;
    return float4(ambient + direct, albedo_sample.a);
}
