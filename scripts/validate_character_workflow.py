#!/usr/bin/env python3
"""Validate authored capsule edits/builds and an optional native composed game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def validate(vice, native):
    passed, observed = [], {}

    def command(*args, expected=0):
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=120)
        if result.returncode != expected:
            raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)

    def entity(value):
        return f"9b000000-0000-4000-8000-{value:012x}"

    with tempfile.TemporaryDirectory(prefix="vice-character-workflow-") as directory:
        root = Path(directory)
        project_root = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/character-playground", project_root,
                        ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "character.viceproj"
        world = project_root / "assets/worlds/playground.vworld"
        before = world.read_bytes()
        revision = command("world", "inspect", world)["data"]["source_revision"]
        request = root / "character.viceedit"
        # This example opts into its automatic acceptance tour through authored desired motion.
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nset_character {entity(0x202)} 1.8 0.3 70 100 0.872664626 0.45 0.5 0 0 0 2 0 0 0\n")
        preview = command("world", "edit", world, request, "--dry-run")
        if world.read_bytes() != before:
            raise RuntimeError("Dry-run changed authored source")
        applied = command("world", "edit", world, request, "--apply")
        if not applied["data"]["published"] or applied["data"]["result_revision"] != preview["data"]["result_revision"]:
            raise RuntimeError("Capsule publication disagrees with preview")
        if entity(0x202) not in preview["data"]["affected_entities"] or 1 not in preview["data"]["affected_regions"]:
            raise RuntimeError("Capsule impact omitted physical identity/region")
        passed.append("revision_safe_capsule_edit_and_preview")
        if command("world", "edit", world, request, "--apply", expected=3)["data"]["code"] != "revision_conflict":
            raise RuntimeError("Stale capsule request accepted")
        passed.append("stale_capsule_edit_rejected")
        revision = command("world", "inspect", world)["data"]["source_revision"]
        digest = hashlib.sha256(world.read_bytes()).hexdigest()
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nset_rigid_box {entity(0x202)} dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 discrete solid\n")
        if command("world", "edit", world, request, "--apply", expected=3)["data"]["code"] != "transaction_rejected":
            raise RuntimeError("Two physical representations accepted")
        if hashlib.sha256(world.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Rejected collider edit changed source")
        passed.append("exclusive_physics_edit_rolls_back")
        command("world", "validate", world)
        built = command("project", "build", project)["data"]
        if built["state"] != "built":
            raise RuntimeError("Character project did not build")
        passed.append("capsule_world_visual_mesh_and_sdk_module_stage")
        if native:
            exe = Path(built["game_executable"])
            sessions = root / "sessions"
            session = sessions / "capsule-check"
            with (root / "native.log").open("w") as log:
                process = subprocess.Popen([str(exe), "--vice-dev-session", str(sessions), session.name],
                                           cwd=exe.parent, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 30
                    while True:
                        status = subprocess.run([str(vice), "runtime", "inspect", str(session)],
                                                capture_output=True, text=True, timeout=5)
                        if status.returncode == 0:
                            data = json.loads(json.loads(status.stdout)["data"]["payload"])
                            if data["execution_state"] == "running" and data["frame"]["index"] > 0:
                                break
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("Native capsule launch failed: " + (root / "native.log").read_text(errors="replace"))
                        time.sleep(0.1)
                    deadline = time.monotonic() + 50
                    wall_start, wall_x = None, None
                    while True:
                        def debug(value):
                            return json.loads(command("runtime", "debug", session, entity(value))["data"]["payload"])
                        actor = debug(0x202)
                        position = actor["spatial"]["global_position"]
                        contact, profile = actor["character_state"], actor["character"]
                        if contact is None or profile is None:
                            raise RuntimeError("Native character debug unavailable")
                        x, y = position[0] - 1e12, position[1]
                        grounded = contact["ground_state"] == "grounded"
                        if grounded and 0.8 < x < 2 and y > 50.15:
                            observed.setdefault("stair", actor)
                        if grounded and 3.2 < x < 3.6 and y < 50.05 and profile["horizontal_velocity"][0] > 1.5:
                            if wall_x is None or abs(x - wall_x) > 0.01:
                                wall_start, wall_x = time.monotonic(), x
                            elif time.monotonic() - wall_start > 0.2:
                                observed.setdefault("wall", actor)
                        else:
                            wall_start, wall_x = None, None
                        if "wall" in observed and not grounded and y > 50.2:
                            observed.setdefault("jump", actor)
                        if "jump" in observed and grounded and y < 50.05:
                            observed.setdefault("landing", actor)
                        failed, independent = debug(0x211), debug(0x212)
                        if "landing" in observed and not failed["dependency"]["available"] and independent["dependency"]["available"]:
                            cause = failed["dependency"]
                            if cause["root_reason"] != "link_broken" or cause["root_cause"] != entity(0x208) or cause["break_impulse"] <= 1000 or cause["break_step"] == 0:
                                raise RuntimeError("Composed physical failure lacks provenance")
                            if "stair" not in observed or not independent["light_supplied"] or failed["light_supplied"]:
                                raise RuntimeError("Composed acceptance did not complete")
                            observed.update(failed_lamp=failed, independent_lamp=independent)
                            passed += ["native_capsule_climbs_authored_stair", "native_capsule_stops_at_tall_wall",
                                       "native_capsule_jumps_and_lands", "native_composed_physical_supply_failure"]
                            break
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError(f"Native tour incomplete: checkpoints={list(observed)}, actor={actor}; " + (root / "native.log").read_text(errors="replace")[-3000:])
                        time.sleep(0.05)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
        if hashlib.sha256(world.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Playing rewrote authored capsule source")
        passed.append("authored_capsule_source_unchanged")
    return {"schema": "vice.character.validation.v1", "ok": True, "native_runtime": native,
            "passed": passed, "observed": observed}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vice", required=True, type=Path)
    parser.add_argument("--run-runtime", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = validate(args.vice.resolve(), args.run_runtime)
    text = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(text)
    print(text, end="")
