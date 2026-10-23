from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one anchor, found {count}")
    return text.replace(old, new, 1)


scene_path = Path("engine/runtime/src/scene.cpp")
scene = scene_path.read_text()
scene = replace_once(
    scene,
    "#include <vice/runtime/post_process.hpp>\n#include <vice/runtime/visibility.hpp>",
    "#include <vice/runtime/post_process.hpp>\n#include <vice/runtime/render_streaming.hpp>\n#include <vice/runtime/visibility.hpp>",
    "render streaming include",
)
scene = replace_once(
    scene,
    "    const auto instances = collect_render_mesh_instances(world);\n",
    "    auto resident_scene_build = build_runtime_render_resident_scene(world);\n"
    "    if (!resident_scene_build) {\n"
    "        set_diagnostics(diagnostics, resident_scene_build.diagnostics);\n"
    "        return false;\n"
    "    }\n"
    "    const auto& resident_scene = resident_scene_build.scene;\n"
    "    const auto& instances = resident_scene.meshes;\n",
    "resident mesh collection",
)
scene = replace_once(
    scene,
    "    const auto authored_lights = collect_runtime_lights(world);\n",
    "    const auto& authored_lights = resident_scene.lights;\n",
    "resident light collection",
)
scene = replace_once(
    scene,
    "    const auto reflection_probes = collect_runtime_reflection_probes(world);\n",
    "    const auto& reflection_probes = resident_scene.reflection_probes;\n",
    "resident reflection probe collection",
)
scene_path.write_text(scene)


test_path = Path("tests/runtime_render_residency_tests.cpp")
test = test_path.read_text()
test = replace_once(
    test,
    "#include <iostream>\n#include <string_view>",
    "#include <iostream>\n#include <optional>\n#include <string_view>",
    "test optional include",
)
test = replace_once(
    test,
    "    add_mesh; // keep the main-world helper lifetime explicit; no effect on small_world\n",
    "",
    "remove no-op helper expression",
)
test_path.write_text(test)


cmake_path = Path("tests/runtime/CMakeLists.txt")
cmake = cmake_path.read_text()
anchor = "add_test(NAME vice_runtime_visibility_tests COMMAND vice_runtime_visibility_tests)\n"
block = """

add_executable(
  vice_runtime_render_residency_tests
  ../runtime_render_residency_tests.cpp
)
target_link_libraries(
  vice_runtime_render_residency_tests
  PRIVATE
    VICE::Runtime
)
target_compile_features(vice_runtime_render_residency_tests PRIVATE cxx_std_23)
vice_enable_warnings(vice_runtime_render_residency_tests)
add_test(
  NAME vice_runtime_render_residency_tests
  COMMAND vice_runtime_render_residency_tests
)
"""
if "vice_runtime_render_residency_tests" in cmake:
    raise SystemExit("render residency test target already exists")
cmake = replace_once(cmake, anchor, anchor + block, "test CMake insertion")
cmake_path.write_text(cmake)
