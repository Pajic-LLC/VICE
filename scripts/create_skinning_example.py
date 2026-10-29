#!/usr/bin/env python3
"""Build a reproducible, studio-owned neutral weighted mannequin validation project."""
import argparse
import base64
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess


def source_model():
    data = bytearray()
    views, accessors = [], []

    def accessor(values, width, component=5126, target=None):
        while len(data) % 4:
            data.append(0)
        offset = len(data)
        fmt = {5126: "f", 5123: "H", 5125: "I"}[component]
        flat = [v for row in values for v in row]
        data.extend(struct.pack("<" + fmt * len(flat), *flat))
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data) - offset}
        if target:
            view["target"] = target
        views.append(view)
        accessors.append({"bufferView": len(views)-1, "componentType": component,
                          "count": len(values), "type": {1: "SCALAR", 3: "VEC3", 4: "VEC4", 16: "MAT4"}[width]})
        return len(accessors)-1

    # Parent-first joint hierarchy. Asset-space metres; World owns locomotion/root facing.
    joints = [
        ("hips", -1, (0, .98, 0)), ("chest", 0, (0, .36, 0)),
        ("head", 1, (0, .34, 0)), ("left_upper_arm", 1, (-.27, .13, 0)),
        ("left_forearm", 3, (0, -.28, 0)), ("right_upper_arm", 1, (.27, .13, 0)),
        ("right_forearm", 5, (0, -.28, 0)), ("left_thigh", 0, (-.115, -.04, 0)),
        ("left_shin", 7, (0, -.42, 0)), ("right_thigh", 0, (.115, -.04, 0)),
        ("right_shin", 9, (0, -.42, 0)),
    ]
    nodes, global_positions = [], []
    for name, parent, translation in joints:
        nodes.append({"name": name, "translation": translation})
        origin = global_positions[parent] if parent >= 0 else (0, 0, 0)
        global_positions.append(tuple(a+b for a, b in zip(origin, translation)))
    for i, (_, parent, _) in enumerate(joints):
        if parent >= 0:
            nodes[parent].setdefault("children", []).append(i)
    positions, normals, colors, indices, vertex_joints, weights = [], [], [], [], [], []

    def influence(y, upper, lower=None, pivot=0):
        if lower is None:
            return (upper, 0, 0, 0), (1, 0, 0, 0)
        amount = max(0, min(1, (y-pivot+.09)/.18))
        # No duplicate non-zero joints, including at exact endpoints.
        return (upper, lower, 0, 0), (amount, 1-amount, 0, 0)

    def vertex(p, n, color, binding):
        length = math.sqrt(sum(x*x for x in n))
        positions.append(p)
        normals.append(tuple(x/length for x in n))
        colors.append(color)
        vertex_joints.append(binding[0])
        weights.append(binding[1])
        return len(positions)-1

    def tube(cx, ymin, ymax, rx, rz, color, upper, lower=None, pivot=0, z=0):
        rings, segments = 18, 20
        start = len(positions)
        for row in range(1, rings):
            t = row/rings
            y = ymin+(ymax-ymin)*t
            profile = math.sin(math.pi*t)**.28
            slope = .28*math.pi*math.cos(math.pi*t)*math.sin(math.pi*t)**(-.72)/(ymax-ymin)
            for column in range(segments):
                angle = column*math.tau/segments
                vertex((cx+rx*profile*math.cos(angle), y, z+rz*profile*math.sin(angle)),
                       (math.cos(angle)/rx, -slope, math.sin(angle)/rz), color,
                       influence(y, upper, lower, pivot))
        for row in range(rings-2):
            for column in range(segments):
                a = start+row*segments+column
                b = start+row*segments+(column+1)%segments
                c, d = a+segments, b+segments
                indices.extend((a, c, b, b, c, d))
        bottom = vertex((cx, ymin, z), (0, -1, 0), color, influence(ymin, upper, lower, pivot))
        top = vertex((cx, ymax, z), (0, 1, 0), color, influence(ymax, upper, lower, pivot))
        for column in range(segments):
            a, b = start+column, start+(column+1)%segments
            indices.extend((bottom, a, b))
            a, b = start+(rings-2)*segments+column, start+(rings-2)*segments+(column+1)%segments
            indices.extend((top, b, a))

    shirt, trousers, neutral = (.67, .73, .78), (.075, .11, .17), (.64, .47, .34)
    tube(0, .89, 1.53, .205, .115, shirt, 1, 0, 1.18)
    tube(0, 1.47, 1.63, .065, .06, neutral, 2, 1, 1.56)
    tube(0, 1.57, 1.88, .125, .115, neutral, 2)
    for sign, arm, forearm, thigh, shin in [(-1, 3, 4, 7, 8), (1, 5, 6, 9, 10)]:
        tube(sign*.27, .88, 1.49, .062, .063, shirt, arm, forearm, 1.19)
        tube(sign*.27, .81, .96, .061, .052, neutral, forearm)
        tube(sign*.115, .09, .98, .086, .087, trousers, thigh, shin, .52)
        tube(sign*.115, .015, .14, .087, .16, (.035, .04, .05), shin, z=.065)
    attributes = {"POSITION": accessor(positions, 3, target=34962),
                  "NORMAL": accessor(normals, 3, target=34962),
                  "COLOR_0": accessor(colors, 3, target=34962),
                  "JOINTS_0": accessor(vertex_joints, 4, 5123, 34962),
                  "WEIGHTS_0": accessor(weights, 4, target=34962)}
    triangle_accessor = accessor([(i,) for i in indices], 1, 5125, 34963)
    inverse = []
    for x, y, z in global_positions:
        inverse.append((1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -x, -y, -z, 1))
    bind_accessor = accessor(inverse, 16)
    mesh_node = len(nodes)
    nodes.append({"name": "weighted_mannequin", "mesh": 0, "skin": 0})
    animations = []
    times = (0, .5, 1, 1.5, 2)
    time_accessor = accessor([(t,) for t in times], 1)

    def channel(animation, joint, axis, angles):
        values = []
        for angle in angles:
            q = [0, 0, 0, math.cos(angle/2)]
            q[axis] = math.sin(angle/2)
            values.extend([(0, 0, 0, 0), q, (0, 0, 0, 0)])
        output = accessor(values, 4)
        animation["samplers"].append({"input": time_accessor, "output": output, "interpolation": "CUBICSPLINE"})
        animation["channels"].append({"sampler": len(animation["samplers"])-1, "target": {"node": joint, "path": "rotation"}})

    for name in ("idle", "walk", "ride"):
        animation = {"name": name, "samplers": [], "channels": []}
        if name == "idle":
            channel(animation, 1, 2, (0, .025, 0, -.025, 0))
            channel(animation, 2, 1, (0, .04, 0, -.04, 0))
        elif name == "walk":
            for joint, sign in ((7, 1), (9, -1), (3, -1), (5, 1)):
                channel(animation, joint, 0, (0, sign*.44, 0, -sign*.44, 0))
            for joint in (8, 10):
                channel(animation, joint, 0, (.08, .38, .08, .38, .08))
            channel(animation, 1, 1, (0, .06, 0, -.06, 0))
        else:
            for joint, angle in ((7, -1.2), (9, -1.2), (8, 1.25), (10, 1.25), (3, -.95), (5, -.95), (4, -.25), (6, -.25)):
                channel(animation, joint, 0, (angle,)*5)
        animations.append(animation)
    return {"asset": {"version": "2.0", "generator": "VICE studio-owned weighted mannequin fixture"},
            "scene": 0, "scenes": [{"nodes": [0, mesh_node]}], "nodes": nodes,
            "meshes": [{"primitives": [{"attributes": attributes, "indices": triangle_accessor}]}],
            "skins": [{"joints": list(range(len(joints))), "skeleton": 0, "inverseBindMatrices": bind_accessor}],
            "animations": animations, "bufferViews": views, "accessors": accessors,
            "buffers": [{"byteLength": len(data), "uri": "data:application/octet-stream;base64,"+base64.b64encode(data).decode()}]}


