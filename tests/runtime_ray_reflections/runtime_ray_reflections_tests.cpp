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
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

void test_camera_reconstruction() {
    const vice::runtime::RuntimeRayReflectionsCamera camera{
        .position = {0.0F, 0.0F, 0.0F}, .forward = {0.0F, 0.0F, -1.0F}, .up = {0.0F, 1.0F, 0.0F},
        .vertical_fov_radians = 1.0F, .aspect_ratio = 1.0F, .near_plane = 0.1F, .far_plane = 100.0F,
    };
    require(near(vice::runtime::runtime_ray_reflections_linear_depth(0.0F, 0.1F, 100.0F), 0.1F), "device near depth must reconstruct the near plane");
    const auto position = vice::runtime::runtime_ray_reflections_world_position(camera, 1U, 1U, 0U, 0U, 0.0F);
    require(position.has_value(), "center pixel must reconstruct a world position");
    if (position.has_value()) require(near(position->x, 0.0F) && near(position->y, 0.0F) && near(position->z, -0.1F), "center pixel must lie on the camera forward ray");
    require(!vice::runtime::runtime_ray_reflections_world_position(camera, 1U, 1U, 0U, 0U, 1.0F).has_value(), "background depth must not produce a reflection origin");
}

void test_reflection_direction_and_eligibility() {
    const auto direction = vice::runtime::runtime_ray_reflections_direction({0.0F, 0.0F, -1.0F}, {0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 1.0F});
    require(direction.has_value(), "front-facing surface must produce a reflection direction");
    if (direction.has_value()) require(near(direction->x, 0.0F) && near(direction->y, 0.0F) && near(direction->z, 1.0F), "normal-incidence reflection must return along +Z");
    require(vice::runtime::runtime_ray_reflections_eligible(0.2F), "smooth surfaces must be reflection-ray eligible");
    require(!vice::runtime::runtime_ray_reflections_eligible(1.0F), "fully rough neutral guide pixels must not launch rays");
    require(!vice::runtime::runtime_ray_reflections_eligible(-0.1F), "invalid negative roughness must not launch rays");
}

void test_constants_and_identity_contract() {
    const vice::runtime::RuntimeRayReflectionsCamera camera{
        .position = {4.0F, 3.0F, 2.0F}, .forward = {0.0F, 0.0F, -1.0F}, .up = {0.0F, 1.0F, 0.0F},
        .vertical_fov_radians = 0.82F, .aspect_ratio = 16.0F / 9.0F, .near_plane = 0.05F, .far_plane = 500.0F,
        .jitter_x_ndc = 0.001F, .jitter_y_ndc = -0.002F,
    };
    const auto constants = vice::runtime::make_runtime_ray_reflections_constants(camera, 1920U, 1080U, 1.5F, 0.25F, 8.0F, 250.0F, 0.02F, 0.85F);
    require(constants.has_value(), "valid reflection constants must be accepted");
    if (constants.has_value()) {
        require(near(constants->target_size_jitter[0], 1920.0F) && near(constants->target_size_jitter[1], 1080.0F), "reflection constants must preserve target extent");
        require(near(constants->trace_parameters[2], 0.85F), "reflection constants must preserve roughness cutoff");
        require(constants->trace_parameters[3] == 16777215.0F, "float contract must preserve the exact 24-bit Runtime instance-ID ceiling");
        require(near(constants->environment_parameters[0], 1.5F) && near(constants->environment_parameters[2], 8.0F), "reflection constants must preserve environment intensity and specular LOD");
    }
    require(sizeof(vice::runtime::RuntimeRayReflectionHit) == 16U, "committed-hit record must remain a compact 16-byte structured-buffer element");
    vice::runtime::RuntimeRayReflectionHit hit{};
    hit.instance_id_plus_one = vice::runtime::runtime_ray_reflection_max_instance_id + 1U;
    require(hit.instance_id_plus_one == 0x01000000U, "24-bit instance identity plus miss sentinel must remain lossless in uint32");
}

