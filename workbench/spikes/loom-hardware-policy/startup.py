#!/usr/bin/env python3
"""Lookup-first native startup; a supplied probe calibrates bounded local policy."""
from __future__ import annotations
import argparse
import concurrent.futures
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import threading
import time

STUDY = Path(__file__).resolve().parent
MEMORY = STUDY.with_name('memory-characterisation')
sys.path.insert(0, str(MEMORY))
from hardware import discover
from lookup import match, consistent, qualify
from analyze import analyse

FIELDS = ['family', 'variant', 'bytes', 'k', 'stride', 'train', 'ahead', 'rep', 'n', 'p10', 'median', 'p90']
KS = [1, 2, 4, 8, 12, 16, 24, 32, 48, 64]
TRAINS = [1, 2, 3, 4, 6, 8, 12, 16]
SEEDS = [357712151618, 357712151619]
DEFAULT_WIDTH = 8  # Hypothetical performance default, not a hardware count.
REPS, TRIALS, SAMPLE_MS = 2, 20, 1
OUTPUT_LIMIT = 2 * 1024 * 1024

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def stop_process(process, grace=.5):
    """Stop its entire group, escalate, and always reap the owned child."""
    if process is None:
        return
    for sig in (signal.SIGTERM, signal.SIGKILL):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=grace)
            break
        except subprocess.TimeoutExpired:
            continue
    process.wait()
    # Also stop descendants if the group leader exited before a helper did.
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except ProcessLookupError:
        pass

def raw_rows(path, mib, line, page):
    """Check the data that inference consumes, before the permissive analyser."""
    with path.open(newline='') as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != FIELDS:
            raise ValueError('unexpected probe CSV header')
        rows = list(reader)
    seen, mlp = set(), set()
    for row in rows:
        if None in row or any(value is None for value in row.values()):
            raise ValueError('incomplete or extra CSV fields')
        for key in FIELDS[2:9]:
            row[key] = int(row[key])
        for key in FIELDS[9:]:
            row[key] = float(row[key])
        if not all(math.isfinite(row[k]) for k in FIELDS[9:]) or not 0 <= row['p10'] <= row['median'] <= row['p90']:
            raise ValueError('invalid probe quantiles')
        if row['bytes'] <= 0 or row['n'] <= 0 or not 0 <= row['rep'] < REPS:
            raise ValueError('invalid probe size, samples or repetition')
        # TLB rows can legitimately repeat a packed byte size in this format;
        # they are not used by this controller's inference.
        if row['family'] == 'mlp' or ':' in row['variant']:
            key = tuple(row[k] for k in FIELDS[:8])
            if key in seen:
                raise ValueError('duplicate inference row/control arm')
            seen.add(key)
        if row['family'] == 'mlp':
            if row['variant'] != 'base' or row['n'] != 3 or row['median'] <= 0 or any(row[k] != 0 for k in ('stride', 'train', 'ahead')):
                raise ValueError('unexpected MLP control')
            mlp.add((row['bytes'], row['k'], row['rep']))
        elif ':' in row['variant'] and row['n'] != TRIALS:
            raise ValueError('unexpected prefetch sample count')
        if ':' in row['variant'] and row['bytes'] != 512 * max(65536, 4 * page):
            raise ValueError('unexpected prefetch mapping size')
    expected = {(size, k, rep) for size in (16 * 1024, 512 * 1024, 8 * 1024 * 1024, mib * 2**20)
                for k in KS for rep in range(REPS)}
    if mlp != expected:
        raise ValueError('incomplete or unexpected MLP grid')
    # Missing/unsupported training evidence stays unknown independently of MLP.
    training = {(row['train'], row['rep'], row['variant']) for row in rows
                if row['family'] == 'learning' and row['stride'] == 1 and row['ahead'] == 1}
    expected_training = {(n, rep, 'within-page:' + arm) for n in TRAINS if (n + 8) * line < page
                         for rep in range(REPS) for arm in ('cold', 'trained', 'hot')}
    return rows, training == expected_training and bool(expected_training)

