#!/usr/bin/env python3
"""Validate imported rigid-node animation/audio in the native walk/drive/save loop."""
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
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=180)
        if result.returncode != expected:
            raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)
    def entity(n):
        return f"a2000000-0000-4000-8000-{n:012x}"
    with tempfile.TemporaryDirectory(prefix="vice-presentation-workflow-") as directory:
        root = Path(directory)
        project_root = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground", project_root,
                        ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "presentation.viceproj"
        world = project_root / "assets/worlds/presentation.vworld"
        original = world.read_bytes()
        def inspect():
            return command("world", "inspect", world)["data"]
        imported = root / "reimported"
        command("rig", "import", project_root / "sources/humanoid.gltf", imported,
                "a4000000-0000-4000-8000-000000000200", entity(3))
        for path in imported.iterdir():
            if path.read_bytes() != (project_root / "assets/character" / path.name).read_bytes():
                raise RuntimeError("CLI reimport changed deterministic character assets")
        passed.append("cli_embedded_gltf_import_is_deterministic")
        initial = inspect()
        request = root / "attachment.viceedit"
        request.write_text(f"vice-world-edit 1\nrevision {initial['source_revision']}\nset_character_attachment {entity(3)} {entity(2)} 0 -0.4 0\n")
        preview = command("world", "edit", world, request, "--dry-run")["data"]
        if world.read_bytes() != original or entity(3) not in preview["affected_entities"] or 1 not in preview["affected_regions"]:
            raise RuntimeError("Attachment dry-run missing identity/region or changed source")
        result = command("world", "edit", world, request, "--apply")["data"]
        if preview["result_revision"] != result["result_revision"]:
            raise RuntimeError("Attachment publication disagrees with dry-run")
        actor = next(e for e in inspect()["entities"] if e["id"] == entity(3))
        if actor["character_attachment"]["carrier"] != entity(2):
            raise RuntimeError("World inspect omitted carrier identity")
        passed.append("revision_safe_attachment_authoring_and_inspection")
        command("world", "edit", world, request, "--apply", expected=3)
        passed.append("stale_attachment_edit_rejected")
        revision = inspect()["source_revision"]
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nset_character_attachment {entity(3)} {entity(99)} 0 0 0\n")
        command("world", "edit", world, request, "--apply", expected=3)
        if inspect()["source_revision"] != revision:
            raise RuntimeError("Invalid carrier changed source")
        passed.append("invalid_carrier_reference_rejected_atomically")
        # Restore the known authored fixture to exercise its walking opening.
        world.write_bytes(original)
        digest = hashlib.sha256(original).hexdigest()
        def source_hashes():
            return {str(p.relative_to(project_root)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in (project_root / "assets").rglob("*") if p.is_file()}
        authored = source_hashes()
        command("world", "validate", world)
        built = command("project", "build", project)["data"]
        content = Path(built["runtime_content_root"])
        if len(list(content.rglob("*.vrigc"))) != 1 or len(list(content.rglob("*.vpcmc"))) != 3:
            raise RuntimeError("Staged rig/audio assets missing")
        if not (Path(built["game_output_root"]) / "THIRD_PARTY_PRESENTATION.txt").is_file():
            raise RuntimeError("Package dependency notices missing")
        passed.append("animated_world_sdk_module_assets_and_notices_cook_and_stage")
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
                        raise RuntimeError("Native interaction failed: " + (root / f"{name}.log").read_text(errors="replace"))
                    time.sleep(.03)
            def wait_for(process, session, predicate):
                deadline = time.monotonic() + 30
                while True:
                    actor, car = debug(session, 3), debug(session, 2)
                    if predicate(actor, car):
                        return actor, car
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Interaction phase timed out: " + json.dumps({"character": actor, "vehicle": car}))
            process, log, session = launch(exe, "cold")
            try:
                walking = debug(session, 3)
                if walking["character_attachment"] is not None or walking["character"]["horizontal_velocity"][0] <= 0:
                    raise RuntimeError("Walking phase did not execute")
                if walking["animation_state"]["clip_name"] != "walk":
                    raise RuntimeError("Walking did not select imported walk clip")
                observed["walking"] = walking
                passed.append("native_physical_walking_opening")
                actor, car = wait_for(process, session, lambda a, v: a["character_attachment"] is not None and v["vehicle"]["distance"] > .5)
                camera = debug(session, 4)
                if actor["character_attachment"]["carrier"] != entity(2) or abs(actor["spatial"]["global_position"][2] - car["spatial"]["global_position"][2]) > .5:
                    raise RuntimeError("Mounted character did not follow actual chassis")
                if abs(camera["spatial"]["global_position"][2] - actor["spatial"]["global_position"][2] + 6) > 1:
                    raise RuntimeError("Authoritative following camera did not move")
                if car["audio_state"]["duration"] <= 0 or car["audio_state"]["left_gain"] + car["audio_state"]["right_gain"] <= 0:
                    raise RuntimeError("Spatial vehicle audio did not evaluate")
                observed["driving"] = {"character": actor, "vehicle": car, "camera": camera}
                passed += ["native_character_attaches_and_follows_physical_vehicle", "native_sdk_camera_tracks_driving_character"]
                saved = root / "mounted.vworldc"
                command("runtime", "checkpoint", session, saved)
                if saved.stat().st_size < 1000:
                    raise RuntimeError("Presentation checkpoint is unexpectedly small")
                if actor["animation_state"]["clip_name"] != "ride" or not car["audio_emitter"]["playing"]:
                    raise RuntimeError("Mounted rig/audio intent missing")
                observed["saved_presentation"] = {"animation": actor["animation"], "audio": car["audio_emitter"]}
                passed.append("native_checkpoint_captures_riding_animation_and_audio")
                actor, car = wait_for(process, session, lambda a, v: a["character_attachment"] is None and v["vehicle"]["distance"] >= 10)
                if abs(car["vehicle_state"]["forward_speed"]) > .3 or actor["spatial"]["global_position"][0] < 1.5:
                    raise RuntimeError("Exit did not restore a stopped physical walker")
                actor, car = wait_for(process, session, lambda a, v: a["animation_state"]["clip_name"] == "idle")
                passed.append("native_exit_selects_imported_idle_clip")
                observed["exited"] = {"character": actor, "vehicle": car}
                passed.append("native_brake_and_safe_exit_restore_walking_controller")
            finally:
                stop(process, log)
            stage = root / "resumed-stage"
            shutil.copytree(exe.parent, stage)
            cooked = list((stage / "content").rglob("presentation.vworldc"))
            if len(cooked) != 1:
                raise RuntimeError("Missing staged interaction World")
            shutil.copyfile(saved, cooked[0])
            process, log, session = launch(stage / exe.name, "resumed")
            try:
                actor = debug(session, 3)
                if not actor["character_attachment"] or actor["character_attachment"]["carrier"] != entity(2):
                    raise RuntimeError("Restart failed to reconstruct attached representation")
                if actor["animation_state"]["clip_name"] != "ride":
                    raise RuntimeError("Saved rig did not resume riding")
                observed["after_resume"] = actor
                passed.append("native_restart_reconstructs_mounted_character")
                actor, car = wait_for(process, session, lambda a, v: a["character_attachment"] is None and v["vehicle"]["distance"] >= 10)
                observed["resumed_exit"] = {"character": actor, "vehicle": car}
                passed.append("native_resumed_driving_braking_and_exit_complete")
            finally:
                stop(process, log)
        if source_hashes() != authored or hashlib.sha256(world.read_bytes()).hexdigest() != digest:
            raise RuntimeError("Runtime changed authored interaction source")
        passed.append("runtime_preserves_authored_world_and_presentation_assets")
    return {"passed": passed, "observed": observed}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--vice", required=True, type=Path)
    parser.add_argument("--run-runtime", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = json.dumps(validate(args.vice.resolve(), args.run_runtime), indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report + "\n")
    print(report)
