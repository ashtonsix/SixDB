#!/usr/bin/env python3
"""Opt-in real rescue and repeated recovery; no network or cloud allocation."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest

SPEC = Path(__file__).resolve().parent
TOOLS = SPEC.parents[1] / "workbench/tools"
MODEL = r"""---- MODULE Grid ----
EXTENDS Naturals
CONSTANT N
VARIABLES x, y
Init == x = 0 /\ y = 0
Step == \/ /\ x < N /\ x' = x + 1 /\ UNCHANGED y
        \/ /\ y < N /\ y' = y + 1 /\ UNCHANGED x
Next == Step \/ /\ x = N /\ y = N /\ UNCHANGED <<x,y>>
Spec == Init /\ [][Next]_<<x,y>> /\ WF_<<x,y>>(Step)
TypeOK == x \in 0..N /\ y \in 0..N
Finish == <> (x = N /\ y = N)
====
"""


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n")


class LocalStore:
    def __init__(self, root):
        self.root = root
        root.mkdir()

    def get(self, uri):
        return (self.root / hashlib.sha256(uri.encode()).hexdigest()).read_bytes()

    def put(self, uri, data, *, immutable=True):
        path = self.root / hashlib.sha256(uri.encode()).hexdigest()
        if immutable and path.exists() and path.read_bytes() != data:
            raise ValueError("fixture attempted to overwrite an immutable chunk")
        path.write_bytes(data)


def stop(process):
    if process.poll() is None:
        children = Path(f"/proc/{process.pid}/task/{process.pid}/children")
        for child in children.read_text().split() if children.exists() else []:
            try:
                if os.getpgid(int(child)) == int(child):
                    os.killpg(int(child), signal.SIGCONT)
                    os.killpg(int(child), signal.SIGKILL)
            except ProcessLookupError:
                pass
        for sig in (signal.SIGCONT, signal.SIGKILL):
            try:
                os.killpg(process.pid, sig)
            except ProcessLookupError:
                pass
        process.wait(timeout=10)


@unittest.skipUnless(sys.platform == "linux" and os.environ.get("TLC_TEST_JAR"),
                     "Linux and TLC_TEST_JAR required; no automatic download")
class RealRecovery(unittest.TestCase):
    def wait_for(self, predicate, process, output, seconds=20):
        deadline = time.monotonic() + seconds
        while not predicate():
            self.assertIsNone(process.poll(), output.read_text(errors="replace")[-4000:])
            self.assertLess(time.monotonic(), deadline, "timed out waiting for TLC")
            time.sleep(0.02)

    def launch(self, args, root, output, env=None):
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("wb") as log:
            child = subprocess.Popen(args, cwd=root, env=env, stdout=log,
                                     stderr=subprocess.STDOUT, start_new_session=True)
        self.addCleanup(stop, child)
        return child

    def test_checker_rescue_then_two_generations_and_worker_group_lifetime(self):
        jar = Path(os.environ["TLC_TEST_JAR"]).resolve()
        pin = json.loads((TOOLS / "tlc/source.json").read_text())
        self.assertEqual(digest(jar), pin["sha256"])
        with tempfile.TemporaryDirectory(prefix="sixdb-tlc-recovery-") as temp:
            root = Path(temp)
            model = root / "model"
            model.mkdir()
            (model / "Grid.tla").write_text(MODEL)
            n = 1600
            (model / "Grid.cfg").write_text(
                f"CONSTANT N = {n}\nSPECIFICATION Spec\nINVARIANT TypeOK\nPROPERTY Finish\n")
            original_output = root / "original-console.log"
            original = self.launch([
                sys.executable, str(SPEC / "check.py"), "--module", str(model / "Grid.tla"),
                "--config", str(model / "Grid.cfg"), "--output", str(root / "original"),
                "--jar", str(jar), "--heap", "256m", "--workers", "1", "--timeout", "180"
            ], root, original_output)
            self.wait_for(lambda: bool(list((root / "original").glob("*/result.json"))),
                          original, original_output)
            original_receipt = next((root / "original").glob("*/result.json"))
            run = original_receipt.parent
            log = run / "tlc.log"
            self.wait_for(lambda: log.exists() and b"Finished computing initial states" in log.read_bytes(),
                          original, original_output)
            children = Path(f"/proc/{original.pid}/task/{original.pid}/children").read_text().split()
            self.assertEqual(len(children), 1)
            jvm = int(children[0])
            argv = [a.decode() for a in Path(f"/proc/{jvm}/cmdline").read_bytes().split(b"\0") if a]
            seed = re.search(r"with fp (\d+) and seed (-?\d+)", log.read_text())
            self.assertIsNotNone(seed)
            self.assertNotEqual(int(seed[2]), 0, "exercise the original runner's implicit seed")
            control = root / "control"
            control.mkdir()
            subprocess.run(["javac", "-d", str(control), str(TOOLS / "tlc/Checkpoint.java")],
                           check=True, timeout=20, stdout=subprocess.DEVNULL)
            requested = time.time()
            offset = log.stat().st_size
            subprocess.run(["java", "--add-modules", "jdk.attach", "-cp", str(control),
                            "Checkpoint", str(jvm)], check=True, timeout=20, stdout=subprocess.DEVNULL)
            end_message = b"@!@!@ENDMSG 2196 @!@!@"
            self.wait_for(lambda: end_message in log.read_bytes()[offset:],
                          original, original_output, seconds=75)
            acknowledged = time.time()
            ack_offset = log.read_bytes().rfind(end_message) + len(end_message)
            rescue = root / "rescue"
            rescue.mkdir()
            paused = time.time()
            os.kill(jvm, signal.SIGSTOP)
            try:
                self.wait_for(lambda: all("\nState:\tT" in p.read_text()
                              for p in Path(f"/proc/{jvm}/task").glob("*/status")),
                              original, original_output)
                stopped = time.time()
                frozen_log = log.read_bytes()
                self.assertEqual(frozen_log.rfind(end_message) + len(end_message), ack_offset)
                self.assertLess(frozen_log.rfind(b"@!@!@STARTMSG 2195:"), frozen_log.rfind(end_message))
                checkpoint_files = {p.relative_to(run / "metadir").as_posix():
                    {"bytes": p.stat().st_size, "mtime_ns": p.stat().st_mtime_ns, "sha256": digest(p)}
                    for p in (run / "metadir").rglob("*.chkpt")}
                self.assertTrue(checkpoint_files)
                shutil.copytree(run / "metadir", rescue / "states")
                shutil.copytree(run, rescue / "original", ignore=shutil.ignore_patterns("metadir"))
                captured_receipt = (rescue / "original/result.json").read_bytes()
                shutil.copyfile(jar, rescue / "tla2tools.jar")
                # Reuse the tested pinned-format liveness normalization on the copy.
                code = "import sys;sys.path.insert(0,sys.argv[1]);import tlc;from pathlib import Path;tlc.trim_liveness_tails(Path(sys.argv[2]))"
                subprocess.run([sys.executable, "-c", code, str(TOOLS / "tlc"), str(rescue / "states")],
                               check=True, timeout=15)
                copied = time.time()
            finally:
                # The frozen checker starts a different JVM process group.
                stop(original)
                try:
                    os.killpg(jvm, signal.SIGCONT)
                    os.killpg(jvm, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            receipt_before = original_receipt.read_bytes()
            generation, = [p.name for p in (rescue / "states").iterdir() if p.is_dir()]
            (rescue / "metadir.txt").write_text(generation + "\n")
            shutil.copyfile(__file__, rescue / "capture-helper.py")
            provenance = {"format": 1, "kind": "manual-tlc-checkpoint-rescue", "job": "local-real-check",
                "original_run": str(run), "original_receipt_status": "running", "argv": argv,
                "cwd": str(run / "sources"), "pid": jvm,
                "java_version": json.loads(receipt_before)["java"]["version"],
                "jar_sha256": pin["sha256"], "fingerprint": int(seed[1]), "seed": int(seed[2]),
                "checkpoint_request_at": requested, "log_request_offset": offset,
                "checkpoint_ack_at": acknowledged, "log_ack_end_offset": ack_offset,
                "pause_requested_at": paused, "all_threads_stopped_at": stopped,
                "checkpoint_files": checkpoint_files, "copy_complete_at": copied,
                "generation": generation, "capture_helper_sha256": digest(__file__),
                "helper_sha256": digest(TOOLS / "tlc/tlc.py"), "recovery_verified": False,
                "job_source": {"digest": digest(model / "Grid.tla")}}
            save(rescue / "files.json", {p.relative_to(rescue).as_posix():
                {"bytes": p.stat().st_size, "sha256": digest(p)}
                for p in sorted(rescue.rglob("*")) if p.is_file()})
            save(rescue / "rescue.json", provenance)
            archive = root / "rescue.tar.gz"
            members = [p for p in sorted(rescue.rglob("*")) if p.is_file()]
            with tarfile.open(archive, "w:gz", compresslevel=1) as bundle:
                for path in members:
                    bundle.add(path, arcname=path.relative_to(rescue))
            reference = root / "reference.json"
            save(reference, {"format": 1, "kind": "files", "bucket": "unused-local-test",
                "region": "us-east-1", "key": "unused-local-test.tar.gz", "sha256": digest(archive),
                "bytes": archive.stat().st_size, "files": len(members)})

            def resume(attempt, checkpoint=None, job=None, manifest=None):
                results = root / attempt / "results"
                state = root / attempt / "state"
                env = os.environ | {"SIXDB_RESULTS": str(results), "SIXDB_CHECKPOINT_STATE": str(state)}
                env.pop("SIXDB_RESUME", None)
                if checkpoint:
                    env["SIXDB_RESUME"] = str(checkpoint)
                    results.mkdir(parents=True)
                    save(results / "job.json", job)
                console = root / attempt / "console.log"
                args = ["--reference", str(reference), "--archive", str(archive),
                    "--output", str(results), "--timeout", "120", "--jar", str(jar)]
                if manifest:
                    # Only replace the object-store GET; production still checks
                    # its manifest hash, identity, every chunk and full membership.
                    program = ("import sys;from pathlib import Path;sys.path.insert(0,sys.argv[1]);"
                        "import recover;recover.load_worker_manifest=lambda ref,region:Path(sys.argv[2]).read_bytes();"
                        "output,result=recover.run(recover.parser().parse_args(sys.argv[3:]));"
                        "sys.exit(0 if result['status']=='complete' else 1)")
                    command = [sys.executable, "-c", program, str(SPEC), str(manifest), *args]
                else:
                    command = [sys.executable, str(SPEC / "recover.py"), *args]
                child = self.launch(command, root, console, env)
                return child, results, state, env, console

            first, results, state, env, console = resume("first")
            self.wait_for(lambda: (state / "pid").exists() and (results / "tlc.log").exists()
                          and b"Recovery completed." in (results / "tlc.log").read_bytes(), first, console)
            self.assertEqual((results / "lineage/original/result.json").read_bytes(), captured_receipt)
            self.assertEqual(json.loads((results / "invocation.json").read_text())["seed"], int(seed[2]))
            resumed_jvm = int((state / "pid").read_text())
            self.assertEqual(os.getpgid(resumed_jvm), first.pid,
                             "worker group must contain the JVM for checkpoint-timeout SIGCONT")
            # A producer killed while copying can leave the JVM stopped. The
            # worker resumes its workload group, without knowing the JVM PID.
            os.kill(resumed_jvm, signal.SIGSTOP)
            self.wait_for(lambda: "\nState:\tT" in Path(f"/proc/{resumed_jvm}/status").read_text(), first, console)
            os.killpg(first.pid, signal.SIGCONT)
            self.wait_for(lambda: "\nState:\tT" not in Path(f"/proc/{resumed_jvm}/status").read_text(), first, console)
            snapshot = root / "periodic"
            snapshot.mkdir()
            subprocess.run(["bash", str(TOOLS / "tlc/checkpoint.sh"), str(snapshot)],
                           cwd=SPEC.parents[1], env=env, check=True, timeout=45,
                           stdout=subprocess.DEVNULL)
            self.assertTrue((snapshot / "lineage").is_dir())
            # Worker-style shutdown after a checkpoint must also stop a paused JVM.
            os.kill(resumed_jvm, signal.SIGSTOP)
            os.killpg(first.pid, signal.SIGCONT)
            os.killpg(first.pid, signal.SIGTERM)
            first.wait(timeout=8)
            self.assertFalse(Path(f"/proc/{resumed_jvm}").exists(), "adapter left its JVM running")
            self.assertEqual(original_receipt.read_bytes(), receipt_before)
            spec = importlib.util.spec_from_file_location("test_worker_checkpoint", TOOLS / "worker_checkpoint.py")
            checkpoint = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(checkpoint)
            job = {"id": "local-first-recovery", "uri": "s3://test/jobs/first", "script": "recover.sh",
                   "args": [], "source": {"digest": digest(SPEC / "recover.py")},
                   "config": {"env": {}, "checkpoint_script": "workbench/tools/tlc/checkpoint.sh",
                              "region": "us-east-1"}}
            store = LocalStore(root / "object-store")
            checkpoint_reference = checkpoint.publish(store, snapshot, job, "periodic")
            restored = root / "restored"
            checkpoint.restore(store, checkpoint_reference, restored, job)
            manifest = root / "checkpoint-manifest.json"
            manifest.write_bytes(store.get(checkpoint_reference["uri"]))
            job["resume"] = {"checkpoint": checkpoint_reference}
            # Later resumes must be self-contained; an initial rescue redownload
            # cannot accidentally rescue an incomplete checkpoint lineage.
            archive.unlink()
            shutil.rmtree(rescue)
            shutil.rmtree(root / "first")
            shutil.rmtree(snapshot)
            shutil.rmtree(store.root)
            second, results, state, env, console = resume("second", restored, job, manifest)
            second.wait(timeout=120)
            text = (results / "tlc.log").read_text()
            result = json.loads((results / "resume-result.json").read_text())
            self.assertEqual(second.returncode, 0,
                             str(result.get("reason")) + text[-3000:] + console.read_text()[-3000:])
            self.assertIn("Model checking completed. No error has been found.", text)
            self.assertIn(f"{(n + 1) ** 2} distinct states found", text)
            recovered = re.search(r"Recovery completed\. (\d+) states examined", text)
            self.assertIsNotNone(recovered)
            self.assertTrue(0 < int(recovered[1]) < (n + 1) ** 2)
            self.assertEqual(result["status"], "complete")
            self.assertEqual((results / "lineage/original/result.json").read_bytes(), captured_receipt)
            self.assertEqual(json.loads(captured_receipt)["status"], "running")
            self.assertEqual(original_receipt.read_bytes(), receipt_before)
            import evidence
            import recover
            current = {"Grid.tla": digest(model / "Grid.tla"), "Grid.cfg": digest(model / "Grid.cfg"),
                       "check.py": digest(SPEC / "check.py")}
            selected = evidence.inspect_receipt(results / "resume-result.json",
                recover.original_case(json.loads(captured_receipt)), current, pin["sha256"])
            self.assertEqual(selected["status"], "complete")
            self.assertEqual(selected["states"]["distinct"], (n + 1) ** 2)
            print(f"Repeated TLC recovery: seed={seed[2]}, recovered={recovered[1]}, final={(n+1)**2}", flush=True)


if __name__ == "__main__":
    unittest.main()