def observations(analyses, complete_training, mib):
    curves = [next(p for p in a['mlp'] if p['condition'] == 'foreground-overlap' and
                   p['page_mode'] == 'base' and p['bytes'] == mib * 2**20) for a in analyses]
    width = qualify(curves)
    width['scope'] = f'two seeds, foreground-overlap, {mib} MiB encoded-pointer chains, base pages'
    width['per_run_right_censored'] = [p['right_censored'] for p in curves]
    candidates = [k for k in width['candidate_k'] if k in KS]
    chosen_width = min(candidates) if width['status'] == 'validated' and candidates and not any(width['per_run_right_censored']) else None
    training = None
    reason = 'missing-or-unsupported-controls'
    if all(complete_training):
        by_length = {n: [p for a in analyses for p in a['prefetch'] if p['family'] == 'learning' and
                        p['variant'] == 'within-page' and p['stride'] == 1 and p['train'] == n and p['ahead'] == 1]
                     for n in TRAINS}
        lengths = [n for n, points in by_length.items() if points]
        suffix = [n for n in lengths if n >= 2 and len([v for v in lengths if v >= n]) >= 2 and
                  all(consistent(by_length[v]) == 'observed-benefit' for v in lengths if v >= n)]
        training = min(suffix) if suffix else None
        reason = 'qualified-forward-suffix' if suffix else 'unresolved-controls-or-benefit'
    return {'condition': 'foreground-overlap', 'global_lookup_eligible': False,
            'width': chosen_width, 'training': training, 'mlp': width, 'training_status': reason,
            'unknown': ['exact outstanding miss count', 'DRAM service tier', 'prefetch table capacity', 'global idle profile']}

def table_observations(entry):
    mlp = entry.get('mlp', {})
    if not isinstance(mlp, dict):
        mlp = {}
    candidates = mlp.get('candidate_k', [])
    if not isinstance(candidates, list):
        candidates = []
    candidates = [k for k in candidates if type(k) is int and k in KS]
    width = min(candidates) if mlp.get('status') == 'validated' and candidates and not mlp.get('right_censored', False) else None
    suffix = entry.get('consistent_training_suffix', {})
    training = suffix.get('1') if isinstance(suffix, dict) else None
    if type(training) is not int or not 2 <= training <= 64:
        training = None
    return {'condition': 'qualified-lookup-observations', 'width': width, 'training': training,
            'global_lookup_eligible': False, 'scope': mlp.get('scope'), 'limitations': entry.get('limitations', [])}

