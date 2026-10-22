from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCENE = ROOT / "engine/runtime/src/scene.cpp"
TEST_CMAKE = ROOT / "tests/runtime/CMakeLists.txt"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


scene = SCENE.read_text()

scene = replace_once(
    scene,
    "#include <vice/runtime/gpu_visibility_set.hpp>\n#include <vice/runtime/post_process.hpp>",
    "#include <vice/runtime/gpu_visibility_set.hpp>\n#include <vice/runtime/hiz.hpp>\n#include <vice/runtime/hiz_occlusion.hpp>\n#include <vice/runtime/post_process.hpp>",
    "Runtime Hi-Z public includes",
)
scene = replace_once(
    scene,
    '#include "gpu_shadow_batches.hpp"\n#include "material_pipeline.hpp"',
    '#include "gpu_shadow_batches.hpp"\n#include "hiz_history.hpp"\n#include "material_pipeline.hpp"',
    "Hi-Z history include",
)
scene = replace_once(
    scene,
    "    RuntimeVisibilityCuller visibility;\n    RuntimeGpuIdentityVisibility gpu_shadow_visibility;",
    "    RuntimeVisibilityCuller visibility;\n    RuntimeHiZPyramid hiz_pyramid;\n    RuntimeHiZOcclusionCuller hiz_occlusion;\n    RuntimeGpuIdentityVisibility gpu_shadow_visibility;",
    "Hi-Z renderer state",
)
scene = replace_once(
    scene,
    "    RuntimeTemporalHistory temporal_history;\n    RuntimePostProcessPlan post_process_plan",
    "    RuntimeTemporalHistory temporal_history;\n    detail::RuntimeHiZHistory hiz_history;\n    RuntimePostProcessPlan post_process_plan",
    "Hi-Z history state",
)
scene = replace_once(
    scene,
    "    if (!state_->visibility.initialize(device, diagnostics) ||\n        !state_->gpu_shadow_visibility.initialize(device, diagnostics) ||",
    "    if (!state_->visibility.initialize(device, diagnostics) ||\n        !state_->hiz_pyramid.initialize(device, diagnostics) ||\n        !state_->hiz_occlusion.initialize(device, diagnostics) ||\n        !state_->gpu_shadow_visibility.initialize(device, diagnostics) ||",
    "Hi-Z initialization",
)

scene = replace_once(
    scene,
    "        state_->previous_models.clear();\n        state_->temporal_history.reset();\n        state_->history_write_index = 0U;\n        state_->depth_width = 0;",
    "        state_->previous_models.clear();\n        state_->temporal_history.reset();\n        state_->hiz_history.reset();\n        state_->history_write_index = 0U;\n        state_->depth_width = 0;",
    "Zero-size history invalidation",
)
scene = replace_once(
    scene,
    "    state_->bloom_composite = std::move(bloom_composite);\n    state_->previous_models.clear();\n    state_->temporal_history.reset();\n    state_->history_write_index = 0U;",
    "    state_->bloom_composite = std::move(bloom_composite);\n    state_->previous_models.clear();\n    state_->temporal_history.reset();\n    state_->hiz_history.reset();\n    state_->history_write_index = 0U;",
    "Resize history invalidation",
)
scene = replace_once(
    scene,
    "    state_->scene_color = std::move(scene_color);\n    state_->temporal_color_history = std::move(temporal_color_history);",
    "    if (!state_->hiz_pyramid.resize(\n            metrics.drawable_width,\n            metrics.drawable_height,\n            diagnostics\n        )) {\n        return false;\n    }\n\n    state_->scene_color = std::move(scene_color);\n    state_->temporal_color_history = std::move(temporal_color_history);",
    "Hi-Z pyramid resize",
)
scene = replace_once(
    scene,
    "    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {\n        if (diagnostics != nullptr) diagnostics->clear();\n        return true;\n    }\n    if (!resize(metrics, diagnostics)) return false;",
    "    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {\n        state_->hiz_history.reset();\n        if (diagnostics != nullptr) diagnostics->clear();\n        return true;\n    }\n    if (!resize(metrics, diagnostics)) return false;",
    "Render zero-size Hi-Z invalidation",
)
scene = replace_once(
    scene,
    "    auto* const current_depth = state_->depth_history[state_->history_write_index].get();\n    auto* const previous_depth = temporal_frame.history_valid",
    "    auto* const current_depth = state_->depth_history[state_->history_write_index].get();\n    auto* const hiz_previous_depth =\n        state_->depth_history[1U - state_->history_write_index].get();\n    auto* const previous_depth = temporal_frame.history_valid",
    "Prior depth selection",
)

