#!/usr/bin/env python3
"""Compile and stage the SDK-only playground; optionally observe its native game."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def validate(vice, run_runtime):
    passed = []
    observed = {}

    def command(*args):
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)

    with tempfile.TemporaryDirectory(prefix="vice-gameplay-workflow-") as directory:
        root = Path(directory)
        source = Path(__file__).resolve().parent.parent / "examples" / "rigid-box-playground"
        project_root = root / "project"
        shutil.copytree(source, project_root, ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "playground.viceproj"
        authored = project_root / "assets" / "worlds" / "playground.vworld"
        digest = hashlib.sha256(authored.read_bytes()).hexdigest()
        command("project", "validate", project)
        passed.append("validate_sdk_playground_project")
        built = command("project", "build", project)["data"]
        if built["state"] != "built":
            raise RuntimeError(f"Project was not built: {built}")
        executable = Path(built["game_executable"])
        if not list((executable.parent / "modules").glob("rigid-box-playground.*")):
            raise RuntimeError("SDK gameplay module was not staged")
        passed.append("compile_and_stage_sdk_only_game_module")
        passed.append("cook_and_stage_authored_mesh_and_world")
        if run_runtime:
            sessions = root / "sessions"
            session = sessions / "gameplay-check"
            with (root / "native.log").open("w", encoding="utf-8") as log:
                process = subprocess.Popen([str(executable), "--vice-dev-session", str(sessions), session.name],
                                           cwd=executable.parent, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 25
                    while True:
                        status = subprocess.run([str(vice), "runtime", "inspect", str(session)], capture_output=True, text=True, timeout=5)
                        if status.returncode == 0:
                            data = json.loads(json.loads(status.stdout)["data"]["payload"])
                            if data["execution_state"] == "running" and data["frame"]["index"] > 0:
                                break
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("Native gameplay failed: " + (root / "native.log").read_text(errors="replace"))
                        time.sleep(0.1)
                    passed.append("native_game_module_start_and_render_frames")
                    deadline = time.monotonic() + 10
                    while True:
                        actor = json.loads(command("runtime", "debug", session, "94000000-0000-4000-8000-000000000202")["data"]["payload"])
                        mover = json.loads(command("runtime", "debug", session, "94000000-0000-4000-8000-000000000203")["data"]["payload"])
                        if actor["state"] != "available" or mover["state"] != "available":
                            raise RuntimeError("Authored gameplay entities unavailable")
                        position = mover["spatial"]["global_position"]
                        velocity = mover["rigid_box"]["linear_velocity"]
                        if abs(actor["spatial"]["global_position"][1] - 0.5) < 0.05 and position[0] > 999999999997.25 and abs(velocity[0] - 1.5) < 0.01:
                            observed = {"actor_position": actor["spatial"]["global_position"], "platform_position": position,
                                        "platform_velocity": velocity}
                            passed.append("native_module_update_controls_authored_kinematic_body")
                            passed.append("native_dynamic_body_contacts_authored_floor")
                            break
                        if process.poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError(f"Gameplay controls not observed: {actor}, {mover}")
                        time.sleep(0.1)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
        if hashlib.sha256(authored.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Build or gameplay rewrote authored source")
        passed.append("authored_source_unchanged")
    return {"schema": "vice.gameplay.validation.v1", "ok": True, "native_runtime": run_runtime,
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
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