class Calibrations:
    """One startup context, one bounded job; duplicate requests share its Future."""
    def __init__(self, probe, output, mib, seconds):
        self.probe, self.output, self.mib, self.seconds = probe, output, mib, seconds
        self.cancelled = threading.Event()
        self.jobs = {}
        self.thread = None
        self.requests = 0
        self.lock = threading.Lock()

    def request(self, hardware, line, page):
        with self.lock:
            self.requests += 1
            key = (hardware['lookup_key'], json.dumps(hardware['lookup_context'], sort_keys=True))
            if key in self.jobs:
                return self.jobs[key]
            if self.jobs:
                raise ValueError('this startup spike bounds calibration to one context')
            future = concurrent.futures.Future()
            self.jobs[key] = future
            self.thread = threading.Thread(target=self._run, args=(future, hardware, line, page), name='startup-calibration')
            self.thread.start()
            return future

    def _run(self, future, hardware, line, page):
        start = time.monotonic()
        deadline = start + self.seconds
        result = {'status': 'failed', 'condition': 'foreground-overlap', 'global_lookup_eligible': False,
                  'seeds': SEEDS, 'commands': [], 'bound': {
                      'sweep_mib': self.mib, 'repetitions': REPS, 'trials': TRIALS, 'sample_target_ms': SAMPLE_MS,
                      'wall_budget_seconds': self.seconds, 'output_bytes_per_stream': OUTPUT_LIMIT,
                      'largest_mapping_bytes': max(self.mib * 2**20, 8192 * page, 512 * max(65536, 4 * page)),
                      'largest_mlp_pointer_order_bytes': self.mib * 2**20 // line * 8,
                      'scope': 'sequential probe allocations; excludes executable/allocator overhead; not an RSS measurement'}}
        process = None
        try:
            result['probe_sha256'] = sha(self.probe)
            analyses, training = [], []
            self.output.mkdir(parents=True, exist_ok=False)
            for index, seed in enumerate(SEEDS):
                if self.cancelled.is_set() or time.monotonic() >= deadline:
                    raise TimeoutError('cancelled or calibration wall budget exhausted')
                directory = self.output / f'seed-{index}'
                directory.mkdir()
                (directory / 'hardware.json').write_text(json.dumps(hardware, indent=2) + '\n')
                command = [str(self.probe), '--cpu', str(hardware['cpu']), '--line', str(line), '--mib', str(self.mib),
                           '--reps', str(REPS), '--ms', str(SAMPLE_MS), '--trials', str(TRIALS), '--seed', str(seed), '--quick']
                record = {'argv': command, 'condition': 'foreground-overlap', 'started_seconds': time.monotonic() - start}
                result['commands'].append(record)
                raw, errors = directory / 'raw.csv', directory / 'probe.stderr'
                with raw.open('wb') as stdout, errors.open('wb') as stderr:
                    process = subprocess.Popen(command, stdout=stdout, stderr=stderr, start_new_session=True)
                    record['pid'] = process.pid
                    while process.poll() is None:
                        if self.cancelled.is_set() or time.monotonic() >= deadline:
                            raise TimeoutError('cancelled or calibration wall budget exhausted')
                        if raw.stat().st_size > OUTPUT_LIMIT or errors.stat().st_size > OUTPUT_LIMIT:
                            raise ValueError('probe output bound exceeded')
                        self.cancelled.wait(.02)
                    record['exit'] = process.wait()
                if record['exit'] != 0:
                    raise ValueError('probe failed with exit ' + str(record['exit']))
                if sha(self.probe) != result['probe_sha256']:
                    raise ValueError('probe changed during calibration')
                if raw.stat().st_size > OUTPUT_LIMIT or errors.stat().st_size > OUTPUT_LIMIT:
                    raise ValueError('probe output bound exceeded')
                rows, complete = raw_rows(raw, self.mib, line, page)
                training.append(complete)
                with (directory / 'samples.csv').open('w', newline='') as stream:
                    writer = csv.DictWriter(stream, ['condition'] + FIELDS)
                    writer.writeheader()
                    for row in rows:
                        writer.writerow({'condition': 'foreground-overlap'} | row)
                analysis = analyse(directory, write=False)
                (directory / 'analysis.json').write_text(json.dumps(analysis, indent=2) + '\n')
                analyses.append(analysis)
                stop_process(process)
                process = None
            if self.cancelled.is_set():
                raise TimeoutError('cancelled before profile publication')
            result['observations'] = observations(analyses, training, self.mib)
            if self.cancelled.is_set() or time.monotonic() >= deadline:
                raise TimeoutError('cancelled or calibration wall budget exhausted before publication')
            result['status'] = 'complete'
        except Exception as error:
            result.pop('observations', None)
            result['status'] = 'cancelled' if self.cancelled.is_set() else 'timeout' if isinstance(error, TimeoutError) else 'failed'
            result['error'] = str(error)
        finally:
            if process is not None:
                stop_process(process)
                result['commands'][-1]['exit'] = process.returncode
            result['elapsed_seconds'] = time.monotonic() - start
            result['children_reaped'] = True
            future.set_result(result)

    def close(self):
        self.cancelled.set()
        if self.thread:
            self.thread.join()

