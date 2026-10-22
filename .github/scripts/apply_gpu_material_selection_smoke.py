from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


test_path = Path("tests/gpu_material_selection_smoke_tests.cpp")
test = test_path.read_text()
test = replace_once(
    test,
    "#include <string_view>\n",
    "#include <string_view>\n#include <utility>\n",
    "utility include",
)
test_path.write_text(test)

cmake_path = Path("tests/CMakeLists.txt")
cmake = cmake_path.read_text()
compute_target = '''add_executable(vice_compute_rhi_smoke_tests compute_rhi_smoke_tests.cpp)\ntarget_link_libraries(vice_compute_rhi_smoke_tests PRIVATE VICE::Render)\ntarget_compile_features(vice_compute_rhi_smoke_tests PRIVATE cxx_std_23)\nvice_enable_warnings(vice_compute_rhi_smoke_tests)\nadd_test(NAME vice_compute_rhi_smoke_tests COMMAND vice_compute_rhi_smoke_tests)\n'''
material_target = compute_target + '''\nadd_executable(\n  vice_gpu_material_selection_smoke_tests\n  gpu_material_selection_smoke_tests.cpp\n)\ntarget_link_libraries(vice_gpu_material_selection_smoke_tests PRIVATE VICE::Render)\ntarget_compile_features(vice_gpu_material_selection_smoke_tests PRIVATE cxx_std_23)\nvice_enable_warnings(vice_gpu_material_selection_smoke_tests)\nadd_test(\n  NAME vice_gpu_material_selection_smoke_tests\n  COMMAND vice_gpu_material_selection_smoke_tests\n)\n'''
cmake = replace_once(cmake, compute_target, material_target, "smoke test CMake target")
cmake_path.write_text(cmake)
