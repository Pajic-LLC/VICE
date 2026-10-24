cbuffer RayAmbientOcclusionFrame : register(b0, space0) {
    float4 camera_position_near;
    float4 camera_forward_far;
    float4 camera_right_tan_half_fov;
    float4 camera_up_aspect;
    float4 target_size_jitter;
    float4 ao_parameters;
};

RaytracingAccelerationStructure SceneTlas : register(t0, space0);
Texture2D<float> CameraDepth : register(t1, space0);
RWTexture2D<float> AmbientOcclusionVisibility : register(u0, space0);

static const float VICE_BACKGROUND_DEPTH = 0.999999;
static const float VICE_TWO_PI = 6.28318530717958647692;
static const uint VICE_RANDOM_MASK = 0x00ffffffu;

uint vice_ao_hash(uint value) {
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float vice_ao_random(uint value) {
    return float(vice_ao_hash(value) & VICE_RANDOM_MASK) / 16777216.0;
}

bool vice_ao_valid_pixel(int2 pixel, uint2 target_size) {
    return pixel.x >= 0 && pixel.y >= 0 &&
        pixel.x < int(target_size.x) && pixel.y < int(target_size.y);
}

bool vice_ao_valid_depth(float depth) {
    return isfinite(depth) && depth >= 0.0 && depth < VICE_BACKGROUND_DEPTH;
}

float vice_ao_linear_depth(float device_depth) {
    const float near_plane = camera_position_near.w;
    const float far_plane = camera_forward_far.w;
    const float depth = saturate(device_depth);
    const float denominator = far_plane - depth * (far_plane - near_plane);
    return denominator <= 0.000001 ? 0.0 : (near_plane * far_plane) / denominator;
}

bool vice_ao_reconstruct_world_position(
    int2 pixel,
    uint2 target_size,
    out float3 world_position,
    out float axial_depth
) {
    world_position = float3(0.0, 0.0, 0.0);
    axial_depth = 0.0;
    if (!vice_ao_valid_pixel(pixel, target_size)) return false;

    const float device_depth = CameraDepth.Load(int3(pixel, 0));
    if (!vice_ao_valid_depth(device_depth)) return false;
    axial_depth = vice_ao_linear_depth(device_depth);
    if (axial_depth <= 0.0) return false;

    const float2 uv = (float2(pixel) + 0.5) / target_size_jitter.xy;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    ndc -= target_size_jitter.zw;

    const float tan_half_fov = camera_right_tan_half_fov.w;
    const float aspect = camera_up_aspect.w;
    const float3 camera_ray =
        camera_forward_far.xyz +
        camera_right_tan_half_fov.xyz * (ndc.x * aspect * tan_half_fov) +
        camera_up_aspect.xyz * (ndc.y * tan_half_fov);
    world_position = camera_position_near.xyz + camera_ray * axial_depth;
    return true;
}

bool vice_ao_preferred_neighbor(
    int2 negative_pixel,
    int2 positive_pixel,
    uint2 target_size,
    float center_depth,
    out float3 neighbor_position,
    out int direction_sign
) {
    float3 negative_position;
    float negative_depth;
    const bool negative_valid = vice_ao_reconstruct_world_position(
        negative_pixel, target_size, negative_position, negative_depth
    );
    float3 positive_position;
    float positive_depth;
    const bool positive_valid = vice_ao_reconstruct_world_position(
        positive_pixel, target_size, positive_position, positive_depth
    );

    neighbor_position = float3(0.0, 0.0, 0.0);
    direction_sign = 0;
    if (!negative_valid && !positive_valid) return false;
    if (!negative_valid) {
        neighbor_position = positive_position;
        direction_sign = 1;
        return true;
    }
    if (!positive_valid) {
        neighbor_position = negative_position;
        direction_sign = -1;
        return true;
    }

    if (abs(negative_depth - center_depth) <= abs(positive_depth - center_depth)) {
        neighbor_position = negative_position;
        direction_sign = -1;
    } else {
        neighbor_position = positive_position;
        direction_sign = 1;
    }
    return true;
}

float3 vice_ao_hemisphere_direction(float3 normal, uint2 pixel) {
    const uint sample_seed = uint(ao_parameters.z) & VICE_RANDOM_MASK;
    const uint seed = sample_seed ^
        (pixel.x * 0x9e3779b9u) ^ (pixel.y * 0x85ebca6bu);
    const float u1 = vice_ao_random(seed ^ 0xa511e9b3u);
    const float u2 = vice_ao_random(seed ^ 0x63d83595u);
    const float radius = sqrt(saturate(u1));
    const float phi = VICE_TWO_PI * u2;
    const float3 local_direction = float3(
        radius * cos(phi),
        radius * sin(phi),
        sqrt(max(0.0, 1.0 - u1))
    );

    const float3 helper = abs(normal.z) < 0.999
        ? float3(0.0, 0.0, 1.0)
        : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(helper, normal));
    const float3 bitangent = cross(normal, tangent);
    return normalize(
        tangent * local_direction.x +
        bitangent * local_direction.y +
        normal * local_direction.z
    );
}

[numthreads(8, 8, 1)]
void vice_ray_ambient_occlusion_cs(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint2 pixel = dispatch_thread_id.xy;
    const uint2 target_size = uint2(target_size_jitter.xy);
    if (pixel.x >= target_size.x || pixel.y >= target_size.y) return;

    float3 world_position;
    float center_depth;
    if (!vice_ao_reconstruct_world_position(
            int2(pixel), target_size, world_position, center_depth
        )) {
        AmbientOcclusionVisibility[pixel] = 1.0;
        return;
    }

    float3 x_neighbor;
    int x_sign;
    const bool x_valid = vice_ao_preferred_neighbor(
        int2(pixel) + int2(-1, 0),
        int2(pixel) + int2(1, 0),
        target_size,
        center_depth,
        x_neighbor,
        x_sign
    );
    float3 y_neighbor;
    int y_sign;
    const bool y_valid = vice_ao_preferred_neighbor(
        int2(pixel) + int2(0, -1),
        int2(pixel) + int2(0, 1),
        target_size,
        center_depth,
        y_neighbor,
        y_sign
    );
    if (!x_valid || !y_valid) {
        AmbientOcclusionVisibility[pixel] = 1.0;
        return;
    }

    const float3 tangent_x = x_sign < 0
        ? world_position - x_neighbor
        : x_neighbor - world_position;
    const float3 tangent_y = y_sign < 0
        ? world_position - y_neighbor
        : y_neighbor - world_position;
    const float tangent_x_length = dot(tangent_x, tangent_x);
    const float tangent_y_length = dot(tangent_y, tangent_y);
    if (tangent_x_length <= 1.0e-12 || tangent_y_length <= 1.0e-12) {
        AmbientOcclusionVisibility[pixel] = 1.0;
        return;
    }

    float3 normal = normalize(cross(tangent_y, tangent_x));
    const float3 toward_camera = camera_position_near.xyz - world_position;
    if (dot(normal, toward_camera) < 0.0) normal = -normal;
    const float3 ray_direction = vice_ao_hemisphere_direction(normal, pixel);

    const float radius = ao_parameters.x;
    const float surface_bias = max(ao_parameters.y, center_depth * 0.00001);
    RayDesc ray;
    ray.Origin = world_position + normal * surface_bias;
    ray.Direction = ray_direction;
    ray.TMin = surface_bias;
    ray.TMax = radius;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(SceneTlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {
    }

    AmbientOcclusionVisibility[pixel] =
        query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}
