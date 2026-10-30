static const uint VICE_MAX_LIGHTS = 8;
static const uint VICE_SHADOW_CASCADE_COUNT = 4;
static const float VICE_SHADOW_TRANSITION_FRACTION = .10;
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

    column_major float4x4 atmosphere_camera_from_model;
    float4 atmosphere_sun_direction_intensity;
    float4 atmosphere_sun_color;
    float4 atmosphere_parameters;
    float4 atmosphere_secondary;
    float4 atmosphere_ground_albedo;
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
Texture2D<float> directional_shadow_map : register(t4, space0);
Texture2D<float> directional_shadow_map_1 : register(t5, space0);
Texture2D<float> directional_shadow_map_2 : register(t6, space0);
Texture2D<float> directional_shadow_map_3 : register(t7, space0);
SamplerState directional_shadow_sampler : register(s1, space0);

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

VertexOut vice_surface_vs(VertexIn input) {
    VertexOut output;
    const float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.model_position = input.position;
    output.world_position = world_position.xyz;
    float3 a=float3(model[0][0],model[1][0],model[2][0]);
    float3 b=float3(model[0][1],model[1][1],model[2][1]);
    float3 c=float3(model[0][2],model[1][2],model[2][2]);
    float3 n=input.normal.x*cross(b,c)+input.normal.y*cross(c,a)+input.normal.z*cross(a,b);
    output.world_normal=normalize(n*(dot(a,cross(b,c))<0?-1:1));
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

float3 vice_atmosphere_radiance(float3 view_direction) {
    view_direction = normalize(view_direction);
    const float3 sun_direction = normalize(atmosphere_sun_direction_intensity.xyz);
    const float sun_intensity = max(atmosphere_sun_direction_intensity.w, 0.0);
    const float3 sun_color = max(atmosphere_sun_color.rgb, 0.0);
    const float rayleigh_strength = max(atmosphere_parameters.x, 0.0);
    const float mie_strength = max(atmosphere_parameters.y, 0.0);
    const float g = clamp(atmosphere_parameters.z, 0.0, 0.98);
    const float sun_radius = max(atmosphere_secondary.x, 0.0001);
    const float horizon_falloff = max(atmosphere_secondary.y, 0.01);
    const float mu = clamp(dot(view_direction, sun_direction), -1.0, 1.0);
    const float rayleigh_phase = (3.0 / (16.0 * VICE_PI)) * (1.0 + mu * mu);
    const float mie_denominator = max(1.0 + g * g - 2.0 * g * mu, 0.0001);
    const float mie_phase = ((1.0 - g * g) / (4.0 * VICE_PI)) / pow(mie_denominator, 1.5);
    const float altitude = max(view_direction.y, 0.0);
    const float air_mass = 1.0 / max(0.10 + altitude * 0.90, 0.10);
    const float3 beta_rayleigh = float3(0.0058, 0.0135, 0.0331) * rayleigh_strength;
    const float3 beta_mie = float3(0.021, 0.021, 0.021) * mie_strength;
    const float3 extinction = exp(-(beta_rayleigh + beta_mie) * air_mass * 1.35);
    float3 sky = sun_color * sun_intensity *
        (beta_rayleigh * rayleigh_phase + beta_mie * mie_phase) * air_mass * 24.0;
    sky *= 0.35 + 0.65 * (1.0 - extinction);
    const float horizon = exp(-altitude / horizon_falloff);
    sky += sun_color * sun_intensity * horizon * float3(0.018, 0.011, 0.006);
    const float sun_disk = smoothstep(cos(sun_radius * 2.2), cos(sun_radius * 0.55), mu);
    sky += sun_color * sun_intensity * sun_disk * 7.5;
    const float sun_height = saturate(sun_direction.y * 0.5 + 0.5);
    const float3 ground = max(atmosphere_ground_albedo.rgb, 0.0) *
        sun_color * (0.12 + sun_intensity * 0.07 * sun_height);
    return max(lerp(ground, sky, smoothstep(-0.08, 0.025, view_direction.y)), 0.0);
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

float directional_shadow_split(uint cascade_index) {
    if (cascade_index == 0) return shadow_cascade_splits.x;
    if (cascade_index == 1) return shadow_cascade_splits.y;
    if (cascade_index == 2) return shadow_cascade_splits.z;
    return shadow_cascade_splits.w;
}

float directional_shadow_cascade_visibility(
    uint cascade_index,
    float3 world_position,
    float3 world_normal
) {
    const float4 shadow_position = directional_shadow_position(cascade_index, world_position);
    if (abs(shadow_position.w) < 0.000001) return 1.0;

    const float3 projected = shadow_position.xyz / shadow_position.w;
    if (projected.z < 0.0 || projected.z > 1.0) return 1.0;

    const float2 uv = float2(
        projected.x * 0.5 + 0.5,
        projected.y * -0.5 + 0.5
    );
    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0) return 1.0;

    const uint shadow_light_index = (uint)shadow_metadata.y;
    const float3 light_direction = normalize(-lights[shadow_light_index].position_or_direction.xyz);
    const float normal_light = saturate(dot(normalize(world_normal), light_direction));
    const float slope_bias = (1.0 - normal_light) * 1.25;
    const float compare_depth = projected.z - shadow_metadata.w *
        (1.0 + 0.35 * cascade_index + slope_bias);
    const float filter_radius = 1.0 + 0.25 * cascade_index;
    const float texel = shadow_metadata.z * filter_radius;

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

float directional_shadow_visibility(
    float2 pixel_position,
    float3 world_position,
    float3 world_normal
) {
    if (shadow_metadata.x < 0.5) return 1.0;
    if (shadow_camera_forward.w > 0.5) {
        return saturate(
            directional_shadow_map.Load(int3(int2(pixel_position), 0))
        );
    }

    const float world_depth = max(
        dot(world_position - camera_position.xyz, shadow_camera_forward.xyz),
        0.0
    );
    if (world_depth > shadow_cascade_splits.w) return 1.0;

    const uint cascade_index = directional_shadow_cascade(world_depth);
    const float primary_visibility = directional_shadow_cascade_visibility(
        cascade_index, world_position, world_normal
    );
    if (cascade_index + 1 >= VICE_SHADOW_CASCADE_COUNT) return primary_visibility;

    const float split_far = directional_shadow_split(cascade_index);
    const float split_near = cascade_index == 0 ? 0.0 : directional_shadow_split(cascade_index - 1);
    const float transition_width = max(
        (split_far - split_near) * VICE_SHADOW_TRANSITION_FRACTION,
        0.0001
    );
    const float blend = saturate(
        (world_depth - (split_far - transition_width)) / transition_width
    );
    if (blend <= 0.0) return primary_visibility;

    const float secondary_visibility = directional_shadow_cascade_visibility(
        cascade_index + 1, world_position, world_normal
    );
    return lerp(primary_visibility, secondary_visibility, blend);
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
    const float4 base_sample = base_color_texture.Sample(material_sampler, input.uv) * base_color * float4(input.color,1);
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
    float3 direct = 0;
    for (uint i=0;i<min((uint)light_metadata.x,VICE_MAX_LIGHTS);++i) {
        const float visibility = shadow_metadata.x>.5 && i==(uint)shadow_metadata.y
            ? directional_shadow_visibility(input.position.xy,input.world_position,normal) : 1;
        direct += visibility * evaluate_light(lights[i],input.world_position,normal,view_direction,
                                               base_sample.rgb,metallic_value,roughness_value);
    }
    float3 surface = environment * base_sample.rgb + direct + emissive;
    if (atmosphere_secondary.z >= 0.5) {
        const float3 camera_relative = mul(
            atmosphere_camera_from_model,
            float4(input.model_position, 1.0)
        ).xyz;
        const float distance_to_camera = length(camera_relative);
        if (distance_to_camera > 0.0001) {
            const float density = max(atmosphere_parameters.w, 0.0);
            const float transmittance = exp(-density * distance_to_camera);
            const float3 aerial_radiance = vice_atmosphere_radiance(
                normalize(camera_relative)
            );
            surface = surface * transmittance + aerial_radiance * (1.0 - transmittance);
        }
    }
    return float4(surface, base_sample.a);
}
