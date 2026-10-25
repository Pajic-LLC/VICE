#include <vice/render/rhi.hpp>
#include <vice/runtime/ray_diffuse_gi_denoise.hpp>
#include <vice/runtime/ray_diffuse_gi_temporal.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

namespace {

std::string read_text(const char* path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream text;
    text << input.rdbuf();
    return text.str();
}

bool near(float lhs, float rhs, float tolerance = 0.00001F) {
    return std::abs(lhs - rhs) <= tolerance;
}

} // namespace

int main() {
    using namespace vice;

    const float center_weight = runtime::runtime_ray_diffuse_gi_bilateral_weight(
        0.5F, 0.5F, 1.0F, 0.0F, 0.1F, 100.0F
    );
    const float depth_edge_weight = runtime::runtime_ray_diffuse_gi_bilateral_weight(
        0.5F, 0.65F, 1.0F, 1.0F, 0.1F, 100.0F
    );
    const float normal_edge_weight = runtime::runtime_ray_diffuse_gi_bilateral_weight(
        0.5F, 0.5F, 0.5F, 1.0F, 0.1F, 100.0F
    );
    CHECK(near(center_weight, 1.0F));
    CHECK(depth_edge_weight < center_weight);
    CHECK(normal_edge_weight < center_weight);
    CHECK(runtime::runtime_ray_diffuse_gi_bilateral_weight(
        1.0F, 0.5F, 1.0F, 0.0F, 0.1F, 100.0F
    ) == 0.0F);

    const auto denoise_constants = runtime::make_runtime_ray_diffuse_gi_denoise_constants(
        1920U, 1080U, 0.1F, 500.0F, 0.025F, 24.0F, 1.5F, 2U
    );
    CHECK(denoise_constants.has_value());
    CHECK(near(denoise_constants->target_size_near_far[0], 1920.0F));
    CHECK(near(denoise_constants->filter_parameters[0], 0.025F));
    CHECK(near(denoise_constants->filter_parameters[1], 24.0F));
    CHECK(near(denoise_constants->filter_parameters[2], 1.5F));
    CHECK(near(denoise_constants->filter_parameters[3], 2.0F));
    CHECK(!runtime::make_runtime_ray_diffuse_gi_denoise_constants(
        16U, 16U, 0.1F, 100.0F, 0.025F, 24.0F, 1.5F, 0U
    ).has_value());

    CHECK(near(runtime::runtime_ray_diffuse_gi_temporal_history_weight(
        0.92F, 0.0F, 0.0F, 32.0F, true
    ), 0.92F));
    CHECK(runtime::runtime_ray_diffuse_gi_temporal_history_weight(
        0.92F, 0.1F, 0.0F, 32.0F, true
    ) < 0.92F);
    CHECK(runtime::runtime_ray_diffuse_gi_temporal_history_weight(
        0.92F, 0.0F, 0.0F, 32.0F, false
    ) == 0.0F);
    CHECK(runtime::runtime_ray_diffuse_gi_temporal_depth_accept(0.5F, 0.505F));
    CHECK(!runtime::runtime_ray_diffuse_gi_temporal_depth_accept(0.5F, 0.55F));
    CHECK(near(runtime::runtime_ray_diffuse_gi_temporal_clamp_history(
        4.0F, 1.0F, 2.0F
    ), 2.0F));

    const auto temporal_constants = runtime::make_runtime_ray_diffuse_gi_temporal_constants(
        1920U, 1080U, true, 0.92F, 0.0025F, 32.0F
    );
    CHECK(temporal_constants.has_value());
    CHECK(near(temporal_constants->target_size_history[2], 1.0F));
    CHECK(near(temporal_constants->target_size_history[3], 0.92F));
    CHECK(near(temporal_constants->rejection_parameters[0], 0.0025F));
    CHECK(near(temporal_constants->rejection_parameters[1], 32.0F));

    const std::string denoise_shader = read_text(VICE_RAY_DIFFUSE_GI_DENOISE_SHADER_SOURCE);
    CHECK(!denoise_shader.empty());
    CHECK(denoise_shader.find("vice_gi_bilateral_weight") != std::string::npos);
    CHECK(denoise_shader.find("vice_decode_octahedral_normal") != std::string::npos);
    CHECK(denoise_shader.find("neighbor_radiance.a <= 0.0") != std::string::npos);
    CHECK(denoise_shader.find("dot(center_normal, neighbor_normal)") != std::string::npos);
    CHECK(denoise_shader.find("max(filtered, 0.0)") != std::string::npos);
    CHECK(denoise_shader.find("saturate(center_radiance.a)") != std::string::npos);

    const std::string temporal_shader = read_text(VICE_RAY_DIFFUSE_GI_TEMPORAL_SHADER_SOURCE);
    CHECK(!temporal_shader.empty());
    CHECK(temporal_shader.find("history_uv = uv - velocity") != std::string::npos);
    CHECK(temporal_shader.find("depth_threshold * 8.0") != std::string::npos);
    CHECK(temporal_shader.find("neighborhood_min") != std::string::npos);
    CHECK(temporal_shader.find("neighborhood_max") != std::string::npos);
    CHECK(temporal_shader.find("history_sample.a <= 0.0") != std::string::npos);
    CHECK(temporal_shader.find("length(velocity) * rejection_parameters.y") != std::string::npos);
    CHECK(temporal_shader.find("float4(resolved, current_alpha)") != std::string::npos);

    auto device = render::Device::create();
    if (device != nullptr) {
        std::string diagnostics;
        runtime::RuntimeRayDiffuseGiDenoisePass denoise;
        CHECK(denoise.initialize(*device, {}, &diagnostics));
        CHECK(!denoise.available());
        CHECK(denoise.resize(4U, 4U, &diagnostics));
        CHECK(denoise.radiance() == nullptr);
        CHECK(diagnostics.empty());

        runtime::RuntimeRayDiffuseGiTemporalPass temporal;
        CHECK(temporal.initialize(*device, {}, &diagnostics));
        CHECK(!temporal.available());
        CHECK(temporal.resize(4U, 4U, &diagnostics));
        CHECK(temporal.radiance() == nullptr);
        CHECK(diagnostics.empty());

#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST
        runtime::RuntimeRayDiffuseGiDenoisePass cooked_denoise;
        CHECK(cooked_denoise.initialize(
            *device,
            std::filesystem::path{VICE_RUNTIME_RAY_DIFFUSE_GI_DENOISE_MANIFEST},
            &diagnostics
        ));
        CHECK(cooked_denoise.available());
        CHECK(cooked_denoise.resize(4U, 4U, &diagnostics));
        CHECK(cooked_denoise.radiance() != nullptr);
        CHECK(diagnostics.empty());
#endif

#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST
        runtime::RuntimeRayDiffuseGiTemporalPass cooked_temporal;
        CHECK(cooked_temporal.initialize(
            *device,
            std::filesystem::path{VICE_RUNTIME_RAY_DIFFUSE_GI_TEMPORAL_MANIFEST},
            &diagnostics
        ));
        CHECK(cooked_temporal.available());
        CHECK(cooked_temporal.resize(4U, 4U, &diagnostics));
        CHECK(cooked_temporal.radiance() == nullptr);
        CHECK(diagnostics.empty());
#endif
    }

    std::cout << "Diffuse GI spatial and temporal reconstruction contracts validated.\n";
    return 0;
}

#undef CHECK
