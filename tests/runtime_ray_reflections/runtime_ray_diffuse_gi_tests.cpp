#include <vice/render/rhi.hpp>
#include <vice/runtime/ray_diffuse_gi.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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

    runtime::RuntimeRayDiffuseGiCamera camera{};
    camera.position = {0.0F, 0.0F, 0.0F};
    camera.forward = {0.0F, 0.0F, -1.0F};
    camera.up = {0.0F, 1.0F, 0.0F};
    camera.vertical_fov_radians = 1.0F;
    camera.aspect_ratio = 16.0F / 9.0F;
    camera.near_plane = 0.1F;
    camera.far_plane = 100.0F;
    camera.jitter_x_ndc = 0.001F;
    camera.jitter_y_ndc = -0.002F;

    const auto constants = runtime::make_runtime_ray_diffuse_gi_constants(
        camera,
        1920U,
        1080U,
        1.25F,
        0.35F,
        runtime::runtime_ray_diffuse_gi_sample_period + 17U,
        35.0F,
        0.025F
    );
    CHECK(constants.has_value());
    CHECK(near(constants->camera_position_near[3], 0.1F));
    CHECK(near(constants->camera_forward_far[2], -1.0F));
    CHECK(near(constants->target_size_jitter[0], 1920.0F));
    CHECK(near(constants->target_size_jitter[1], 1080.0F));
    CHECK(near(constants->target_size_jitter[2], 0.001F));
    CHECK(near(constants->target_size_jitter[3], -0.002F));
    CHECK(near(constants->trace_parameters[0], 35.0F));
    CHECK(near(constants->trace_parameters[1], 0.025F));
    CHECK(near(constants->trace_parameters[2], 17.0F));
    CHECK(near(
        constants->trace_parameters[3],
        static_cast<float>(runtime::runtime_ray_diffuse_gi_max_instance_id)
    ));
    CHECK(near(constants->environment_parameters[0], 1.25F));
    CHECK(near(constants->environment_parameters[1], 0.35F));
    CHECK(near(constants->environment_parameters[3], 1.0F));

    CHECK(!runtime::make_runtime_ray_diffuse_gi_constants(
        camera, 0U, 1080U, 1.0F, 0.0F, 0U
    ).has_value());
    CHECK(!runtime::make_runtime_ray_diffuse_gi_constants(
        camera, 16U, 16U, 1.0F, 0.0F, 0U, 0.01F, 0.02F
    ).has_value());
    CHECK(!runtime::make_runtime_ray_diffuse_gi_constants(
        camera,
        16U,
        16U,
        std::numeric_limits<float>::quiet_NaN(),
        0.0F,
        0U
    ).has_value());

    const math::Vec3 normal{0.0F, 1.0F, 0.0F};
    const auto first = runtime::runtime_ray_diffuse_gi_direction(normal, 42U, 77U, 5U);
    const auto repeat = runtime::runtime_ray_diffuse_gi_direction(normal, 42U, 77U, 5U);
    const auto next_frame = runtime::runtime_ray_diffuse_gi_direction(normal, 42U, 77U, 6U);
    CHECK(first.has_value());
    CHECK(repeat.has_value());
    CHECK(next_frame.has_value());
    CHECK(near(first->x, repeat->x));
    CHECK(near(first->y, repeat->y));
    CHECK(near(first->z, repeat->z));
    CHECK(near(math::dot(*first, *first), 1.0F, 0.0001F));
    CHECK(math::dot(*first, normal) > 0.0F);
    CHECK(math::dot(*next_frame, normal) > 0.0F);
    CHECK(
        !near(first->x, next_frame->x) ||
        !near(first->y, next_frame->y) ||
        !near(first->z, next_frame->z)
    );
    CHECK(!runtime::runtime_ray_diffuse_gi_direction(
        math::Vec3{0.0F, 0.0F, 0.0F}, 0U, 0U, 0U
    ).has_value());

    const std::string shader = read_text(VICE_RAY_DIFFUSE_GI_SHADER_SOURCE);
    CHECK(!shader.empty());
    CHECK(shader.find("RaytracingAccelerationStructure SceneTlas") != std::string::npos);
    CHECK(shader.find("query.TraceRayInline(SceneTlas") != std::string::npos);
    CHECK(shader.find("query.CommittedInstanceIndex()") != std::string::npos);
    CHECK(shader.find("SceneInstanceIndices[tlas_instance_index]") != std::string::npos);
    CHECK(shader.find("vice_cosine_hemisphere_direction") != std::string::npos);
    CHECK(shader.find("EnvironmentSpecular.SampleLevel") != std::string::npos);
    CHECK(shader.find("sqrt(max(1.0 - u1, 0.0))") != std::string::npos);
    CHECK(shader.find("scene_instance_index + 1u") != std::string::npos);
    CHECK(shader.find("float4(vice_environment_radiance(ray_direction), 1.0)") != std::string::npos);

    auto device = render::Device::create();
    if (device != nullptr) {
        std::string diagnostics;
        runtime::RuntimeRayDiffuseGiPass fallback_pass;
        CHECK(fallback_pass.initialize(*device, {}, &diagnostics));
        CHECK(!fallback_pass.available());
        CHECK(diagnostics.empty());
        CHECK(fallback_pass.resize(1U, 1U, &diagnostics));
        CHECK(fallback_pass.radiance() == nullptr);
        CHECK(fallback_pass.hits() == nullptr);
        CHECK(diagnostics.empty());

#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_CONTRACT_MANIFEST
        if (device->capabilities().ray_tracing) {
            runtime::RuntimeRayDiffuseGiPass cooked_pass;
            CHECK(cooked_pass.initialize(
                *device,
                std::filesystem::path{VICE_RUNTIME_RAY_DIFFUSE_GI_CONTRACT_MANIFEST},
                &diagnostics
            ));
            CHECK(cooked_pass.available());
            CHECK(diagnostics.empty());
            CHECK(cooked_pass.resize(4U, 4U, &diagnostics));
            CHECK(cooked_pass.radiance() != nullptr);
            CHECK(cooked_pass.hits() != nullptr);
            CHECK(diagnostics.empty());
        }
#endif
    }

    std::cout << "Diffuse GI ray sampling and authoritative hit contract validated.\n";
    return 0;
}

#undef CHECK
