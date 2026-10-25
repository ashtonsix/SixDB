#!/usr/bin/env python3
"""Independent tiny fixtures for the frozen storage lifecycle prototype.

Run on Linux. This tests regular-file recovery only, with no external services.
The implementation under test is copied once before any fixture is exercised.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time


REPORT = Path('build/reviews/storage-20261024/reader-metadata-v2').absolute()
BUDGET = 256 * 1024
EXPECTED_SOURCE = 'f8cbff0022b9eeed47d7731163cc13a0043aab5864551337f857743db31a1ee2'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def allocation(root):
    """Independently charge actual directory/file allocations once per inode."""
    seen = set()
    total = 0
    if not root.exists():
        return 0
    for directory, children, names in os.walk(root):
        for path in [Path(directory), *[Path(directory) / n for n in names]]:
            try:
                info = path.lstat()
            except FileNotFoundError:
                continue
            inode = (info.st_dev, info.st_ino)
            if inode not in seen:
                total += info.st_blocks * 512
                seen.add(inode)
    return total


class Fixture:
    def __init__(self, name, frozen):
        self.name = name
        self.base = Path(tempfile.mkdtemp(prefix=name + '-', dir=REPORT))
        self.root = self.base / 'managed'
        self.sources = self.base / 'sources'
        self.sources.mkdir()
        self.frozen = frozen
        self.expected = {}
        self.processes = []
        self.peak = 0
        self.stop = threading.Event()
        self.monitor = threading.Thread(target=self.sample, daemon=True)
        self.monitor.start()

    def sample(self):
        while not self.stop.wait(0.001):
            self.peak = max(self.peak, allocation(self.root))

    def command(self, *args):
        return [sys.executable, str(self.frozen), '--root', str(self.root),
                '--budget', str(BUDGET), *map(str, args)]

    def start(self, *args):
        p = subprocess.Popen(self.command(*args), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True)
        self.processes.append(p)
        return p

    def run(self, *args, ok=True):
        p = subprocess.run(self.command(*args), text=True, capture_output=True, timeout=15)
        self.peak = max(self.peak, allocation(self.root))
        if ok and p.returncode != 0:
            raise AssertionError(f'command {args} failed ({p.returncode}): {p.stderr}')
        if not ok and p.returncode == 0:
            raise AssertionError(f'command {args} unexpectedly succeeded: {p.stdout}')
        return json.loads(p.stdout if p.returncode == 0 else p.stderr)

    def source(self, label, size):
        data = hashlib.shake_256(label.encode()).digest(size)
        path = self.sources / label
        path.write_bytes(data)
        digest = sha(data)
        self.expected[digest] = data
        return path, digest

    def fetch(self, key, path, digest, **kwargs):
        options = ['--lease', kwargs['lease']] if 'lease' in kwargs else []
        result = self.run('fetch', key, path, '--sha256', digest, *options)
        assert result['sha256'] == digest
        self.verify()
        return result

    def verify(self):
        current = allocation(self.root)
        self.peak = max(self.peak, current)
        assert current <= BUDGET, (current, BUDGET)
        for path in (self.root / 'objects').iterdir():
            assert path.read_bytes() == self.expected[path.name], path
        assert allocation(self.base) <= 16 * 1024 * 1024, self.base

    def catalog(self):
        return json.loads((self.root / 'catalog.json').read_text())

    def finish(self):
        (self.base / 'release-reader').touch()
        for p in self.processes:
            if p.poll() is None:
                p.kill()
            try:
                p.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        self.stop.set()
        self.monitor.join()
        assert self.peak <= BUDGET, ('observed managed allocation above budget', self.peak)


def wait_file(path, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            try:
                return json.loads(path.read_text())
            except json.JSONDecodeError:
                pass
        time.sleep(0.01)
    raise AssertionError(f'timed out waiting for {path}')


READER = r'''
import hashlib, json, os, pathlib, sys, time
base = pathlib.Path(sys.argv[1])
mode = sys.argv[2]
paths = json.loads(os.environ['SIXDB_INPUTS_JSON'])
if mode == 'forked':
    if os.fork():
        deadline = time.monotonic() + 30
        while not (base / 'release-command').exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        sys.exit(0)
if mode in ('close-fds', 'forked'):
    # Ordinary daemon/consumer descriptor hygiene; the lock fd is undocumented.
    os.closerange(3, 256)
streams = {key: open(path, 'rb') for key, path in paths.items()}
before = {key: hashlib.sha256(inp.read()).hexdigest() for key, inp in streams.items()}
(base / 'ready').write_text(json.dumps({'pid': os.getpid(), 'paths': paths,
    'fds': {key: inp.fileno() for key, inp in streams.items()}, 'before': before}))
deadline = time.monotonic() + 30
while not (base / 'release-reader').exists() and time.monotonic() < deadline:
    time.sleep(0.01)
after = {}
for key, inp in streams.items():
    inp.seek(0)
    after[key] = hashlib.sha256(inp.read()).hexdigest()
(base / 'done').write_text(json.dumps({'before': before, 'after': after,
    'paths_exist': {key: pathlib.Path(path).exists() for key, path in paths.items()}}))
'''


def start_reader(f, keys, mode='ordinary'):
    wrapper = f.start('use', *keys, '--', sys.executable, '-c', READER, f.base, mode)
    ready = wait_file(f.base / 'ready')
    # A revised implementation may put the reader in a PID namespace. Resolve
    # its host PID from this fixture's subprocess tree rather than trusting the
    # PID as observed inside that namespace.
    queue = [wrapper.pid]
    while queue:
        pid = queue.pop()
        try:
            argv = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
            status = Path(f'/proc/{pid}/status').read_text()
            namespace_pids = next(line.split()[1:] for line in status.splitlines()
                                  if line.startswith('NSpid:'))
            if (len(argv) >= 5 and argv[1:4] == [b'-c', READER.encode(), str(f.base).encode()]
                    and int(namespace_pids[-1]) == ready['pid']):
                ready['namespace_pid'] = ready['pid']
                ready['pid'] = pid
                break
            queue.extend(map(int, Path(f'/proc/{pid}/task/{pid}/children').read_text().split()))
        except FileNotFoundError:
            pass
    else:
        raise AssertionError('ready reader not found in fixture subprocess tree')
    return wrapper, ready


def alive(pid):
    try:
        return Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[0] != 'Z'
    except FileNotFoundError:
        return False


def await_reader_death(pid, timeout=0.5):
    deadline = time.monotonic() + timeout
    while alive(pid) and time.monotonic() < deadline:
        time.sleep(0.01)
    return not alive(pid)


def finish_reader(f, wrapper, ready, killed=False):
    (f.base / 'release-reader').touch()
    done = wait_file(f.base / 'done')
    assert done['before'] == done['after']
    if not killed:
        out, err = wrapper.communicate(timeout=5)
        assert wrapper.returncode == 0, err
        assert json.loads(out)['returncode'] == 0
    return done


def churn(f):
    unique, digest = f.source('accepted-unique', 24 * 1024)
    accepted = f.run('put', 'unique', unique)
    assert accepted['sha256'] == digest
    pinned_src, pinned_digest = f.source('persistent-pin', 12 * 1024)
    f.fetch('pinned-through-retirement', pinned_src, pinned_digest, lease='sticky-pin')
    retired = set()
    seen = set()
    for i in range(220):
        src, value = f.source(f'changing-{i:03}', 12 * 1024)
        key = f'input-{i:03}-' + ('k' * 160)
        f.fetch(key, src, value)
        seen.add(key)
        if i % 3 == 0:
            f.fetch('overlapping-alias', src, value)
            f.fetch(f'alias-{i % 4}', src, value)
        cat = f.catalog()
        retired |= seen - set(cat['keys'])
        assert Path(accepted['path']).read_bytes() == f.expected[digest]
        assert cat['objects'][digest]['recovery'] is None
        assert cat['keys']['pinned-through-retirement'] == pinned_digest
        assert (f.root / 'objects' / pinned_digest).read_bytes() == f.expected[pinned_digest]
    assert retired, 'fixture did not force metadata retirement'
    last = f'input-{219:03}-' + ('k' * 160)
    assert f.run('get', last)['sha256'] == value
    assert f.run('get', 'unique')['sha256'] == digest
    assert f.catalog()['leases'] == {'sticky-pin': [pinned_digest]}
    f.run('release', 'sticky-pin')
    assert not f.catalog()['leases']
    f.verify()
    return {'accepted_fetches': 220, 'retired_aliases': len(retired),
            'catalog_bytes': (f.root / 'catalog.json').stat().st_size,
            'final_keys': len(f.catalog()['keys']), 'continued_progress': True,
            'persistent_pin_survived_metadata_retirement': True}


def archive(f):
    src, digest = f.source('unique', 112 * 1024)
    accepted = f.run('put', 'unique', src)
    pressure, value = f.source('pressure', 112 * 1024)
    refused = f.run('fetch', 'pressure', pressure, '--sha256', value, ok=False)
    assert 'capacity refused' in refused['error']
    assert Path(accepted['path']).read_bytes() == f.expected[digest]
    f.run('archive', 'unique', pressure, '--sha256', digest, ok=False)
    assert Path(accepted['path']).read_bytes() == f.expected[digest]
    f.run('archive', 'unique', src, '--sha256', digest)
    f.fetch('pressure', pressure, value)
    assert not Path(accepted['path']).exists()
    f.run('get', 'unique')
    f.verify()
    return {'unique_preserved_on_refusal': True, 'bad_archive_preserved_unique': True,
            'archive_enabled_eviction_and_recovery': True}


def pins(f):
    src, digest = f.source('pinned', 112 * 1024)
    f.fetch('pinned-alias', src, digest, lease='explicit-owner')
    pressure, value = f.source('pressure', 112 * 1024)
    refusal = f.run('fetch', 'pressure', pressure, '--sha256', value, ok=False)
    assert 'capacity refused' in refusal['error']
    assert (f.root / 'objects' / digest).read_bytes() == f.expected[digest]
    cat = f.catalog()
    assert cat['leases'] == {'explicit-owner': [digest]}
    assert cat['pin_keys'] == {'explicit-owner': ['pinned-alias']}
    f.run('release', 'explicit-owner')
    f.fetch('pressure', pressure, value)
    assert not (f.root / 'objects' / digest).exists()
    assert not f.catalog()['leases']
    return {'pin_persisted_across_commands': True, 'release_restored_progress': True}


def scoped(f, kill_wrapper=False):
    for key in ['a', 'b']:
        src, digest = f.source(key, 32 * 1024)
        f.fetch(key, src, digest)
    wrapper, ready = start_reader(f, ['a', 'b'])
    assert not f.catalog()['leases']
    if kill_wrapper:
        wrapper.kill()
        wrapper.wait(timeout=5)
        if await_reader_death(ready['pid']):
            pressure, digest = f.source('pressure', 144 * 1024)
            f.fetch('pressure', pressure, digest)
            assert not f.catalog()['leases']
            return {'wrapper_killed': True, 'reader_terminated_after_wrapper_failure': True,
                    'writer_progress_after_reader_death': True, 'no_persistent_pin': True}
    pressure, digest = f.source('pressure', 144 * 1024)
    contender = f.start('fetch', 'pressure', pressure, '--sha256', digest)
    time.sleep(0.3)
    assert contender.poll() is None, 'concurrent writer entered during scoped reader'
    assert all(Path(path).exists() for path in ready['paths'].values())
    done = finish_reader(f, wrapper, ready, killed=kill_wrapper)
    out, err = contender.communicate(timeout=5)
    assert contender.returncode == 0, err
    assert json.loads(out)['sha256'] == digest
    assert not f.catalog()['leases']
    f.verify()
    return {'wrapper_killed': kill_wrapper, 'writer_blocked_while_reader_live': True,
            'contents_after_pressure': done['after'], 'no_persistent_pin': True}


def closed_descriptor_reader(f):
    src, digest = f.source('held-reader-input', 112 * 1024)
    f.fetch('input', src, digest)
    wrapper, ready = start_reader(f, ['input'], 'close-fds')
    child_pid = ready['pid']
    child_fd = ready['fds']['input']
    wrapper.kill()
    wrapper.wait(timeout=5)
    if await_reader_death(child_pid):
        pressure, value = f.source('pressure', 160 * 1024)
        f.fetch('pressure', pressure, value)
        return {'reader_terminated_after_wrapper_failure': True,
                'closed_descriptor_reader_cannot_retain_deleted_input': True,
                'post_failure_allocated': allocation(f.root), 'budget': BUDGET}
    # The direct child remains alive with its input open after wrapper failure.
    os.kill(child_pid, 0)
    pressure, value = f.source('pressure', 160 * 1024)
    accepted = f.fetch('pressure', pressure, value)
    held = Path(f'/proc/{child_pid}/fd/{child_fd}')
    link = os.readlink(held)
    retained = held.stat().st_blocks * 512
    visible = allocation(f.root)
    child_alive = alive(child_pid)
    done = finish_reader(f, wrapper, ready, killed=True)
    assert done['before']['input'] == digest == done['after']['input']
    assert not done['paths_exist']['input']
    assert link.endswith(' (deleted)')
    assert visible + retained > BUDGET
    return {'finding': 'live direct consumer loses protection after closing inherited descriptors',
            'wrapper_exit': wrapper.returncode, 'direct_child_alive_during_eviction': child_alive,
            'pressure_acknowledged': accepted['sha256'], 'managed_visible_allocated': visible,
            'retained_unlinked_allocated': retained, 'physical_owned_total': visible + retained,
            'budget': BUDGET, 'consumer_fd': link, 'original_bytes_still_readable': True}


def replacement_recovery(f):
    old, digest = f.source('old-recovery', 112 * 1024)
    f.fetch('input', old, digest)
    new = f.sources / 'replacement-recovery'
    new.write_bytes(old.read_bytes())
    old.unlink()
    accepted = f.fetch('input', new, digest)
    pressure, value = f.source('pressure', 112 * 1024)
    recovery = f.catalog()['objects'][digest]['recovery']
    if recovery == str(new):
        f.fetch('pressure', pressure, value)
        return {'fresh_recovery_reference_refreshed': True, 'pressure_progress': True}
    refusal = f.run('fetch', 'pressure', pressure, '--sha256', value, ok=False)
    assert 'capacity refused' in refusal['error']
    assert recovery == str(old)
    assert sha(new.read_bytes()) == accepted['sha256']
    return {'finding': 'successful fetch does not refresh a stale recovery reference',
            'accepted_fresh_recovery': str(new), 'catalog_recovery': recovery,
            'fresh_recovery_exists': new.exists(), 'pressure_refusal': refusal['error']}


def descendant_exit(f):
    src, digest = f.source('descendant-input', 112 * 1024)
    f.fetch('input', src, digest)
    wrapper, ready = start_reader(f, ['input'], 'forked')
    (f.base / 'release-command').touch()
    wrapper.wait(timeout=5)
    assert wrapper.returncode == 0
    descendant_dead = await_reader_death(ready['pid'])
    pressure, value = f.source('pressure', 160 * 1024)
    f.fetch('pressure', pressure, value)
    if descendant_dead:
        return {'forked_reader_terminated_on_normal_command_exit': True,
                'writer_progress_after_descendant_death': True,
                'post_exit_allocated': allocation(f.root)}
    held = Path(f"/proc/{ready['pid']}/fd/{ready['fds']['input']}")
    retained = held.stat().st_blocks * 512
    visible = allocation(f.root)
    assert os.readlink(held).endswith(' (deleted)')
    done = finish_reader(f, wrapper, ready)
    assert done['before'] == done['after']
    assert visible + retained > BUDGET
    return {'finding': 'forked reader survives successful command scope and retains deleted bytes',
            'visible_allocated': visible, 'retained_unlinked_allocated': retained,
            'physical_owned_total': visible + retained, 'budget': BUDGET}


def child_failure(f):
    src, digest = f.source('input', 1024)
    f.fetch('input', src, digest)
    wrapper = f.start('use', 'input', '--', sys.executable, '-c', 'raise SystemExit(7)')
    out, err = wrapper.communicate(timeout=5)
    result = json.loads(out)
    assert result['returncode'] == 7
    assert not f.catalog()['leases']
    return {'wrapper_exit': wrapper.returncode, 'reported_command_exit': result['returncode'],
            'child_exit_propagated': wrapper.returncode == 7}


def multi_alias_retirement(f):
    src, digest = f.source('shared-input', 1024)
    keys = []
    for i in range(100):
        key = f'key-{i:03}-' + 'a' * 220
        f.fetch(key, src, digest)
        keys.append(key)
        if (f.root / 'catalog.json').stat().st_size > BUDGET // 16:
            break
    else:
        raise AssertionError('fixture did not cross metadata retirement threshold')
    before = f.catalog()
    wanted = [keys[-1], keys[0]]
    assert all(key in before['keys'] for key in wanted)
    p = subprocess.run(f.command('use', *wanted, '--', sys.executable, '-c', 'pass'),
                       text=True, capture_output=True, timeout=15)
    if p.returncode == 0:
        assert all(key in f.catalog()['keys'] for key in wanted)
        return {'both_requested_aliases_preserved': True, 'consumer_started': True}
    result = json.loads(p.stderr)
    assert keys[0] not in f.catalog()['keys']
    return {'finding': 'metadata retirement deletes a later alias in a valid multi-key use',
            'both_requested_aliases_existed_before_use': True,
            'consumer_started': False, 'error': result['error'],
            'aliases_before': len(before['keys']),
            'aliases_after': len(f.catalog()['keys'])}


CASES = {'churn': churn, 'archive': archive, 'pins': pins,
         'scoped': scoped, 'wrapper-kill': lambda f: scoped(f, True),
         'closed-descriptor-reader': closed_descriptor_reader,
         'replacement-recovery': replacement_recovery, 'child-failure': child_failure,
         'multi-alias-retirement': multi_alias_retirement,
         'descendant-exit': descendant_exit}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--case', choices=CASES)
    parser.add_argument('--frozen', type=Path, default=REPORT / 'store.frozen.py')
    parser.add_argument('--sha256', default=EXPECTED_SOURCE)
    args = parser.parse_args()
    REPORT.mkdir(parents=True, exist_ok=True)
    frozen = args.frozen.absolute()
    assert sha(frozen.read_bytes()) == args.sha256, 'frozen source differs from reviewed source'
    results = {'source_sha256': args.sha256, 'budget': BUDGET, 'cases': {}}
    for name in ([args.case] if args.case else CASES):
        f = Fixture(name, frozen)
        try:
            details = CASES[name](f)
            results['cases'][name] = {'status': 'observed', **details}
        except Exception as error:
            results['cases'][name] = {'status': 'test-failed', 'error': repr(error)}
        finally:
            try:
                f.finish()
            except Exception as error:
                results['cases'][name]['cleanup_error'] = repr(error)
            results['cases'][name]['peak_visible_allocation'] = f.peak
            results['cases'][name]['fixture'] = str(f.base)
        print(json.dumps({name: results['cases'][name]}, sort_keys=True), flush=True)
    report = REPORT / ('results-' + args.sha256[:8] + '-' + (args.case or 'all') + '.json')
    report.write_text(json.dumps(results, indent=2, sort_keys=True) + '\n')
    return int(any(case['status'] == 'test-failed' or 'cleanup_error' in case
                   for case in results['cases'].values()))


if __name__ == '__main__':
    sys.exit(main())
