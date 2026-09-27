#!/usr/bin/env python3
"""Pinned standalone TLC with a cooperative worker checkpoint producer."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import struct
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
import datasets

SOURCE = json.loads((HERE / 'source.json').read_text())
JAR_SHA256 = SOURCE['sha256']

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def run(arguments):
    # These modes do not share the tested standalone BFS checkpoint contract.
    reserved = {'-checkpoint', '-recover', '-metadir', '-cleanup', '-tool', '-fp', '-seed',
                '-simulate', '-dfid', '-parallel', '-server'}
    if not arguments or reserved.intersection(arguments):
        raise ValueError('supply model/options; checkpoint, recovery, seed and execution mode are managed by this runner')
    jar = Path(os.environ['TLC_JAR']).resolve() if os.environ.get('TLC_JAR') else datasets.source(SOURCE)
    if digest(jar) != JAR_SHA256:
        raise ValueError('TLC jar does not match the tested pin')
    state = Path(os.environ['SIXDB_CHECKPOINT_STATE'])
    results = Path(os.environ['SIXDB_RESULTS'])
    state.mkdir(parents=True, exist_ok=True)
    results.mkdir(parents=True, exist_ok=True)
    meta = state / 'states'
    if meta.exists():
        raise ValueError('use a fresh checkpoint-state directory for each TLC attempt')
    java_options = shlex.split(os.environ.get('TLC_JAVA_OPTS', '-XX:MaxRAMPercentage=70'))
    invocation = {'jar_sha256': JAR_SHA256, 'arguments': arguments, 'java_options': java_options,
        'java': subprocess.check_output(['java', '--version'], text=True), 'fp': 0, 'seed': 0}
    recovery = []
    if previous := os.environ.get('SIXDB_RESUME'):
        previous = Path(previous)
        if json.loads((previous / 'invocation.json').read_text()) != invocation:
            raise ValueError('TLC jar, Java version or invocation changed since checkpoint')
        shutil.copytree(previous / 'states', meta)
        shutil.copyfile(previous / 'tlc.log', results / 'previous-tlc.log')
        recovery = ['-recover', str(meta / (previous / 'metadir.txt').read_text().strip())]
    (results / 'invocation.json').write_text(json.dumps(invocation, indent=2) + '\n')
    subprocess.run(['javac', '--add-modules', 'jdk.attach', '-d', str(state),
                    str(HERE / 'Checkpoint.java')], check=True)
    command = ['java', *java_options, '-Dtlc2.tool.ModelChecker.vetoCleanup=true', '-Dtlc2.TLC.progressInterval=5',
        '-cp', str(jar), 'tlc2.TLC', '-tool', '-checkpoint', '0', '-fp', '0', '-seed', '0',
        '-metadir', str(meta), *recovery, *arguments]
    with (results / 'tlc.log').open('wb') as log:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        (state / 'pid').write_text(str(child.pid))
        try:
            for line in child.stdout:
                log.write(line)
                log.flush()
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()
            return child.wait()
        finally:
            (state / 'pid').unlink(missing_ok=True)
            # TLC can emit counterexamples beside the model; retain them as evidence.
            model_dirs = {Path(arg).resolve().parent for arg in arguments if arg.endswith('.tla')} or {Path.cwd()}
            for directory in model_dirs:
                for path in directory.glob('*_TTrace*'):
                    if path.is_file():
                        target = results / 'counterexamples' / path.relative_to(Path.cwd())
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(path, target)



def trim_liveness_tails(metadata):
    # TLC 8f4bc8b recovers these append positions with seek(), not truncate().
    # A subsequent full graph scan can otherwise interpret post-checkpoint bytes.
    # Offsets are the two Java DataOutputStream longs written by AbstractDiskGraph.
    for receipt in metadata.rglob('dgraph_*.chkpt'):
        offsets = struct.unpack('>qq', receipt.read_bytes())
        index = receipt.stem.removeprefix('dgraph_')
        for prefix, size in zip(('nodes_', 'ptrs_'), offsets):
            path = receipt.with_name(prefix + index)
            if not 0 <= size <= path.stat().st_size:
                raise ValueError('liveness checkpoint references unavailable graph bytes')
            with path.open('r+b') as stream:
                stream.truncate(size)


def checkpoint(destination):
    state = Path(os.environ['SIXDB_CHECKPOINT_STATE'])
    results = Path(os.environ['SIXDB_RESULTS'])
    pid = int((state / 'pid').read_text())
    log = results / 'tlc.log'
    # Only this adapter requests checkpoints; TLC's independent timer is disabled.
    with log.open('rb') as output:
        output.seek(0, 2)
        subprocess.run(['java', '--add-modules', 'jdk.attach', '-cp', str(state),
                        'Checkpoint', str(pid)], check=True)
        pending = b''
        while b'@!@!@ENDMSG 2196 @!@!@' not in pending:
            pending = (pending + output.read())[-65536:]
            os.kill(pid, 0)
            time.sleep(0.05)
    def interrupted(signum, frame):
        raise InterruptedError('checkpoint producer cancelled')
    signal.signal(signal.SIGTERM, interrupted)
    try:
        os.kill(pid, signal.SIGSTOP)
        # Wait for all JVM threads to stop before copying mutable trace/graph files.
        while any('\nState:\tT' not in path.read_text()
                  for path in Path(f'/proc/{pid}/task').glob('*/status')):
            time.sleep(0.01)
        subprocess.run(['cp', '-a', '--reflink=auto', str(state / 'states'), str(destination / 'states')], check=True)
        trim_liveness_tails(destination / 'states')
        generations = [p.name for p in (destination / 'states').iterdir() if p.is_dir()]
        if len(generations) != 1:
            raise ValueError('expected one TLC metadata directory')
        (destination / 'metadir.txt').write_text(generations[0] + '\n')
        for name in ('invocation.json', 'tlc.log'):
            shutil.copyfile(results / name, destination / name)
        if (state / 'lineage').exists():
            # Preserve links for the shared publisher to reject, never follow them.
            shutil.copytree(state / 'lineage', destination / 'lineage', symlinks=True)
    finally:
        try:
            os.kill(pid, signal.SIGCONT)
        except ProcessLookupError:
            pass


if __name__ == '__main__':
    if sys.argv[1] == 'run':
        sys.exit(run(sys.argv[2:]))
    elif sys.argv[1] == 'checkpoint':
        checkpoint(Path(sys.argv[2]))
    else:
        sys.exit('expected run or checkpoint')
