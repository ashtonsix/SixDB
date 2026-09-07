"""Small local-run receipts. Study-specific commands and interpretation stay local."""

from __future__ import annotations

import hashlib
import io
import json
import platform
import resource
import subprocess
import tarfile
import time
from datetime import datetime, timezone
from pathlib import Path


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_files(root: Path) -> dict[str, bytes]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=root, check=True, stdout=subprocess.PIPE,
    )
    files = {}
    for name in sorted(set(result.stdout.decode().split("\0")) - {""}):
        # Evidence is an output, even when its compact selection is tracked.
        # Do not recursively snapshot old measurements and source archives.
        if name.startswith("build/") or (
            name.startswith("workbench/spikes/") and "evidence" in Path(name).parts
        ):
            continue
        path = root / name
        if path.is_symlink():
            raise ValueError(f"source snapshot does not yet support symlinks: {name}")
        if path.is_file():
            files[name] = path.read_bytes()
    return files


class Run:
    def __init__(self, root: Path, output: Path, config: dict):
        self.root = root
        self.output = output
        output.mkdir(parents=True, exist_ok=False)
        self.started = time.monotonic()
        self.sources = source_files(root)
        hashes = {name: sha256(data) for name, data in self.sources.items()}
        with tarfile.open(output / "source.tar.gz", "w:gz") as archive:
            for name, data in self.sources.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                info.mode = (root / name).stat().st_mode & 0o777
                archive.addfile(info, io.BytesIO(data))
        self.receipt = {
            "format": 1,
            "started_utc": datetime.now(timezone.utc).isoformat(),
            "status": "running",
            "config": config,
            "platform": platform.platform(),
            "source_files_sha256": hashes,
            "source_digest": sha256(json.dumps(hashes, sort_keys=True).encode()),
            "commands": [],
        }
        self.save()

    def save(self):
        path = self.output / "run.json"
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.receipt, indent=2, sort_keys=True) + "\n")
        temporary.replace(path)

    def step(self, name: str, argv: list[str], stdout_name: str | None = None):
        print(f"{name} …", flush=True)
        started = time.monotonic()
        record = {"name": name, "argv": [str(x) for x in argv], "cwd": str(self.root)}
        self.receipt["commands"].append(record)
        self.save()
        with (self.output / (stdout_name or f"{name}.stdout")).open("wb") as out:
            with (self.output / f"{name}.stderr").open("wb") as err:
                process = subprocess.run(argv, cwd=self.root, stdout=out, stderr=err)
        record.update(returncode=process.returncode, seconds=time.monotonic() - started)
        self.save()
        if process.returncode:
            raise RuntimeError(f"{name} exited {process.returncode}; see {self.output / (name + '.stderr')}")
        print(f"{name}: {record['seconds']:.2f}s", flush=True)

    def finish(self, error: Exception | None = None):
        try:
            after = source_files(self.root)
            current = {name: sha256(data) for name, data in after.items()}
            self.receipt["source_unchanged"] = current == self.receipt["source_files_sha256"]
            if not self.receipt["source_unchanged"] and error is None:
                error = RuntimeError("source changed during the run; results need rerunning")
        except Exception as exc:
            error = error or exc
        self.receipt["status"] = "failed" if error else "complete"
        self.receipt["elapsed_seconds"] = time.monotonic() - self.started
        self.receipt["children_peak_rss_kib"] = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        if error:
            self.receipt["error"] = str(error)
        self.receipt["artifact_sha256"] = {
            str(path.relative_to(self.output)): sha256(path.read_bytes())
            for path in sorted(self.output.rglob("*"))
            if path.is_file() and path.name != "run.json"
        }
        self.save()
        return error
