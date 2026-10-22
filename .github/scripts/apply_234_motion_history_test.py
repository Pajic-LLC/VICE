from pathlib import Path

scene_path = Path("engine/runtime/src/scene.cpp")
scene = scene_path.read_text()


def replace_scene(old: str, new: str) -> None:
    global scene
    count = scene.count(old)
    if count != 1:
        raise SystemExit(f"scene expected exactly one match, found {count}: {old[:140]!r}")
    scene = scene.replace(old, new, 1)


replace_scene(
    '#include "gpu_pbr_batches.hpp"\n#include "gpu_shadow_batches.hpp"',
    '#include "gpu_motion_history.hpp"\n#include "gpu_pbr_batches.hpp"\n#include "gpu_shadow_batches.hpp"',
)

old = '''    if (!gpu_shadow_plan.batches.empty()) {
        std::vector<math::Mat4> gpu_previous_models;
        gpu_previous_models.reserve(instances.size());
        for (std::size_t instance_index = 0U; instance_index < instances.size(); ++instance_index) {
            math::Mat4 previous_model = gpu_instance_models[instance_index];
            if (temporal_frame.history_valid) {
                const auto previous = state_->previous_models.find(instances[instance_index].entity);
                if (previous != state_->previous_models.end()) previous_model = previous->second;
            }
            gpu_previous_models.push_back(previous_model);
        }
        if (!state_->gpu_previous_instances.upload(*commands, gpu_previous_models, diagnostics) ||
            !state_->gpu_motion_draw_batches.build(
'''
new = '''    if (!gpu_shadow_plan.batches.empty()) {
        const auto gpu_previous_build = detail::build_runtime_gpu_previous_models(
            instances,
            gpu_instance_models,
            state_->previous_models,
            temporal_frame.history_valid
        );
        if (!gpu_previous_build) {
            set_diagnostics(diagnostics, gpu_previous_build.diagnostics);
            return false;
        }
        if (!state_->gpu_previous_instances.upload(
                *commands, gpu_previous_build.models, diagnostics
            ) || !state_->gpu_motion_draw_batches.build(
'''
replace_scene(old, new)
scene_path.write_text(scene)

cmake_path = Path("tests/runtime/CMakeLists.txt")
cmake = cmake_path.read_text()
marker = 'if(TARGET vice_mesh_cooker_lib)\n'
if cmake.count(marker) != 1:
    raise SystemExit("expected one runtime scene-test marker")
block = '''add_executable(
  vice_runtime_gpu_motion_history_tests
  ../runtime_gpu_motion_history_tests.cpp
)
target_include_directories(
  vice_runtime_gpu_motion_history_tests
  PRIVATE
    "${PROJECT_SOURCE_DIR}/engine/runtime/src"
)
target_link_libraries(
  vice_runtime_gpu_motion_history_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_gpu_motion_history_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_gpu_motion_history_tests)
add_test(
  NAME vice_runtime_gpu_motion_history_tests
  COMMAND vice_runtime_gpu_motion_history_tests
)

'''
cmake = cmake.replace(marker, block + marker, 1)
cmake_path.write_text(cmake)

print("Applied #234 motion-history alignment regression integration.")
