static const float VICE_BACKGROUND_DEPTH = 0.999999;

cbuffer ReflectionHistoryFrame : register(b0, space0) {
    column_major float4x4 previous_view_projection;
    float4 target_size_history;
};

struct ReflectionHit {
    float3 world_position;
    uint instance_id_plus_one;
};

StructuredBuffer<ReflectionHit> ReflectionHits : register(t0, space0);
Texture2D<float4> PreviousSceneColor : register(t1, space0);
Texture2D<float> PreviousDepth : register(t2, space0);
RWTexture2D<float4> ReflectionRadiance : register(u0, space0);

bool vice_finite4(float4 value) {
    return all(abs(value) < 1.0e20);
}

bool vice_finite3(float3 value) {
    return all(abs(value) < 1.0e20);
}

[numthreads(8, 8, 1)]
void vice_ray_reflection_history_cs(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    const uint width = (uint)target_size_history.x;
    const uint height = (uint)target_size_history.y;
    const uint2 pixel = dispatch_thread_id.xy;
    if (pixel.x >= width || pixel.y >= height || target_size_history.z < 0.5) return;

    const float4 current_radiance = ReflectionRadiance[pixel];
    if (current_radiance.a <= 0.0) return;

    const ReflectionHit hit = ReflectionHits[pixel.y * width + pixel.x];
    if (hit.instance_id_plus_one == 0u || !vice_finite3(hit.world_position)) return;

    const float4 previous_clip = mul(previous_view_projection, float4(hit.world_position, 1.0));
    if (!vice_finite4(previous_clip) || previous_clip.w <= 0.000001) return;

    const float3 previous_ndc = previous_clip.xyz / previous_clip.w;
    if (!vice_finite3(previous_ndc) || previous_ndc.x < -1.0 || previous_ndc.x > 1.0 ||
        previous_ndc.y < -1.0 || previous_ndc.y > 1.0 ||
        previous_ndc.z < 0.0 || previous_ndc.z >= VICE_BACKGROUND_DEPTH) {
        return;
    }

    const float2 previous_uv = float2(
        previous_ndc.x * 0.5 + 0.5,
        previous_ndc.y * -0.5 + 0.5
    );
    const uint2 history_pixel = min(
        (uint2)(previous_uv * float2(width, height)),
        uint2(width - 1u, height - 1u)
    );
    const float previous_depth = PreviousDepth.Load(int3(history_pixel, 0));
    if (previous_depth < 0.0 || previous_depth >= VICE_BACKGROUND_DEPTH) return;

    const float depth_threshold = max(target_size_history.w, 0.000001);
    const float relative_depth_threshold = max(
        depth_threshold,
        depth_threshold * 8.0 * max(abs(previous_ndc.z), abs(previous_depth))
    );
    if (abs(previous_ndc.z - previous_depth) > relative_depth_threshold) return;

    const float3 history_radiance = PreviousSceneColor.Load(int3(history_pixel, 0)).rgb;
    if (!vice_finite3(history_radiance)) return;
    ReflectionRadiance[pixel] = float4(max(history_radiance, 0.0), current_radiance.a);
}
