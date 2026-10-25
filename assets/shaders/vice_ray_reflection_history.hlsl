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

bool vice_history_depth_matches(float expected_depth, float sampled_depth, float threshold) {
    if (sampled_depth < 0.0 || sampled_depth >= VICE_BACKGROUND_DEPTH) return false;
    const float relative_threshold = max(
        threshold,
        threshold * 8.0 * max(abs(expected_depth), abs(sampled_depth))
    );
    return abs(expected_depth - sampled_depth) <= relative_threshold;
}

bool vice_history_sample(uint2 history_pixel, float expected_depth, float threshold, out float3 radiance) {
    const float sampled_depth = PreviousDepth.Load(int3(history_pixel, 0));
    if (!vice_history_depth_matches(expected_depth, sampled_depth, threshold)) return false;
    radiance = PreviousSceneColor.Load(int3(history_pixel, 0)).rgb;
    return vice_finite3(radiance);
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

    const float2 history_position = float2(
        (previous_ndc.x * 0.5 + 0.5) * float(width) - 0.5,
        (previous_ndc.y * -0.5 + 0.5) * float(height) - 0.5
    );
    const int2 base_pixel = int2(floor(history_position));
    const float2 fraction = frac(history_position);
    const float depth_threshold = max(target_size_history.w, 0.000001);

    float3 reconstructed = 0.0;
    float total_weight = 0.0;
    [unroll]
    for (uint sample_index = 0u; sample_index < 4u; ++sample_index) {
        const int2 offset = int2(sample_index & 1u, sample_index >> 1u);
        const int2 candidate = base_pixel + offset;
        if (candidate.x < 0 || candidate.y < 0 || candidate.x >= int(width) || candidate.y >= int(height)) continue;
        const float2 sample_weight_2d = lerp(1.0 - fraction, fraction, float2(offset));
        const float weight = sample_weight_2d.x * sample_weight_2d.y;
        if (weight <= 0.0) continue;
        float3 history_radiance = 0.0;
        if (!vice_history_sample(uint2(candidate), previous_ndc.z, depth_threshold, history_radiance)) continue;
        reconstructed += max(history_radiance, 0.0) * weight;
        total_weight += weight;
    }

    // Fail closed on disocclusion or insufficient support: keep the current environment fallback.
    // Requiring majority bilinear support avoids pulling a single stale neighbor across an edge.
    if (total_weight < 0.5) return;
    ReflectionRadiance[pixel] = float4(reconstructed / total_weight, current_radiance.a);
}
