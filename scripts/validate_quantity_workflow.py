#!/usr/bin/env python3
"""Generic quantities: revision-safe authoring and native transfer/checkpoint continuity."""
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
            raise RuntimeError(f"{args} (exit {result.returncode}): {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)

    with tempfile.TemporaryDirectory(prefix="vice-quantities-") as tmp:
        root = Path(tmp)
        project = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground", project,
                        ignore=shutil.ignore_patterns(".vice"))
        world = project / "assets/worlds/presentation.vworld"
        edit = root / "quantities.viceedit"
        resource = "c9000000-0000-4000-8000-000000000100"
        first = "a2000000-0000-4000-8000-000000000002"
        second = "a2000000-0000-4000-8000-000000000003"

        def inspect():
            return command("world", "inspect", world)["data"]

        def transaction(operations):
            edit.write_text(f"vice-world-edit 1\nrevision {inspect()['source_revision']}\n{operations}\n")

        transaction(f'register_quantity_type {resource} 1 100 "AuthoredUnits"\nset_quantity_account {first} {resource} 100 1000\nset_quantity_account {second} {resource} 0 1000')
        original = world.read_bytes()
        preview = command("world", "edit", world, edit, "--dry-run")["data"]
        if world.read_bytes() != original or not {first, second}.issubset(preview["affected_entities"]) or 1 not in preview["affected_regions"]:
            raise RuntimeError("Quantity preview changed source or omitted endpoint/region impact")
        published = command("world", "edit", world, edit, "--apply")["data"]
        state = inspect()
        if preview["result_revision"] != published["result_revision"] or len(state["quantity_accounts"]) != 2 or state["quantity_types"][0]["quanta_per_unit"] != 100:
            raise RuntimeError("Quantity publication/inspection differs from preview")
        passed.append("revision_safe_quantity_types_and_seeded_holders")
        command("world", "edit", world, edit, "--apply", expected=3)
        before = world.read_bytes()
        transaction(f"set_quantity_account {first} {resource} 50 1000\nset_quantity_account a2000000-0000-4000-8000-000000000099 {resource} 1 100")
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Invalid second holder partially published first edit")
        transaction(f'set_quantity_account {first} {resource} 50 1000\nregister_quantity_type {resource} 2 100 "AuthoredUnits"')
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Schema conflict partially changed quantities")
        passed.append("stale_missing_holder_and_schema_failures_are_atomic")
        transaction(f"remove_quantity_account {first} {resource}")
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Removal discarded nonempty holder")
        passed.append("nonempty_holder_cannot_be_silently_removed")
        command("project", "build", project / "presentation.viceproj")
        passed.append("quantity_state_cooks_and_stages_with_game_module")
        transaction(f"set_quantity_account {first} {resource} 0 1000\nremove_quantity_account {first} {resource}")
        command("world", "edit", world, edit, "--apply")
        if len(inspect()["quantity_accounts"]) != 1:
            raise RuntimeError("Empty account removal did not persist")
        passed.append("explicit_authoring_reset_and_empty_removal")
    if native:
        data = tour(vice, True)
        observed["tour"] = data
        cold = data["observed"]["driving"]
        resumed = data["observed"]["resumed_exit"]
        resource = "a4000000-0000-4000-8000-000000000900"
        receipt_type = "c7000000-0000-4000-8000-000000000001"
        def account(state):
            return next(a for a in state["quantity_accounts"] if a["type"] == resource)
        if (account(cold["vehicle"])["amount"], account(cold["character"])["amount"]) != ("75", "25"):
            raise RuntimeError("Cold game failed to produce/transfer initial generic units")
        if (account(resumed["vehicle"])["amount"], account(resumed["character"])["amount"]) != ("50", "50"):
            raise RuntimeError("Fresh Runtime reseeded or lost persistent holdings")
        def receipts(state):
            return [e for e in state["causal_history"]["events"] if e["kind"] == "world_event" and e["payload"][0]["type"] == receipt_type]
        old, new = receipts(cold["vehicle"])[-1], receipts(resumed["vehicle"])[-1]
        values = {f["id"]: f["value"] for f in new["payload"][0]["fields"]}
        if not any(p["id"] == old["id"] for p in new["parents"]) or [values[k] for k in [3,4,5,6,7,8]] != ["2","25","75","50","25","50"]:
            raise RuntimeError("Transfer receipt lost exact before/after quantities or causal continuation")
        passed += ["native_production_and_atomic_transfer_persist_across_restart", "native_receipts_preserve_exact_quantities_and_prior_causes"]
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
