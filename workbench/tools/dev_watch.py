#!/usr/bin/env python3
"""Configure-only editor maintenance; never run builds or index build outputs."""

from __future__ import annotations

import contextlib
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

from dev import BUILD, ROOT, active_selection, refresh

STATE = BUILD / '.editor-watch.json'


def inputs():
    """Read CMake inputs and source names, excluding generated/retained trees."""
    result = {}
    excluded = {'build', '.git', 'evidence', 'node_modules', '__pycache__', '.venv', 'venv'}
    for directory, children, files in os.walk(ROOT):
        children[:] = [name for name in children if name not in excluded]
        for name in files:
            path = Path(directory) / name
            if path.suffix in {'.cpp', '.cc', '.cxx'}:
                # Adding a file can change a CMake glob even without a CMake
                # edit. Ordinary source edits must not trigger reconfiguration.
                result[str(path.relative_to(ROOT))] = 'source'
                continue
            if name != 'CMakeLists.txt' and name != 'CMakePresets.json' and not name.endswith('.cmake'):
                continue
            try:
                result[str(path.relative_to(ROOT))] = hashlib.sha256(path.read_bytes()).hexdigest()
            except FileNotFoundError:
                pass  # An agent may be renaming a study during this scan.
    return result


def initial_known(spikes):
    # Existing, tracked studies remain opt-in. Newly authored, untracked studies
    # already present at installation are new too. Outside Git, baseline the tree.
    result = subprocess.run(['git', 'ls-files', '-z', '--', 'workbench/spikes/*/CMakeLists.txt'],
                            cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        return spikes
    return {Path(name).parent.name for name in result.stdout.split('\0') if name}


def configure(selected, benchmarks, label):
    with tempfile.TemporaryFile(mode='w+') as output, contextlib.redirect_stdout(output):
        status = refresh(selected, benchmarks, log=output)
        output.seek(0)
        text = output.read()
    name = '.editor-watch-error.log' if status else '.editor-watch-configure.log'
    (BUILD / name).write_text(text[-131072:])
    print(f'Editor {label}: {"configure failed; keeping previous commands" if status else "refreshed"}', flush=True)
    return status


def sync(snapshot=None):
    BUILD.parent.mkdir(parents=True, exist_ok=True)
    with (BUILD.parent / '.dev-configure.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        snapshot = inputs() if snapshot is None else snapshot
        spikes = {Path(name).parent.name for name in snapshot
                  if name.startswith('workbench/spikes/') and len(Path(name).parts) == 4
                  and name.endswith('/CMakeLists.txt')}
        state = json.loads(STATE.read_text()) if STATE.exists() else {'known': sorted(initial_known(spikes))}
        pending = (set(state.get('pending', [])) | (spikes - set(state['known']))) & spikes
        selected = active_selection('SIXDB_SPIKES') & spikes
        benchmark_dirs = {Path(name).parent.name for name in snapshot
                          if name.startswith('workbench/benchmarks/') and len(Path(name).parts) == 4
                          and name.endswith('/CMakeLists.txt')}
        benchmarks = active_selection('SIXDB_BENCHMARKS') & benchmark_dirs
        changed = snapshot != state.get('inputs')
        failed = bool(state.get('refresh_failed'))
        if changed or failed:
            failed = bool(configure(selected, benchmarks, 'active targets'))
        for name in sorted(pending):
            if name in selected or not configure(selected | {name}, benchmarks, f'new spike {name}'):
                selected.add(name)
                pending.remove(name)
        state = {'known': sorted(spikes), 'pending': sorted(pending), 'inputs': snapshot,
                 'refresh_failed': failed, 'last_attempt': time.time()}
        temporary = STATE.with_suffix('.tmp')
        temporary.write_text(json.dumps(state, indent=2) + '\n')
        temporary.replace(STATE)
        if not failed and not pending:
            (BUILD / '.editor-watch-error.log').unlink(missing_ok=True)
        return int(failed or bool(pending))


def watch():
    BUILD.parent.mkdir(parents=True, exist_ok=True)
    with (BUILD.parent / '.editor-watch.lock').open('a') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            print('Editor watcher already running.', flush=True)
            return 0
        previous = None
        while True:
            current = inputs()
            if current == previous:  # One quiet polling interval avoids partial edits.
                state = json.loads(STATE.read_text()) if STATE.exists() else {}
                changed = current != state.get('inputs')
                retry = (state.get('pending') or state.get('refresh_failed')) and time.time() - state.get('last_attempt', 0) >= 30
                if changed or retry:
                    sync(current)
            previous = current
            time.sleep(3)


def install():
    name = 'sixdb-editor-' + hashlib.sha256(str(ROOT).encode()).hexdigest()[:12] + '.service'
    directory = Path.home() / '.config/systemd/user'
    directory.mkdir(parents=True, exist_ok=True)

    def quote(value):
        return '"' + str(value).replace('%', '%%').replace('\\', '\\\\').replace('"', '\\"') + '"'

    (directory / name).write_text(
        '[Unit]\nDescription=SixDB editor compilation database\n\n'
        '[Service]\nType=simple\n'
        f'WorkingDirectory={str(ROOT).replace("%", "%%")}\n'
        f'ExecStart={quote(sys.executable)} -u {quote(ROOT / "workbench/tools/dev.py")} --watch\n'
        'Restart=on-failure\nRestartSec=5\nNice=10\n\n'
        '[Install]\nWantedBy=default.target\n')
    subprocess.run(['systemd-analyze', '--user', 'verify', str(directory / name)], check=True)
    subprocess.run(['systemctl', '--user', 'daemon-reload'], check=True)
    subprocess.run(['systemctl', '--user', 'enable', name], check=True)
    subprocess.run(['systemctl', '--user', 'restart', name], check=True)
    # Window-scoped extension settings in a folder's .vscode/settings.json are
    # ignored by multi-root VS Code workspaces. Apply the same two preferences
    # to an existing Remote SSH installation as well.
    remote = Path.home() / '.vscode-server/data/Machine/settings.json'
    if remote.exists():
        settings = json.loads(remote.read_text())
        settings.update({'clangd.onConfigChanged': 'restart',
                         'clangd.onConfigChangedForceEnable': True})
        remote.write_text(json.dumps(settings, indent=2) + '\n')
    print(f'Installed {name}; inspect with systemctl --user status {name}')
    return 0
