#!/usr/bin/env python3
"""Accept shared material authoring, native physical behavior and checkpoint resume."""
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
        result=subprocess.run([str(vice),*map(str,args)],capture_output=True,text=True,timeout=180)
        if result.returncode!=expected: raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)
    def entity(value): return f"9e000000-0000-4000-8000-{value:012x}"
    with tempfile.TemporaryDirectory(prefix="vice-material-workflow-") as directory:
        root=Path(directory); project_root=root/"project"
        shutil.copytree(Path(__file__).resolve().parent.parent/"examples/material-playground",project_root,ignore=shutil.ignore_patterns(".vice"))
        project=project_root/"materials.viceproj";world=project_root/"assets/worlds/materials.vworld"
        def inspect(): return command("world","inspect",world)["data"]
        before=world.read_bytes();initial=inspect();request=root/"material.viceedit"
        request.write_text(f"vice-world-edit 1\nrevision {initial['source_revision']}\nset_material {entity(0x102)} \"Slippery\" 300 0 0 none\n")
        preview=command("world","edit",world,request,"--dry-run")
        if world.read_bytes()!=before or not {entity(2),entity(7)}.issubset(preview["data"]["affected_entities"]) or 1 not in preview["data"]["affected_regions"]: raise RuntimeError("Material dry-run missing users/region or changing source")
        applied=command("world","edit",world,request,"--apply")
        if applied["data"]["result_revision"]!=preview["data"]["result_revision"]: raise RuntimeError("Preview and publication disagree")
        passed.append("revision_safe_catalog_edit_reports_all_material_users")
        command("world","edit",world,request,"--apply",expected=3);passed.append("stale_material_edit_rejected")
        revision=inspect()["source_revision"]
        request.write_text(f"vice-world-edit 1\nrevision {revision}\nremove_material {entity(0x102)}\n")
        command("world","edit",world,request,"--apply",expected=3)
        if inspect()["source_revision"]!=revision: raise RuntimeError("Invalid removal changed source")
        passed.append("live_material_removal_rejected_atomically")
        current=inspect();entities={e["id"]:e for e in current["entities"]}
        if entities[entity(2)]["material"]["mass"]!=300 or entities[entity(7)]["material"]["mass"]!=300: raise RuntimeError("Catalog density did not update both users")
        digest=hashlib.sha256(world.read_bytes()).hexdigest()
        command("world","validate",world);built=command("project","build",project)["data"]
        passed.append("shared_material_catalog_and_sdk_module_cook_and_stage")
        if native:
            exe=Path(built["game_executable"]);sessions=root/"sessions"
            def debug(session,n): return json.loads(command("runtime","debug",session,entity(n))["data"]["payload"])
            def stop(process,log):
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill();process.wait(timeout=5)
                log.close()
            def launch(executable,name):
                session=sessions/name;log=(root/f"{name}.log").open("w")
                process=subprocess.Popen([str(executable),"--vice-dev-session",str(sessions),name],cwd=executable.parent,stdout=log,stderr=subprocess.STDOUT)
                deadline=time.monotonic()+30
                while True:
                    status=subprocess.run([str(vice),"runtime","inspect",str(session)],capture_output=True,text=True,timeout=5)
                    if status.returncode==0:
                        state=json.loads(json.loads(status.stdout)["data"]["payload"])
                        if state["execution_state"]=="running" and state["frame"]["index"]>0:return process,log,session
                    if process.poll() is not None or time.monotonic()>deadline:
                        stop(process,log);raise RuntimeError("Native material launch failed: "+(root/f"{name}.log").read_text(errors="replace"))
                    time.sleep(.05)
            process,log,session=launch(exe,"cold")
            try:
                deadline=time.monotonic()+20;rebound=None;sliding=None;failure=None
                while not (rebound and sliding and failure):
                    elastic=debug(session,4)
                    if elastic["rigid_box"]["linear_velocity"][1]>1 and elastic["spatial"]["global_position"][1]>50.8:rebound=elastic
                    ice,grip=debug(session,2),debug(session,3)
                    if abs(grip["rigid_box"]["linear_velocity"][0])<.2 and ice["rigid_box"]["linear_velocity"][0]>3:
                        sliding={"slippery":ice,"grip":grip}
                    lamp=debug(session,0x12)
                    roots=[e for e in lamp["causal_history"]["events"] if e["kind"]=="constraint_broken"]
                    if roots and not lamp["dependency"]["available"]:
                        root_event=roots[0]
                        if root_event["first_material"]!=entity(0x103) or root_event["second_material"]!=entity(0x102): raise RuntimeError("Failure material provenance disagrees with participants")
                        failure=lamp
                    if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError(f"Native material behavior incomplete: rebound={bool(rebound)} slide={bool(sliding)} failure={bool(failure)}")
                for n,mass in ((2,300),(3,2000)):
                    sample=debug(session,n)
                    if abs(sample["native_mass"]-mass)>.1 or sample["material"]["mass"]!=mass:raise RuntimeError("Native mass differs from shared density")
                observed["sliding"]=sliding;observed["rebound"]=rebound;observed["failure"]=failure
                passed += ["native_density_sets_actual_solver_mass","native_shared_friction_changes_sliding","native_shared_restitution_causes_rebound","native_break_records_both_material_identities"]
                saved=root/"saved.vworldc";command("runtime","checkpoint",session,saved)
                passed.append("native_material_world_checkpoint_exported")
            finally:stop(process,log)
            stage=root/"resumed-stage";shutil.copytree(exe.parent,stage)
            cooked=list((stage/"content").rglob("materials.vworldc"))
            if len(cooked)!=1:raise RuntimeError("Missing staged material startup World")
            shutil.copyfile(saved,cooked[0]);process,log,session=launch(stage/exe.name,"resumed")
            try:
                ice=debug(session,2);lamp=debug(session,0x12)
                roots=[e for e in lamp["causal_history"]["events"] if e["kind"]=="constraint_broken"]
                if abs(ice["native_mass"]-300)>.1 or ice["material"]["id"]!=entity(0x102) or not roots or roots[0]!=root_event or lamp["dependency"]["available"]:
                    raise RuntimeError("Material identity, native mass or historical failure changed on resume")
                observed["after_resume"]={"slippery":ice,"lamp":lamp}
                passed.append("native_restart_rebuilds_material_mass_and_preserves_historical_material_ids")
            finally:stop(process,log)
        if hashlib.sha256(world.read_bytes()).hexdigest()!=digest:raise RuntimeError("Runtime changed authored source")
        passed.append("authored_material_source_unchanged")
    return {"schema":"vice.material.validation.v1","ok":True,"native_runtime":native,"passed":passed,"observed":observed}

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument("--vice",required=True,type=Path);parser.add_argument("--run-runtime",action="store_true");parser.add_argument("--report",type=Path)
    args=parser.parse_args();report=validate(args.vice.resolve(),args.run_runtime);text=json.dumps(report,indent=2)+"\n"
    if args.report:args.report.write_text(text)
    print(text,end="")
