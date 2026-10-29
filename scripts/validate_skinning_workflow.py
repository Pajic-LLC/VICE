#!/usr/bin/env python3
"""Weighted glTF profiles, atomic import and native World/render/checkpoint parity."""
import argparse
import base64
import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from validate_presentation_workflow import validate as tour


def validate(vice, native):
    example = Path(__file__).resolve().parent.parent / "examples/skinning-playground"
    result = tour(vice, native, example)
    original = json.loads((example / "sources/humanoid.gltf").read_text())
    original_buffer = base64.b64decode(original["buffers"][0]["uri"].split(",", 1)[1])
    attributes = original["meshes"][0]["primitives"][0]["attributes"]

    def offset(document, name):
        a = document["accessors"][attributes[name]]
        return document["bufferViews"][a["bufferView"]].get("byteOffset", 0) + a.get("byteOffset", 0)

    with tempfile.TemporaryDirectory(prefix="vice-skin-profiles-") as tmp:
        root = Path(tmp)

        def imported(name, document, data, valid):
            document["buffers"][0]["byteLength"] = len(data)
            document["buffers"][0]["uri"] = "data:application/octet-stream;base64," + base64.b64encode(data).decode()
            source, target = root / (name + ".gltf"), root / name
            source.write_text(json.dumps(document, separators=(",", ":")))
            completed = subprocess.run([str(vice), "rig", "import", str(source), str(target),
                                        "a4000000-0000-4000-8000-000000000200",
                                        "a2000000-0000-4000-8000-000000000003"],
                                       capture_output=True, text=True, timeout=60)
            if valid:
                if completed.returncode or not (target / "character.vrig").read_text().startswith("vice-rig 2"):
                    raise RuntimeError(f"Supported skin profile {name} failed: {completed.stdout} {completed.stderr}")
            elif completed.returncode == 0 or target.exists():
                raise RuntimeError(f"Malformed skin {name} published a partial import")

        for bits, component in [(8, 5121), (16, 5123)]:
            document, data = copy.deepcopy(original), bytearray(original_buffer)
            a = document["accessors"][attributes["WEIGHTS_0"]]
            start = offset(document, "WEIGHTS_0")
            values = []
            maximum = (1 << bits) - 1
            for i in range(a["count"]):
                weights = struct.unpack_from("<4f", original_buffer, start + i*16)
                quantized = [round(w*maximum) for w in weights]
                quantized[max(range(4), key=lambda k: weights[k])] += maximum-sum(quantized)
                values.extend(quantized)
            packed = struct.pack("<" + ("B" if bits == 8 else "H")*len(values), *values)
            data[start:start+len(packed)] = packed
            document["bufferViews"][a["bufferView"]]["byteLength"] = len(packed)
            a["componentType"], a["normalized"] = component, True
            imported("normalized"+str(bits), document, data, True)
        result["passed"].append("normalized_uint8_and_uint16_skin_weights")

        document, data = copy.deepcopy(original), bytearray(original_buffer)
        count = document["accessors"][attributes["WEIGHTS_0"]]["count"]
        joint_start, weight_start = offset(document, "JOINTS_0"), offset(document, "WEIGHTS_0")
        for i in range(count):
            struct.pack_into("<4H", data, joint_start+i*8, 0, 1, 2, 3)
            struct.pack_into("<4f", data, weight_start+i*16, .125, .125, .125, .125)
        for semantic, component, fmt, values in [("JOINTS_1", 5123, "H", (4, 5, 6, 7)),
                                                 ("WEIGHTS_1", 5126, "f", (.125,)*4)]:
            while len(data) % 4:
                data.append(0)
            start = len(data)
            data.extend(struct.pack("<" + fmt*(4*count), *(values*count)))
            document["bufferViews"].append({"buffer": 0, "byteOffset": start, "byteLength": len(data)-start})
            document["accessors"].append({"bufferView": len(document["bufferViews"])-1, "componentType": component, "count": count, "type": "VEC4"})
            document["meshes"][0]["primitives"][0]["attributes"][semantic] = len(document["accessors"])-1
        imported("eight_influences", document, data, True)
        result["passed"].append("both_joint_weight_sets_preserve_eight_influences")

        for name in ["invalid_joint", "nonfinite_weight", "nonaffine_inverse_bind", "extra_influence_set", "nonuniform_node_scale"]:
            document, data = copy.deepcopy(original), bytearray(original_buffer)
            if name == "invalid_joint":
                struct.pack_into("<H", data, offset(document, "JOINTS_0"), 127)
            elif name == "nonfinite_weight":
                struct.pack_into("<f", data, offset(document, "WEIGHTS_0"), float("nan"))
            elif name == "nonaffine_inverse_bind":
                a = document["accessors"][document["skins"][0]["inverseBindMatrices"]]
                start = document["bufferViews"][a["bufferView"]].get("byteOffset", 0)
                struct.pack_into("<f", data, start+12, 1)
            elif name == "extra_influence_set":
                attrs = document["meshes"][0]["primitives"][0]["attributes"]
                attrs["JOINTS_2"], attrs["WEIGHTS_2"] = attrs["JOINTS_0"], attrs["WEIGHTS_0"]
            else:
                document["nodes"][0]["scale"] = [1, 2, 1]
            imported(name, document, data, False)
        result["passed"].append("malformed_and_unsupported_skin_inputs_never_partially_publish")

    if native:
        observed = result["observed"]
        walk, ride, resumed = observed["walking"], observed["driving"]["character"], observed["resumed_exit"]["character"]
        for state in [walk, ride, resumed]:
            animation, geometry = state["animation_state"], state["skin_render"]
            if animation["skin_count"] != 1 or animation["joint_count"] != 11 or animation["weighted_vertices"] != 3762 or len(geometry["meshes"]) != 1:
                raise RuntimeError("Native World did not expose the weighted skeletal asset")
            if not geometry["meshes"][0]["gpu_uploaded"] or geometry["meshes"][0]["vertices"] != 3762:
                raise RuntimeError("Deformed geometry did not reach the GPU stream")
        if walk["skin_render"]["meshes"][0]["fingerprint"] == ride["skin_render"]["meshes"][0]["fingerprint"]:
            raise RuntimeError("Changing clips did not change actual deformed geometry")
        if ride["skin_render"]["evaluated_vertices"] != 0 or ride["skin_render"]["meshes"][0]["fingerprint"] != ride["skin_render"]["meshes"][0]["previous_fingerprint"]:
            raise RuntimeError("Unchanged seated palette did not reuse geometry/settle deformation history")
        if walk["skin_render"]["meshes"][0]["source"] != resumed["skin_render"]["meshes"][0]["source"]:
            raise RuntimeError("Checkpoint changed the persistent bind-mesh identity")
        result["passed"] += ["native_weighted_geometry_reaches_gpu_across_walk_ride_and_restart",
                              "static_joint_palettes_skip_vertex_deformation",
                              "checkpoint_reconstructs_skin_from_persistent_intent"]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--vice", required=True, type=Path)
    parser.add_argument("--run-runtime", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(validate(args.vice.resolve(), args.run_runtime), indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report+"\n")
    print(report)
