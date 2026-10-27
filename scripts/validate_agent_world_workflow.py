#!/usr/bin/env python3
"""Exercise the real CLI edit/build pipeline; optionally inspect a native game."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time


def validate(vice, run_runtime):
    steps = []

    def command(*args, expected=0):
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=45)
        response = json.loads(result.stdout)
        if result.returncode != expected:
            raise RuntimeError(f"{args}: exit {result.returncode}, expected {expected}: {response}")
        return response

    with tempfile.TemporaryDirectory(prefix="vice-agent-workflow-") as directory:
        root = Path(directory)
        worlds = root / "assets" / "worlds"
        worlds.mkdir(parents=True)
        world = worlds / "main.vworld"
        entity = "90000000-0000-4000-8000-000000000201"
        related = "90000000-0000-4000-8000-000000000202"
        world.write_text(
            "vice-world-source 1\nregion 1 10 1000000000000 0 0 0 0 0 reduced none\n"
            f"entity {entity} spatial 1000000000000.25 0 0 1\nentity {related} none\n", encoding="utf-8")
        project = root / "workflow.viceproj"
        project.write_text(
            'vice-project 1\nid 90000000-0000-4000-8000-000000000001\n'
            'name "VICE Workflow Check"\ngame_name "VICE Workflow Check"\n'
            'game_target "vice-workflow-check"\nassets "assets"\nstartup_world "worlds/main.vworld"\n',
            encoding="utf-8")
        command("project", "validate", project)
        before = world.read_bytes()
        inspected = command("world", "inspect", world)
        revision = inspected["data"]["source_revision"]
        if revision != "sha256:" + hashlib.sha256(before).hexdigest():
            raise RuntimeError("Inspect revision does not match the exact source bytes")
        steps.append("inspect_source_revision")

        request = root / "move.viceedit"
        request.write_text(
            f"vice-world-edit 1\nrevision {revision}\n"
            f"set_spatial {entity} 1000000000001.25 2.5 3.75 1\n"
            f"set_transform {entity} 0 0 0 1 1 1 1\n"
            f"add_relation {entity} 90000000-0000-4000-8000-000000000301 {related}\n",
            encoding="utf-8")
        preview = command("world", "edit", world, request, "--dry-run")
        if world.read_bytes() != before or preview["data"]["published"]:
            raise RuntimeError("Dry-run mutated authored source")
        steps.append("dry_run_without_mutation")
        applied = command("world", "edit", world, request, "--apply")
        if not applied["data"]["published"] or applied["data"]["result_revision"] != preview["data"]["result_revision"]:
            raise RuntimeError("Published edit differs from the preview")
        steps.append("atomic_apply_matches_preview")
        updated = world.read_bytes()
        stale = command("world", "edit", world, request, "--apply", expected=3)
        if stale["data"]["code"] != "revision_conflict" or world.read_bytes() != updated:
            raise RuntimeError("Stale edit was not rejected without mutation")
        steps.append("stale_revision_rejected")
        revision = applied["data"]["result_revision"]
        request.write_text(
            f"vice-world-edit 1\nrevision {revision}\nset_fidelity 1 full\n"
            f"set_transform {entity} 0 0 0 0 1 1 1\n", encoding="utf-8")
        invalid = command("world", "edit", world, request, "--apply", expected=3)
        if invalid["data"]["failed_operation"] != 1 or world.read_bytes() != updated:
            raise RuntimeError("Invalid batch was not rejected atomically")
        steps.append("invalid_batch_rejected_without_partial_edit")
        after = command("world", "inspect", world)
        if after["data"]["entities"][0]["spatial"]["global_position"] != [1000000000001.25, 2.5, 3.75]:
            raise RuntimeError("Authored large-world position did not survive the edit")
        command("world", "validate", world)
        steps.append("validate_edited_world")
        built = command("project", "build", project)
        if built["data"]["state"] != "built":
            raise RuntimeError("Project did not build")
        steps.append("build_staged_project")

        if run_runtime:
            executable = Path(built["data"]["game_executable"])
            session_root = root / "sessions"
            session = session_root / "workflow-check"
            with (root / "runtime.log").open("w", encoding="utf-8") as log:
                process = subprocess.Popen([str(executable), "--vice-dev-session", str(session_root), session.name],
                                           cwd=executable.parent, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic() + 20
                    while True:
                        result = subprocess.run([str(vice), "runtime", "inspect", str(session)],
                                                capture_output=True, text=True, timeout=5)
                        if result.returncode == 0:
                            payload = json.loads(json.loads(result.stdout)["data"]["payload"])
                            if payload["execution_state"] == "running" and payload["frame"]["index"] > 0:
                                break
                        if process.poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError("Native Runtime did not become observable: " +
                                               (root / "runtime.log").read_text(encoding="utf-8", errors="replace"))
                        time.sleep(0.1)
                    if payload["world"]["entities"] != 2 or payload["world"]["relations"] != 1:
                        raise RuntimeError("Runtime did not load the edited authored World")
                    steps.append("native_runtime_inspect")
                    command("runtime", "profile", session)
                    steps.append("native_runtime_profile")
                    debug = command("runtime", "debug", session, entity)
                    entity_state = json.loads(debug["data"]["payload"])
                    if entity_state["state"] != "available" or entity_state["spatial"]["global_position"] != [1000000000001.25, 2.5, 3.75]:
                        raise RuntimeError("Runtime entity differs from the authored edit")
                    steps.append("native_runtime_entity_debug_matches_authored_edit")
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
    return {"schema": "vice.workflow.validation.v1", "ok": True, "native_runtime": run_runtime, "passed": steps}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vice", type=Path, required=True)
    parser.add_argument("--run-runtime", action="store_true", help="Open and inspect a native game window, then close the test process")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    report = validate(args.vice.resolve(), args.run_runtime)
    text = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.write_text(text, encoding="utf-8")
    print(text, end="")
