#include <vice/render/rhi.hpp>
#include <vice/runtime/ray_diffuse_gi.hpp>
#include <vice/runtime/ray_hit_radiance_history.hpp>
#include <vice/runtime/ray_scene_hit.hpp>

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

    CHECK(sizeof(runtime::RuntimeRayDiffuseGiHit) == sizeof(runtime::RuntimeRaySceneHit));
    runtime::RuntimeRayDiffuseGiHit hit{};
    hit.world_position[0] = 7.0F;
    hit.world_position[1] = 8.0F;
    hit.world_position[2] = 9.0F;
    hit.instance_id_plus_one = 18U;
    CHECK(hit.instance_id_plus_one == 18U);

    const world::GlobalPosition current_origin{
        1.0e12 + 3.0,
        -2.0e12 + 4.0,
        3.0e12 - 5.0,
    };
    const world::GlobalPosition previous_origin{1.0e12, -2.0e12, 3.0e12};
    const auto rebased = runtime::runtime_ray_hit_radiance_history_rebase_position(
        {hit.world_position[0], hit.world_position[1], hit.world_position[2]},
        current_origin,
        previous_origin
    );
    CHECK(rebased.has_value());
    CHECK(near(rebased->x, 10.0F));
    CHECK(near(rebased->y, 12.0F));
    CHECK(near(rebased->z, 4.0F));

    const auto history_constants = runtime::make_runtime_ray_hit_radiance_history_constants(
        math::identity(),
        1920U,
        1080U,
        true,
        current_origin,
        previous_origin
    );
    CHECK(history_constants.has_value());
    CHECK(near(history_constants->target_size_history[0], 1920.0F));
    CHECK(near(history_constants->target_size_history[1], 1080.0F));
    CHECK(near(history_constants->target_size_history[2], 1.0F));
    CHECK(near(history_constants->current_to_previous_origin[0], 3.0F));
    CHECK(near(history_constants->current_to_previous_origin[1], 4.0F));
    CHECK(near(history_constants->current_to_previous_origin[2], -5.0F));
    CHECK(runtime::runtime_ray_hit_radiance_history_depth_matches(0.5F, 0.505F));
    CHECK(!runtime::runtime_ray_hit_radiance_history_depth_matches(0.5F, 0.55F));

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

    const std::string history_shader = read_text(VICE_RAY_DIFFUSE_GI_HISTORY_SHADER_SOURCE);
    CHECK(!history_shader.empty());
    CHECK(history_shader.find("StructuredBuffer<ReflectionHit>") != std::string::npos);
    CHECK(history_shader.find("hit.instance_id_plus_one == 0u") != std::string::npos);
    CHECK(history_shader.find("hit.world_position + current_to_previous_origin.xyz") != std::string::npos);
    CHECK(history_shader.find("PreviousSceneColor.Load") != std::string::npos);
    CHECK(history_shader.find("PreviousDepth.Load") != std::string::npos);
    CHECK(history_shader.find("vice_history_depth_matches") != std::string::npos);
    CHECK(history_shader.find("current_radiance.a") != std::string::npos);

    const std::string history_cpp = read_text(VICE_RAY_HIT_RADIANCE_HISTORY_CPP_SOURCE);
    CHECK(!history_cpp.empty());
    CHECK(history_cpp.find("sizeof(RuntimeRaySceneHit)") != std::string::npos);
    CHECK(history_cpp.find("set_compute_buffer") != std::string::npos);
    CHECK(history_cpp.find("set_compute_texture") != std::string::npos);
    CHECK(history_cpp.find("set_compute_storage_texture") != std::string::npos);

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

        runtime::RuntimeRayHitRadianceHistoryPass fallback_history;
        CHECK(fallback_history.initialize(*device, {}, &diagnostics));
        CHECK(!fallback_history.available());
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

#ifdef VICE_RUNTIME_RAY_DIFFUSE_GI_HISTORY_MANIFEST
        if (device->capabilities().ray_tracing) {
            runtime::RuntimeRayHitRadianceHistoryPass cooked_history;
            CHECK(cooked_history.initialize(
                *device,
                std::filesystem::path{VICE_RUNTIME_RAY_DIFFUSE_GI_HISTORY_MANIFEST},
                &diagnostics
            ));
            CHECK(cooked_history.available());
            CHECK(diagnostics.empty());
        }
#endif
    }

    std::cout << "Diffuse GI ray and large-world shared history contract validated.\n";
    return 0;
}

#undef CHECK
