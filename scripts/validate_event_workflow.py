#!/usr/bin/env python3
"""Typed World facts: cook/inspect validation and native causal checkpoint continuation."""
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

    with tempfile.TemporaryDirectory(prefix="vice-world-events-") as temporary:
        root = Path(temporary)
        project = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground", project,
                        ignore=shutil.ignore_patterns(".vice"))
        world = project / "assets/worlds/presentation.vworld"
        original = world.read_text()
        history = "bc000000-0000-4000-8000-000000000001"
        marker = "bc000000-0000-4000-8000-000000000002"
        fact = "bc000000-0000-4000-8000-000000000003"
        schema = "bc000000-0000-4000-8000-000000000004"
        subject = "a2000000-0000-4000-8000-000000000003"
        target = "a2000000-0000-4000-8000-000000000002"
        appended = f'''world_clock 1000 1000 0 0 0 1
component {schema} 1 "ExampleFact" fields 2 1 integer "quantity" 2 text "label"
causal_history {history} 3
causal_event {marker} none {marker} 1 0 capture_started runtime none none none none 0 available available none 0 0 0 at 500
causal_event {fact} {marker} {marker} 2 1 world_event gameplay {subject} {target} none none 0 available available none 0 0 1 at 1000 typed {schema} 1 2 1 integer 50 2 text "generic transfer evidence" 1 {history} {marker} 1
'''
        world.write_text(original + appended)
        state = command("world", "inspect", world)["data"]
        event = state["causal_history"]["events"][-1]
        if event["id"] != fact or event["world_ticks"] != "1000" or event["parents"][0]["id"] != marker or len(event["payload"]) != 1:
            raise RuntimeError("World source inspection lost typed fact, time or cause")
        passed.append("typed_fact_source_inspection_in_shared_causal_journal")
        command("project", "build", project / "presentation.viceproj")
        passed.append("typed_fact_cooks_and_stages_with_native_module")
        for malformed in (appended.replace("at 1000 typed", "at 1001 typed"),
                          appended.replace(f"1 {history} {marker} 1", f"1 {history} {fact} 1"),
                          appended.replace(f"typed {schema} 1 2", f"typed {schema} 2 2")):
            world.write_text(original + malformed)
            command("world", "validate", world, expected=3)
        passed.append("future_time_wrong_parent_and_schema_version_rejected")
    if native:
        data = tour(vice, True)
        observed["tour"] = data
        cold = data["observed"]["driving"]["vehicle"]
        resumed = data["observed"]["after_resume"]
        def facts(state):
            return [e for e in state["causal_history"]["events"] if e["kind"] == "world_event"]
        first, continuation = facts(cold)[-1], facts(resumed)[-1]
        if continuation["id"] == first["id"] or continuation["parents"][0]["id"] != first["id"]:
            raise RuntimeError("Fresh native Runtime lost previous startup fact as cause")
        if continuation["payload"][0]["fields"][0]["value"] != "2" or int(continuation["world_ticks"]) <= int(first["world_ticks"]):
            raise RuntimeError("Counter/time did not continue through checkpoint")
        if cold["world_signals"]["retained_count"] != 0:
            raise RuntimeError("Startup signal exceeded its World-time lifetime")
        passed += ["native_fact_identity_payload_time_and_cause_resume", "native_signal_expires_while_persistent_fact_remains"]
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
