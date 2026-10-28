#!/usr/bin/env python3
"""Slice A: revision-safe authoring and component persistence in the physical demo."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
from validate_presentation_workflow import validate as presentation_workflow


def validate(vice, native):
    passed, observed = [], {}

    def command(*args, expected=0):
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=180)
        if result.returncode != expected:
            raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)

    def entity(n):
        return f"a2000000-0000-4000-8000-{n:012x}"

    with tempfile.TemporaryDirectory(prefix="vice-components-") as directory:
        root = Path(directory)
        project = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground",
                        project, ignore=shutil.ignore_patterns(".vice"))
        world = project / "assets/worlds/presentation.vworld"
        edit = root / "state.viceedit"

        def inspect():
            return command("world", "inspect", world)["data"]

        initial = inspect()
        original = world.read_bytes()
        first, second = "b7000000-0000-4000-8000-000000000100", "b7000000-0000-4000-8000-000000000101"
        # The names and interpretation belong entirely to game content.
        edit.write_text(f'''vice-world-edit 1
revision {initial['source_revision']}
register_component {first} 1 "VehicleCondition" 2 1 real "condition" 2 integer "service_count"
register_component {second} 1 "AmmoState" 1 1 integer "amount"
set_component {entity(2)} {first} 1 2 2 integer 0 1 real 0.75
set_component {entity(3)} {second} 1 1 1 integer 30
''')
        preview = command("world", "edit", world, edit, "--dry-run")["data"]
        if world.read_bytes() != original or entity(2) not in preview["affected_entities"]:
            raise RuntimeError("Component dry run mutated source or omitted impact")
        applied = command("world", "edit", world, edit, "--apply")["data"]
        if applied["result_revision"] != preview["result_revision"]:
            raise RuntimeError("Component preview and publication differ")
        state = inspect()
        car = next(e for e in state["entities"] if e["id"] == entity(2))
        if not any(p["type"] == first and p["fields"][0]["value"] == .75 for p in car["component_payloads"]):
            raise RuntimeError("Structured component value missing from normal inspection")
        passed.append("generic_schemas_for_two_genres_share_revision_safe_world_authoring")
        command("world", "edit", world, edit, "--apply", expected=3)
        passed.append("stale_component_transaction_rejected")
        before = world.read_bytes()
        edit.write_text(f'''vice-world-edit 1
revision {state['source_revision']}
set_component {entity(2)} {first} 1 2 1 real 0.25 2 integer 5
set_component {entity(3)} {second} 9 1 1 integer 40
''')
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Invalid second payload partially published first mutation")
        passed.append("schema_mismatch_rolls_back_entire_authoring_transaction")
        edit.write_text(f'''vice-world-edit 1
revision {state['source_revision']}
remove_component {entity(3)} {second}
''')
        command("world", "edit", world, edit, "--apply")
        if any(p["type"] == second for e in inspect()["entities"] for p in e["component_payloads"]):
            raise RuntimeError("Remove component left payload behind")
        passed.append("component_removal_visible_in_world_inspection")
        source_hash = hashlib.sha256(world.read_bytes()).hexdigest()
        command("world", "validate", world)
        built = command("project", "build", project / "presentation.viceproj")["data"]
        if not Path(built["cooked_startup_world"]).is_file() or source_hash != hashlib.sha256(world.read_bytes()).hexdigest():
            raise RuntimeError("Project cook missing state or changed authored source")
        passed.append("component_schemas_payloads_and_sdk_module_cook_and_stage")
    if native:
        tour = presentation_workflow(vice, True)
        observed["physical_animation_audio_tour"] = tour
        cold = tour["observed"]["driving"]["vehicle"]
        resumed = tour["observed"]["resumed_exit"]["vehicle"]
        type_id = "a4000000-0000-4000-8000-000000000400"
        def count(data):
            payload = next(p for p in data["component_payloads"] if p["type"] == type_id)
            return int(next(f["value"] for f in payload["fields"] if f["id"] == 1))
        if count(cold) != 1 or count(resumed) != 2:
            raise RuntimeError("Native checkpoint/restart reset or lost game-defined state")
        if not any(s["name"] == "SessionState" for s in resumed["component_schemas"]):
            raise RuntimeError("Runtime debugger omitted schema metadata")
        passed += ["native_sdk_payload_coexists_with_physics_animation_and_audio",
                   "native_checkpoint_and_new_process_resume_game_defined_state",
                   "native_debug_exposes_component_values_and_schema"]
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
