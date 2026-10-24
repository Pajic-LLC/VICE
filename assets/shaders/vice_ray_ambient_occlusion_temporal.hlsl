cbuffer ViceRayAmbientOcclusionTemporalConstants : register(b0) {
    float4 target_size_history;
    float4 rejection_parameters;
};

Texture2D<float> current_visibility : register(t0);
Texture2D<float> history_visibility : register(t1);
Texture2D<float4> motion_vectors : register(t2);
Texture2D<float> current_depth : register(t3);
Texture2D<float> previous_depth : register(t4);
RWTexture2D<float> resolved_visibility : register(u0);

static const float VICE_AO_BACKGROUND_DEPTH = 0.999999;

[numthreads(8, 8, 1)]
void vice_ray_ambient_occlusion_temporal_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint2 target_size = uint2(target_size_history.xy);
    const uint2 pixel = dispatch_id.xy;
    if (pixel.x >= target_size.x || pixel.y >= target_size.y) return;

    const float center_depth = current_depth.Load(int3(pixel, 0));
    if (center_depth >= VICE_AO_BACKGROUND_DEPTH) {
        resolved_visibility[pixel] = 1.0;
        return;
    }

    const float current = saturate(current_visibility.Load(int3(pixel, 0)));
    if (target_size_history.z < 0.5) {
        resolved_visibility[pixel] = current;
        return;
    }

    const float2 velocity = motion_vectors.Load(int3(pixel, 0)).xy;
    const float2 uv = (float2(pixel) + 0.5) / float2(target_size);
    const float2 history_uv = uv - velocity;
    if (history_uv.x < 0.0 || history_uv.x > 1.0 ||
        history_uv.y < 0.0 || history_uv.y > 1.0) {
        resolved_visibility[pixel] = current;
        return;
    }

    const uint2 history_pixel = min(
        uint2(history_uv * float2(target_size)),
        target_size - 1u
    );
    const float previous_z = previous_depth.Load(int3(history_pixel, 0));
    const float depth_threshold = rejection_parameters.x;
    const float relative_depth_threshold = max(
        depth_threshold,
        depth_threshold * 8.0 * max(abs(center_depth), abs(previous_z))
    );
    if (previous_z >= VICE_AO_BACKGROUND_DEPTH ||
        abs(center_depth - previous_z) > relative_depth_threshold) {
        resolved_visibility[pixel] = current;
        return;
    }

    float neighborhood_min = current;
    float neighborhood_max = current;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 sample_pixel = clamp(
                int2(pixel) + int2(x, y),
                int2(0, 0),
                int2(target_size) - 1
            );
            const float sample_visibility = saturate(
                current_visibility.Load(int3(sample_pixel, 0))
            );
            neighborhood_min = min(neighborhood_min, sample_visibility);
            neighborhood_max = max(neighborhood_max, sample_visibility);
        }
    }

    float history = saturate(history_visibility.Load(int3(history_pixel, 0)));
    history = clamp(history, neighborhood_min, neighborhood_max);
    const float motion_factor = saturate(1.0 - length(velocity) * rejection_parameters.y);
    const float history_weight = saturate(target_size_history.w) * motion_factor;
    resolved_visibility[pixel] = saturate(lerp(current, history, history_weight));
}
