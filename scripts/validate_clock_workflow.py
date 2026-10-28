#!/usr/bin/env python3
"""Authoritative clock authoring and native checkpoint continuity."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from validate_presentation_workflow import validate as tour


def validate(vice, native):
    passed = []
    observed = {}
    def command(*args, expected=0):
        r = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=180)
        if r.returncode != expected:
            raise RuntimeError(r.stdout + r.stderr)
        return json.loads(r.stdout)
    with tempfile.TemporaryDirectory(prefix="vice-clock-") as tmp:
        root = Path(tmp)
        project = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground", project, ignore=shutil.ignore_patterns(".vice"))
        world = project / "assets/worlds/presentation.vworld"
        initial = command("world", "inspect", world)["data"]
        edit = root / "clock.viceedit"
        edit.write_text(f"vice-world-edit 1\nrevision {initial['source_revision']}\nset_world_clock 1000 500 125 0.25 1 0\n")
        original = world.read_bytes()
        preview = command("world", "edit", world, edit, "--dry-run")["data"]
        if world.read_bytes() != original:
            raise RuntimeError("Clock preview changed source")
        applied = command("world", "edit", world, edit, "--apply")["data"]
        if preview["result_revision"] != applied["result_revision"]:
            raise RuntimeError("Clock preview/publication mismatch")
        state = command("world", "inspect", world)["data"]
        if state["world_clock"]["ticks"] != "1000" or state["world_clock"]["input_fraction"] != .25:
            raise RuntimeError("Clock source inspection lost state")
        passed.append("revision_safe_clock_authoring_and_inspection")
        command("world", "edit", world, edit, "--apply", expected=3)
        before = world.read_bytes()
        edit.write_text(f"vice-world-edit 1\nrevision {state['source_revision']}\nset_world_clock 1001 0 0 0 0 1\n")
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Invalid clock edit changed source")
        passed.append("stale_and_invalid_clock_edits_rejected")
        command("project", "build", project / "presentation.viceproj")
        passed.append("clock_state_cooks_and_stages")
    if native:
        data = tour(vice, True)
        observed["tour"] = data
        cold = int(data["observed"]["driving"]["vehicle"]["world_clock"]["ticks"])
        resumed = int(data["observed"]["resumed_exit"]["vehicle"]["world_clock"]["ticks"])
        if cold <= 0 or resumed <= cold:
            raise RuntimeError("Runtime clock did not advance/continue after checkpoint")
        passed.append("native_world_time_advances_and_resumes_from_checkpoint")
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
        a.output.write_text(result + "\n")
    print(result)
