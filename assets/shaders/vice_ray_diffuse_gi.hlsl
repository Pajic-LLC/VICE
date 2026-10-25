static const float VICE_PI = 3.14159265358979323846;
static const float VICE_BACKGROUND_DEPTH = 0.999999;
static const uint VICE_INVALID_SCENE_INSTANCE = 0xFFFFFFFFu;

cbuffer DiffuseGiFrame : register(b0, space0) {
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
RWTexture2D<float4> DiffuseGiRadiance : register(u0, space0);

struct DiffuseGiHit {
    float3 world_position;
    uint instance_id_plus_one;
};
RWStructuredBuffer<DiffuseGiHit> DiffuseGiHits : register(u1, space0);

uint vice_hash_u32(uint value) {
    value ^= value >> 16u;
    value *= 0x7FEB352Du;
    value ^= value >> 15u;
    value *= 0x846CA68Bu;
    value ^= value >> 16u;
    return value;
}

float vice_hash_unit(uint value) {
    return float(vice_hash_u32(value) & 0x00FFFFFFu) / 16777216.0;
}

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

float3 vice_environment_radiance(float3 direction) {
    return max(
        EnvironmentSpecular.SampleLevel(
            EnvironmentSampler,
            vice_environment_uv(direction, environment_parameters.y),
            0.0
        ).rgb,
        0.0
    );
}

float3 vice_cosine_hemisphere_direction(float3 normal, uint2 pixel) {
    normal = normalize(normal);
    const uint frame_index = (uint)trace_parameters.z;
    const uint pixel_seed = vice_hash_u32(pixel.x ^ vice_hash_u32(pixel.y + 0x9E3779B9u));
    const uint seed = vice_hash_u32(pixel_seed ^ vice_hash_u32(frame_index + 0x85EBCA6Bu));
    const float u1 = vice_hash_unit(seed ^ 0xA511E9B3u);
    const float u2 = vice_hash_unit(seed ^ 0x63D83595u);
    const float radius = sqrt(saturate(u1));
    const float phi = 2.0 * VICE_PI * u2;
    const float3 local = float3(
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(1.0 - u1, 0.0))
    );
    const float3 helper = abs(normal.z) < 0.999
        ? float3(0.0, 0.0, 1.0)
        : float3(0.0, 1.0, 0.0);
    const float3 tangent = normalize(cross(helper, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

[numthreads(8, 8, 1)]
void vice_ray_diffuse_gi_cs(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint width = (uint)target_size_jitter.x;
    const uint height = (uint)target_size_jitter.y;
    const uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= width || pixel.y >= height) return;

    const uint linear_index = pixel.y * width + pixel.x;
    DiffuseGiHit hit;
    hit.world_position = float3(0.0, 0.0, 0.0);
    hit.instance_id_plus_one = 0u;
    DiffuseGiHits[linear_index] = hit;
    DiffuseGiRadiance[pixel] = float4(0.0, 0.0, 0.0, 0.0);

    const float device_depth = CameraDepth.Load(int3(pixel, 0));
    if (device_depth < 0.0 || device_depth >= VICE_BACKGROUND_DEPTH ||
        environment_parameters.w < 0.5) return;

    const float4 guide = SurfaceGuide.Load(int3(pixel, 0));
    const float3 normal = vice_decode_octahedral_normal(guide.xy);
    const float3 world_position = vice_world_position(pixel, device_depth);
    const float3 ray_direction = vice_cosine_hemisphere_direction(normal, pixel);
    if (dot(ray_direction, normal) <= 0.0) return;

    // A cosine-weighted diffuse sample has p(w) = cos(theta) / PI, so the
    // diffuse irradiance divided by PI is estimated directly by incoming radiance.
    DiffuseGiRadiance[pixel] = float4(vice_environment_radiance(ray_direction), 1.0);

    RayDesc ray;
    ray.Origin = world_position + normal * trace_parameters.y;
    ray.Direction = ray_direction;
    ray.TMin = trace_parameters.y;
    ray.TMax = trace_parameters.x;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(SceneTlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {
    }

    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT) return;

    // Preserve VICE's persistent 24-bit instance identity for validation/debugging,
    // but resolve Runtime scene data only through the TLAS array index map.
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
    DiffuseGiHits[linear_index] = hit;
}