def run(args):
    started = time.monotonic()
    journal = {'format': 1, 'status': 'starting', 'performance_claim': False, 'events': [], 'native_event_counts': {},
               'lookup': {}, 'calibration': None, 'profile_sent': None, 'profile_applied': None}
    args.journal.parent.mkdir(parents=True, exist_ok=True)
    if args.journal.exists():
        raise ValueError('journal must be a new path')
    process, registry, future = None, None, None
    selected = None
    ready_at, progress_at, sent_at = None, None, None
    stopping = threading.Event()
    old_handlers = {}
    def event(kind, **values):
        journal['events'].append({'kind': kind, 'seconds': time.monotonic() - started} | values)
    def interrupted(signum, frame):
        stopping.set()
    def send(command):
        process.stdin.write(command + '\n')
        process.stdin.flush()
        event('native_command', command=command)
    try:
        for sig in (signal.SIGINT, signal.SIGTERM):
            old_handlers[sig] = signal.signal(sig, interrupted)
        cpu = min(os.sched_getaffinity(0)) if args.cpu is None else args.cpu
        hardware = discover(cpu)
        line, page = hardware['line_bytes'] or 64, hardware['pages']['base_bytes']
        if line not in (32, 64, 128, 256, 512) or not 4096 <= page <= 65536 or page & (page - 1):
            raise ValueError('reported geometry is outside this bounded spike')
        journal['hardware'] = hardware
        journal['effective_geometry'] = {'line': line, 'page': page, 'line_source': hardware['cache_line_source']}
        journal['defaults'] = {'width': DEFAULT_WIDTH, 'training': 0, 'scope': 'hypothetical performance defaults; reported geometry retained'}
        try:
            database = json.loads(args.lookup.read_text())
            selected = None if args.force_miss else match(hardware, database)
            journal['lookup'] = {'status': 'hit' if selected else 'forced-miss' if args.force_miss else 'miss',
                                 'path': str(args.lookup), 'sha256': sha(args.lookup), 'key': hardware['lookup_key']}
        except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
            journal['lookup'] = {'status': 'miss', 'error': str(error), 'path': str(args.lookup), 'key': hardware['lookup_key']}
        command = [str(args.driver), 'serve', '--line', str(line), '--page', str(page), '--width', str(DEFAULT_WIDTH), '--cpu', str(cpu)]
        journal['driver'] = {'argv': command, 'sha256': sha(args.driver)}
        with args.journal.with_suffix('.driver.stderr').open('wb') as stderr:
            process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=stderr,
                                       text=True, bufsize=1, start_new_session=True)
            journal['driver']['pid'] = process.pid
            os.set_blocking(process.stdout.fileno(), False)
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            buffer, launched, settled, profile_revision = b'', False, False, 0
            profile = {'line': line, 'page': page, 'width': DEFAULT_WIDTH, 'training': 0}
            def receive(message, shutdown=False):
                nonlocal ready_at, progress_at
                kind = message.get('event', message.get('kind', message.get('type')))
                if kind not in ('ready', 'progress', 'applied', 'pressure', 'rejected', 'stopped'):
                    raise ValueError('unexpected native event')
                journal['native_event_counts'][kind] = journal['native_event_counts'].get(kind, 0) + 1
                if kind != 'progress' or journal['native_event_counts'][kind] <= 16:
                    event('native_' + kind, native=message)
                if kind == 'ready':
                    if ready_at is not None or message.get('revision') != 0 or shutdown:
                        raise ValueError('invalid default-ready revision')
                    ready_at = time.monotonic()
                elif kind == 'progress':
                    if ready_at is None or type(message.get('loads')) is not int or message['loads'] <= 0 or type(message.get('checksum')) is not int:
                        raise ValueError('invalid native progress')
                    applied = journal['profile_applied']['revision'] if journal['profile_applied'] else 0
                    if message.get('revision') != applied or message['loads'] < journal.get('last_progress', {}).get('loads', 0):
                        raise ValueError('invalid progress revision or regressed load counter')
                    progress_at = progress_at or time.monotonic()
                    journal['last_progress'] = message
                elif kind == 'applied':
                    if message.get('revision') != profile_revision or journal['profile_sent'] is None:
                        raise ValueError('unexpected profile acknowledgement')
                    for field in ('width', 'training'):
                        if field in message and message[field] != journal['profile_sent'][field]:
                            raise ValueError('profile acknowledgement changed supplied policy')
                    journal['profile_applied'] = message
                elif kind == 'pressure':
                    if message.get('value') not in ('low', 'busy') or type(message.get('revision')) is not int or not 0 <= message['revision'] <= profile_revision:
                        raise ValueError('invalid pressure acknowledgement')
                elif kind == 'rejected':
                    journal['profile_rejected'] = message
                    journal['error'] = 'native rejected controller input: ' + str(message.get('reason'))
                elif not shutdown:
                    raise RuntimeError('native stopped before controller stop')
                else:
                    journal['native_stopped'] = message
            while True:
                now = time.monotonic()
                for key, _ in selector.select(.02):
                    chunk = os.read(key.fd, 16384)
                    if not chunk:
                        if process.poll() is not None:
                            raise RuntimeError('native service ended before stop')
                        continue
                    buffer += chunk
                    if len(buffer) > 65536:
                        raise ValueError('native JSONL line bound exceeded')
                    while b'\n' in buffer:
                        text, buffer = buffer.split(b'\n', 1)
                        receive(json.loads(text))
                if process.poll() is not None:
                    raise RuntimeError('native service exited with ' + str(process.returncode))
                if stopping.is_set():
                    event('controller_cancel')
                    break
                if ready_at is None or progress_at is None:
                    if now - started > 5:
                        raise TimeoutError('native default service did not become ready and progress')
                    continue
                if not launched:
                    launched = True
                    if selected is not None:
                        journal['observations'] = table_observations(selected)
                        settled = True
                    else:
                        registry = Calibrations(args.probe, args.journal.with_suffix('.calibration'), args.mib, args.calibration_seconds)
                        send('pressure busy')
                        event('calibration_requested', condition='foreground-overlap')
                        future = registry.request(hardware, line, page)
                if future is not None and future.done() and not settled:
                    journal['calibration'] = future.result()
                    journal['calibration']['requests'] = registry.requests
                    journal['calibration']['jobs'] = len(registry.jobs)
                    journal['native_progress_before_calibration_complete'] = progress_at is not None
                    event('calibration_settled', status=journal['calibration']['status'])
                    send('pressure low')
                    journal['observations'] = journal['calibration'].get('observations', {}) if journal['calibration']['status'] == 'complete' else {}
                    settled = True
                if settled and journal['profile_sent'] is None:
                    observation = journal.get('observations', {})
                    next_profile = profile | {k: observation.get(source) if observation.get(source) is not None else profile[k]
                                              for k, source in (('width', 'width'), ('training', 'training'))}
                    if next_profile != profile:
                        if stopping.is_set():
                            event('controller_cancel')
                            break
                        profile_revision = 1
                        send(f"profile {profile_revision} {line} {page} {next_profile['width']} {next_profile['training']}")
                        journal['profile_sent'] = {'revision': profile_revision} | next_profile
                        sent_at = time.monotonic()
                    else:
                        journal['profile_no_change'] = True
                if sent_at is not None and journal['profile_applied'] is None and 'profile_rejected' not in journal and now - sent_at > 3:
                    raise TimeoutError('native owner did not acknowledge profile at a boundary')
                if settled and now - ready_at >= args.serve_seconds and (sent_at is None or journal['profile_applied'] is not None or 'profile_rejected' in journal):
                    break
            selector.close()
            if registry:
                registry.close()
                journal['calibration'] = future.result()
                journal['calibration']['requests'] = registry.requests
                journal['calibration']['jobs'] = len(registry.jobs)
            send('stop')
            stop_deadline = time.monotonic() + 2
            os.set_blocking(process.stdout.fileno(), True)
            # communicate drains both remaining stdout and the command pipe.
            remaining, _ = process.communicate(timeout=max(.01, stop_deadline - time.monotonic()))
            for text in (buffer.decode() + remaining).splitlines():
                receive(json.loads(text), shutdown=True)
            if process.returncode != 0 or 'native_stopped' not in journal:
                raise RuntimeError('native service did not stop cleanly')
            journal['driver']['exit'] = process.returncode
            journal['status'] = 'cancelled' if stopping.is_set() else 'failed' if 'profile_rejected' in journal else 'complete'
    except Exception as error:
        journal['status'], journal['error'] = 'failed', str(error)
    finally:
        if registry:
            registry.close()
            if future.done():
                journal['calibration'] = future.result() | {'requests': registry.requests, 'jobs': len(registry.jobs)}
        stop_process(process)
        if process is not None:
            journal['driver']['exit'] = process.returncode
        journal['children_reaped'] = True
        journal['elapsed_seconds'] = time.monotonic() - started
        for sig, handler in old_handlers.items():
            signal.signal(sig, handler)
        args.journal.write_text(json.dumps(journal, indent=2) + '\n')
    return journal

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--probe', type=Path, required=True, help='Prebuilt memory_characterisation_probe; never built by this controller')
    parser.add_argument('--lookup', type=Path, default=MEMORY / 'lookup.json')
    parser.add_argument('--journal', type=Path, required=True)
    parser.add_argument('--cpu', type=int)
    parser.add_argument('--force-miss', action='store_true')
    parser.add_argument('--mib', type=int, default=64)
    parser.add_argument('--calibration-seconds', type=float, default=15)
    parser.add_argument('--serve-seconds', type=float, default=3, help='Minimum interval after native ready; service also waits through calibration')
    args = parser.parse_args(argv)
    if not hasattr(os, 'sched_getaffinity'):
        parser.error('Linux required; prefix with orb -m ubuntu from macOS')
    if not 16 <= args.mib <= 64 or not math.isfinite(args.calibration_seconds) or not .05 <= args.calibration_seconds <= 60 or not math.isfinite(args.serve_seconds) or not .05 <= args.serve_seconds <= 30:
        parser.error('bounded spike requires mib16..64, calibration-seconds0.05..60 and serve-seconds0.05..30')
    args.driver, args.probe, args.journal, args.lookup = (p.resolve() for p in (args.driver, args.probe, args.journal, args.lookup))
    result = run(args)
    print(json.dumps({'status': result['status'], 'journal': str(args.journal), 'performance_claim': False}))
    return 1 if result['status'] == 'failed' else 130 if result['status'] == 'cancelled' else 0

if __name__ == '__main__':
    raise SystemExit(main())
