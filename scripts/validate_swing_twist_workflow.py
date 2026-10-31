#!/usr/bin/env python3
"""Persistent swing/twist joint authoring, SDK staging and native checkpoint reconstruction."""
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
        r = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=180)
        if r.returncode != expected:
            raise RuntimeError(f"{args}: {r.stdout}\n{r.stderr}")
        return json.loads(r.stdout)
    with tempfile.TemporaryDirectory(prefix="vice-articulation-") as directory:
        root = Path(directory)
        project_root = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/swing-twist-articulation", project_root,
                        ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "articulation.viceproj"
        world = project_root / "assets/worlds/articulation.vworld"
        joint = "d7000000-0000-4000-8000-000000000003"
        initial = command("world", "inspect", world)["data"]
        record = next(e for e in initial["entities"] if e["id"] == joint)["swing_twist_joint"]["record"]
        request = root / "joint.viceedit"
        request.write_text(f"vice-world-edit 1\nrevision {initial['source_revision']}\nset_swing_twist_joint {joint} {record}\n")
        before = world.read_bytes()
        preview = command("world", "edit", world, request, "--dry-run")["data"]
        if world.read_bytes() != before or joint not in preview["affected_entities"] or 1 not in preview["affected_regions"]:
            raise RuntimeError("Joint dry-run changed source or omitted endpoints")
        applied = command("world", "edit", world, request, "--apply")["data"]
        if applied["result_revision"] != preview["result_revision"]:
            raise RuntimeError("Joint apply disagrees with preview")
        passed.append("revision_safe_joint_edit_reports_bodies_and_region")
        # Invalid frames must not partially publish.
        current = command("world", "inspect", world)["data"]
        fields = record.split()
        fields[6:9] = ["0", "0", "0"]
        request.write_text(f"vice-world-edit 1\nrevision {current['source_revision']}\nset_swing_twist_joint {joint} {' '.join(fields)}\n")
        snapshot = world.read_bytes()
        r = subprocess.run([str(vice), "world", "edit", str(world), str(request), "--apply"], capture_output=True, text=True)
        if not r.returncode or world.read_bytes() != snapshot:
            raise RuntimeError("Invalid joint frame published partial state")
        passed.append("invalid_frame_edit_is_atomic")
        digest = hashlib.sha256(world.read_bytes()).hexdigest()
        built = command("project", "build", project)["data"]
        if built["state"] != "built":
            raise RuntimeError("Articulation SDK project failed to stage")
        passed.append("joint_world_and_game_module_cook_and_stage")
        if native:
            exe = Path(built["game_executable"])
            sessions = root / "sessions"
            def launch(executable, name):
                session = sessions / name
                log = (root / (name+".log")).open("w")
                p = subprocess.Popen([str(executable), "--vice-dev-session", str(sessions), name], cwd=executable.parent,
                                     stdout=log, stderr=subprocess.STDOUT)
                return p, log, session
            def stop(p, log):
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        p.kill(); p.wait(timeout=5)
                log.close()
            def settle(p, session):
                deadline = time.monotonic()+35
                stable = []
                while time.monotonic() < deadline:
                    r = subprocess.run([str(vice), "runtime", "debug", str(session), joint], capture_output=True, text=True, timeout=5)
                    if r.returncode == 0:
                        data = json.loads(json.loads(r.stdout)["data"]["payload"])
                        o = data.get("swing_twist_state")
                        if o and o["active"] and o["angular_error"] < .05 and max(map(abs,o["motor_impulses"])) > 0:
                            stable.append(o["angular_error"])
                            stable = stable[-12:]
                            if len(stable) == 12 and max(stable)-min(stable) < .008:
                                return data
                        else:
                            stable = []
                    if p.poll() is not None:
                        break
                    time.sleep(.08)
                raise RuntimeError("Native multi-axis pose did not settle: "+str(r.stdout[-2000:]))
            p, log, session = launch(exe, "held")
            saved = root / "pose.vworldc"
            try:
                observed["held"] = settle(p, session)
                command("runtime", "checkpoint", session, saved)
                passed.append("native_gravity_loaded_pose_reports_actual_error_and_impulse")
            finally:
                stop(p, log)
            stage = root / "resumed-stage"
            shutil.copytree(exe.parent, stage)
            cooked = list((stage / "content").rglob("articulation.vworldc"))
            if len(cooked) != 1:
                raise RuntimeError("Expected one staged articulation World")
            shutil.copy2(saved, cooked[0])
            p, log, session = launch(stage / exe.name, "restored")
            try:
                observed["restored"] = settle(p, session)
                if abs(observed["held"]["swing_twist_state"]["angular_error"]-observed["restored"]["swing_twist_state"]["angular_error"]) > .04:
                    raise RuntimeError("Fresh Runtime rebased the saved angular reference")
                passed.append("fresh_runtime_reconstructs_saved_joint_reference_and_target")
            finally:
                stop(p, log)
        if hashlib.sha256(world.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Runtime modified authored joint source")
        passed.append("authored_joint_source_remains_unchanged")
    return {"passed": passed, "observed": observed}


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--vice", required=True, type=Path)
    p.add_argument("--run-runtime", action="store_true")
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    result = json.dumps(validate(a.vice.resolve(), a.run_runtime), indent=2)
    if a.output:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(result+"\n")
    print(result)