void test_history_projection_and_depth_contract() {
    const auto identity = vice::math::identity();
    const auto projection = vice::runtime::runtime_ray_reflection_history_projection(identity, {0.0F, 0.0F, 0.5F});
    require(projection.has_value(), "center history point must project into the frame");
    if (projection.has_value()) require(near(projection->u, 0.5F) && near(projection->v, 0.5F) && near(projection->device_depth, 0.5F), "history projection must preserve center UV and zero-to-one depth");
    require(!vice::runtime::runtime_ray_reflection_history_projection(identity, {2.0F, 0.0F, 0.5F}).has_value(), "offscreen history points must be rejected");
    require(!vice::runtime::runtime_ray_reflection_history_projection(identity, {0.0F, 0.0F, 1.0F}).has_value(), "background-depth history points must be rejected");
    require(vice::runtime::runtime_ray_reflection_history_depth_matches(0.5F, 0.505F), "small relative depth deltas must preserve valid reflection history");
    require(!vice::runtime::runtime_ray_reflection_history_depth_matches(0.5F, 0.55F), "large depth deltas must reject disoccluded reflection history");
    const auto constants = vice::runtime::make_runtime_ray_reflection_history_constants(identity, 1920U, 1080U, true);
    require(constants.has_value(), "valid reflection-history constants must be accepted");
    if (constants.has_value()) require(near(constants->target_size_history[0], 1920.0F) && near(constants->target_size_history[1], 1080.0F) && near(constants->target_size_history[2], 1.0F) && near(constants->target_size_history[3], 0.0025F), "reflection-history constants must preserve extent, validity and depth threshold");

    const vice::world::GlobalPosition current_origin{1.0e12 + 3.0, -2.0e12 + 4.0, 3.0e12 - 5.0};
    const vice::world::GlobalPosition previous_origin{1.0e12, -2.0e12, 3.0e12};
    const auto rebased = vice::runtime::runtime_ray_reflection_history_rebase_position(
        {7.0F, 8.0F, 9.0F},
        current_origin,
        previous_origin
    );
    require(rebased.has_value(), "large-world reflection history must preserve small origin deltas");
    if (rebased.has_value()) {
        require(
            near(rebased->x, 10.0F) && near(rebased->y, 12.0F) && near(rebased->z, 4.0F),
            "current-camera-relative hits must rebase into the previous camera frame"
        );
    }
    const auto origin_constants = vice::runtime::make_runtime_ray_reflection_history_constants(
        identity,
        1920U,
        1080U,
        true,
        current_origin,
        previous_origin
    );
    require(origin_constants.has_value(), "large-world reflection-history constants must be accepted");
    if (origin_constants.has_value()) {
        require(
            near(origin_constants->current_to_previous_origin[0], 3.0F) &&
            near(origin_constants->current_to_previous_origin[1], 4.0F) &&
            near(origin_constants->current_to_previous_origin[2], -5.0F),
            "history constants must publish only the precise current-to-previous origin delta"
        );
    }
}

void test_public_default_state() {
    vice::runtime::RuntimeSceneRenderer renderer;
    require(renderer.ray_reflection_radiance() == nullptr, "uninitialized Runtime renderer must expose no ray-reflection radiance");
    require(renderer.ray_reflection_hits() == nullptr, "uninitialized Runtime renderer must expose no ray-reflection hit buffer");
}

void test_shader_contract() {
    const auto shader = read_text(VICE_RAY_REFLECTIONS_SHADER_SOURCE);
    require(!shader.empty(), "ray-reflection shader source must be readable");
    require(shader.find("vice_ray_reflections_cs") != std::string::npos, "ray-reflection shader must expose its compute entry point");
    require(shader.find("RaytracingAccelerationStructure SceneTlas") != std::string::npos && shader.find("RayQuery<RAY_FLAG_NONE>") != std::string::npos && shader.find("TraceRayInline") != std::string::npos, "ray-reflection shader must use the portable inline-RayQuery contract");
    require(shader.find("CommittedInstanceID()") != std::string::npos && shader.find("CommittedRayT()") != std::string::npos, "ray-reflection shader must preserve committed identity and hit distance");
    require(shader.find("SurfaceGuide.Load") != std::string::npos && shader.find("vice_decode_octahedral_normal") != std::string::npos, "ray origin/direction must consume the authoritative reflection surface guide");
    require(shader.find("roughness > trace_parameters.z") != std::string::npos, "reflection ray eligibility must consume authored roughness");
    require(shader.find("EnvironmentSpecular.SampleLevel") != std::string::npos, "ray hits and misses must have an explicit environment-specular fallback");
    require(shader.find("ReflectionRadiance[pixel] = float4(fallback_radiance, 1.0)") != std::string::npos, "raw reflection alpha must mark a valid per-pixel replacement contribution");
    require(shader.find("reflection_weight") == std::string::npos && shader.find(".rgb * environment_parameters.x") == std::string::npos, "raw reflection radiance must remain composition-neutral and leave BRDF/intensity to PBR");
    require(shader.find("RWStructuredBuffer<ReflectionHit>") != std::string::npos, "committed hit position and exact identity must use a structured GPU buffer");
}

