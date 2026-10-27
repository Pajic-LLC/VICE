#!/usr/bin/env python3
"""Build the causal-history example and optionally prove native checkpoint/resume."""
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
        result = subprocess.run([str(vice), *map(str, args)], capture_output=True, text=True, timeout=180)
        if result.returncode != expected: raise RuntimeError(f"{args}: {result.stdout}\n{result.stderr}")
        return json.loads(result.stdout)
    def entity(value): return f"97000000-0000-4000-8000-{value:012x}"
    with tempfile.TemporaryDirectory(prefix="vice-causal-workflow-") as directory:
        root = Path(directory); project_root = root / "project"
        shutil.copytree(Path(__file__).resolve().parent.parent / "examples/causal-history", project_root, ignore=shutil.ignore_patterns(".vice"))
        project = project_root / "history.viceproj"; world = project_root / "assets/worlds/history.vworld"
        digest = hashlib.sha256(world.read_bytes()).hexdigest()
        initial = command("world", "inspect", world)["data"]
        if initial["causal_history"]["events"]: raise RuntimeError("Cold authored source invents history")
        command("world", "validate", world)
        built = command("project", "build", project)["data"]
        passed.append("causal_sdk_example_cooks_and_stages")
        if native:
            exe = Path(built["game_executable"]); sessions = root / "sessions"
            def debug(session, value):
                return json.loads(command("runtime", "debug", session, entity(value))["data"]["payload"])
            def launch(executable, name):
                session = sessions/name
                log = (root/f"{name}.log").open("w")
                process = subprocess.Popen([str(executable), "--vice-dev-session", str(sessions), name], cwd=executable.parent, stdout=log, stderr=subprocess.STDOUT)
                deadline = time.monotonic()+30
                while True:
                    result = subprocess.run([str(vice), "runtime", "inspect", str(session)], capture_output=True, text=True, timeout=5)
                    if result.returncode == 0:
                        status = json.loads(json.loads(result.stdout)["data"]["payload"])
                        if status["execution_state"] == "running" and status["frame"]["index"] > 0: return process, log, session
                    if process.poll() is not None or time.monotonic()>deadline:
                        stop(process, log)
                        raise RuntimeError("Launch failed: "+(root/f"{name}.log").read_text(errors="replace"))
                    time.sleep(.1)
            def stop(process, log):
                if process.poll() is None:
                    process.terminate()
                    try: process.wait(timeout=5)
                    except subprocess.TimeoutExpired: process.kill(); process.wait(timeout=5)
                log.close()
            process, log, session = launch(exe, "cold")
            try:
                deadline=time.monotonic()+25
                outage=False
                while True:
                    failed, independent=debug(session,0x223),debug(session,0x224)
                    events=failed["causal_history"]["events"]
                    roots=[e for e in events if e["kind"]=="constraint_broken"]
                    if roots and not failed["dependency"]["available"]:
                        root_event=roots[0]
                        by_id={e["id"]:e for e in events}
                        lamp=[e for e in events if e["kind"]=="dependency_state_changed" and e["subject"]==entity(0x223) and e["root_reason"]=="link_broken"][0]
                        harness=by_id[lamp["parent_id"]]
                        if harness["parent_id"]!=root_event["id"] or harness["subject"]!=entity(0x222) or root_event["quantity"]<=1000 or root_event["solver_step"]==0:
                            raise RuntimeError("Physical cause chain is invalid")
                        if not independent["dependency"]["available"]: outage=True
                        if outage and independent["dependency"]["available"]:
                            observed["before_checkpoint"]={"failed":failed,"independent":independent}
                            break
                    if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError("Physical break / source outage / restoration not observed")
                    time.sleep(.1)
                passed += ["native_physics_harness_lamp_parent_ids", "native_sdk_source_outage_and_restoration", "original_failure_retained_after_later_changes"]
                checkpoint=root/"saved.vworldc"
                command("runtime","checkpoint",session,checkpoint)
                saved_digest=hashlib.sha256(checkpoint.read_bytes()).hexdigest()
                command("runtime","checkpoint",session,checkpoint,expected=3)
                if hashlib.sha256(checkpoint.read_bytes()).hexdigest()!=saved_digest: raise RuntimeError("Existing save overwritten")
                passed.append("native_atomic_world_checkpoint_no_overwrite")
            finally: stop(process,log)
            stage=root/"resumed-stage"
            shutil.copytree(exe.parent, stage)
            cooked=list((stage/"content").rglob("history.vworldc"))
            if len(cooked)!=1: raise RuntimeError(f"Expected one staged startup World: {cooked}")
            shutil.copyfile(checkpoint,cooked[0])
            process,log,session=launch(stage/exe.name,"resumed")
            try:
                failed,independent=debug(session,0x223),debug(session,0x224)
                history=failed["causal_history"]; previous=observed["before_checkpoint"]["failed"]["causal_history"]
                retained={e["id"]:e for e in history["events"]}
                if history["history_id"]!=previous["history_id"] or history["capture_id"]==previous["capture_id"] or history["next_sequence"]<=previous["next_sequence"]:
                    raise RuntimeError("Resume lineage/segment/sequence is invalid")
                if retained.get(root_event["id"])!=root_event: raise RuntimeError("Saved physical event changed on resume")
                if failed["dependency"]["available"] or not independent["dependency"]["available"]: raise RuntimeError("Saved availability changed")
                observed["after_resume"]={"failed":failed,"independent":independent}
                passed += ["native_restart_preserves_original_event_ids_and_evidence", "native_restart_starts_new_capture_segment", "native_restart_preserves_broken_and_restored_supply_state"]
            finally: stop(process,log)
        if hashlib.sha256(world.read_bytes()).hexdigest()!=digest: raise RuntimeError("Authored World changed")
        passed.append("authored_source_unchanged")
    return {"schema":"vice.causal.validation.v1","ok":True,"native_runtime":native,"passed":passed,"observed":observed}

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--vice",required=True,type=Path);parser.add_argument("--run-runtime",action="store_true");parser.add_argument("--report",type=Path)
    args=parser.parse_args();report=validate(args.vice.resolve(),args.run_runtime);text=json.dumps(report,indent=2)+"\n"
    if args.report: args.report.write_text(text)
    print(text,end="")