def create(vice, output):
    base = Path(__file__).resolve().parent.parent / "examples/presentation-playground"
    if output.exists():
        raise RuntimeError("Choose a new output directory")
    shutil.copytree(base, output, ignore=shutil.ignore_patterns(".vice", "character"))
    game = output / "game/game.cpp"
    game.write_text(game.read_text().replace("std::clamp(walk / 1.5F, 0.6F, 1.8F)", "std::clamp(walk / 0.8F, 0.5F, 2.5F)"))
    (output / "sources/humanoid.gltf").write_text(json.dumps(source_model(), separators=(",", ":"))+"\n")
    subprocess.run([str(vice), "rig", "import", str(output / "sources/humanoid.gltf"), str(output / "assets/character"),
                    "a4000000-0000-4000-8000-000000000200", "a2000000-0000-4000-8000-000000000003"], check=True, capture_output=True)
    world = output / "assets/worlds/presentation.vworld"
    lines = [line for line in world.read_text().splitlines() if " rig_part " not in line and not line.startswith("resource e400")]
    bindings = (output / "assets/character/bindings.vworld").read_text().splitlines()
    lines += [line for line in bindings if line.startswith("resource e400") or " rig_part " in line]
    world.write_text("\n".join(lines)+"\n")
    project = output / "presentation.viceproj"
    project.write_text(project.read_text().replace("Animated Walk and Drive", "Weighted Skeletal Walk and Drive"))
    (output / "README.md").write_text("""# Weighted skeletal walk and drive

This standalone validation project exercises true weighted skinning, inverse-bind matrices and cubic animation through the same World/Project/Runtime/module contracts as the rigid-node example. The neutral mannequin is studio-owned procedural test geometry, not production CRASHOUT character art.

Build `presentation.viceproj` with VICE. Walk with arrows, enter/exit with E. The shared game module controls idle/walk/ride clips, physical movement, vehicle controls, audio and the Rules of Reality example. The skin is one indexed mesh with eleven joints and blended limb weights, not rigid pieces moved as a substitute for skinning.

Reproduce in a new directory with `scripts/create_skinning_example.py --vice PATH --output NEW_DIRECTORY`. Original audio/scene dependency notices are retained. The neutral mannequin source is generated by that script and carries no third-party character license.
""")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--vice", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    create(args.vice.resolve(), args.output.resolve())
