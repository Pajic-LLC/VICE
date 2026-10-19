static const uint VICE_MAX_LIGHTS = 8;
static const float VICE_PI = 3.14159265358979323846;

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
    float4 environment_metadata;
    float4 environment_secondary_metadata;
    float4 environment_tertiary_metadata;

    column_major float4x4 environment_probe_from_model_0;
    float4 environment_probe_half_extents_0;
    float4 environment_probe_rotation_0;
    column_major float4x4 environment_probe_from_model_1;
    float4 environment_probe_half_extents_1;
    float4 environment_probe_rotation_1;
    column_major float4x4 environment_probe_from_model_2;
    float4 environment_probe_half_extents_2;
    float4 environment_probe_rotation_2;
};

cbuffer MaterialParams : register(b1, space0) {
    float4 base_color;
    float metallic;
    float roughness;
    float normal_scale;
    float emissive_intensity;
    float4 emissive_color;
};

Texture2D<float4> base_color_texture : register(t0, space0);
Texture2D<float4> normal_texture : register(t1, space0);
Texture2D<float4> orm_texture : register(t2, space0);
Texture2D<float4> emissive_texture : register(t3, space0);
SamplerState material_sampler : register(s0, space0);

Texture2D<float4> environment_irradiance_0 : register(t8, space0);
Texture2D<float4> environment_specular_0 : register(t9, space0);
Texture2D<float4> environment_brdf_0 : register(t10, space0);
Texture2D<float4> environment_irradiance_1 : register(t11, space0);
Texture2D<float4> environment_specular_1 : register(t12, space0);
Texture2D<float4> environment_brdf_1 : register(t13, space0);
Texture2D<float4> environment_irradiance_2 : register(t14, space0);
Texture2D<float4> environment_specular_2 : register(t15, space0);
Texture2D<float4> environment_brdf_2 : register(t16, space0);
SamplerState environment_sampler : register(s2, space0);

struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 model_position : TEXCOORD0;
    float3 world_position : TEXCOORD1;
    float3 world_normal : TEXCOORD2;
    float3 color : COLOR0;
    float2 uv : TEXCOORD3;
};