void test_history_shader_contract() {
    const auto shader = read_text(VICE_RAY_REFLECTION_HISTORY_SHADER_SOURCE);
    require(!shader.empty(), "reflection-history shader source must be readable");
    require(shader.find("vice_ray_reflection_history_cs") != std::string::npos && shader.find("StructuredBuffer<ReflectionHit>") != std::string::npos, "reflection-history resolve must consume committed trace hits in compute");
    require(shader.find("previous_view_projection") != std::string::npos && shader.find("PreviousSceneColor.Load") != std::string::npos && shader.find("PreviousDepth.Load") != std::string::npos, "reflection-history resolve must reuse shared scene color, depth, and camera history");
    require(shader.find("hit.instance_id_plus_one == 0u") != std::string::npos && shader.find("target_size_history.z < 0.5") != std::string::npos, "misses and invalid temporal history must retain the environment fallback");
    require(shader.find("depth_threshold * 8.0") != std::string::npos && shader.find("abs(previous_ndc.z - previous_depth) > relative_depth_threshold") != std::string::npos, "reflection-history resolve must reject disocclusion with the shared relative-depth rule");
    require(shader.find("current_to_previous_origin") != std::string::npos && shader.find("hit.world_position + current_to_previous_origin.xyz") != std::string::npos, "history resolve must rebase current-camera-relative hits into the previous camera frame");
    require(shader.find("ReflectionRadiance[pixel] = float4(max(history_radiance, 0.0), current_radiance.a)") != std::string::npos, "validated hits must replace only raw incident RGB and preserve the PBR replacement mask");
}

void test_gpu_pbr_composition_contract() {
    const auto shader = read_text(VICE_GPU_PBR_SURFACE_SHADER_SOURCE);
    require(!shader.empty(), "GPU PBR shader source must be readable");
    require(shader.find("Texture2D<float4> ray_reflection_radiance_texture : register(t26, space2);") != std::string::npos, "GPU PBR must expose the portable Runtime reflection-radiance binding");
    require(shader.find("Texture2D<float4> ray_diffuse_gi_radiance_texture : register(t27, space2);") != std::string::npos, "GPU PBR must expose a backend-neutral diffuse-GI binding after the existing lighting resources");
    require(shader.find("ambient_occlusion_metadata.y < 0.5") != std::string::npos, "GPU PBR must gate RT reflection replacement on current-frame validity");
    require(shader.find("ambient_occlusion_metadata.z < 0.5") != std::string::npos, "GPU PBR must gate diffuse-GI replacement on a separate current-frame validity marker");
    require(shader.find("ray_reflection_radiance_texture.Load") != std::string::npos && shader.find("lerp(raster_specular, max(reflection.rgb, 0.0), saturate(reflection.a))") != std::string::npos, "GPU PBR must replace raster specular per pixel instead of adding RT radiance");
    require(shader.find("ray_diffuse_gi_radiance_texture.Load") != std::string::npos && shader.find("diffuse_gi.a <= 0.0") != std::string::npos && shader.find("return max(diffuse_gi.rgb, 0.0)") != std::string::npos, "diffuse GI must require current-pixel validity and remain composition-neutral until the PBR diffuse lobe");
    const auto source0 = shader.find("float3 vice_environment_source_0(");
    const auto source1 = shader.find("float3 vice_environment_source_1(");
    const auto source2 = shader.find("float3 vice_environment_source_2(");
    const auto reflection_replacement = shader.find("vice_runtime_reflection_specular(", source0);
    const auto gi_replacement = shader.find("vice_runtime_diffuse_gi(", source0);
    require(source0 != std::string::npos && source1 != std::string::npos && source2 != std::string::npos && reflection_replacement != std::string::npos && reflection_replacement < source1, "only global environment source 0 may consume the RT reflection replacement");
    require(gi_replacement != std::string::npos && gi_replacement < source1, "only global environment source 0 may consume reconstructed diffuse GI");
    require(shader.find("vice_runtime_reflection_specular(", source1) == std::string::npos, "local reflection probes must stay on their existing raster/environment path");
    require(shader.find("vice_runtime_diffuse_gi(", source1) == std::string::npos, "local diffuse probes must stay on their existing raster/environment path");
    require(shader.find("const float3 raster_diffuse_irradiance = environment_irradiance_0.Sample(") != std::string::npos && shader.find("pixel_position, raster_diffuse_irradiance") != std::string::npos, "valid GI must replace source-0 irradiance rather than add another diffuse term");
    require(shader.find("const float3 diffuse = diffuse_irradiance * kd * albedo * occlusion;") != std::string::npos, "GI must enter before the existing metallic/albedo/occlusion diffuse BRDF contract");
    require(shader.find("+ ray_reflection_radiance_texture") == std::string::npos && shader.find("+ ray_diffuse_gi_radiance_texture") == std::string::npos, "Runtime lighting textures must never be double-counted as additive post-BRDF terms");
    require(shader.find("environment += vice_environment_source_1(") != std::string::npos && shader.find("environment += vice_environment_source_2(") != std::string::npos, "local probes must remain additive and independent of global diffuse-GI replacement");
    require(shader.find("environment *= ray_ambient_occlusion;") != std::string::npos, "RTAO must remain an independent environment-visibility stage after source composition");
    require(shader.find("occlusion, input.position.xy") != std::string::npos, "GPU PBR must address Runtime replacements with the current raster pixel");
}

