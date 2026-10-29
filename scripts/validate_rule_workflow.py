#!/usr/bin/env python3
"""Revision-safe declarative rules and native schedule/event/rule/checkpoint continuity."""
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

    with tempfile.TemporaryDirectory(prefix="vice-rules-") as tmp:
        root = Path(tmp)
        project = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground", project,
                        ignore=shutil.ignore_patterns(".vice"))
        world = project / "assets/worlds/presentation.vworld"
        edit = root / "rule.viceedit"
        wake = "ca000000-0000-4000-8000-000000000100"
        state = "ca000000-0000-4000-8000-000000000101"
        rule = "ca000000-0000-4000-8000-000000000200"
        subject = "a2000000-0000-4000-8000-000000000002"
        target = "a2000000-0000-4000-8000-000000000003"
        record = f"{rule} 1 1 {wake} {subject} 1 4 500 0 {target} {state} 1 1 1 integer 7 0 0 0 0 0 0 none none 0 none none 0 none".replace("none", "00000000-0000-0000-0000-000000000000")

        def inspect():
            return command("world", "inspect", world)["data"]

        def transaction(ops):
            edit.write_text(f"vice-world-edit 1\nrevision {inspect()['source_revision']}\n{ops}\n")

        transaction(f'register_component {wake} 1 "AuthoredWake" 0\nregister_component {state} 1 "AuthoredRuleState" 1 1 integer "value"\nadd_rule {record}')
        original = world.read_bytes()
        preview = command("world", "edit", world, edit, "--dry-run")["data"]
        if world.read_bytes() != original or not {subject, target}.issubset(preview["affected_entities"]) or 1 not in preview["affected_regions"]:
            raise RuntimeError("Rule dry-run changed source or omitted affected endpoints/regions")
        applied = command("world", "edit", world, edit, "--apply")["data"]
        rules = inspect()["rules"]
        if preview["result_revision"] != applied["result_revision"] or len(rules) != 1 or rules[0]["outcome"] != "never_evaluated" or rules[0]["effect_operation"] != 0:
            raise RuntimeError("Rule publication differs from preview/inspection")
        passed.append("revision_safe_rule_definition_and_inspection")
        command("world", "edit", world, edit, "--apply", expected=3)
        before = world.read_bytes()
        transaction(f"remove_rule {rule}\nadd_rule {record.replace(rule + ' 1 1', rule + ' 2 1')}")
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Invalid replacement partially removed existing rule")
        transaction(f"add_rule {record}")
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Duplicate rule changed source")
        passed.append("stale_duplicate_and_unknown_version_failures_are_atomic")
        transaction(f"set_rule_enabled {rule} 0")
        command("world", "edit", world, edit, "--apply")
        if inspect()["rules"][0]["enabled"]:
            raise RuntimeError("Disabled rule remained enabled")
        command("project", "build", project / "presentation.viceproj")
        passed.append("disabled_rule_cooks_and_stages_through_project")
        transaction(f"remove_rule {rule}")
        command("world", "edit", world, edit, "--apply")
        if inspect()["rules"]:
            raise RuntimeError("Rule removal did not persist")
        passed.append("rule_removal_updates_persistent_source")
    if native:
        data = tour(vice, True)
        observed["tour"] = data
        cold = data["observed"]["driving"]
        resumed = data["observed"]["resumed_exit"]
        first_id = "a4000000-0000-4000-8000-000000000a02"
        second_id = "a4000000-0000-4000-8000-000000000a03"
        observed_type = "a4000000-0000-4000-8000-000000000a01"
        pulse_type = "a4000000-0000-4000-8000-000000000700"

        def rules(state):
            return {r["id"]: r for r in state["world_rules"]["page"]["entries"]}

        initial, later = rules(cold["vehicle"]), rules(resumed["vehicle"])
        for rid in (first_id, second_id):
            if initial[rid]["outcome"] != "applied" or later[rid]["outcome"] != "applied" or int(later[rid]["evaluations"]) <= int(initial[rid]["evaluations"]):
                raise RuntimeError("Native scheduled rule chain did not continue saved decisions")
        component = next(c for c in resumed["character"]["component_payloads"] if c["type"] == observed_type)
        if component["fields"][0]["value"] != "1":
            raise RuntimeError("Rule effect did not persist authoritative component state")
        events = resumed["vehicle"]["causal_history"]["events"]
        facts = [e for e in events if e.get("payload") and e["payload"][0]["type"] == observed_type]
        if not facts:
            raise RuntimeError("Rule output facts missing")
        prior = {e["id"]: e for e in events}
        if not any(p["id"] in prior and prior[p["id"]].get("payload") and prior[p["id"]]["payload"][0]["type"] == pulse_type for p in facts[-1]["parents"]):
            raise RuntimeError("Rule output lost scheduled pulse as causal parent")
        passed += ["native_scheduled_event_rule_component_chain", "native_checkpoint_preserves_rule_identity_and_decision_continuation", "native_rule_output_keeps_typed_causal_parent"]
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
