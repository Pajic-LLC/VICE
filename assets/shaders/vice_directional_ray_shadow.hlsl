cbuffer RayShadowFrame : register(b0, space0) {
    float4 camera_position_near;
    float4 camera_forward_far;
    float4 camera_right_tan_half_fov;
    float4 camera_up_aspect;
    float4 light_direction_bias;
    float4 target_size_jitter;
};

RaytracingAccelerationStructure SceneTlas : register(t0, space0);
Texture2D<float> CameraDepth : register(t1, space0);
RWTexture2D<float> ShadowVisibility : register(u0, space0);

[numthreads(8, 8, 1)]
void vice_directional_ray_shadow_cs(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint2 pixel = dispatch_thread_id.xy;
    const uint2 target_size = uint2(target_size_jitter.xy);
    if (pixel.x >= target_size.x || pixel.y >= target_size.y) {
        return;
    }

    const float device_depth = CameraDepth.Load(int3(pixel, 0));
    if (device_depth >= 0.999999) {
        ShadowVisibility[pixel] = 1.0;
        return;
    }

    const float near_plane = camera_position_near.w;
    const float far_plane = camera_forward_far.w;
    const float depth = saturate(device_depth);
    const float denominator = far_plane - depth * (far_plane - near_plane);
    if (denominator <= 0.000001) {
        ShadowVisibility[pixel] = 1.0;
        return;
    }
    const float axial_depth = (near_plane * far_plane) / denominator;

    const float2 uv = (float2(pixel) + 0.5) / target_size_jitter.xy;
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    ndc -= target_size_jitter.zw;

    const float tan_half_fov = camera_right_tan_half_fov.w;
    const float aspect = camera_up_aspect.w;
    const float3 camera_ray =
        camera_forward_far.xyz +
        camera_right_tan_half_fov.xyz * (ndc.x * aspect * tan_half_fov) +
        camera_up_aspect.xyz * (ndc.y * tan_half_fov);
    const float3 world_position = camera_position_near.xyz + camera_ray * axial_depth;

    const float3 toward_light = normalize(light_direction_bias.xyz);
    const float surface_bias = max(light_direction_bias.w, axial_depth * 0.00001);

    RayDesc ray;
    ray.Origin = world_position + toward_light * surface_bias;
    ray.Direction = toward_light;
    ray.TMin = surface_bias;
    ray.TMax = max(far_plane, 1.0);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(SceneTlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {
    }

    ShadowVisibility[pixel] =
        query.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}