void test_runtime_binding_and_order_contract() {
    const auto implementation = read_text(VICE_RAY_REFLECTIONS_CPP_SOURCE);
    const auto history_implementation = read_text(VICE_RAY_REFLECTION_HISTORY_CPP_SOURCE);
    const auto bridge = read_text(VICE_RAY_REFLECTIONS_BRIDGE_SOURCE);
    require(implementation.find("render::TextureFormat::rgba16_float") != std::string::npos && implementation.find("render::BufferUsage::storage") != std::string::npos, "raw radiance and hit data must use portable RHI resources");
    require(implementation.find("set_compute_acceleration_structure") != std::string::npos && implementation.find("set_compute_storage_buffer") != std::string::npos, "Runtime must bind the authoritative TLAS and lossless hit buffer through compute RHI");
    require(implementation.find("state.valid = dispatched") != std::string::npos && implementation.find("|| !state.valid") != std::string::npos, "public raw reflection outputs must only expose data produced by the current frame");
    require(history_implementation.find("set_compute_buffer") != std::string::npos && history_implementation.find("set_compute_texture") != std::string::npos && history_implementation.find("set_compute_storage_texture") != std::string::npos, "reflection-history resolve must use only portable compute RHI bindings");
    require(bridge.find("binding = 26U") != std::string::npos &&
            bridge.find("using SetGraphicsTexture = bool (render::CommandList::*)") != std::string::npos &&
            bridge.find("&render::CommandList::set_graphics_texture") != std::string::npos &&
            bridge.find("(commands.*bind_texture)(reflection_binding, bound_texture)") != std::string::npos,
        "Runtime must bind the portable GPU PBR reflection descriptor through the macro-proof RHI member call");
    require(bridge.find("binding.binding != 16U || binding.space != 2U") != std::string::npos &&
            bridge.find("binding = 27U") != std::string::npos &&
            bridge.find("(commands.*bind_texture)(diffuse_gi_binding, bound_texture)") != std::string::npos,
        "Runtime must bind diffuse GI through t27/space2 only when source-0 irradiance is being prepared");
    require(bridge.find("runtime_ray_diffuse_gi_gpu_pbr_frame = RuntimeRayDiffuseGiGpuPbrFrame") != std::string::npos &&
            bridge.find(".radiance = nullptr") != std::string::npos &&
            bridge.find("runtime_ray_diffuse_gi_publish_gpu_pbr") != std::string::npos,
        "Runtime must clear diffuse-GI state every frame before a producer can publish current-frame radiance");
    require(bridge.find("runtime_ray_diffuse_gi_gpu_pbr_frame.owner != owner") != std::string::npos, "diffuse-GI publication must be scoped to the renderer that began the current frame");
    require(bridge.find("offsetof(GpuPbrSceneConstants, ambient_occlusion_metadata) + 2U * sizeof(float)") != std::string::npos, "diffuse GI must use a validity marker independent of RTAO and RT reflections");
    require(bridge.find("radiance != nullptr ? *radiance : fallback_texture") != std::string::npos, "GPU PBR Runtime lighting descriptors must remain valid on fallback frames");
    const auto motion_macro = bridge.find("#define encode_motion_vectors");
    const auto gi_reset_position = bridge.find("runtime_ray_diffuse_gi_begin_gpu_pbr_frame(this)", motion_macro);
    const auto guide_position = bridge.find("runtime_reflection_surface_guide_after_motion_vectors(", motion_macro);
    const auto trace_position = bridge.find("runtime_ray_reflections_after_surface_guide(", motion_macro);
    const auto history_position = bridge.find("runtime_ray_reflection_history_after_trace(", motion_macro);
    const auto reflection_marker_position = bridge.find("runtime_ray_reflections_mark_gpu_pbr(", motion_macro);
    const auto gi_marker_position = bridge.find("runtime_ray_diffuse_gi_mark_gpu_pbr(", motion_macro);
    require(motion_macro != std::string::npos && gi_reset_position != std::string::npos && guide_position != std::string::npos && trace_position != std::string::npos && history_position != std::string::npos && reflection_marker_position != std::string::npos && gi_marker_position != std::string::npos && gi_reset_position < guide_position && guide_position < trace_position && trace_position < history_position && history_position < reflection_marker_position && reflection_marker_position < gi_marker_position, "current-frame reset, guide, trace, history, reflection marker, and diffuse-GI marker must execute in causal order");
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
