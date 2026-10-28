#!/usr/bin/env python3
"""Slice B: stateful edge authoring and persistence in the physical demo."""
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

    with tempfile.TemporaryDirectory(prefix="vice-stateful-relations-") as directory:
        root = Path(directory)
        project = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/presentation-playground",
                        project, ignore=shutil.ignore_patterns(".vice"))
        world = project / "assets/worlds/presentation.vworld"
        edit = root / "relations.viceedit"
        schema = "b8000000-0000-4000-8000-000000000100"
        first, second = "b8000000-0000-4000-8000-000000000050", "b8000000-0000-4000-8000-000000000051"

        def inspect():
            return command("world", "inspect", world)["data"]

        def transaction(operations):
            edit.write_text(f"vice-world-edit 1\nrevision {inspect()['source_revision']}\n{operations}\n")

        original = world.read_bytes()
        transaction(f'''register_component {schema} 1 "LinkState" 2 1 real "weight" 2 text "label"
add_relation {entity(3)} {first} {entity(2)}
set_relation_component {entity(3)} {first} {entity(2)} {schema} 1 2 1 real 0.8 2 text "civilian example"
add_relation {entity(2)} {second} {entity(4)}
set_relation_component {entity(2)} {second} {entity(4)} {schema} 1 2 1 real 2 2 text "tactical example"''')
        preview = command("world", "edit", world, edit, "--dry-run")["data"]
        if world.read_bytes() != original or not {entity(2), entity(3), entity(4)}.issubset(preview["affected_entities"]) or 1 not in preview["affected_regions"]:
            raise RuntimeError("Relation dry-run mutated source or omitted endpoint/region impact")
        result = command("world", "edit", world, edit, "--apply")["data"]
        if result["result_revision"] != preview["result_revision"]:
            raise RuntimeError("Relation preview/publication disagree")
        edges = inspect()["relations"]
        if len([r for r in edges if r["kind"] in (first, second) and r["component_payloads"]]) != 2:
            raise RuntimeError("Normal World inspection omitted edge data")
        passed.append("shared_typed_state_on_two_game_defined_relation_kinds")
        command("world", "edit", world, edit, "--apply", expected=3)
        passed.append("stale_relation_edit_rejected")
        before = world.read_bytes()
        transaction(f'''set_relation_component {entity(3)} {first} {entity(2)} {schema} 1 2 1 real 0.5 2 text "changed"
set_relation_component {entity(2)} {second} {entity(4)} {schema} 9 2 1 real 3 2 text "invalid version"''')
        command("world", "edit", world, edit, "--apply", expected=3)
        if world.read_bytes() != before:
            raise RuntimeError("Invalid second relation mutation partially published the first")
        passed.append("relation_state_transaction_failure_is_atomic")
        transaction(f"remove_relation_component {entity(3)} {first} {entity(2)} {schema}")
        command("world", "edit", world, edit, "--apply")
        if next(r for r in inspect()["relations"] if r["kind"] == first)["component_payloads"]:
            raise RuntimeError("Remove payload left state on edge")
        passed.append("removing_payload_preserves_relation")
        transaction(f"remove_relation {entity(2)} {second} {entity(4)}\nadd_relation {entity(2)} {second} {entity(4)}")
        command("world", "edit", world, edit, "--apply")
        if next(r for r in inspect()["relations"] if r["kind"] == second)["component_payloads"]:
            raise RuntimeError("Removing/recreating an edge resurrected state")
        passed.append("edge_recreation_has_no_ghost_state")
        transaction(f"set_relation_component {entity(3)} {first} {entity(2)} {schema} 1 2 1 real 0.9 2 text \"persisted\"")
        command("world", "edit", world, edit, "--apply")
        digest = hashlib.sha256(world.read_bytes()).hexdigest()
        command("world", "validate", world)
        built = command("project", "build", project / "presentation.viceproj")["data"]
        if not Path(built["cooked_startup_world"]).is_file() or digest != hashlib.sha256(world.read_bytes()).hexdigest():
            raise RuntimeError("Relation cook missing output or changed source")
        passed.append("stateful_relations_and_sdk_module_cook_and_stage")
    if native:
        tour = presentation_workflow(vice, True)
        observed["physical_animation_audio_tour"] = tour
        cold = tour["observed"]["driving"]["vehicle"]
        resumed = tour["observed"]["resumed_exit"]["vehicle"]
        kind, type_id = "a4000000-0000-4000-8000-000000000500", "a4000000-0000-4000-8000-000000000501"
        def counter(data):
            edge = next(r for r in data["relation_states"] if r["subject"] == entity(3) and r["kind"] == kind and r["object"] == entity(2))
            state = next(p for p in edge["component_payloads"] if p["type"] == type_id)
            return int(next(f["value"] for f in state["fields"] if f["id"] == 1))
        if counter(cold) != 1 or counter(resumed) != 2:
            raise RuntimeError("Native restart lost relation key/state or reset the counter")
        if not any(s["name"] == "ConnectionState" for s in resumed["component_schemas"]):
            raise RuntimeError("Runtime debugger omitted relation schema")
        passed += ["native_relation_payload_coexists_with_physics_animation_audio",
                   "native_mounted_checkpoint_restart_preserves_relation_state",
                   "native_runtime_debug_exposes_relation_keys_values_and_schema"]
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
