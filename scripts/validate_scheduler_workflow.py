#!/usr/bin/env python3
"""Persistent scheduler authoring and native save/restart cadence."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from validate_presentation_workflow import validate as tour


def validate(vice, native):
    passed, observed = [], {}

    def command(*args, expected=0):
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=180)
        if result.returncode != expected:
            raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)

    with tempfile.TemporaryDirectory(prefix="vice-scheduler-") as tmp:
        root = Path(tmp)
        project = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground", project,
                        ignore=shutil.ignore_patterns(".vice"))
        world = project / "assets/worlds/presentation.vworld"
        edit = root / "schedule.viceedit"
        schema = "bd000000-0000-4000-8000-000000000001"
        schedule = "bd000000-0000-4000-8000-000000000002"
        subject = "a2000000-0000-4000-8000-000000000003"
        target = "a2000000-0000-4000-8000-000000000002"

        def inspect():
            return command("world", "inspect", world)["data"]

        def transaction(operations):
            edit.write_text(f"vice-world-edit 1\nrevision {inspect()['source_revision']}\n{operations}\n")

        record = f"{schedule} 5000000 1000000 0 1 0 0 1 {subject} {target} none 0 {schema} 1 0 0"
        transaction(f'register_component {schema} 1 "TimedFact" 0\nschedule_event {record}')
        original = world.read_bytes()
        preview = command("world", "edit", world, edit, "--dry-run")["data"]
        if world.read_bytes() != original or subject not in preview["affected_entities"] or 1 not in preview["affected_regions"]:
            raise RuntimeError("Schedule preview changed source or omitted affected entity/region")
        applied = command("world", "edit", world, edit, "--apply")["data"]
        state = inspect()
        if preview["result_revision"] != applied["result_revision"] or state["scheduler"]["pending"] != 1 or state["schedules"][0]["due_ticks"] != "5000000":
            raise RuntimeError("Published schedule disagreed with preview or inspection")
        passed.append("revision_safe_schedule_creation_and_inspection")
        command("world", "edit", world, edit, "--apply", expected=3)
        before = world.read_bytes()
        transaction(f'cancel_schedule {schedule}\nschedule_event {record.replace(subject, "a2000000-0000-4000-8000-000000000099")}')
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Failed schedule creation partially cancelled existing work")
        passed.append("stale_edit_and_transaction_failure_are_atomic")
        command("project", "build", project / "presentation.viceproj")
        passed.append("persistent_schedule_cooks_and_stages")
        transaction(f"cancel_schedule {schedule}")
        command("world", "edit", world, edit, "--apply")
        if inspect()["scheduler"]["count"]:
            raise RuntimeError("Cancellation left scheduled work")
        passed.append("schedule_cancellation_removes_persistent_work")
    if native:
        data = tour(vice, True)
        observed["tour"] = data
        cold = data["observed"]["driving"]["vehicle"]
        resumed = data["observed"]["resumed_exit"]["vehicle"]
        schedule_id = "a4000000-0000-4000-8000-000000000800"
        def scheduled(state):
            return next(s for s in state["world_schedules"]["entries"] if s["id"] == schedule_id)
        first, later = scheduled(cold), scheduled(resumed)
        if int(first["occurrence"]) < 1 or int(later["occurrence"]) <= int(first["occurrence"]):
            raise RuntimeError("Recurring occurrence did not continue after checkpoint")
        if (int(later["due_ticks"]) - 1000000) % 3000000 or first["parents"] != later["parents"]:
            raise RuntimeError("Restart reset cadence or scheduling cause")
        firings = [e for e in resumed["causal_history"]["events"] if e.get("schedule") and e["schedule"]["id"] == schedule_id]
        if not firings or max(int(e["schedule"]["occurrence"]) for e in firings) != int(later["occurrence"]):
            raise RuntimeError("Runtime did not explain latest scheduled consequence")
        passed += ["native_scheduler_identity_cadence_and_cause_resume", "native_firing_provenance_explains_future_consequences"]
    return {"passed": passed, "observed": observed}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--vice", required=True, type=Path)
    parser.add_argument("--run-runtime", action="store_true")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = json.dumps(validate(args.vice.resolve(), args.run_runtime), indent=2)
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(result + "\n")
    print(result)
