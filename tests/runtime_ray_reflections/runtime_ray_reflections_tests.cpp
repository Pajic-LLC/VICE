#include <vice/runtime/ray_reflection_history.hpp>
#include <vice/runtime/ray_reflections.hpp>
#include <vice/runtime/scene.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] bool near(const float left, const float right, const float epsilon = 1.0e-4F) {
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] std::string read_text(const char* path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

void test_camera_reconstruction() {
    const vice::runtime::RuntimeRayReflectionsCamera camera{
        .position = {0.0F, 0.0F, 0.0F},
        .forward = {0.0F, 0.0F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .vertical_fov_radians = 1.0F,
        .aspect_ratio = 1.0F,
        .near_plane = 0.1F,
        .far_plane = 100.0F,
    };
    require(near(vice::runtime::runtime_ray_reflections_linear_depth(0.0F, 0.1F, 100.0F), 0.1F),
        "device near depth must reconstruct the near plane");
    const auto position = vice::runtime::runtime_ray_reflections_world_position(
        camera, 1U, 1U, 0U, 0U, 0.0F
    );
    require(position.has_value(), "center pixel must reconstruct a world position");
    if (position.has_value()) {
        require(near(position->x, 0.0F) && near(position->y, 0.0F) && near(position->z, -0.1F),
            "center pixel must lie on the camera forward ray");
    }
    require(!vice::runtime::runtime_ray_reflections_world_position(
            camera, 1U, 1U, 0U, 0U, 1.0F
        ).has_value(),
        "background depth must not produce a reflection origin");
}

void test_reflection_direction_and_eligibility() {
    const auto direction = vice::runtime::runtime_ray_reflections_direction(
        {0.0F, 0.0F, -1.0F},
        {0.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F}
    );
    require(direction.has_value(), "front-facing surface must produce a reflection direction");
    if (direction.has_value()) {
        require(near(direction->x, 0.0F) && near(direction->y, 0.0F) && near(direction->z, 1.0F),
            "normal-incidence reflection must return along +Z");
    }
    require(vice::runtime::runtime_ray_reflections_eligible(0.2F),
        "smooth surfaces must be reflection-ray eligible");
    require(!vice::runtime::runtime_ray_reflections_eligible(1.0F),
        "fully rough neutral guide pixels must not launch rays");
    require(!vice::runtime::runtime_ray_reflections_eligible(-0.1F),
        "invalid negative roughness must not launch rays");
}

void test_constants_and_identity_contract() {
    const vice::runtime::RuntimeRayReflectionsCamera camera{
        .position = {4.0F, 3.0F, 2.0F},
        .forward = {0.0F, 0.0F, -1.0F},
        .up = {0.0F, 1.0F, 0.0F},
        .vertical_fov_radians = 0.82F,
        .aspect_ratio = 16.0F / 9.0F,
        .near_plane = 0.05F,
        .far_plane = 500.0F,
        .jitter_x_ndc = 0.001F,
        .jitter_y_ndc = -0.002F,
    };
    const auto constants = vice::runtime::make_runtime_ray_reflections_constants(
        camera, 1920U, 1080U, 1.5F, 0.25F, 8.0F, 250.0F, 0.02F, 0.85F
    );
    require(constants.has_value(), "valid reflection constants must be accepted");
    if (constants.has_value()) {
        require(near(constants->target_size_jitter[0], 1920.0F) &&
                near(constants->target_size_jitter[1], 1080.0F),
            "reflection constants must preserve target extent");
        require(near(constants->trace_parameters[2], 0.85F),
            "reflection constants must preserve roughness cutoff");
        require(constants->trace_parameters[3] == 16777215.0F,
            "float contract must preserve the exact 24-bit Runtime instance-ID ceiling");
        require(near(constants->environment_parameters[0], 1.5F) &&
                near(constants->environment_parameters[2], 8.0F),
            "reflection constants must preserve environment intensity and specular LOD");
    }
    require(sizeof(vice::runtime::RuntimeRayReflectionHit) == 16U,
        "committed-hit record must remain a compact 16-byte structured-buffer element");
    vice::runtime::RuntimeRayReflectionHit hit{};
    hit.instance_id_plus_one = vice::runtime::runtime_ray_reflection_max_instance_id + 1U;
    require(hit.instance_id_plus_one == 0x01000000U,
        "24-bit instance identity plus miss sentinel must remain lossless in uint32");
}

void test_history_projection_and_depth_contract() {
    const auto identity = vice::math::identity();
    const auto projection = vice::runtime::runtime_ray_reflection_history_projection(
        identity,
        {0.0F, 0.0F, 0.5F}
    );
    require(projection.has_value(), "center history point must project into the frame");
    if (projection.has_value()) {
        require(near(projection->u, 0.5F) && near(projection->v, 0.5F) &&
                near(projection->device_depth, 0.5F),
            "history projection must preserve center UV and zero-to-one depth");
    }
    require(!vice::runtime::runtime_ray_reflection_history_projection(
            identity, {2.0F, 0.0F, 0.5F}
        ).has_value(),
        "offscreen history points must be rejected");
    require(!vice::runtime::runtime_ray_reflection_history_projection(
            identity, {0.0F, 0.0F, 1.0F}
        ).has_value(),
        "background-depth history points must be rejected");

    require(vice::runtime::runtime_ray_reflection_history_depth_matches(0.5F, 0.505F),
        "small relative depth deltas must preserve valid reflection history");
    require(!vice::runtime::runtime_ray_reflection_history_depth_matches(0.5F, 0.55F),
        "large depth deltas must reject disoccluded reflection history");

    const auto constants = vice::runtime::make_runtime_ray_reflection_history_constants(
        identity, 1920U, 1080U, true
    );
    require(constants.has_value(), "valid reflection-history constants must be accepted");
    if (constants.has_value()) {
        require(near(constants->target_size_history[0], 1920.0F) &&
                near(constants->target_size_history[1], 1080.0F) &&
                near(constants->target_size_history[2], 1.0F) &&
                near(constants->target_size_history[3], 0.0025F),
            "reflection-history constants must preserve extent, validity and depth threshold");
    }
}

void test_public_default_state() {
    vice::runtime::RuntimeSceneRenderer renderer;
    require(renderer.ray_reflection_radiance() == nullptr,
        "uninitialized Runtime renderer must expose no ray-reflection radiance");
    require(renderer.ray_reflection_hits() == nullptr,
        "uninitialized Runtime renderer must expose no ray-reflection hit buffer");
}

void test_shader_contract() {
    const auto shader = read_text(VICE_RAY_REFLECTIONS_SHADER_SOURCE);
    require(!shader.empty(), "ray-reflection shader source must be readable");
    require(shader.find("vice_ray_reflections_cs") != std::string::npos,
        "ray-reflection shader must expose its compute entry point");
    require(shader.find("RaytracingAccelerationStructure SceneTlas") != std::string::npos &&
            shader.find("RayQuery<RAY_FLAG_NONE>") != std::string::npos &&
            shader.find("TraceRayInline") != std::string::npos,
        "ray-reflection shader must use the portable inline-RayQuery contract");
    require(shader.find("CommittedInstanceID()") != std::string::npos &&
            shader.find("CommittedRayT()") != std::string::npos,
        "ray-reflection shader must preserve committed identity and hit distance");
    require(shader.find("SurfaceGuide.Load") != std::string::npos &&
            shader.find("vice_decode_octahedral_normal") != std::string::npos,
        "ray origin/direction must consume the authoritative reflection surface guide");
    require(shader.find("roughness > trace_parameters.z") != std::string::npos,
        "reflection ray eligibility must consume authored roughness");
    require(shader.find("EnvironmentSpecular.SampleLevel") != std::string::npos,
        "ray hits and misses must have an explicit environment-specular fallback");
    require(shader.find("ReflectionRadiance[pixel] = float4(fallback_radiance, 1.0)") !=
            std::string::npos,
        "raw reflection alpha must mark a valid per-pixel replacement contribution");
    require(shader.find("reflection_weight") == std::string::npos &&
            shader.find(".rgb * environment_parameters.x") == std::string::npos,
        "raw reflection radiance must remain composition-neutral and leave BRDF/intensity to PBR");
    require(shader.find("RWStructuredBuffer<ReflectionHit>") != std::string::npos,
        "committed hit position and exact identity must use a structured GPU buffer");
}

void test_history_shader_contract() {
    const auto shader = read_text(VICE_RAY_REFLECTION_HISTORY_SHADER_SOURCE);
    require(!shader.empty(), "reflection-history shader source must be readable");
    require(shader.find("vice_ray_reflection_history_cs") != std::string::npos &&
            shader.find("StructuredBuffer<ReflectionHit>") != std::string::npos,
        "reflection-history resolve must consume committed trace hits in compute");
    require(shader.find("previous_view_projection") != std::string::npos &&
            shader.find("PreviousSceneColor.Load") != std::string::npos &&
            shader.find("PreviousDepth.Load") != std::string::npos,
        "reflection-history resolve must reuse shared scene color, depth, and camera history");
    require(shader.find("hit.instance_id_plus_one == 0u") != std::string::npos &&
            shader.find("target_size_history.z < 0.5") != std::string::npos,
        "misses and invalid temporal history must retain the environment fallback");
    require(shader.find("depth_threshold * 8.0") != std::string::npos &&
            shader.find("abs(previous_ndc.z - previous_depth) > relative_depth_threshold") !=
                std::string::npos,
        "reflection-history resolve must reject disocclusion with the shared relative-depth rule");
    require(shader.find(
            "ReflectionRadiance[pixel] = float4(max(history_radiance, 0.0), current_radiance.a)"
        ) != std::string::npos,
        "validated hits must replace only raw incident RGB and preserve the PBR replacement mask");
}

void test_gpu_pbr_composition_contract() {
    const auto shader = read_text(VICE_GPU_PBR_SURFACE_SHADER_SOURCE);
    require(!shader.empty(), "GPU PBR shader source must be readable");
    require(shader.find(
            "Texture2D<float4> ray_reflection_radiance_texture : register(t26, space2);"
        ) != std::string::npos,
        "GPU PBR must expose the portable Runtime reflection-radiance binding");
    require(shader.find("ambient_occlusion_metadata.y < 0.5") != std::string::npos,
        "GPU PBR must gate RT reflection replacement on current-frame validity");
    require(shader.find("ray_reflection_radiance_texture.Load") != std::string::npos &&
            shader.find("lerp(raster_specular, max(reflection.rgb, 0.0), saturate(reflection.a))") !=
                std::string::npos,
        "GPU PBR must replace raster specular per pixel instead of adding RT radiance");

    const auto source0 = shader.find("float3 vice_environment_source_0(");
    const auto source1 = shader.find("float3 vice_environment_source_1(");
    const auto source2 = shader.find("float3 vice_environment_source_2(");
    const auto replacement = shader.find("vice_runtime_reflection_specular(", source0);
    require(source0 != std::string::npos && source1 != std::string::npos &&
            source2 != std::string::npos && replacement != std::string::npos &&
            replacement < source1,
        "only global environment source 0 may consume the RT reflection replacement");
    require(shader.find("vice_runtime_reflection_specular(", source1) == std::string::npos,
        "local reflection probes must stay on their existing raster/environment path");
    require(shader.find("+ ray_reflection_radiance_texture") == std::string::npos,
        "RT reflection radiance must never be added on top of environment specular");
    require(shader.find("occlusion, input.position.xy") != std::string::npos,
        "GPU PBR must address the reflection replacement with the current raster pixel");
}

void test_runtime_binding_and_order_contract() {
    const auto implementation = read_text(VICE_RAY_REFLECTIONS_CPP_SOURCE);
    const auto history_implementation = read_text(VICE_RAY_REFLECTION_HISTORY_CPP_SOURCE);
    const auto bridge = read_text(VICE_RAY_REFLECTIONS_BRIDGE_SOURCE);
    require(implementation.find("render::TextureFormat::rgba16_float") != std::string::npos &&
            implementation.find("render::BufferUsage::storage") != std::string::npos,
        "raw radiance and hit data must use portable RHI resources");
    require(implementation.find("set_compute_acceleration_structure") != std::string::npos &&
            implementation.find("set_compute_storage_buffer") != std::string::npos,
        "Runtime must bind the authoritative TLAS and lossless hit buffer through compute RHI");
    require(implementation.find("state.valid = dispatched") != std::string::npos &&
            implementation.find("|| !state.valid") != std::string::npos,
        "public raw reflection outputs must only expose data produced by the current frame");
    require(history_implementation.find("set_compute_buffer") != std::string::npos &&
            history_implementation.find("set_compute_texture") != std::string::npos &&
            history_implementation.find("set_compute_storage_texture") != std::string::npos,
        "reflection-history resolve must use only portable compute RHI bindings");
    require(bridge.find("binding = 26U") != std::string::npos &&
            bridge.find("set_graphics_texture(reflection_binding, bound_texture)") != std::string::npos,
        "Runtime must bind the portable GPU PBR reflection descriptor");
    require(bridge.find("radiance != nullptr ? *radiance : fallback_texture") != std::string::npos,
        "GPU PBR must receive a valid t26 descriptor even on non-RT fallback frames");

    const auto motion_macro = bridge.find("#define encode_motion_vectors");
    const auto guide_position = bridge.find(
        "runtime_reflection_surface_guide_after_motion_vectors(", motion_macro
    );
    const auto trace_position = bridge.find(
        "runtime_ray_reflections_after_surface_guide(", motion_macro
    );
    const auto history_position = bridge.find(
        "runtime_ray_reflection_history_after_trace(", motion_macro
    );
    const auto marker_position = bridge.find(
        "runtime_ray_reflections_mark_gpu_pbr(", motion_macro
    );
    require(motion_macro != std::string::npos && guide_position != std::string::npos &&
            trace_position != std::string::npos && history_position != std::string::npos &&
            marker_position != std::string::npos && guide_position < trace_position &&
            trace_position < history_position && history_position < marker_position,
        "guide, trace, history resolve, and PBR validity marker must execute in causal order");
}

} // namespace

int main() {
    test_camera_reconstruction();
    test_reflection_direction_and_eligibility();
    test_constants_and_identity_contract();
    test_history_projection_and_depth_contract();
    test_public_default_state();
    test_shader_contract();
    test_history_shader_contract();
    test_gpu_pbr_composition_contract();
    test_runtime_binding_and_order_contract();

    if (failures != 0) {
        std::cerr << failures << " Runtime ray-reflection test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime ray-reflection tests passed\n";
    return 0;
}
