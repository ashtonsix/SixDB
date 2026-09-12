"""Small local-run receipts. Study-specific commands and interpretation stay local."""

from __future__ import annotations

import hashlib
import fcntl
import io
import json
import os
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
    def __init__(self, root: Path, output: Path, config: dict, *, workspace: Path | None = None):
        self.root = root
        self.output = output
        output.mkdir(parents=True, exist_ok=False)
        self.started = time.monotonic()
        self.sources = source_files(root)
        hashes = {name: sha256(data) for name, data in self.sources.items()}
        self.source_root = root
        self.workspace_lock = None
        if workspace is not None:
            workspace = workspace.resolve()
            workspace.mkdir(parents=True, exist_ok=True)
            self.workspace_lock = (workspace / '.lock').open('a')
            fcntl.flock(self.workspace_lock, fcntl.LOCK_EX)
            self.source_root = workspace / 'source'
            self.build_dir = workspace / 'build'
            self.source_root.mkdir(exist_ok=True)
            # A stable path and unchanged mtimes let Ninja reuse independent TUs.
            # The lock belongs to this build workspace, never the working tree.
            try:
                for path in self.source_root.rglob('*'):
                    if path.is_file() and str(path.relative_to(self.source_root)) not in self.sources:
                        path.unlink()
                for name, data in self.sources.items():
                    path = self.source_root / name
                    path.parent.mkdir(parents=True, exist_ok=True)
                    if not path.exists() or path.read_bytes() != data:
                        temporary = path.with_name(path.name + '.snapshot-tmp')
                        temporary.write_bytes(data)
                        temporary.chmod(0o555 if (root / name).stat().st_mode & 0o111 else 0o444)
                        temporary.replace(path)
            except BaseException:
                self.workspace_lock.close()
                raise
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
            "source_mode": "captured" if workspace is not None else "working-tree",
            "source_root": str(self.source_root),
            "commands": [],
        }
        self.save()

    def input(self, mount: str, directory: Path):
        """Use cached prepared bytes now; retain/fetch handles their storage later."""
        from datasets import verify
        directory = directory.resolve()
        if Path(mount).is_absolute() or '..' in Path(mount).parts or not Path(mount).parts:
            raise ValueError('Input mount must be a relative subdirectory')
        if (self.output / mount).exists() or any(Path(mount).is_relative_to(p) or Path(p).is_relative_to(mount)
                                               for p in self.receipt.get('inputs', {})):
            raise ValueError('Input mount overlaps an existing output or input')
        meta = verify(directory)
        self.receipt.setdefault('inputs', {})[mount] = {
            'path': str(directory), 'id': meta['id'], 'key': meta['key']}
        self.save()
        return directory

    def compact(self, files: list[str], regenerate: list[str]):
        """Select offline comparison inputs; the full run stays recoverable in S3.

        Prefer an explicit list to a glob of everything emitted. Keep competitors
        and repetitions together; artifacts.md shows selection before retention.
        """
        self.receipt['compact'] = {'files': files, 'regenerate': regenerate}
        self.save()

    def save(self):
        path = self.output / "run.json"
        temporary = path.with_suffix(".tmp")
        temporary.write_text(json.dumps(self.receipt, indent=2, sort_keys=True) + "\n")
        temporary.replace(path)

    def step(self, name: str, argv: list[str], stdout_name: str | None = None, *, check: bool = True):
        print(f"{name} …", flush=True)
        started = time.monotonic()
        record = {"name": name, "argv": [str(x) for x in argv], "cwd": str(self.root), "required": check}
        self.receipt["commands"].append(record)
        self.save()
        with (self.output / (stdout_name or f"{name}.stdout")).open("wb") as out:
            with (self.output / f"{name}.stderr").open("wb") as err:
                process = subprocess.run(argv, cwd=self.root, stdout=out, stderr=err,
                                         env=os.environ | {'PYTHONDONTWRITEBYTECODE': '1'})
        record.update(returncode=process.returncode, seconds=time.monotonic() - started)
        self.save()
        if process.returncode:
            message = f"{name} exited {process.returncode}; see {self.output / (name + '.stderr')}"
            if check:
                raise RuntimeError(message)
            print(f'Optional step: {message}', flush=True)
        print(f"{name}: {record['seconds']:.2f}s", flush=True)

    def finish(self, error: Exception | None = None):
        try:
            if self.workspace_lock is None:
                after = source_files(self.root)
            else:
                after = {name: (self.source_root / name).read_bytes() for name in self.sources}
            current = {name: sha256(data) for name, data in after.items()}
            self.receipt["source_unchanged"] = current == self.receipt["source_files_sha256"]
            if not self.receipt["source_unchanged"] and error is None:
                error = RuntimeError("measured source changed during the run")
        except Exception as exc:
            error = error or exc
        self.receipt["status"] = "failed" if error else "complete"
        self.receipt["elapsed_seconds"] = time.monotonic() - self.started
        self.receipt["children_peak_rss_kib"] = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
        if error:
            self.receipt["error"] = str(error)
        try:
            from datasets import digest, verify
            for entry in self.receipt.get('inputs', {}).values():
                if verify(Path(entry['path']))['id'] != entry['id']:
                    raise ValueError('Prepared input changed during the run')
            self.receipt["artifact_sha256"] = {
                str(path.relative_to(self.output)): digest(path)
                for path in sorted(self.output.rglob("*"))
                if path.is_file() and path.name != "run.json"
            }
        except Exception as exc:
            error = error or exc
            self.receipt.update(status='failed', error=str(error))
        finally:
            if self.workspace_lock is not None:
                self.workspace_lock.close()
            self.save()
        return error
