#!/usr/bin/env python3
"""Exercise revision-safe link authoring and optionally a real native failure."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def validate(vice, native):
    passed = []
    observed = {}
    def command(*args, expected=0):
        result = subprocess.run([str(vice), *map(str,args)], capture_output=True, text=True, timeout=120)
        if result.returncode != expected:
            raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)
    with tempfile.TemporaryDirectory(prefix="vice-links-workflow-") as directory:
        root = Path(directory)
        source = Path(__file__).resolve().parent.parent / "examples/breakable-links"
        project_root = root / "project"
        shutil.copytree(source, project_root, ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "links.viceproj"
        world = project_root / "assets/worlds/links.vworld"
        weak = "97000000-0000-4000-8000-000000000203"
        first = "97000000-0000-4000-8000-000000000201"
        second = "97000000-0000-4000-8000-000000000202"
        before = world.read_bytes()
        revision = command("world","inspect",world)["data"]["source_revision"]
        request = root / "links.viceedit"
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nset_distance_link {weak} {first} {second} 2 2 0 0 1200 intact 0 0\n")
        preview = command("world","edit",world,request,"--dry-run")
        if world.read_bytes() != before: raise RuntimeError("Dry-run changed source")
        applied = command("world","edit",world,request,"--apply")
        if not applied["data"]["published"] or applied["data"]["result_revision"] != preview["data"]["result_revision"]:
            raise RuntimeError("Link publication did not match preview")
        passed.append("revision_safe_distance_link_edit")
        digest = hashlib.sha256(world.read_bytes()).hexdigest()
        stale = command("world","edit",world,request,"--apply",expected=3)
        if stale["data"]["code"] != "revision_conflict": raise RuntimeError("Stale link edit accepted")
        passed.append("stale_link_edit_rejected")
        command("world","validate",world)
        built = command("project","build",project)["data"]
        if built["state"] != "built": raise RuntimeError("Link project did not build")
        passed.append("cook_and_stage_link_world_and_sdk_module")
        if native:
            exe = Path(built["game_executable"])
            sessions = root / "sessions"; session = sessions / "links-check"
            with (root / "native.log").open("w") as log:
                process = subprocess.Popen([str(exe),"--vice-dev-session",str(sessions),session.name],cwd=exe.parent,stdout=log,stderr=subprocess.STDOUT)
                try:
                    deadline = time.monotonic()+25
                    while True:
                        status = subprocess.run([str(vice),"runtime","inspect",str(session)],capture_output=True,text=True,timeout=5)
                        if status.returncode == 0:
                            data = json.loads(json.loads(status.stdout)["data"]["payload"])
                            if data["execution_state"] == "running" and data["frame"]["index"] > 0: break
                        if process.poll() is not None or time.monotonic()>deadline:
                            raise RuntimeError("Native link game failed: "+(root/"native.log").read_text(errors="replace"))
                        time.sleep(0.1)
                    deadline=time.monotonic()+12
                    while True:
                        def debug(entity): return json.loads(command("runtime","debug",session,entity)["data"]["payload"])
                        broken=debug(weak)["distance_link"]
                        strong=debug("97000000-0000-4000-8000-000000000206")["distance_link"]
                        payload=debug(second)["spatial"]["global_position"]
                        independent=debug("97000000-0000-4000-8000-000000000205")["spatial"]["global_position"]
                        if broken["broken"] and broken["last_break_impulse"]>broken["break_threshold"] and broken["break_step"]>0 and not strong["broken"] and abs(payload[1]-0.5)<0.06 and abs(independent[1]-3)<0.06:
                            observed={"broken_link":broken,"independent_link":strong,"released_position":payload,"held_position":independent}
                            passed.extend(["native_physical_stress_breaks_selected_link","native_independent_link_remains_intact","native_released_body_falls_to_floor"])
                            break
                        if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError(f"Local failure not observed: {broken}, {strong}, {payload}, {independent}")
                        time.sleep(0.1)
                finally:
                    if process.poll() is None:
                        process.terminate()
                        try: process.wait(timeout=5)
                        except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
        if hashlib.sha256(world.read_bytes()).hexdigest()!=digest: raise RuntimeError("Runtime rewrote authored source")
        passed.append("authored_source_unchanged")
    return {"schema":"vice.links.validation.v1","ok":True,"native_runtime":native,"passed":passed,"observed":observed}

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vice",required=True,type=Path)
    parser.add_argument("--run-runtime",action="store_true")
    parser.add_argument("--report",type=Path)
    args=parser.parse_args()
    report=validate(args.vice.resolve(),args.run_runtime)
    text=json.dumps(report,indent=2)+"\n"
    if args.report: args.report.write_text(text)
    print(text,end="")
