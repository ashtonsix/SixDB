#!/usr/bin/env python3
"""Continue a verified TLC rescue; keep its producer and checkpoint lineage explicit."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone

import check

ROOT = check.ROOT
KIND = 'orbital-tlc-resume'
BUNDLE_FILES = {'resume-result.json', 'tlc.log', 'progress.jsonl', 'invocation.json',
                'recover.py', 'check.py', 'tool-source.json', 'checkpoint-manifest.json',
                'previous-tlc.log', 'job.json'}


def digest(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path: Path):
    return json.loads(path.read_bytes())


def safe_path(root: Path, name: str) -> Path:
    relative = PurePosixPath(name)
    if not relative.parts or relative.is_absolute() or '..' in relative.parts:
        raise ValueError('Unsafe checkpoint member: ' + name)
    result = root.joinpath(*relative.parts)
    if any(p.is_symlink() for p in [result, *result.parents] if p.is_relative_to(root)):
        raise ValueError('Checkpoint symlink: ' + name)
    return result


def file_hashes(root: Path) -> dict[str, str]:
    answer = {}
    for path in sorted(root.rglob('*')):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError('Lineage requires ordinary files/directories')
        if path.is_file():
            answer[path.relative_to(root).as_posix()] = digest(path)
    return answer


def restore_archive(reference: Path, destination: Path, archive: Path | None = None) -> dict:
    # The tools package also has an evidence.py; isolate its imports from Orbital's.
    program = '''import json,sys,tempfile
from pathlib import Path
sys.path.insert(0,sys.argv[1])
import artifacts
reference=json.loads(Path(sys.argv[2]).read_bytes()); destination=Path(sys.argv[3])
if reference.get('subdirectory'): raise ValueError('Rescue must reference a whole archive')
with tempfile.TemporaryDirectory(dir=destination.parent) as temporary:
    bundle=Path(sys.argv[4]) if sys.argv[4] else Path(temporary)/'bundle.tar.gz'
    if not sys.argv[4]: artifacts.download(reference,bundle)
    if bundle.stat().st_size!=reference['bytes'] or artifacts.sha256(bundle)!=reference['sha256']:
        raise ValueError('Rescue archive size/hash mismatch')
    destination.mkdir()
    manifest={}
    count=artifacts.unpack(bundle,destination,manifest=manifest)
    if count!=reference['files']: raise ValueError('Rescue archive file count mismatch')
    print(json.dumps(manifest))
'''
    result = subprocess.run([sys.executable, '-c', program, str(ROOT / 'workbench/tools'),
                             str(reference.resolve()), str(destination.resolve()),
                             str(archive.resolve()) if archive else ''], capture_output=True, text=True, check=True)
    return json.loads(result.stdout)


def checkpoint_module():
    spec = importlib.util.spec_from_file_location('orbital_worker_checkpoint', ROOT / 'workbench/tools/worker_checkpoint.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def load_worker_manifest(reference: dict, region: str) -> bytes:
    raw = checkpoint_module().Store(region).get(reference['uri'])
    if check.sha(raw) != reference['sha256']:
        raise ValueError('Worker checkpoint manifest hash mismatch')
    return raw


def verify_worker_snapshot(snapshot: Path, raw: bytes, job: dict) -> dict:
    reference = job['resume']['checkpoint']
    if check.sha(raw) != reference['sha256']:
        raise ValueError('Worker checkpoint manifest hash mismatch')
    manifest = json.loads(raw)
    if manifest['format'] != 1 or manifest['identity'] != checkpoint_module().identity(job):
        raise ValueError('Worker checkpoint invocation identity differs')
    seen = set()
    for member in manifest['files']:
        name = member['path']
        if name in seen:
            raise ValueError('Duplicate checkpoint member')
        seen.add(name)
        path = safe_path(snapshot, name)
        with path.open('rb') as stream:
            for chunk in member['chunks']:
                data = stream.read(chunk['bytes'])
                if len(data) != chunk['bytes'] or check.sha(data) != chunk['sha256']:
                    raise ValueError('Restored worker checkpoint member differs: ' + name)
            if stream.read(1) or path.stat().st_size != member['bytes']:
                raise ValueError('Restored worker checkpoint size differs: ' + name)
    actual = set()
    for path in snapshot.rglob('*'):
        if path.is_symlink() or not (path.is_file() or path.is_dir()):
            raise ValueError('Invalid restored worker checkpoint member')
        if path.is_file():
            actual.add(path.relative_to(snapshot).as_posix())
    if actual != seen:
        raise ValueError('Restored worker checkpoint member set differs')
    return manifest


def original_case(original: dict) -> dict:
    case = {'name': original['case'], 'module': Path(original['module']).stem,
            'config': original['configuration'], 'workers': original['workers']}
    if original['expectation'] != 'complete':
        case['expect'] = original['expectation']
    if original.get('witness'):
        case['witness'] = original['witness']
    return case


def inspect_lineage(lineage: Path, current: dict | None = None, case: dict | None = None):
    import evidence
    rescue, members = read(lineage / 'rescue.json'), read(lineage / 'archive-members.json')
    original = read(lineage / 'original/result.json')
    if rescue.get('format') != 1 or rescue.get('kind') != 'manual-tlc-checkpoint-rescue':
        raise ValueError('Unsupported rescue provenance')
    for name, info in members.items():
        safe_path(lineage, name)
    for path in lineage.rglob('*'):
        if not path.is_file() or path.name in {'artifact.json', 'archive-members.json'}:
            continue
        name = path.relative_to(lineage).as_posix()
        info = members.get(name)
        if not info or path.stat().st_size != info['bytes'] or digest(path) != info['sha256']:
            raise ValueError('Captured lineage differs from verified archive: ' + name)
    declared = read(lineage / 'files.json')
    if set(declared) != set(members) - {'files.json', 'rescue.json'}:
        raise ValueError('Rescue file manifest is incomplete')
    for name, info in declared.items():
        if any(info[k] != members[name][k] for k in ('bytes', 'sha256')):
            raise ValueError('Rescue file manifest differs from archive: ' + name)
    if digest(lineage / 'capture-helper.py') != rescue['capture_helper_sha256']:
        raise ValueError('Capture helper identity differs')
    log = (lineage / 'original/tlc.log').read_bytes()
    seeds = re.findall(rb'with fp (\d+) and seed (-?\d+)', log)
    if len(seeds) != 1 or tuple(map(int, seeds[0])) != (rescue['fingerprint'], rescue['seed']):
        raise ValueError('Original resolved seed/fingerprint differs')
    if rescue['fingerprint'] != 0 or original['argv'] != rescue['argv'] or original['cwd'] != rescue['cwd']:
        raise ValueError('Original observed command differs')
    start, end = rescue['log_request_offset'], rescue['log_ack_end_offset']
    fragment = log[start:end]
    if (not 0 <= start < end <= len(log) or b'@!@!@STARTMSG 2195:' not in fragment
            or not fragment.endswith(b'@!@!@ENDMSG 2196 @!@!@')):
        raise ValueError('Fresh checkpoint acknowledgment missing')
    if (rescue['java_version'] != original['java']['version'] or rescue['jar_sha256'] != check.JAR_SHA256
            or original['jar']['sha256'] != check.JAR_SHA256):
        raise ValueError('Original Java/TLC identity differs')
    generation = (lineage / 'metadir.txt').read_text().strip()
    if generation != rescue['generation'] or len(PurePosixPath(generation).parts) != 1:
        raise ValueError('Checkpoint generation differs')
    if not any(n.startswith('states/' + generation + '/') and n.endswith('.chkpt') for n in members):
        raise ValueError('Checkpoint generation has no checkpoints')
    parsed = check.Output(); parsed.feed(log.decode(errors='replace') + '\n', 0)
    if any(m['level'] in {1, 2} for m in parsed.messages):
        raise ValueError('Original captured log already reports an error/violation')
    if current is None:
        current = dict(original['source_sha256'], **{'check.py': digest(Path(check.__file__))})
    inspected = evidence.inspect_receipt(lineage / 'original/result.json', case or original_case(original), current, check.JAR_SHA256)
    return original, rescue, inspected


def prepare_rescue(captured: Path, reference: dict, members: dict, lineage: Path):
    lineage.mkdir()
    for name in ('rescue.json', 'files.json', 'metadir.txt', 'capture-helper.py'):
        shutil.copyfile(captured / name, lineage / name)
    shutil.copytree(captured / 'original', lineage / 'original')
    check.write_json(lineage / 'artifact.json', reference)
    check.write_json(lineage / 'archive-members.json', members)
    original, rescue, inspected = inspect_lineage(lineage)
    if digest(captured / 'tla2tools.jar') != check.JAR_SHA256:
        raise ValueError('Captured TLC jar differs')
    for name, info in rescue['checkpoint_files'].items():
        recorded = members['states/' + name]
        if any(info[k] != recorded[k] for k in ('bytes', 'sha256')):
            raise ValueError('Committed checkpoint differs: ' + name)
    for path in (lineage / 'original/sources').rglob('*'):
        if path.is_file() and path.relative_to(lineage / 'original/sources').as_posix() not in inspected['dependency_sha256']:
            path.unlink()
    return original, rescue, inspected


def invocation(lineage: Path, original: dict, rescue: dict) -> dict:
    return {'kind': KIND, 'format': 1, 'producer_sha256': digest(Path(__file__)),
            'classifier_sha256': digest(Path(check.__file__)), 'lineage_sha256': file_hashes(lineage),
            'jar_sha256': check.JAR_SHA256, 'java_version': original['java']['version'],
            'fingerprint': rescue['fingerprint'], 'seed': rescue['seed'],
            'workers': original['workers'], 'heap': original['heap'],
            'module': original['module'], 'configuration': original['configuration'],
            'expectation': original['expectation'], 'witness': original.get('witness')}


def command(original: dict, rescue: dict, java: str, jar: Path, states: Path) -> list[str]:
    return [java, '-Xmx' + original['heap'], '-XX:+UseParallelGC', '-Duser.language=en', '-Duser.country=US',
            '-Dtlc2.tool.ModelChecker.vetoCleanup=true', '-Dtlc2.TLC.progressInterval=5',
            '-cp', str(jar), 'tlc2.TLC', '-tool', '-checkpoint', '0', '-workers', str(original['workers']),
            '-fp', str(rescue['fingerprint']), '-seed', str(rescue['seed']), '-metadir', str(states),
            '-recover', str(states / rescue['generation']), '-config', original['configuration'], Path(original['module']).stem]


def recovery_diagnostics(log: Path) -> dict:
    block, body, found, startup = None, [], {}, []
    with log.open(errors='replace') as stream:
        for line in stream:
            startup.extend(re.findall(r'with fp (\d+) and seed (-?\d+)', line))
            start, end = check.START.fullmatch(line.strip()), check.END.fullmatch(line.strip())
            if start and int(start[1]) in {2197, 2198}:
                block, body = int(start[1]), []
            elif block and end and int(end[1]) == block:
                if block in found:
                    raise ValueError('Repeated recovery diagnostics')
                found[block] = ''.join(body).strip(); block = None
            elif block:
                body.append(line)
    if set(found) != {2197, 2198} or len(startup) != 1:
        raise ValueError('Complete recovery/startup diagnostics missing')
    prefix = 'Starting recovery from checkpoint '
    counts = re.fullmatch(r'Recovery completed\. ([\d,]+) states examined\. ([\d,]+) states on queue\.', found[2198])
    if not found[2197].startswith(prefix) or not counts:
        raise ValueError('Unrecognized recovery diagnostics')
    return {'directory': found[2197][len(prefix):], 'states': int(counts[1].replace(',', '')),
            'queue': int(counts[2].replace(',', '')), 'fingerprint': int(startup[0][0]), 'seed': int(startup[0][1])}


def verify_previous_log(path: Path) -> None:
    parsed = check.Output()
    with path.open(errors='replace') as stream:
        for line in stream:
            parsed.feed(line, 0)
    parsed.feed('\n', 0)
    if any(m['level'] in {1, 2} for m in parsed.messages):
        raise ValueError('A checkpoint ancestor log reports an error/violation')


def validate_receipt(path: Path, data: dict, case: dict, current: dict, pin: str) -> dict:
    """Validate continuation lineage offline; the known producer verified archive bytes."""
    root = path.parent
    if (data.get('kind') != KIND or data.get('schema') != 1
            or data.get('producer_sha256') != digest(Path(__file__))
            or digest(root / 'recover.py') != data['producer_sha256']
            or data.get('classifier_sha256') != current['check.py']
            or digest(root / 'check.py') != current['check.py']):
        raise ValueError('Recovery producer/classifier differs')
    original, rescue, _ = inspect_lineage(root / 'lineage', current, case)
    expected = invocation(root / 'lineage', original, rescue)
    if (read(root / 'invocation.json') != expected
            or digest(root / 'invocation.json') != data['invocation_sha256']):
        raise ValueError('Recovery invocation/lineage differs')
    for key in ('module', 'configuration', 'expectation', 'witness', 'purpose', 'workers', 'heap'):
        if data.get(key) != original.get(key):
            raise ValueError('Recovery changed original ' + key)
    argv = data['argv']
    states = Path(argv[argv.index('-metadir') + 1])
    if (argv != command(original, rescue, data['java']['path'], Path(data['jar']['path']), states)
            or data['java']['version'] != original['java']['version']
            or data['jar']['sha256'] != pin or data.get('locale') != 'C'):
        raise ValueError('Recovery actual JVM invocation differs')
    origin = data['resumed_from']
    if origin['kind'] == 'artifact':
        if (origin['reference'] != read(root / 'lineage/artifact.json')
                or origin['rescue_sha256'] != digest(root / 'lineage/rescue.json')):
            raise ValueError('Rescue archive lineage differs')
    elif origin['kind'] == 'worker_checkpoint':
        raw = (root / 'checkpoint-manifest.json').read_bytes()
        if check.sha(raw) != origin['reference']['sha256'] or check.sha(raw) != origin['manifest_sha256']:
            raise ValueError('Continuation checkpoint manifest differs')
        job, manifest = read(root / 'job.json'), json.loads(raw)
        if (job['resume']['checkpoint'] != origin['reference']
                or manifest['identity'] != checkpoint_module().identity(job)):
            raise ValueError('Continuation worker identity differs')
        recorded = {m['path']: m for m in manifest['files']}
        if len(recorded) != len(manifest['files']):
            raise ValueError('Duplicate continuation checkpoint member')
        available = {name: root / name for name in ['invocation.json']}
        available['tlc.log'] = root / 'previous-tlc.log'
        available.update({'lineage/' + n: root / 'lineage' / n for n in expected['lineage_sha256']})
        for name, source in available.items():
            member = recorded[name]
            with source.open('rb') as stream:
                for chunk in member['chunks']:
                    data_chunk = stream.read(chunk['bytes'])
                    if len(data_chunk) != chunk['bytes'] or check.sha(data_chunk) != chunk['sha256']:
                        raise ValueError('Continuation captured member differs: ' + name)
                if stream.read(1) or source.stat().st_size != member['bytes']:
                    raise ValueError('Continuation captured member size differs: ' + name)
        if digest(root / 'previous-tlc.log') != origin['previous_log_sha256']:
            raise ValueError('Previous attempt log differs')
        verify_previous_log(root / 'previous-tlc.log')
    else:
        raise ValueError('Unknown recovery lineage kind')
    if data.get('status') not in {'running', 'incomplete_timeout', 'incomplete_interrupted', 'tool_error'}:
        recovered = recovery_diagnostics(root / 'tlc.log')
        if (recovered != data.get('recovery') or Path(recovered['directory']) != states / rescue['generation']
                or recovered['seed'] != rescue['seed'] or recovered['fingerprint'] != rescue['fingerprint']
                or recovered['states'] <= 0):
            raise ValueError('Raw recovery diagnostics differ')
    if data.get('status') != 'running' and digest(root / 'tlc.log') != data.get('log_sha256'):
        raise ValueError('Resumed log differs from producer hash')
    return {'original_result_sha256': digest(root / 'lineage/original/result.json'),
            'rescue_reference': read(root / 'lineage/artifact.json'), 'resumed_from': origin}


def wait_child(process, timeout: float, log: Path, progress_log: Path):
    # Unlike check.wait_child, this JVM stays in the worker's process group. A
    # killed checkpoint hook is followed by worker SIGCONT for that whole group.
    start = time.monotonic(); stop = None; deadline = None
    parsed = check.Output(); consumed = 0
    with log.open(errors='replace') as reader, progress_log.open('w') as progress:
        while True:
            try:
                elapsed = time.monotonic() - start
                parsed.feed(reader.read(), elapsed)
                for row in parsed.progress[consumed:]:
                    progress.write(json.dumps(row, sort_keys=True) + '\n')
                consumed = len(parsed.progress); progress.flush()
                pid, status, usage = os.wait4(process.pid, os.WNOHANG)
                if pid:
                    process.returncode = os.waitstatus_to_exitcode(status)
                    parsed.feed(reader.read() + '\n', time.monotonic() - start)
                    for row in parsed.progress[consumed:]:
                        progress.write(json.dumps(row, sort_keys=True) + '\n')
                    return process.returncode, stop, time.monotonic() - start, usage, parsed
                if stop is None and elapsed >= timeout:
                    stop, deadline = 'timeout', time.monotonic() + 2
                    process.send_signal(signal.SIGCONT); process.terminate()
                elif deadline is not None and time.monotonic() >= deadline:
                    process.kill(); deadline = None
                time.sleep(0.05)
            except KeyboardInterrupt:
                stop, deadline = 'interrupted', time.monotonic() + 2
                process.send_signal(signal.SIGCONT); process.terminate()
            except ProcessLookupError:
                pass


def run(args):
    if sys.platform != 'linux':
        raise ValueError('Run on Linux')
    if not math.isfinite(args.timeout) or args.timeout <= 0:
        raise ValueError('Timeout must be finite and positive')
    output = args.output.resolve(); output.mkdir(parents=True, exist_ok=True)
    if os.environ.get('SIXDB_RESULTS') and Path(os.environ['SIXDB_RESULTS']).resolve() != output:
        raise ValueError('--output must be SIXDB_RESULTS for the shared checkpoint producer')
    state = Path(os.environ['SIXDB_CHECKPOINT_STATE']).resolve(); state.mkdir(parents=True, exist_ok=True)
    if (output / 'resume-result.json').exists() or (state / 'states').exists() or (state / 'pid').exists():
        raise ValueError('Use fresh results and checkpoint state for each recovery attempt')
    lineage = state / 'lineage'
    previous = os.environ.get('SIXDB_RESUME')
    if previous:
        previous = Path(previous)
        job = read(output / 'job.json')
        raw = load_worker_manifest(job['resume']['checkpoint'], job['config']['region'])
        verify_worker_snapshot(previous, raw, job)
        verify_previous_log(previous / 'tlc.log')
        shutil.copytree(previous / 'lineage', lineage)
        original, rescue, inspected = inspect_lineage(lineage)
        expected = invocation(lineage, original, rescue)
        if read(previous / 'invocation.json') != expected:
            raise ValueError('Checkpoint invocation/producer/lineage differs')
        shutil.copytree(previous / 'states', state / 'states')
        shutil.copyfile(previous / 'tlc.log', output / 'previous-tlc.log')
        (output / 'checkpoint-manifest.json').write_bytes(raw)
        resumed_from = {'kind': 'worker_checkpoint', 'reference': job['resume']['checkpoint'],
                        'manifest_sha256': check.sha(raw), 'previous_log_sha256': digest(previous / 'tlc.log')}
    else:
        captured = state / 'capture'
        members = restore_archive(args.reference, captured, args.archive)
        reference = read(args.reference)
        original, rescue, inspected = prepare_rescue(captured, reference, members, lineage)
        (captured / 'states').rename(state / 'states')
        shutil.rmtree(captured)
        expected = invocation(lineage, original, rescue)
        resumed_from = {'kind': 'artifact', 'reference': reference, 'rescue_sha256': digest(lineage / 'rescue.json')}
    shutil.copytree(lineage, output / 'lineage')
    shutil.copytree(lineage / 'original/sources', output / 'sources')
    for name, source in [('recover.py', Path(__file__)), ('check.py', Path(check.__file__)),
                          ('tool-source.json', lineage / 'original/tool-source.json')]:
        shutil.copyfile(source, output / name)
    check.write_json(output / 'invocation.json', expected)
    jar = check.resolve_jar(args.jar)
    if digest(jar) != check.JAR_SHA256:
        raise ValueError('TLC jar differs from rescue')
    java = str(Path(shutil.which(args.java) or args.java).resolve())
    env = os.environ.copy()
    removed = {k: env.pop(k) for k in ('JAVA_TOOL_OPTIONS', 'JDK_JAVA_OPTIONS', '_JAVA_OPTIONS', 'CLASSPATH', 'TLA_LIBRARYPATH') if k in env}
    env['LC_ALL'] = 'C'
    version = subprocess.run([java, '-version'], capture_output=True, text=True, env=env, check=True)
    if version.stdout + version.stderr != original['java']['version']:
        raise ValueError('Java version differs from original')
    subprocess.run(['javac', '--add-modules', 'jdk.attach', '-d', str(state),
                    str(ROOT / 'workbench/tools/tlc/Checkpoint.java')], check=True)
    argv = command(original, rescue, java, jar, state / 'states')
    receipt = {k: original[k] for k in ('case', 'module', 'configuration', 'expectation', 'purpose', 'witness', 'workers', 'heap')}
    receipt.update(schema=1, kind=KIND, producer_sha256=expected['producer_sha256'],
                   classifier_sha256=expected['classifier_sha256'],
                   source_sha256=inspected['dependency_sha256'], invocation_sha256=digest(output / 'invocation.json'),
                   jar=original['jar'] | {'path': str(jar)}, java={'path': java, 'version': version.stdout + version.stderr},
                   argv=argv, cwd=str(output / 'sources'), resumed_from=resumed_from,
                   started_utc=datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'), timeout_seconds=args.timeout,
                   removed_environment=removed, locale='C', status='running')
    check.write_json(output / 'resume-result.json', receipt)
    process = None
    def interrupt(signum, frame):
        raise KeyboardInterrupt
    prior_handler = signal.signal(signal.SIGTERM, interrupt)
    try:
        with (output / 'tlc.log').open('wb') as log:
            process = subprocess.Popen(argv, cwd=output / 'sources', env=env, stdout=log, stderr=subprocess.STDOUT)
            (state / 'pid').write_text(str(process.pid))
            code, stop, elapsed, usage, parsed = wait_child(process, args.timeout, output / 'tlc.log', output / 'progress.jsonl')
        status, reason = check.classify(parsed, code, stop,
                                       None if original['expectation'] == 'complete' else original['expectation'], original.get('witness'))
        recovery = None
        try:
            recovery = recovery_diagnostics(output / 'tlc.log')
            if (Path(recovery['directory']) != state / 'states' / rescue['generation']
                    or recovery['seed'] != rescue['seed'] or recovery['fingerprint'] != rescue['fingerprint']
                    or recovery['states'] <= 0):
                raise ValueError('TLC recovered a different generation/seed')
        except ValueError as error:
            if status in {'complete', 'expected_violation', 'witnessed'}:
                status, reason = 'tool_error', str(error)
        if digest(jar) != check.JAR_SHA256:
            status, reason = 'tool_error', 'Jar changed during execution.'
        receipt.update(status=status, reason=reason, returncode=code, elapsed_seconds=elapsed,
                       peak_rss_bytes=usage.ru_maxrss * 1024, cpu_user_seconds=usage.ru_utime,
                       cpu_system_seconds=usage.ru_stime, states=parsed.stats, depth=parsed.depth,
                       messages=parsed.messages, recovery=recovery, log_sha256=digest(output / 'tlc.log'),
                       metadata_storage=check.disk_usage(state / 'states'))
        check.write_json(output / 'resume-result.json', receipt)
        return output, receipt
    finally:
        signal.signal(signal.SIGTERM, prior_handler)
        if process is not None and process.returncode is None:
            process.send_signal(signal.SIGCONT); process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()
        (state / 'pid').unlink(missing_ok=True)


def parser():
    out = argparse.ArgumentParser(description=__doc__)
    out.add_argument('--reference', type=Path, required=True, help='Verified rescue artifact reference')
    out.add_argument('--archive', type=Path, help='Use an existing local archive matching the reference')
    out.add_argument('--output', type=Path, required=True, help='Fresh attempt results, equal to SIXDB_RESULTS on workers')
    out.add_argument('--timeout', type=float, default=3600)
    out.add_argument('--jar', type=Path)
    out.add_argument('--java', default='java')
    return out


if __name__ == '__main__':
    try:
        destination, result = run(parser().parse_args())
        print(json.dumps({'result': str(destination / 'resume-result.json'), 'status': result['status']}))
        raise SystemExit(0 if result['status'] in {'complete', 'expected_violation', 'witnessed'} else 1)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        print(f'recover.py: {error}', file=sys.stderr)
        raise SystemExit(2)
