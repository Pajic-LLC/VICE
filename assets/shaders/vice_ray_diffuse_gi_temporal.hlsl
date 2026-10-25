cbuffer DiffuseGiTemporalConstants : register(b0, space0) {
    float4 target_size_history;
    float4 rejection_parameters;
};

Texture2D<float4> CurrentDiffuseGi : register(t0, space0);
Texture2D<float4> HistoryDiffuseGi : register(t1, space0);
Texture2D<float4> MotionVectors : register(t2, space0);
Texture2D<float> CurrentDepth : register(t3, space0);
Texture2D<float> PreviousDepth : register(t4, space0);
RWTexture2D<float4> ResolvedDiffuseGi : register(u0, space0);

static const float VICE_GI_BACKGROUND_DEPTH = 0.999999;

[numthreads(8, 8, 1)]
void vice_ray_diffuse_gi_temporal_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint2 target_size = uint2(target_size_history.xy);
    const uint2 pixel = dispatch_id.xy;
    if (pixel.x >= target_size.x || pixel.y >= target_size.y) return;

    const float4 current_sample = CurrentDiffuseGi.Load(int3(pixel, 0));
    const float3 current = max(current_sample.rgb, 0.0);
    const float current_alpha = saturate(current_sample.a);
    const float center_depth = CurrentDepth.Load(int3(pixel, 0));
    if (center_depth >= VICE_GI_BACKGROUND_DEPTH || current_alpha <= 0.0 ||
        target_size_history.z < 0.5) {
        ResolvedDiffuseGi[pixel] = float4(current, current_alpha);
        return;
    }

    const float2 velocity = MotionVectors.Load(int3(pixel, 0)).xy;
    const float2 uv = (float2(pixel) + 0.5) / float2(target_size);
    const float2 history_uv = uv - velocity;
    if (history_uv.x < 0.0 || history_uv.x > 1.0 ||
        history_uv.y < 0.0 || history_uv.y > 1.0) {
        ResolvedDiffuseGi[pixel] = float4(current, current_alpha);
        return;
    }

    const uint2 history_pixel = min(
        uint2(history_uv * float2(target_size)),
        target_size - 1u
    );
    const float previous_z = PreviousDepth.Load(int3(history_pixel, 0));
    const float depth_threshold = max(rejection_parameters.x, 0.000001);
    const float relative_depth_threshold = max(
        depth_threshold,
        depth_threshold * 8.0 * max(abs(center_depth), abs(previous_z))
    );
    if (previous_z >= VICE_GI_BACKGROUND_DEPTH ||
        abs(center_depth - previous_z) > relative_depth_threshold) {
        ResolvedDiffuseGi[pixel] = float4(current, current_alpha);
        return;
    }

    float3 neighborhood_min = current;
    float3 neighborhood_max = current;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 sample_pixel = clamp(
                int2(pixel) + int2(x, y),
                int2(0, 0),
                int2(target_size) - 1
            );
            const float4 sample = CurrentDiffuseGi.Load(int3(sample_pixel, 0));
            if (sample.a <= 0.0) continue;
            const float3 sample_radiance = max(sample.rgb, 0.0);
            neighborhood_min = min(neighborhood_min, sample_radiance);
            neighborhood_max = max(neighborhood_max, sample_radiance);
        }
    }

    const float4 history_sample = HistoryDiffuseGi.Load(int3(history_pixel, 0));
    if (history_sample.a <= 0.0) {
        ResolvedDiffuseGi[pixel] = float4(current, current_alpha);
        return;
    }

    const float3 history = clamp(
        max(history_sample.rgb, 0.0),
        neighborhood_min,
        neighborhood_max
    );
    const float motion_factor = saturate(1.0 - length(velocity) * rejection_parameters.y);
    const float history_weight = saturate(target_size_history.w) * motion_factor;
    const float3 resolved = max(lerp(current, history, history_weight), 0.0);
    ResolvedDiffuseGi[pixel] = float4(resolved, current_alpha);
}
