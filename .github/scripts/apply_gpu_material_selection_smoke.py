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

workflow_path = Path(".github/workflows/ci.yml")
workflow = workflow_path.read_text()
tail = '''          foreach ($binding in 0..1) {\n            $resource = $gpuPbrFragment.reflection.resources | Where-Object {\n              $_.class -eq "sampler" -and $_.binding -eq $binding -and $_.space -eq 2\n            }\n            if (-not $resource -or $resource.count -ne 1 -or $resource.unbounded) {\n              throw "GPU PBR shader is missing bounded lighting sampler s$binding,space2."\n            }\n          }'''
smoke = tail + '''\n\n          $materialSelectionOutput = "build/cooked/vice_gpu_material_selection_smoke"\n          & $cooker `\n            --input "assets/shaders/vice_gpu_material_selection_smoke.hlsl" `\n            --output $materialSelectionOutput `\n            --entry "vice_gpu_material_selection_cs" `\n            --stage compute `\n            --target d3d12 `\n            --shader-model 6.6 `\n            --dxc $dxc.FullName `\n            --force\n          if ($LASTEXITCODE -ne 0) {\n            exit $LASTEXITCODE\n          }\n\n          $materialSelectionManifest = Get-Content "$materialSelectionOutput/shader.vice.json" -Raw | ConvertFrom-Json\n          $materialTextures = $materialSelectionManifest.reflection.resources | Where-Object {\n            $_.name -eq "material_textures"\n          }\n          $materialSamplers = $materialSelectionManifest.reflection.resources | Where-Object {\n            $_.name -eq "material_samplers"\n          }\n          if (-not $materialTextures -or\n              $materialTextures.class -ne "shader_resource" -or\n              $materialTextures.binding -ne 0 -or\n              $materialTextures.space -ne 1 -or\n              $materialTextures.count -ne 0 -or\n              -not $materialTextures.unbounded) {\n            throw "GPU material selection smoke did not preserve the unbounded texture table."\n          }\n          if (-not $materialSamplers -or\n              $materialSamplers.class -ne "sampler" -or\n              $materialSamplers.binding -ne 0 -or\n              $materialSamplers.space -ne 1 -or\n              $materialSamplers.count -ne 0 -or\n              -not $materialSamplers.unbounded) {\n            throw "GPU material selection smoke did not preserve the unbounded sampler table."\n          }\n\n          $materialSelectionSmoke = Resolve-Path "build/tests/Release/vice_gpu_material_selection_smoke_tests.exe"\n          $env:VICE_GPU_MATERIAL_SELECTION_SMOKE_MANIFEST = (Resolve-Path "$materialSelectionOutput/shader.vice.json").Path\n          & $materialSelectionSmoke\n          if ($LASTEXITCODE -ne 0) {\n            exit $LASTEXITCODE\n          }\n'''
workflow = replace_once(workflow, tail, smoke.rstrip("\n"), "Windows material selection execution smoke")
workflow_path.write_text(workflow)
