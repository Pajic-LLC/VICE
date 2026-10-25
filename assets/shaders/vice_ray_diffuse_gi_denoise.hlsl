cbuffer DiffuseGiDenoiseConstants : register(b0, space0) {
    float4 target_size_near_far;
    float4 filter_parameters;
};

Texture2D<float4> DiffuseGiRadiance : register(t0, space0);
Texture2D<float> CameraDepth : register(t1, space0);
Texture2D<float4> SurfaceGuide : register(t2, space0);
RWTexture2D<float4> FilteredDiffuseGi : register(u0, space0);

static const float VICE_GI_BACKGROUND_DEPTH = 0.999999;

float vice_gi_linear_depth(float device_depth) {
    const float near_plane = target_size_near_far.z;
    const float far_plane = target_size_near_far.w;
    const float denominator = far_plane - saturate(device_depth) * (far_plane - near_plane);
    return denominator > 0.0 ? (near_plane * far_plane) / denominator : 0.0;
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

float vice_gi_bilateral_weight(
    float center_device_depth,
    float neighbor_device_depth,
    float3 center_normal,
    float3 neighbor_normal,
    float pixel_distance_squared
) {
    if (center_device_depth >= VICE_GI_BACKGROUND_DEPTH ||
        neighbor_device_depth >= VICE_GI_BACKGROUND_DEPTH) {
        return 0.0;
    }

    const float center_depth = vice_gi_linear_depth(center_device_depth);
    const float neighbor_depth = vice_gi_linear_depth(neighbor_device_depth);
    if (center_depth <= 0.0 || neighbor_depth <= 0.0) return 0.0;

    const float depth_sigma = max(filter_parameters.x, 0.000001);
    const float normal_power = max(filter_parameters.y, 1.0);
    const float spatial_sigma = max(filter_parameters.z, 0.000001);
    const float relative_depth = abs(neighbor_depth - center_depth) /
        max(center_depth, max(target_size_near_far.z, 0.00001));
    const float depth_ratio = relative_depth / depth_sigma;
    const float spatial_ratio = pixel_distance_squared / (spatial_sigma * spatial_sigma);
    const float geometry_weight = exp(-0.5 * (depth_ratio * depth_ratio + spatial_ratio));
    const float normal_weight = pow(saturate(dot(center_normal, neighbor_normal)), normal_power);
    return geometry_weight * normal_weight;
}

[numthreads(8, 8, 1)]
void vice_ray_diffuse_gi_denoise_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint width = (uint)target_size_near_far.x;
    const uint height = (uint)target_size_near_far.y;
    const uint2 pixel = dispatch_id.xy;
    if (pixel.x >= width || pixel.y >= height) return;

    const float4 center_radiance = DiffuseGiRadiance.Load(int3(pixel, 0));
    const float center_device_depth = CameraDepth.Load(int3(pixel, 0));
    if (center_device_depth >= VICE_GI_BACKGROUND_DEPTH || center_radiance.a <= 0.0) {
        FilteredDiffuseGi[pixel] = float4(max(center_radiance.rgb, 0.0), saturate(center_radiance.a));
        return;
    }

    const float3 center_normal = vice_decode_octahedral_normal(
        SurfaceGuide.Load(int3(pixel, 0)).xy
    );
    const int radius = clamp((int)filter_parameters.w, 1, 4);
    float3 weighted_radiance = 0.0;
    float weight_sum = 0.0;

    [loop]
    for (int y = -radius; y <= radius; ++y) {
        [loop]
        for (int x = -radius; x <= radius; ++x) {
            const int2 candidate = int2(pixel) + int2(x, y);
            if (candidate.x < 0 || candidate.y < 0 ||
                candidate.x >= (int)width || candidate.y >= (int)height) {
                continue;
            }

            const float4 neighbor_radiance = DiffuseGiRadiance.Load(int3(candidate, 0));
            if (neighbor_radiance.a <= 0.0) continue;
            const float neighbor_device_depth = CameraDepth.Load(int3(candidate, 0));
            const float3 neighbor_normal = vice_decode_octahedral_normal(
                SurfaceGuide.Load(int3(candidate, 0)).xy
            );
            const float weight = vice_gi_bilateral_weight(
                center_device_depth,
                neighbor_device_depth,
                center_normal,
                neighbor_normal,
                (float)(x * x + y * y)
            );
            if (weight <= 0.0) continue;
            weighted_radiance += max(neighbor_radiance.rgb, 0.0) * weight;
            weight_sum += weight;
        }
    }

    const float3 filtered = weight_sum > 0.000001
        ? weighted_radiance / weight_sum
        : max(center_radiance.rgb, 0.0);
    FilteredDiffuseGi[pixel] = float4(max(filtered, 0.0), saturate(center_radiance.a));
}
