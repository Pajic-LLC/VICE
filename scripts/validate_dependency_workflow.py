#!/usr/bin/env python3
"""Accept revision-safe supply authoring and optional native physical failure."""
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
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=120)
        if result.returncode != expected: raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)
    def entity(value): return f"97000000-0000-4000-8000-{value:012x}"
    with tempfile.TemporaryDirectory(prefix="vice-supply-workflow-") as directory:
        root = Path(directory); project_root = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/supply-failure", project_root, ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "supply.viceproj"; world = project_root / "assets/worlds/supply.vworld"
        before = world.read_bytes()
        def inspect(): return command("world", "inspect", world)["data"]
        def entities(): return {e["id"]: e for e in inspect()["entities"]}
        revision = inspect()["source_revision"]
        request = root / "supply.viceedit"
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nset_dependency {entity(0x221)} none none disabled\n")
        preview = command("world", "edit", world, request, "--dry-run")
        if world.read_bytes() != before: raise RuntimeError("Dry-run changed source")
        if not {entity(0x221),entity(0x222),entity(0x223),entity(0x224)}.issubset(preview["data"]["affected_entities"]) or 1 not in preview["data"]["affected_regions"]: raise RuntimeError("Preview omitted downstream supply impact")
        applied = command("world", "edit", world, request, "--apply")
        if not applied["data"]["published"] or applied["data"]["result_revision"] != preview["data"]["result_revision"]: raise RuntimeError("Publication disagrees with preview")
        current = entities()
        if current[entity(0x223)]["dependency"]["available"] or current[entity(0x224)]["dependency"]["available"]: raise RuntimeError("Disabled source failed to propagate")
        passed.append("revision_safe_source_disable_propagates")
        if command("world", "edit", world, request, "--apply", expected=3)["data"]["code"] != "revision_conflict": raise RuntimeError("Stale request accepted")
        passed.append("stale_dependency_edit_rejected")
        revision = inspect()["source_revision"]
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nset_dependency {entity(0x221)} {entity(0x223)} none enabled\n")
        command("world", "edit", world, request, "--apply", expected=3)
        if inspect()["source_revision"] != revision: raise RuntimeError("Invalid cycle changed source")
        passed.append("cyclic_dependency_edit_rolls_back")
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nset_dependency {entity(0x221)} none none enabled\n")
        command("world", "edit", world, request, "--apply")
        current = entities()
        if not all(current[entity(v)]["dependency"]["available"] for v in (0x223,0x224)): raise RuntimeError("Re-enable failed")
        digest = hashlib.sha256(world.read_bytes()).hexdigest()
        command("world", "validate", world)
        built = command("project", "build", project)["data"]
        if built["state"] != "built": raise RuntimeError("Project did not build")
        passed.append("supply_graph_cooks_and_sdk_module_stages")
        if native:
            exe = Path(built["game_executable"]); sessions = root / "sessions"; session = sessions / "supply-check"
            with (root/"native.log").open("w") as log:
                process = subprocess.Popen([str(exe), "--vice-dev-session", str(sessions), session.name], cwd=exe.parent, stdout=log, stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic()+30
                    while True:
                        status = subprocess.run([str(vice), "runtime", "inspect", str(session)], capture_output=True, text=True, timeout=5)
                        if status.returncode == 0:
                            data = json.loads(json.loads(status.stdout)["data"]["payload"])
                            if data["execution_state"] == "running" and data["frame"]["index"] > 0: break
                        if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError("Native launch failed: "+(root/"native.log").read_text(errors="replace"))
                        time.sleep(0.1)
                    deadline = time.monotonic()+15
                    while True:
                        def debug(v): return json.loads(command("runtime", "debug", session, entity(v))["data"]["payload"])
                        failed, independent, gate = debug(0x223), debug(0x224), debug(0x203)
                        cause = failed["dependency"]
                        if not cause["available"] and independent["dependency"]["available"]:
                            if cause["reason"] != "upstream_unavailable" or cause["root_reason"] != "link_broken" or cause["root_cause"] != entity(0x203) or cause["path"] != [entity(0x223),entity(0x222),entity(0x203)]: raise RuntimeError("Invalid causal path")
                            if cause["break_impulse"] <= gate["distance_link"]["break_threshold"] or cause["break_step"] == 0 or not gate["distance_link"]["broken"]: raise RuntimeError("Missing physical provenance")
                            if not failed["light_enabled"] or failed["light_supplied"] or not independent["light_supplied"]: raise RuntimeError("Effective lighting disagrees with supply")
                            observed = {"failed_device":failed,"independent_device":independent,"physical_gate":gate}
                            passed += ["native_stress_causes_local_supply_failure","native_causal_path_reaches_physical_impulse","native_independent_light_remains_supplied"]
                            break
                        if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError(f"Failure not observed: {failed}, {independent}")
                        time.sleep(0.1)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try: process.wait(timeout=5)
                        except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
        if hashlib.sha256(world.read_bytes()).hexdigest() != digest: raise RuntimeError("Runtime changed authored source")
        passed.append("authored_supply_source_unchanged")
    return {"schema":"vice.dependency.validation.v1","ok":True,"native_runtime":native,"passed":passed,"observed":observed}

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vice",required=True,type=Path); parser.add_argument("--run-runtime",action="store_true"); parser.add_argument("--report",type=Path)
    args=parser.parse_args(); report=validate(args.vice.resolve(),args.run_runtime); text=json.dumps(report,indent=2)+"\n"
    if args.report: args.report.write_text(text)
    print(text,end="")
