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
  --target vice_shader_cooker vice_metal_compute_pipeline_tests \
  --parallel

COOKER="$BUILD_DIR/tools/shader_cooker/vice_shader_cooker"
SMOKE="$BUILD_DIR/tests/vice_metal_compute_pipeline_tests"
if [[ ! -x "$COOKER" ]]; then
  echo "Shader cooker was not built at $COOKER." >&2
  exit 5
fi
if [[ ! -x "$SMOKE" ]]; then
  echo "Metal compute test was not built at $SMOKE." >&2
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

raw_reflection_path = path.parent / artifacts["metal_reflection"]
raw_reflection = json.loads(raw_reflection_path.read_text())
raw_locations = raw_reflection.get("TopLevelArgumentBuffer", [])
if len(raw_locations) != len(locations):
    fail(
        "VICE manifest/root count differs from Metal Shader Converter reflection: "
        f"{len(locations)} vs {len(raw_locations)}"
    )

class_map = {
    "CBV": "constant_buffer",
    "SRV": "shader_resource",
    "UAV": "unordered_access",
    "SMP": "sampler",
}
manifest_by_identity = {
    (location.get("class"), location.get("binding"), location.get("space")): location
    for location in locations
}
for raw in raw_locations:
    raw_type = raw.get("Type")
    resource_class = class_map.get(raw_type)
    if resource_class is None and raw_type != "Table":
        fail(f"unsupported raw converter resource type: {raw_type}")

    if raw_type == "Table":
        candidates = [
            location
            for location in locations
            if location.get("argument_buffer_offset") == raw.get("EltOffset")
        ]
        if len(candidates) != 1:
            fail(
                "converter descriptor-table root does not uniquely match the VICE manifest offset: "
                f"{raw.get('EltOffset')}"
            )
        normalized = candidates[0]
        identity = (
            normalized.get("class"),
            normalized.get("binding"),
            normalized.get("space"),
        )
    else:
        identity = (resource_class, raw.get("Slot"), raw.get("Space"))
        normalized = manifest_by_identity.get(identity)
        if normalized is None:
            fail(f"converter root {identity} is missing from VICE manifest")

    if normalized.get("argument_buffer_offset") != raw.get("EltOffset"):
        fail(
            f"VICE changed converter EltOffset for {identity}: "
            f"{normalized.get('argument_buffer_offset')} vs {raw.get('EltOffset')}"
        )
    if normalized.get("size") != raw.get("Size"):
        fail(
            f"VICE changed converter Size for {identity}: "
            f"{normalized.get('size')} vs {raw.get('Size')}"
        )

print("Cooked Metal unbounded manifest and authoritative converter offsets verified.")
PY

VICE_METAL_UNBOUNDED_SMOKE_MANIFEST="$MANIFEST" "$SMOKE"
