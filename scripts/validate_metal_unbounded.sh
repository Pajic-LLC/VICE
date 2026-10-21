#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Metal unbounded execution validation requires macOS." >&2
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

BUILD_DIR="${VICE_VALIDATION_BUILD_DIR:-build-metal-unbounded-validation}"
COOK_DIR="$BUILD_DIR/cooked/vice_metal_unbounded_smoke"

cmake -S . -B "$BUILD_DIR" \
  -DVICE_BUILD_TESTS=ON \
  -DVICE_BUILD_TOOLS=ON \
  -DVICE_BUILD_LAB=OFF
cmake --build "$BUILD_DIR" \
  --target vice_shader_cooker vice_metal_unbounded_cooked_smoke_tests \
  --parallel

COOKER="$BUILD_DIR/tools/shader_cooker/vice_shader_cooker"
SMOKE="$BUILD_DIR/tests/vice_metal_unbounded_cooked_smoke_tests"
if [[ ! -x "$COOKER" ]]; then
  echo "Shader cooker was not built at $COOKER." >&2
  exit 5
fi
if [[ ! -x "$SMOKE" ]]; then
  echo "Metal unbounded smoke test was not built at $SMOKE." >&2
  exit 6
fi

rm -rf "$COOK_DIR"
"$COOKER" \
  --input assets/shaders/vice_metal_unbounded_smoke.hlsl \
  --output "$COOK_DIR" \
  --entry vice_metal_unbounded_cs \
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
    raise SystemExit(f"Metal unbounded manifest validation failed: {message}")

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
if metal.get("descriptor_entry_size") != 24:
    fail("expected 24-byte Metal descriptor entries")
reflection = data.get("reflection", {})
if reflection.get("thread_group_size") != {"x": 1, "y": 1, "z": 1}:
    fail("expected authored numthreads(1,1,1)")
resources = reflection.get("resources", [])
inputs = [r for r in resources if r.get("class") == "shader_resource" and r.get("binding") == 0 and r.get("space") == 0]
outputs = [r for r in resources if r.get("class") == "unordered_access" and r.get("binding") == 0 and r.get("space") == 1]
if len(inputs) != 1 or inputs[0].get("count") != 0 or inputs[0].get("unbounded") is not True:
    fail("t0,space0 is not reflected as one unbounded SRV range")
if len(outputs) != 1 or outputs[0].get("count") != 1 or outputs[0].get("unbounded") is not False:
    fail("u0,space1 is not reflected as one bounded UAV")
locations = metal.get("locations", [])
if len(locations) != 2:
    fail(f"expected two explicit descriptor-table root locations, got {len(locations)}")
if any(location.get("size") != 8 for location in locations):
    fail("explicit descriptor-table roots must be 64-bit GPU-address entries")
print("Cooked Metal unbounded manifest contract verified.")
PY

"$SMOKE" "$MANIFEST"