visibility_anchor = """    if (!state_->visibility.cull(
            *commands,
            visibility_spheres,
            visibility_frustum,
            diagnostics
        )) {
        return false;
    }

    std::vector<math::Mat4> gpu_instance_models;
"""
visibility_replacement = """    if (!state_->visibility.cull(
            *commands,
            visibility_spheres,
            visibility_frustum,
            diagnostics
        )) {
        return false;
    }

    RuntimeVisibilityGpuResult camera_visibility = state_->visibility.result();
    const bool use_hiz_occlusion = !instances.empty() && hiz_previous_depth != nullptr &&
        state_->hiz_history.usable(
            runtime_camera.entity,
            camera_global_position,
            runtime_camera.forward,
            runtime_camera.up,
            runtime_camera.camera,
            runtime_camera.aspect_ratio,
            instances
        );
    if (use_hiz_occlusion) {
        if (!state_->hiz_pyramid.build(*commands, *hiz_previous_depth, diagnostics)) {
            return false;
        }

        const auto& previous_camera = state_->hiz_history.camera();
        std::vector<RuntimeVisibilitySphere> previous_visibility_spheres;
        previous_visibility_spheres.reserve(instances.size());
        for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
            const auto& instance = instances[instance_index];
            const auto& mesh = state_->meshes.at(instance.mesh);
            const auto sphere = runtime_transform_bounding_sphere(
                mesh.bounds,
                runtime_model_matrix_relative_to(instance, previous_camera.global_position)
            );
            if (!valid(sphere)) {
                set_diagnostics(
                    diagnostics,
                    "VICE Runtime produced an invalid previous-camera Hi-Z visibility sphere."
                );
                return false;
            }
            previous_visibility_spheres.push_back(RuntimeVisibilitySphere{
                .center = sphere.center,
                .radius = sphere.radius,
                .instance_index = static_cast<std::uint32_t>(instance_index),
            });
        }

        if (!state_->hiz_occlusion.cull(
                *commands,
                camera_visibility,
                previous_visibility_spheres,
                previous_camera.camera_relative_view_projection,
                previous_camera.near_plane,
                previous_camera.far_plane,
                state_->hiz_pyramid,
                0.001F,
                diagnostics
            )) {
            return false;
        }
        camera_visibility = state_->hiz_occlusion.result();
    }

    std::vector<math::Mat4> gpu_instance_models;
"""
scene = replace_once(
    scene,
    visibility_anchor,
    visibility_replacement,
    "Hi-Z visibility filtering",
)

visibility_result_count = scene.count("state_->visibility.result(),")
if visibility_result_count != 3:
    raise RuntimeError(
        f"Camera batch visibility inputs: expected exactly 3 matches, found {visibility_result_count}"
    )
scene = scene.replace("state_->visibility.result(),", "camera_visibility,", 3)

submission_anchor = """    state_->previous_models.clear();
    if (temporal_resolved) {
"""
submission_replacement = """    const math::Mat4 hiz_relative_view_projection = runtime_apply_projection_jitter(
        math::multiply(
            math::perspective_rh_zo(
                runtime_camera.camera.vertical_fov_radians,
                runtime_camera.aspect_ratio,
                runtime_camera.camera.near_plane,
                runtime_camera.camera.far_plane
            ),
            math::look_at_rh({}, runtime_camera.forward, runtime_camera.up)
        ),
        jitter
    );
    state_->hiz_history.commit(
        runtime_camera.entity,
        camera_global_position,
        runtime_camera.forward,
        runtime_camera.up,
        runtime_camera.camera,
        runtime_camera.aspect_ratio,
        hiz_relative_view_projection,
        instances
    );

    state_->previous_models.clear();
    if (temporal_resolved) {
"""
scene = replace_once(
    scene,
    submission_anchor,
    submission_replacement,
    "Hi-Z successful-frame history commit",
)

SCENE.write_text(scene)

cmake = TEST_CMAKE.read_text()
cmake_anchor = """add_test(
  NAME vice_runtime_hiz_occlusion_tests
  COMMAND vice_runtime_hiz_occlusion_tests
)

if(TARGET vice_mesh_cooker_lib)
"""
cmake_replacement = """add_test(
  NAME vice_runtime_hiz_occlusion_tests
  COMMAND vice_runtime_hiz_occlusion_tests
)

add_executable(
  vice_runtime_hiz_history_tests
  ../runtime_hiz_history_tests.cpp
)
target_include_directories(
  vice_runtime_hiz_history_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/src"
)
target_link_libraries(
  vice_runtime_hiz_history_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_hiz_history_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_hiz_history_tests)
add_test(
  NAME vice_runtime_hiz_history_tests
  COMMAND vice_runtime_hiz_history_tests
)

if(TARGET vice_mesh_cooker_lib)
"""
cmake = replace_once(
    cmake,
    cmake_anchor,
    cmake_replacement,
    "Hi-Z history test registration",
)
TEST_CMAKE.write_text(cmake)

print("Applied guarded Runtime Hi-Z scene/history integration.")
