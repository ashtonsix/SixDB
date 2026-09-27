#!/usr/bin/env python3
"""Run one bounded local TLC case and retain its exact inputs and partial evidence.

All .tla/.cfg files beneath the module directory are copied with relative paths.
Use the reusable workbench tools for remote workers and checkpoint management.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone

ROOT = next(p for p in Path(__file__).resolve().parents
            if (p / "workbench/tools/tlc/source.json").is_file())
TOOL_SOURCE_BYTES = (ROOT / "workbench/tools/tlc/source.json").read_bytes()
TOOL_SOURCE = json.loads(TOOL_SOURCE_BYTES)
JAR_SHA256 = TOOL_SOURCE["sha256"]
DEFAULT_JAR = ROOT / "build/tools/tlc" / JAR_SHA256 / "tla2tools.jar"
START = re.compile(r"@!@!@STARTMSG (\d+):(\d+) @!@!@")
END = re.compile(r"@!@!@ENDMSG (\d+) @!@!@")
STATS = re.compile(r"([\d,]+) states generated(?:[^\n]*?), ([\d,]+) distinct states found(?:[^\n]*?), ([\d,]+) states left on queue")
CONFIG_WORDS = set("CONSTANT CONSTANTS CONSTRAINT CONSTRAINTS ACTION_CONSTRAINT ACTION_CONSTRAINTS INIT NEXT SPECIFICATION INVARIANT INVARIANTS PROPERTY PROPERTIES SYMMETRY VIEW ALIAS POSTCONDITION CHECK_DEADLOCK RL_REWARD PERIODIC _PERIODIC".split())


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def resolve_jar(explicit: Path | None) -> Path:
    if explicit is not None:
        return explicit.resolve()
    if DEFAULT_JAR.is_file():
        return DEFAULT_JAR
    spec = importlib.util.spec_from_file_location("orbital_shared_datasets", ROOT / "workbench/tools/datasets.py")
    datasets = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(datasets)
    return datasets.source(TOOL_SOURCE)


def temporal_properties(config: str) -> list[str]:
    # Configs used here have ordinary identifier lists, not generated TLC syntax.
    config = re.sub(r"\(\*.*?\*\)", "", config, flags=re.S)
    config = re.sub(r"\\\*[^\n]*", "", config)
    names: list[str] = []
    active = False
    for word in re.findall(r"[A-Za-z_][A-Za-z_0-9!]*|\S", config):
        if word in CONFIG_WORDS:
            active = word in {"PROPERTY", "PROPERTIES"}
        elif active:
            names.append(word)
    return names


class Output:
    """Stream summaries only; counterexample bodies remain in the raw log."""
    def __init__(self) -> None:
        self.partial = ""
        self.block: tuple[int, int] | None = None
        self.body = ""
        self.messages: list[dict] = []
        self.progress: list[dict] = []
        self.ids: set[int] = set()
        self.depth: int | None = None
        self.stats: dict = {}

    def feed(self, text: str, elapsed: float) -> None:
        self.partial += text
        lines = self.partial.split("\n")
        self.partial = lines.pop()
        for line in lines:
            start, end = START.fullmatch(line.strip()), END.fullmatch(line.strip())
            if start:
                self.block = (int(start[1]), int(start[2]))
                self.body = ""
            elif end and self.block and int(end[1]) == self.block[0]:
                code, level = self.block
                self.ids.add(code)
                # Omit verbose trace/state/progress bodies from the JSON summary.
                if level in {1, 2, 3} or code in {2186, 2193}:
                    self.messages.append({"code": code, "level": level, "text": self.body.strip()})
                self.block = None
            elif self.block and len(self.body) < 8192:
                self.body += line + "\n"
            depth = re.search(r"Progress\((\d+)\)|depth of the complete state graph search is (\d+)", line)
            if depth:
                self.depth = int(depth[1] or depth[2])
            counts = STATS.search(line)
            if counts:
                self.stats = dict(zip(("generated", "distinct", "queue"), (int(x.replace(",", "")) for x in counts.groups())))
                self.progress.append({"elapsed_seconds": elapsed, "depth": self.depth, **self.stats})


def classify(output: Output, returncode: int, stop: str | None,
             expected: str | None, witness: str | None) -> tuple[str, str]:
    if stop:
        return "incomplete_" + stop, "Exploration stopped before a classified completion."
    finished = 2186 in output.ids
    errors = [m for m in output.messages if m["level"] in {1, 2}]
    invariant_names = []
    for message in output.messages:
        if message["code"] in {2107, 2110, 2146}:
            found = re.search(r"\bInvariant ([A-Za-z_][A-Za-z_0-9!]*) is violated\b", message["text"])
            if found:
                invariant_names.append(found[1])
    if expected:
        kind, name = expected.split(":", 1)
        match = (kind == "invariant" and invariant_names == [name] and returncode == 12)
        match |= kind == "temporal" and 2116 in output.ids and returncode == 13
        allowed = {2107, 2110, 2146, 2120, 2121, 2264} if kind == "invariant" else {2116, 2120, 2121, 2264}
        if match and finished and all(m["code"] in allowed for m in errors):
            return ("witnessed" if witness else "expected_violation"), expected
    if (returncode == 0 and finished and 2193 in output.ids and not errors
            and output.stats.get("queue") == 0 and output.stats.get("distinct", 0) > 0):
        if expected:
            return "missing_expected_violation", "The finite graph completed without the expected violation."
        return "complete", "The finite graph completed with no reported violation."
    if invariant_names or 2116 in output.ids or 2114 in output.ids or returncode in {10, 11, 12, 13, 14}:
        return "unexpected_violation", "The reported violation did not match this case's expectation."
    return "tool_error", "TLC did not produce a recognized complete result."


def disk_usage(path: Path) -> dict:
    sizes = [item.stat() for item in path.rglob("*") if item.is_file()]
    return {"logical_bytes": sum(s.st_size for s in sizes),
            "allocated_bytes": sum(s.st_blocks * 512 for s in sizes), "files": len(sizes)}


def wait_child(process: subprocess.Popen, timeout: float, log: Path,
               progress_log: Path) -> tuple[int, str | None, float, object, Output]:
    start = time.monotonic()
    stopped: str | None = None
    deadline: float | None = None
    parsed = Output()
    progress_count = 0
    with log.open(errors="replace") as reader, progress_log.open("w") as progress:
        while True:
            try:
                elapsed = time.monotonic() - start
                parsed.feed(reader.read(), elapsed)
                for receipt in parsed.progress[progress_count:]:
                    progress.write(json.dumps(receipt, sort_keys=True) + "\n")
                progress_count = len(parsed.progress)
                progress.flush()
                pid, status, usage = os.wait4(process.pid, os.WNOHANG)
                if pid:
                    process.returncode = os.waitstatus_to_exitcode(status)
                    parsed.feed(reader.read() + "\n", time.monotonic() - start)
                    for receipt in parsed.progress[progress_count:]:
                        progress.write(json.dumps(receipt, sort_keys=True) + "\n")
                    return process.returncode, stopped, time.monotonic() - start, usage, parsed
                if stopped is None and elapsed >= timeout:
                    stopped, deadline = "timeout", time.monotonic() + 2
                    os.killpg(process.pid, signal.SIGTERM)
                elif deadline is not None and time.monotonic() >= deadline:
                    os.killpg(process.pid, signal.SIGKILL)
                    deadline = None
                time.sleep(0.05)
            except KeyboardInterrupt:
                stopped, deadline = "interrupted", time.monotonic() + 2
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                # The child exited between wait4 and process-group termination.
                continue


def run(args: argparse.Namespace) -> tuple[Path, dict]:
    if sys.platform != "linux":
        raise ValueError("Run under Linux (prefix with orb -m ubuntu in the macOS workspace).")
    module = (args.module or Path(__file__).parent / (args.case + ".tla")).resolve()
    default_config = module.with_suffix(".cfg")
    if not default_config.exists():
        default_config = module.parent / "configs" / (module.stem + ".cfg")
    config = (args.config or default_config).resolve()
    if module.suffix != ".tla" or config.suffix != ".cfg" or not config.is_relative_to(module.parent):
        raise ValueError("Configuration must be a .cfg beneath the .tla module's directory.")
    if not module.is_file() or not config.is_file():
        raise ValueError("Module or configuration does not exist.")
    if args.output.resolve().is_relative_to(module.parent):
        raise ValueError("Keep output outside the model source directory to avoid capturing previous runs.")
    config_relative = config.relative_to(module.parent)
    if not math.isfinite(args.timeout) or args.timeout <= 0 or args.workers <= 0:
        raise ValueError("Timeout and workers must be positive.")
    if not re.fullmatch(r"[1-9][0-9]*[mMgG]", args.heap):
        raise ValueError("Heap must have an explicit m or g suffix, for example 512m.")
    if args.expect and not re.fullmatch(r"(?:invariant|temporal):[A-Za-z_][A-Za-z_0-9!]*", args.expect):
        raise ValueError("Expectation must be invariant:Name or temporal:Name.")
    if args.witness and (not args.expect or not args.expect.startswith("invariant:")):
        raise ValueError("A reachability witness requires an expected invariant violation.")
    jar = resolve_jar(args.jar)
    jar_bytes = jar.read_bytes()
    jar_hash = sha(jar_bytes)
    if jar_hash != JAR_SHA256 or len(jar_bytes) != TOOL_SOURCE["bytes"]:
        raise ValueError("TLC jar does not match workbench/tools/tlc/source.json.")
    java = shutil.which(args.java)
    if not java:
        raise ValueError("Java executable was not found.")
    java = str(Path(java).resolve())
    env = os.environ.copy()
    removed = {key: env.pop(key) for key in ("JAVA_TOOL_OPTIONS", "JDK_JAVA_OPTIONS", "_JAVA_OPTIONS", "CLASSPATH", "TLA_LIBRARYPATH") if key in env}
    env["LC_ALL"] = "C"
    version = subprocess.run([java, "-version"], capture_output=True, text=True, env=env, timeout=10, check=True)
    args.output.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    destination = Path(tempfile.mkdtemp(prefix=f"{stamp}-{config.stem}-", dir=args.output.resolve()))
    source = destination / "sources"
    source.mkdir()
    hashes = {}
    for path in sorted(module.parent.rglob("*")):
        if path.is_file() and path.suffix in {".tla", ".cfg"}:
            data = path.read_bytes()
            relative = path.relative_to(module.parent)
            (source / relative).parent.mkdir(parents=True, exist_ok=True)
            (source / relative).write_bytes(data)
            hashes[str(relative)] = sha(data)
    runner = Path(__file__).read_bytes()
    (destination / "check.py").write_bytes(runner)
    (destination / "tool-source.json").write_bytes(TOOL_SOURCE_BYTES)
    config_text = (source / config_relative).read_text()
    uncommented = re.sub(r"\(\*.*?\*\)|\\\*[^\n]*", "", config_text, flags=re.S)
    if re.search(r"\bCHECK_DEADLOCK\s+FALSE\b", uncommented):
        raise ValueError("This runner requires deadlock checking; model legitimate terminal stuttering explicitly.")
    if args.expect and args.expect.startswith("temporal:"):
        required = args.expect.split(":", 1)[1]
        if temporal_properties(config_text) != [required]:
            raise ValueError("A temporal control must configure exactly its one named PROPERTY (TLC's violation diagnostic is unnamed).")
    metadir = destination / "metadir"
    metadir.mkdir()
    argv = [java, "-Xmx" + args.heap, "-XX:+UseParallelGC", "-Duser.language=en", "-Duser.country=US",
            "-cp", str(jar), "tlc2.TLC", "-tool", "-workers", str(args.workers), "-fp", "0",
            "-metadir", str(metadir), "-config", str(config_relative), module.stem]
    result = {"schema": 1, "case": args.case or config.stem, "started_utc": stamp,
              "expectation": args.expect or "complete", "purpose": "reachability" if args.witness else ("negative_control" if args.expect else "check"),
              "witness": args.witness, "module": module.name, "configuration": str(config_relative),
              "source_directory": str(module.parent), "source_sha256": hashes, "runner_sha256": sha(runner),
              "jar": {"path": str(jar), "sha256": jar_hash, "source_metadata": TOOL_SOURCE,
                      "source_metadata_sha256": sha(TOOL_SOURCE_BYTES)},
              "java": {"path": java, "version": version.stdout + version.stderr},
              "argv": argv, "cwd": str(source), "timeout_seconds": args.timeout, "workers": args.workers,
              "heap": args.heap, "removed_environment": removed, "locale": "C", "status": "running"}
    write_json(destination / "result.json", result)
    with (destination / "tlc.log").open("wb") as log:
        process = subprocess.Popen(argv, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        code, stop, elapsed, usage, output = wait_child(process, args.timeout, destination / "tlc.log", destination / "progress.jsonl")
    status, reason = classify(output, code, stop, args.expect, args.witness)
    if sha(jar.read_bytes()) != jar_hash:
        status, reason = "tool_error", "Jar changed during execution."
    result.update(status=status, reason=reason, returncode=code, elapsed_seconds=elapsed,
                  peak_rss_bytes=usage.ru_maxrss * 1024, cpu_user_seconds=usage.ru_utime, cpu_system_seconds=usage.ru_stime,
                  states=output.stats, depth=output.depth, progress=output.progress, messages=output.messages,
                  metadata_storage=disk_usage(metadir), retained_storage=disk_usage(destination))
    write_json(destination / "result.json", result)
    return destination, result


def parser() -> argparse.ArgumentParser:
    out = argparse.ArgumentParser(description=__doc__)
    choice = out.add_mutually_exclusive_group(required=True)
    choice.add_argument("--module", type=Path)
    choice.add_argument("--case", help="Resolve NAME.tla beside this runner, NAME.cfg beside it or under configs/")
    out.add_argument("--config", type=Path)
    out.add_argument("--output", type=Path, required=True, help="Parent directory for a new unique evidence directory")
    out.add_argument("--timeout", type=float, default=60)
    out.add_argument("--workers", type=int, default=1)
    out.add_argument("--heap", default="512m")
    out.add_argument("--jar", type=Path, help="Verified override; otherwise use local bytes or the shared pinned source resolver")
    out.add_argument("--java", default="java")
    out.add_argument("--expect", help="Exact invariant:Name or temporal:Name expected to fail")
    out.add_argument("--witness", help="Reachability label; expected failure is a witness, not a defect")
    return out


def main() -> int:
    try:
        destination, result = run(parser().parse_args())
        print(json.dumps({"result": str(destination / "result.json"), "status": result["status"], "states": result["states"], "elapsed_seconds": result["elapsed_seconds"]}, sort_keys=True))
        return 0 if result["status"] in {"complete", "expected_violation", "witnessed"} else 1
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print(f"check.py: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
