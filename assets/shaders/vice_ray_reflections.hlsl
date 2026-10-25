static const float VICE_PI = 3.14159265358979323846;
static const float VICE_BACKGROUND_DEPTH = 0.999999;
static const uint VICE_INVALID_SCENE_INSTANCE = 0xFFFFFFFFu;

cbuffer ReflectionFrame : register(b0, space0) {
    float4 camera_position_near;
    float4 camera_forward_far;
    float4 camera_right_tan_half_fov;
    float4 camera_up_aspect;
    float4 target_size_jitter;
    float4 trace_parameters;
    float4 environment_parameters;
};

RaytracingAccelerationStructure SceneTlas : register(t0, space0);
Texture2D<float> CameraDepth : register(t1, space0);
Texture2D<float4> SurfaceGuide : register(t2, space0);
Texture2D<float4> EnvironmentSpecular : register(t3, space0);
StructuredBuffer<uint> SceneInstanceIndices : register(t4, space0);
SamplerState EnvironmentSampler : register(s0, space0);
RWTexture2D<float4> ReflectionRadiance : register(u0, space0);

struct ReflectionHit {
    float3 world_position;
    uint instance_id_plus_one;
};
RWStructuredBuffer<ReflectionHit> ReflectionHits : register(u1, space0);

float vice_linear_depth(float device_depth) {
    const float near_plane = camera_position_near.w;
    const float far_plane = camera_forward_far.w;
    const float denominator = far_plane - saturate(device_depth) * (far_plane - near_plane);
    return denominator > 0.0 ? (near_plane * far_plane) / denominator : 0.0;
}

float3 vice_world_position(uint2 pixel, float device_depth) {
    const float2 target_size = target_size_jitter.xy;
    const float2 uv = (float2(pixel) + 0.5) / target_size;
    const float ndc_x = uv.x * 2.0 - 1.0 - target_size_jitter.z;
    const float ndc_y = 1.0 - uv.y * 2.0 - target_size_jitter.w;
    const float right_scale = ndc_x * camera_up_aspect.w * camera_right_tan_half_fov.w;
    const float up_scale = ndc_y * camera_right_tan_half_fov.w;
    const float3 camera_ray = camera_forward_far.xyz +
        camera_right_tan_half_fov.xyz * right_scale + camera_up_aspect.xyz * up_scale;
    return camera_position_near.xyz + camera_ray * vice_linear_depth(device_depth);
}

float vice_sign_not_zero(float value) {
    return value >= 0.0 ? 1.0 : -1.0;
}

float3 vice_decode_octahedral_normal(float2 encoded) {
    float2 folded = saturate(encoded) * 2.0 - 1.0;
    float3 normal = float3(folded, 1.0 - abs(folded.x) - abs(folded.y));
    if (normal.z < 0.0) {
        const float old_x = normal.x;
        normal.x = (1.0 - abs(normal.y)) * vice_sign_not_zero(old_x);
        normal.y = (1.0 - abs(old_x)) * vice_sign_not_zero(normal.y);
    }
    return normalize(normal);
}

float2 vice_environment_uv(float3 direction, float rotation_radians) {
    direction = normalize(direction);
    const float longitude = atan2(direction.z, direction.x) + rotation_radians;
    const float latitude = acos(clamp(direction.y, -1.0, 1.0));
    return float2(frac(longitude / (2.0 * VICE_PI) + 0.5), latitude / VICE_PI);
}

float3 vice_environment_fallback(float3 reflection_direction, float roughness) {
    return max(
        EnvironmentSpecular.SampleLevel(
            EnvironmentSampler,
            vice_environment_uv(reflection_direction, environment_parameters.y),
            saturate(roughness) * environment_parameters.z
        ).rgb,
        0.0
    );
}

[numthreads(8, 8, 1)]
void vice_ray_reflections_cs(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint width = (uint)target_size_jitter.x;
    const uint height = (uint)target_size_jitter.y;
    const uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= width || pixel.y >= height) return;

    const uint linear_index = pixel.y * width + pixel.x;
    ReflectionHit hit;
    hit.world_position = float3(0.0, 0.0, 0.0);
    hit.instance_id_plus_one = 0u;
    ReflectionHits[linear_index] = hit;
    ReflectionRadiance[pixel] = float4(0.0, 0.0, 0.0, 0.0);

    const float device_depth = CameraDepth.Load(int3(pixel, 0));
    if (device_depth < 0.0 || device_depth >= VICE_BACKGROUND_DEPTH) return;

    const float4 guide = SurfaceGuide.Load(int3(pixel, 0));
    const float roughness = saturate(guide.z);
    if (roughness > trace_parameters.z || environment_parameters.w < 0.5) return;

    const float3 world_position = vice_world_position(pixel, device_depth);
    const float3 normal = vice_decode_octahedral_normal(guide.xy);
    const float3 to_camera = camera_position_near.xyz - world_position;
    const float to_camera_length_squared = dot(to_camera, to_camera);
    if (to_camera_length_squared <= 0.00000001) return;
    const float3 view_direction = to_camera * rsqrt(to_camera_length_squared);
    const float3 reflection_direction = normalize(reflect(-view_direction, normal));
    if (dot(reflection_direction, normal) <= 0.00001) return;

    const float3 fallback_radiance = vice_environment_fallback(reflection_direction, roughness);
    ReflectionRadiance[pixel] = float4(fallback_radiance, 1.0);

    RayDesc ray;
    ray.Origin = world_position + normal * trace_parameters.y;
    ray.Direction = reflection_direction;
    ray.TMin = trace_parameters.y;
    ray.TMax = trace_parameters.x * lerp(1.0, 0.35, roughness);

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(SceneTlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {
    }

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return;

    // Keep the authored 24-bit custom identity channel intact for deterministic identity/debugging,
    // but address Runtime scene data only through the TLAS array index plus VICE's authoritative map.
    const uint persistent_instance_id = query.CommittedInstanceID();
    const uint tlas_instance_index = query.CommittedInstanceIndex();
    const float hit_t = query.CommittedRayT();
    if (persistent_instance_id > (uint)trace_parameters.w || hit_t < ray.TMin || hit_t > ray.TMax) return;

    uint scene_index_count = 0u;
    uint scene_index_stride = 0u;
    SceneInstanceIndices.GetDimensions(scene_index_count, scene_index_stride);
    if (scene_index_stride != 4u || tlas_instance_index >= scene_index_count) return;

    const uint scene_instance_index = SceneInstanceIndices[tlas_instance_index];
    if (scene_instance_index == VICE_INVALID_SCENE_INSTANCE) return;

    hit.world_position = ray.Origin + ray.Direction * hit_t;
    hit.instance_id_plus_one = scene_instance_index + 1u;
    ReflectionHits[linear_index] = hit;
}
