cbuffer DenoiseConstants : register(b0, space0) {
    float4 target_size_near_far;
    float4 filter_parameters;
};

Texture2D<float> raw_ambient_occlusion : register(t0, space0);
Texture2D<float> camera_depth : register(t1, space0);
RWTexture2D<float> filtered_ambient_occlusion : register(u0, space0);

static const float VICE_AO_BACKGROUND_DEPTH = 0.999999;

float vice_ao_linear_depth(float device_depth) {
    const float near_plane = target_size_near_far.z;
    const float far_plane = target_size_near_far.w;
    const float denominator = far_plane - saturate(device_depth) * (far_plane - near_plane);
    return denominator > 0.0 ? (near_plane * far_plane) / denominator : 0.0;
}

float vice_ao_bilateral_weight(
    float center_device_depth,
    float neighbor_device_depth,
    float pixel_distance_squared
) {
    if (center_device_depth >= VICE_AO_BACKGROUND_DEPTH ||
        neighbor_device_depth >= VICE_AO_BACKGROUND_DEPTH) {
        return 0.0;
    }
    const float center_depth = vice_ao_linear_depth(center_device_depth);
    const float neighbor_depth = vice_ao_linear_depth(neighbor_device_depth);
    if (center_depth <= 0.0 || neighbor_depth <= 0.0) return 0.0;

    const float depth_sigma = max(filter_parameters.x, 0.000001);
    const float spatial_sigma = max(filter_parameters.y, 0.000001);
    const float relative_depth = abs(neighbor_depth - center_depth) /
        max(center_depth, max(target_size_near_far.z, 0.00001));
    const float depth_ratio = relative_depth / depth_sigma;
    const float spatial_ratio = pixel_distance_squared / (spatial_sigma * spatial_sigma);
    return exp(-0.5 * (depth_ratio * depth_ratio + spatial_ratio));
}

[numthreads(8, 8, 1)]
void vice_ray_ambient_occlusion_denoise_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    const uint width = (uint)target_size_near_far.x;
    const uint height = (uint)target_size_near_far.y;
    if (dispatch_id.x >= width || dispatch_id.y >= height) return;

    const uint2 pixel = dispatch_id.xy;
    const float center_device_depth = camera_depth.Load(int3(pixel, 0));
    if (center_device_depth >= VICE_AO_BACKGROUND_DEPTH) {
        filtered_ambient_occlusion[pixel] = 1.0;
        return;
    }

    const int radius = clamp((int)filter_parameters.z, 1, 4);
    float weighted_visibility = 0.0;
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
            const float neighbor_device_depth = camera_depth.Load(int3(candidate, 0));
            const float weight = vice_ao_bilateral_weight(
                center_device_depth,
                neighbor_device_depth,
                (float)(x * x + y * y)
            );
            if (weight <= 0.0) continue;
            const float visibility = saturate(raw_ambient_occlusion.Load(int3(candidate, 0)));
            weighted_visibility += visibility * weight;
            weight_sum += weight;
        }
    }

    const float center_visibility = saturate(raw_ambient_occlusion.Load(int3(pixel, 0)));
    filtered_ambient_occlusion[pixel] = weight_sum > 0.000001
        ? saturate(weighted_visibility / weight_sum)
        : center_visibility;
}
