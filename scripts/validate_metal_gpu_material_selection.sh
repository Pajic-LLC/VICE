#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Metal GPU material selection validation requires macOS." >&2
  exit 2
fi

resolve_tool() {
  local requested="$1"
  if [[ "$requested" == */* ]]; then
    [[ -x "$requested" ]] || return 1
    python3 -c 'import os,sys; print(os.path.realpath(sys.argv[1]))' "$requested"
    return
  fi
  command -v "$requested"
}

DXC_REQUESTED="${VICE_DXC:-dxc}"
MSC_REQUESTED="${VICE_METAL_SHADER_CONVERTER:-metal-shaderconverter}"
DXC="$(resolve_tool "$DXC_REQUESTED")" || {
  echo "DXC was not found. Set VICE_DXC to the official dxc executable." >&2
  exit 3
}
MSC="$(resolve_tool "$MSC_REQUESTED")" || {
  echo "Metal Shader Converter was not found. Install Apple's official tool and set VICE_METAL_SHADER_CONVERTER if needed." >&2
  exit 4
}

echo "DXC: $DXC"
"$DXC" --version
echo "Metal Shader Converter: $MSC"
"$MSC" --version || "$MSC" -help | head -40

BUILD_DIR="${VICE_GPU_MATERIAL_SELECTION_BUILD_DIR:-build-metal-gpu-material-selection-validation}"
COOK_DIR="$BUILD_DIR/cooked/vice_gpu_material_selection_smoke"

cmake -S . -B "$BUILD_DIR" \
  -DVICE_BUILD_TESTS=ON \
  -DVICE_BUILD_TOOLS=ON \
  -DVICE_BUILD_LAB=OFF
cmake --build "$BUILD_DIR" \
  --target vice_shader_cooker vice_gpu_material_selection_smoke_tests \
  --parallel

COOKER="$BUILD_DIR/tools/shader_cooker/vice_shader_cooker"
SMOKE="$BUILD_DIR/tests/vice_gpu_material_selection_smoke_tests"
if [[ ! -x "$COOKER" ]]; then
  echo "Shader cooker was not built at $COOKER." >&2
  exit 5
fi
if [[ ! -x "$SMOKE" ]]; then
  echo "GPU material selection smoke test was not built at $SMOKE." >&2
  exit 6
fi

rm -rf "$COOK_DIR"
"$COOKER" \
  --input assets/shaders/vice_gpu_material_selection_smoke.hlsl \
  --output "$COOK_DIR" \
  --entry vice_gpu_material_selection_cs \
  --stage compute \
  --target metal \
  --shader-model 6.6 \
  --dxc "$DXC" \
  --metal-shader-converter "$MSC" \
  --force

MANIFEST="$COOK_DIR/shader.vice.json"
python3 - "$MANIFEST" <<'PY'
import json
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
data = json.loads(path.read_text())

def fail(message: str) -> None:
    raise SystemExit(f"Metal GPU material selection manifest validation failed: {message}")

if data.get("format") != "vice.shader" or data.get("version") != 5:
    fail("expected vice.shader V5")
if data.get("stage") != "compute":
    fail("expected compute stage")
artifacts = data.get("artifacts", {})
if not artifacts.get("metallib") or not artifacts.get("metal_reflection"):
    fail("expected converter-produced metallib and reflection artifacts")
metal = data.get("metal_binding", {})
if metal.get("version") != 3 or metal.get("model") != "explicit_descriptor_tables":
    fail("expected Metal binding v3 explicit_descriptor_tables")
reflection = data.get("reflection", {})
if reflection.get("thread_group_size") != {"x": 2, "y": 1, "z": 1}:
    fail("expected authored numthreads(2,1,1)")
resources = reflection.get("resources", [])

def resource(resource_class: str, binding: int, space: int):
    matches = [
        entry for entry in resources
        if entry.get("class") == resource_class
        and entry.get("binding") == binding
        and entry.get("space") == space
    ]
    if len(matches) != 1:
        fail(f"expected exactly one {resource_class} at binding {binding}, space {space}")
    return matches[0]

textures = resource("shader_resource", 0, 1)
samplers = resource("sampler", 0, 1)
output = resource("unordered_access", 0, 2)
if textures.get("count") != 0 or textures.get("unbounded") is not True:
    fail("material texture table is not unbounded")
if samplers.get("count") != 0 or samplers.get("unbounded") is not True:
    fail("material sampler table is not unbounded")
if output.get("count") != 1 or output.get("unbounded") is not False:
    fail("output UAV is not one bounded resource")

for binding in range(3):
    structured = resource("shader_resource", binding, 0)
    if structured.get("count") != 1 or structured.get("unbounded"):
        fail(f"scene table t{binding},space0 is not one bounded SRV")

print("Cooked Metal GPU material selection manifest verified.")
PY

VICE_GPU_MATERIAL_SELECTION_SMOKE_MANIFEST="$MANIFEST" "$SMOKE"