VertexOut vice_probe_surface_vs(VertexIn input) {
    VertexOut output;
    const float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.model_position = input.position;
    output.world_position = world_position.xyz;
    output.world_normal = normalize(mul((float3x3)model, input.normal));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float3 vice_rotate_quaternion(float3 value, float4 rotation) {
    return value + 2.0 * cross(rotation.xyz, cross(rotation.xyz, value) + rotation.w * value);
}

float2 vice_environment_uv(float3 direction, float rotation_radians) {
    direction = normalize(direction);
    const float longitude = atan2(direction.z, direction.x) + rotation_radians;
    const float latitude = acos(clamp(direction.y, -1.0, 1.0));
    return float2(frac(longitude / (2.0 * VICE_PI) + 0.5), latitude / VICE_PI);
}

float3 vice_box_project_direction(
    float3 model_position,
    float3 world_direction,
    column_major float4x4 probe_from_model,
    float4 half_extents_enabled,
    float4 probe_rotation
) {
    world_direction = normalize(world_direction);
    if (half_extents_enabled.w < 0.5) return world_direction;

    const float3 half_extents = half_extents_enabled.xyz;
    if (any(half_extents <= 0.0)) return world_direction;

    const float3 local_position = mul(probe_from_model, float4(model_position, 1.0)).xyz;
    if (any(abs(local_position) > half_extents + 0.001)) return world_direction;

    const float4 inverse_rotation = float4(-probe_rotation.xyz, probe_rotation.w);
    const float3 local_direction = normalize(vice_rotate_quaternion(world_direction, inverse_rotation));
    const float3 direction_sign = lerp(float3(-1.0, -1.0, -1.0), float3(1.0, 1.0, 1.0), step(0.0, local_direction));
    const float3 safe_direction = direction_sign * max(abs(local_direction), float3(0.00001, 0.00001, 0.00001));
    const float3 first = (-half_extents - local_position) / safe_direction;
    const float3 second = (half_extents - local_position) / safe_direction;
    const float3 far_distance = max(first, second);
    const float distance = min(far_distance.x, min(far_distance.y, far_distance.z));
    if (distance <= 0.0 || distance > 1000000.0) return world_direction;

    const float3 local_hit = local_position + local_direction * distance;
    if (dot(local_hit, local_hit) <= 0.0000001) return world_direction;
    return normalize(vice_rotate_quaternion(normalize(local_hit), probe_rotation));
}

float3 vice_surface_normal(VertexOut input) {
    const float3 geometric_normal = normalize(input.world_normal);
    const float3 sampled_normal = normal_texture.Sample(material_sampler, input.uv).xyz * 2.0 - 1.0;
    const float3 position_dx = ddx(input.world_position);
    const float3 position_dy = ddy(input.world_position);
    const float2 uv_dx = ddx(input.uv);
    const float2 uv_dy = ddy(input.uv);
    const float determinant = uv_dx.x * uv_dy.y - uv_dx.y * uv_dy.x;
    if (abs(determinant) <= 0.000001) return geometric_normal;
    const float inverse = 1.0 / determinant;
    const float3 tangent = normalize((position_dx * uv_dy.y - position_dy * uv_dx.y) * inverse);
    const float3 bitangent = normalize((-position_dx * uv_dy.x + position_dy * uv_dx.x) * inverse);
    const float3 mapped = normalize(float3(sampled_normal.xy * normal_scale, sampled_normal.z));
    return normalize(tangent * mapped.x + bitangent * mapped.y + geometric_normal * mapped.z);
}

float3 vice_environment_source_0(
    float3 model_position,
    float3 normal,
    float3 reflection_direction,
    float3 f0,
    float normal_view,
    float roughness_value,
    float occlusion
) {
    const float4 metadata = environment_metadata;
    const float3 projected = vice_box_project_direction(
        model_position,
        reflection_direction,
        environment_probe_from_model_0,
        environment_probe_half_extents_0,
        environment_probe_rotation_0
    );
    const float3 diffuse = environment_irradiance_0.Sample(
        environment_sampler,
        vice_environment_uv(normal, metadata.y)
    ).rgb;
    const float3 specular = environment_specular_0.SampleLevel(
        environment_sampler,
        vice_environment_uv(projected, metadata.y),
        saturate(roughness_value) * metadata.z
    ).rgb;
    const float2 brdf = environment_brdf_0.Sample(
        environment_sampler,
        float2(saturate(normal_view), saturate(roughness_value))
    ).rg;
    const float3 fresnel = f0 * brdf.x + brdf.y;
    return (diffuse * (1.0 - f0) * occlusion + specular * fresnel) * metadata.x * metadata.w;
}

float3 vice_environment_source_1(
    float3 model_position,
    float3 normal,
    float3 reflection_direction,
    float3 f0,
    float normal_view,
    float roughness_value,
    float occlusion
) {
    const float4 metadata = environment_secondary_metadata;
    const float3 projected = vice_box_project_direction(
        model_position,
        reflection_direction,
        environment_probe_from_model_1,
        environment_probe_half_extents_1,
        environment_probe_rotation_1
    );
    const float3 diffuse = environment_irradiance_1.Sample(environment_sampler, vice_environment_uv(normal, metadata.y)).rgb;
    const float3 specular = environment_specular_1.SampleLevel(
        environment_sampler,
        vice_environment_uv(projected, metadata.y),
        saturate(roughness_value) * metadata.z
    ).rgb;
    const float2 brdf = environment_brdf_1.Sample(environment_sampler, float2(saturate(normal_view), saturate(roughness_value))).rg;
    const float3 fresnel = f0 * brdf.x + brdf.y;
    return (diffuse * (1.0 - f0) * occlusion + specular * fresnel) * metadata.x * metadata.w;
}

float3 vice_environment_source_2(
    float3 model_position,
    float3 normal,
    float3 reflection_direction,
    float3 f0,
    float normal_view,
    float roughness_value,
    float occlusion
) {
    const float4 metadata = environment_tertiary_metadata;
    const float3 projected = vice_box_project_direction(
        model_position,
        reflection_direction,
        environment_probe_from_model_2,
        environment_probe_half_extents_2,
        environment_probe_rotation_2
    );
    const float3 diffuse = environment_irradiance_2.Sample(environment_sampler, vice_environment_uv(normal, metadata.y)).rgb;
    const float3 specular = environment_specular_2.SampleLevel(
        environment_sampler,
        vice_environment_uv(projected, metadata.y),
        saturate(roughness_value) * metadata.z
    ).rgb;
    const float2 brdf = environment_brdf_2.Sample(environment_sampler, float2(saturate(normal_view), saturate(roughness_value))).rg;
    const float3 fresnel = f0 * brdf.x + brdf.y;
    return (diffuse * (1.0 - f0) * occlusion + specular * fresnel) * metadata.x * metadata.w;
}

float4 vice_probe_surface_fs(VertexOut input) : SV_Target0 {
    const float4 base_sample = base_color_texture.Sample(material_sampler, input.uv) * base_color;
    const float4 orm = orm_texture.Sample(material_sampler, input.uv);
    const float3 emissive_sample = emissive_texture.Sample(material_sampler, input.uv).rgb;
    const float3 normal = vice_surface_normal(input);
    const float3 view_direction = normalize(camera_position.xyz - input.world_position);
    const float normal_view = saturate(dot(normal, view_direction));
    const float roughness_value = saturate(roughness * orm.g);
    const float metallic_value = saturate(metallic * orm.b);
    const float occlusion = saturate(orm.r);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), base_sample.rgb, metallic_value);
    const float3 reflection_direction = reflect(-view_direction, normal);

    float3 environment = vice_environment_source_0(
        input.model_position, normal, reflection_direction, f0, normal_view, roughness_value, occlusion
    );
    environment += vice_environment_source_1(
        input.model_position, normal, reflection_direction, f0, normal_view, roughness_value, occlusion
    );
    environment += vice_environment_source_2(
        input.model_position, normal, reflection_direction, f0, normal_view, roughness_value, occlusion
    );

    const float3 emissive = emissive_sample * emissive_color.rgb * emissive_intensity;
    return float4(environment * base_sample.rgb + emissive, base_sample.a);
}
