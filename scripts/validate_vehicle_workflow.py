#!/usr/bin/env python3
"""Accept vehicle authoring/build, native traction/braking, and checkpoint restart."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import struct
import uuid
import time


def validate(vice, native):
    passed, observed = [], {}
    def command(*args, expected=0):
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=180)
        if result.returncode != expected:
            raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)
    def entity(n):
        return f"a1000000-0000-4000-8000-{n:012x}"
    with tempfile.TemporaryDirectory(prefix="vice-vehicle-workflow-") as directory:
        root = Path(directory)
        project_root = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/vehicle-playground", project_root,
                        ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "vehicles.viceproj"
        world = project_root / "assets/worlds/vehicles.vworld"
        def inspect():
            return command("world", "inspect", world)["data"]
        initial = inspect()
        before = world.read_bytes()
        request = root / "vehicle.viceedit"
        request.write_text(f"vice-world-edit 1\nrevision {initial['source_revision']}\n"
                           f"set_vehicle {entity(2)} 2.5 1.5 0 0.32 0.5 30000 3500 7200 12000 4000 0.5 1 1 0 0 0\n")
        preview = command("world", "edit", world, request, "--dry-run")["data"]
        if world.read_bytes() != before or entity(2) not in preview["affected_entities"] or 1 not in preview["affected_regions"]:
            raise RuntimeError("Vehicle dry-run missing chassis/region or changing source")
        applied = command("world", "edit", world, request, "--apply")["data"]
        if preview["result_revision"] != applied["result_revision"]:
            raise RuntimeError("Dry-run and publication disagree")
        passed.append("revision_safe_vehicle_edit_with_chassis_and_region_impact")
        command("world", "edit", world, request, "--apply", expected=3)
        passed.append("stale_vehicle_edit_rejected")
        revision = inspect()["source_revision"]
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nclear_rigid_box {entity(2)}\n")
        command("world", "edit", world, request, "--apply", expected=3)
        if inspect()["source_revision"] != revision:
            raise RuntimeError("Invalid chassis removal changed source")
        passed.append("invalid_vehicle_chassis_edit_rejected_atomically")
        entities = {e["id"]: e for e in inspect()["entities"]}
        if entities[entity(2)]["vehicle"]["drive_force"] != 7200:
            raise RuntimeError("Inspect did not expose edited vehicle profile")
        digest = hashlib.sha256(world.read_bytes()).hexdigest()
        command("world", "validate", world)
        built = command("project", "build", project)["data"]
        passed.append("vehicle_world_and_sdk_game_module_cook_and_stage")
        if native:
            exe = Path(built["game_executable"])
            sessions = root / "sessions"
            def debug(session, n):
                return json.loads(command("runtime", "debug", session, entity(n))["data"]["payload"])
            def stop(process, log):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
                log.close()
            def launch(executable, name):
                session = sessions / name
                log = (root / f"{name}.log").open("w")
                process = subprocess.Popen([str(executable), "--vice-dev-session", str(sessions), name],
                                           cwd=executable.parent, stdout=log, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 30
                while True:
                    status = subprocess.run([str(vice), "runtime", "inspect", str(session)], capture_output=True, text=True, timeout=5)
                    if status.returncode == 0:
                        state = json.loads(json.loads(status.stdout)["data"]["payload"])
                        if state["execution_state"] == "running" and state["frame"]["index"] > 0:
                            return process, log, session
                    if process.poll() is not None or time.monotonic() > deadline:
                        stop(process, log)
                        raise RuntimeError("Native vehicle launch failed: " + (root / f"{name}.log").read_text(errors="replace"))
                    time.sleep(.05)
            process, log, session = launch(exe, "cold")
            try:
                deadline = time.monotonic() + 25
                while True:
                    dry, slippery, ice = [debug(session, n) for n in (2, 3, 4)]
                    if (dry["vehicle_state"] and slippery["vehicle_state"] and ice["vehicle_state"] and
                            dry["vehicle_state"]["forward_speed"] > 2 and slippery["vehicle_state"]["forward_speed"] > .3):
                        break
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Native vehicles did not accelerate")
                if abs(ice["vehicle_state"]["forward_speed"]) > .02:
                    raise RuntimeError("Zero-grip road invented propulsion")
                wheels = dry["vehicle_state"]["wheels"]
                if not all(w["grounded"] and w["material"] == entity(100) and w["normal_force"] > 0 for w in wheels):
                    raise RuntimeError("Native wheel/material contact evidence missing")
                observed["accelerating"] = {"dry": dry, "slippery": slippery, "zero_grip": ice}
                passed += ["native_suspension_loads_and_material_identities", "native_dry_and_slippery_traction", "native_zero_grip_prevents_propulsion"]
                saved = root / "saved.vworldc"
                command("runtime", "checkpoint", session, saved)
                # Format 14 follows the vehicle table with an empty attachment count. Decode only that
                # documented tail here; the engine validates the full file on restart.
                raw = saved.read_bytes()
                tail = len(raw) - 4 - 3 * 84 - 16
                if struct.unpack_from("<I", raw, 8)[0] != 17 or struct.unpack_from("<I", raw, tail)[0] != 3:
                    raise RuntimeError("Checkpoint vehicle table missing")
                saved_vehicles = {}
                for i in range(3):
                    fields = struct.unpack_from("<16s15fd", raw, tail + 4 + i * 84)
                    saved_vehicles[str(uuid.UUID(bytes=fields[0]))] = {"distance": fields[16], "throttle": fields[13], "steering": fields[14], "brake": fields[15]}
                saved_car = {"vehicle": saved_vehicles[entity(2)]}
                observed["saved_vehicles"] = saved_vehicles
                if saved_car["vehicle"]["distance"] <= 0:
                    raise RuntimeError("Checkpoint omitted vehicle travel")
                passed.append("native_vehicle_checkpoint_exports_controls_and_distance")
                while True:
                    stopped = debug(session, 2)
                    if stopped["vehicle"]["brake"] == 1 and abs(stopped["vehicle_state"]["forward_speed"]) < .1:
                        break
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Native automatic tour did not brake")
                observed["braked"] = stopped
                passed.append("native_sdk_braking_stops_vehicle")
            finally:
                stop(process, log)
            stage = root / "resumed-stage"
            shutil.copytree(exe.parent, stage)
            cooked = list((stage / "content").rglob("vehicles.vworldc"))
            if len(cooked) != 1:
                raise RuntimeError("Missing staged vehicle World")
            shutil.copyfile(saved, cooked[0])
            process, log, session = launch(stage / exe.name, "resumed")
            try:
                sample = debug(session, 2)
                if sample["vehicle"]["distance"] < saved_car["vehicle"]["distance"] or sample["vehicle"]["drive_force"] != 7200 or not sample["vehicle_state"]:
                    raise RuntimeError("Restart lost saved vehicle state or physical instantiation")
                observed["after_resume"] = sample
                passed.append("native_restart_reconstructs_vehicle_and_preserves_distance")
            finally:
                stop(process, log)
        if hashlib.sha256(world.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Runtime changed authored vehicle source")
        passed.append("runtime_preserves_authored_vehicle_source")
    return {"passed": passed, "observed": observed}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--vice", required=True, type=Path)
    parser.add_argument("--run-runtime", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = validate(args.vice.resolve(), args.run_runtime)
    report = json.dumps(result, indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report + "\n")
    print(report)
