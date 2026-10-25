#!/usr/bin/env python3
"""Fresh, external acquisition review. Run on Linux; only owns --work.

Fixtures are generated from independent bytes and tarfile, never from candidate
inspection results. Frozen candidate CLIs are subprocesses. No network/builds.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import random
import shutil
import signal
import stat
import subprocess
import sys
import tarfile
import time

HASHES = {
    'store.py': '4f0169e68785aa1dc5cbef1e7ce8ce269312b5f8585e897acb18dce5f7fa47d1',
    'acquire.py': '023bc2f88f0474bc41b9bea3f3d0281ea2157137bc13aeab11394dac1cbd511a',
}
CAP = 32 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encode(value):
    return json.dumps(value, sort_keys=True).encode()


def allocation(root):
    """Independent allocated-path oracle, including directories; no used()."""
    return sum(p.lstat().st_blocks * 512 for p in [root, *root.rglob('*')]) if root.exists() else 0


def content_map(root):
    result = {}
    for path in sorted((root / 'objects').glob('*')):
        assert path.is_file() and not path.is_symlink()
        result[path.name] = (sha(path.read_bytes()), path.stat().st_size)
    return result


def consumer(expected_path, ready=None, release=None):
    expected = json.loads(Path(expected_path).read_text())
    with os.fdopen(os.dup(int(os.environ['SIXDB_INPUTS_FD'])), 'r') as inp:
        mapping = json.load(inp)
    assert set(mapping) == set(expected), (set(mapping) ^ set(expected))
    for name, path in mapping.items():
        data = Path(path).read_bytes()
        assert [len(data), sha(data)] == expected[name], name
    if ready:
        Path(ready).write_text('ready')
        while not Path(release).exists():
            time.sleep(.01)
        for name, path in mapping.items():
            data = Path(path).read_bytes()
            assert [len(data), sha(data)] == expected[name], name
    print(json.dumps({'independent_consumer': len(mapping)}), flush=True)


class Review:
    def __init__(self, work, source):
        self.work, self.source = work, source
        self.frozen = work / 'frozen'
        self.results, self.sequence = [], 0
        self.work.mkdir(parents=True, exist_ok=True)
        self.frozen.mkdir(exist_ok=True)
        for name, expected in HASHES.items():
            target = self.frozen / name
            if not target.exists():
                data = (source / name).read_bytes()
                assert sha(data) == expected, (name, sha(data))
                target.write_bytes(data)
            assert sha(target.read_bytes()) == expected, name
        self.fixtures = work / 'fixtures'
        self.fixtures.mkdir(exist_ok=True)

    def check_cap(self):
        amount = allocation(self.work)
        assert amount <= CAP, ('fixture cap exceeded', amount)
        return amount

    def cli(self, kind, root, budget, *args, ok=True, env=None):
        argv = [sys.executable, str(self.frozen / (kind + '.py'))]
        if root is not None:
            argv += ['--root', str(root), '--budget', str(budget)]
        argv += list(map(str, args))
        process = subprocess.run(argv, capture_output=True, text=True, env=env, timeout=30)
        if ok:
            assert process.returncode == 0, (argv, process.returncode, process.stdout, process.stderr)
        else:
            assert process.returncode != 0, ('unexpected acceptance', argv, process.stdout)
        self.check_cap()
        lines = process.stdout.strip().splitlines()
        return process, json.loads(lines[-1]) if lines else None

    def archive(self, label, files, *, prefix='', records=None):
        archive = self.fixtures / (label + '.tar.gz')
        raw = io.BytesIO()
        with tarfile.open(fileobj=raw, mode='w', format=tarfile.PAX_FORMAT) as out:
            for name, data in files.items():
                item = tarfile.TarInfo(prefix + name)
                item.size = len(data)
                out.addfile(item, io.BytesIO(data))
            for item, data in records or []:
                out.addfile(item, io.BytesIO(data))
        archive.write_bytes(gzip.compress(raw.getvalue(), mtime=0))
        ref = {'format': 1, 'sha256': sha(archive.read_bytes()), 'bytes': archive.stat().st_size,
               'files': len(files) + len(records or [])}
        if prefix:
            ref['subdirectory'] = prefix.rstrip('/')
        reference = self.fixtures / (label + '.json')
        reference.write_bytes(encode(ref))
        self.check_cap()
        return reference, archive

    def manifest(self, label, files, *, prefix='', omitted=()):
        value = {'members': {prefix + name: {'bytes': len(data), 'sha256': sha(data),
                    'rebuildable': name in omitted} for name, data in files.items()}}
        path = self.fixtures / (label + '.manifest.json')
        path.write_bytes(encode(value))
        return path

    def expected(self, label, files):
        path = self.fixtures / (label + '.expected.json')
        path.write_bytes(encode({name: [len(data), sha(data)] for name, data in files.items()}))
        return path

    def run(self, label, root, budget, ref, archive, files, *selection):
        expected = self.expected(label, files)
        return self.cli('acquire', root, budget, 'run', ref, archive, *selection,
                        '--', sys.executable, str(Path(__file__).resolve()), '--consumer', expected)

    def case(self, name, test):
        start = time.monotonic()
        try:
            details = test()
            result = {'case': name, 'ok': not (isinstance(details, dict) and details.get('defeated')),
                      'seconds': round(time.monotonic() - start, 3),
                      'details': details}
        except Exception as error:
            result = {'case': name, 'ok': False, 'seconds': round(time.monotonic() - start, 3),
                      'error': repr(error)}
        self.results.append(result)
        (self.work / 'results.json').write_bytes(encode({'frozen': HASHES, 'cases': self.results,
                                                       'allocated_bytes': self.check_cap()}))
        print(json.dumps(result), flush=True)

    def ordinary(self):
        root, budget = self.work / 'ordinary-store', 512 * 1024
        shared = random.Random(9001).randbytes(65536)
        compiler = b'\x7fELF' + b'Z' * (1024 * 1024 - 4)
        allocations, inode = [], None
        for job in range(18):
            files = {'inputs/data.bin': shared, 'samples.json': encode({'job': job}),
                     'source.tar.gz': b'source-package', 'nested/source.tar.zst': b'nested-package',
                     'build/probe': compiler, 'probe.cpp': b'int main(){}\n'}
            ref, archive = self.archive('job-' + str(job), files)
            chosen = {name: files[name] for name in ('inputs/data.bin', 'samples.json', 'probe.cpp')}
            _, result = self.run('job-' + str(job), root, budget, ref, archive, chosen)
            assert set(result['omitted']) == {'source.tar.gz', 'nested/source.tar.zst', 'build/probe'}
            shared_path = root / 'objects' / sha(shared)
            if inode is None:
                inode = shared_path.stat().st_ino
            assert shared_path.stat().st_ino == inode, 'shared input physically replaced'
            assert shared_path.read_bytes() == shared
            allocated = allocation(root)
            assert allocated <= budget, (job, allocated)
            allocations.append(allocated)
        physical = content_map(root)
        assert all(name == digest for name, (digest, size) in physical.items())
        return {'jobs': 18, 'managed_peak_allocated': max(allocations),
                'last_allocated': allocations[-1], 'budget': budget,
                'shared_input_same_inode': True, 'retained_objects': len(physical)}

    def selection(self):
        files = {'readme.txt': b'hello', 'source.tar.gz': b'package',
                 'nested/source.tar.zst': b'inner', 'program': b'\x7fELF' + b'P' * (1024 * 1024 - 4)}
        ref, archive = self.archive('selection', files, prefix='study/')
        root, budget = self.work / 'selection-store', 2 * 1024 * 1024
        self.run('selected', root, budget, ref, archive,
                 {'source.tar.gz': files['source.tar.gz']}, '--file', 'source.tar.gz')
        self.run('full', root, budget, ref, archive, files, '--full')
        return {'subdirectory': 'study', 'explicit_source': True, 'full_members': len(files),
                'allocated': allocation(root), 'budget': budget}

    def wide_and_offline(self):
        files = {('nested/' + str(i).zfill(4) + '-' + 'n' * 180): b'same tiny payload'
                 for i in range(700)}
        ref, archive = self.archive('wide', files)
        manifest = self.manifest('wide', files)
        root, budget = self.work / 'wide-store', 256 * 1024
        self.run('wide-first', root, budget, ref, archive, files)
        parked = archive.with_suffix('.offline')
        archive.rename(parked)
        try:
            _, result = self.run('wide-offline', root, budget, ref, archive, files,
                                  '--manifest', manifest)
            assert result['cache_hit'] is True
        finally:
            parked.rename(archive)
        return {'members': len(files), 'mapping_bytes_lower_bound': sum(len(n) for n in files),
                'offline_cache_hit': True, 'objects': len(content_map(root)),
                'allocated': allocation(root), 'budget': budget}

    def manifest_authentication(self):
        files = {'data': b'certified payload', 'source.tar.gz': b'omitted source'}
        ref, archive = self.archive('proof', files)
        manifest = self.manifest('proof', files)
        root, budget = self.work / 'proof-store', 256 * 1024
        self.run('proof-prime', root, budget, ref, archive, {'data': files['data']})
        parked = archive.with_suffix('.offline')
        archive.rename(parked)
        refusals = []
        try:
            for attack in ('omitted-hash', 'omitted-name', 'policy', 'reference-hash', 'missing-policy'):
                value = json.loads(manifest.read_text())
                ref_value = json.loads(ref.read_text())
                if attack == 'omitted-hash':
                    value['members']['source.tar.gz']['sha256'] = '0' * 64
                elif attack == 'omitted-name':
                    value['members']['different'] = value['members'].pop('source.tar.gz')
                elif attack == 'policy':
                    value['members']['data']['rebuildable'] = True
                elif attack == 'reference-hash':
                    ref_value['sha256'] = '1' * 64
                else:
                    del value['members']['source.tar.gz']['rebuildable']
                hint = self.fixtures / ('attack-' + attack + '.manifest.json')
                hint.write_bytes(encode(value))
                attack_ref = self.fixtures / ('attack-' + attack + '.json')
                attack_ref.write_bytes(encode(ref_value))
                process, _ = self.cli('acquire', root, budget, 'acquire', attack_ref, archive,
                                      '--file', 'data', '--manifest', hint, ok=False)
                refusals.append({'attack': attack, 'error': process.stderr.strip()})
        finally:
            parked.rename(archive)
        assert (root / 'objects' / sha(files['data'])).read_bytes() == files['data']
        return {'forged_offline_hints_refused': refusals, 'accepted_content_unchanged': True}

    def malformed(self):
        attacks = []
        for label, name, kind in [('traversal', '../escape', tarfile.REGTYPE),
                                  ('absolute', '/tmp/escape', tarfile.REGTYPE),
                                  ('symlink', 'linked', tarfile.SYMTYPE),
                                  ('hardlink', 'linked', tarfile.LNKTYPE)]:
            item = tarfile.TarInfo(name)
            item.type = kind
            item.linkname = 'elsewhere' if kind != tarfile.REGTYPE else ''
            item.size = 1 if kind == tarfile.REGTYPE else 0
            attacks.append((label, {}, [(item, b'X' if item.size else b'')]))
        duplicate = tarfile.TarInfo('same'); duplicate.size = 1
        attacks.append(('duplicate', {'same': b'X'}, [(duplicate, b'Y')]))
        attacks.append(('parent-conflict', {'same': b'X', 'same/child': b'Y'}, []))
        results = []
        for label, files, records in attacks:
            ref, archive = self.archive('bad-' + label, files, records=records)
            root = self.work / ('bad-' + label + '-store')
            process, _ = self.cli('acquire', root, 256 * 1024, 'acquire', ref, archive, '--full', ok=False)
            assert not content_map(root)
            results.append({'attack': label, 'error': process.stderr.strip()})
        for label in ('gzip-crc', 'truncated'):
            ref, archive = self.archive('bad-' + label, {'data': b'payload'})
            raw = bytearray(archive.read_bytes())
            if label == 'gzip-crc':
                raw[-8] ^= 1
            else:
                del raw[-4:]
            archive.write_bytes(raw)
            value = json.loads(ref.read_text()); value.update(bytes=len(raw), sha256=sha(raw))
            ref.write_bytes(encode(value))
            root = self.work / ('bad-' + label + '-store')
            process, _ = self.cli('acquire', root, 256 * 1024, 'acquire', ref, archive, ok=False)
            assert not content_map(root)
            results.append({'attack': label, 'error': process.stderr.strip()})
        return {'malformed_refusals': results, 'published_payloads': 0}

    def recovery_mutation(self):
        files = {'data': random.Random(9).randbytes(65536)}
        ref, archive = self.archive('recovery', files)
        root, budget = self.work / 'recovery-store', 256 * 1024
        self.run('recovery-prime', root, budget, ref, archive, files)
        original = archive.read_bytes()
        changed = bytearray(original); changed[len(changed) // 2] ^= 1
        archive.write_bytes(changed)
        pressure = self.fixtures / 'pressure.bin'
        pressure.write_bytes(random.Random(10).randbytes(131072))
        pressure_sha = sha(pressure.read_bytes())
        process, _ = self.cli('store', root, budget, 'fetch', 'pressure', pressure,
                              '--sha256', pressure_sha, ok=False)
        assert (root / 'objects' / sha(files['data'])).read_bytes() == files['data']
        archive.write_bytes(original)
        self.cli('store', root, budget, 'fetch', 'pressure', pressure, '--sha256', pressure_sha)
        assert not (root / 'objects' / sha(files['data'])).exists(), 'pressure did not evict target'
        self.cli('store', root, budget, 'get', 'member:' + sha(files['data']), '--lease', 'recovered')
        assert (root / 'objects' / sha(files['data'])).read_bytes() == files['data']
        return {'corrupt_recovery_pressure_refused': process.stderr.strip(),
                'known_content_preserved': True, 'eviction_then_store_cli_recovery': True,
                'allocated': allocation(root), 'budget': budget}

    def local_corruption(self):
        files = {'data': b'original payload'}
        ref, archive = self.archive('corrupt-local', files)
        root, budget = self.work / 'corrupt-local-store', 256 * 1024
        self.run('corrupt-local-prime', root, budget, ref, archive, files)
        target = root / 'objects' / sha(files['data'])
        target.chmod(0o600); target.write_bytes(b'edited content!!')
        saved = target.read_bytes()
        process, _ = self.cli('acquire', root, budget, 'acquire', ref, archive, ok=False)
        assert target.read_bytes() == saved
        return {'local_change_refused': process.stderr.strip(), 'changed_content_preserved': True}

    def alternative_recovery(self):
        """A bad older backend must not prevent reclaiming a healthy alternative."""
        root, budget = self.work / 'alternative-store', 384 * 1024
        first = {'first': random.Random(101).randbytes(65536)}
        second = {'second': random.Random(102).randbytes(65536)}
        ref1, archive1 = self.archive('alternative-first', first)
        ref2, archive2 = self.archive('alternative-second', second)
        self.run('alternative-first', root, budget, ref1, archive1, first)
        self.run('alternative-second', root, budget, ref2, archive2, second)
        raw = bytearray(archive1.read_bytes()); raw[len(raw) // 2] ^= 1
        archive1.write_bytes(raw)
        pressure = self.fixtures / 'alternative-pressure.bin'
        pressure.write_bytes(random.Random(103).randbytes(196608))
        before = content_map(root)
        # Capture an actual ordinary store entry-point admission, not just an
        # expected refusal. The same bytes are then admitted via acquire as a
        # control proving usable capacity and healthy independent recovery.
        argv = [sys.executable, str(self.frozen / 'store.py'), '--root', str(root),
                '--budget', str(budget), 'fetch', 'pressure', str(pressure),
                '--sha256', sha(pressure.read_bytes())]
        proc = subprocess.run(argv, capture_output=True, text=True, timeout=10)
        after_store = content_map(root)
        third = {'third': pressure.read_bytes()}
        ref3, archive3 = self.archive('alternative-third', third)
        self.run('alternative-third', root, budget, ref3, archive3, third)
        assert (root / 'objects' / sha(first['first'])).read_bytes() == first['first']
        assert not (root / 'objects' / sha(second['second'])).exists()
        assert (root / 'objects' / sha(third['third'])).read_bytes() == third['third']
        return {'defeated': proc.returncode != 0, 'store_cli_returncode': proc.returncode,
                'store_cli_error_tail': '\n'.join(proc.stderr.strip().splitlines()[-4:]),
                'store_cli_left_both_payloads': before == after_store,
                'same_store_acquire_control_succeeded': True,
                'corrupt_backend_payload_preserved': True, 'healthy_alternative_evicted': True,
                'allocated': allocation(root), 'budget': budget}

    def metadata_churn(self):
        root, budget = self.work / 'churn-store', 256 * 1024
        anchor = self.fixtures / 'churn-anchor.bin'; anchor.write_bytes(b'unarchived unique anchor')
        self.cli('store', root, budget, 'put', 'unique-anchor', anchor)
        allocations, first = [], None
        for i in range(96):
            files = {'metric.json': encode({'job': i, 'value': i * i})}
            ref, archive = self.archive('churn-' + str(i), files)
            if first is None:
                first = (ref, archive, files)
            self.cli('acquire', root, budget, 'acquire', ref, archive)
            assert (root / 'objects' / sha(anchor.read_bytes())).read_bytes() == anchor.read_bytes()
            allocations.append(allocation(root))
            assert allocations[-1] <= budget
        catalog = json.loads((root / 'catalog.json').read_text())
        assert len(catalog['keys']) < 96, 'obsolete aliases were never retired'
        assert catalog['keys']['unique-anchor'] == sha(anchor.read_bytes())
        assert 'member:' + sha(first[2]['metric.json']) not in catalog['keys'], 'first alias not retired'
        self.run('churn-first-recovery', root, budget, first[0], first[1], first[2])
        return {'acquisitions': 96, 'peak_allocated': max(allocations), 'budget': budget,
                'last_catalog_keys': len(catalog['keys']), 'first_alias_retired_then_recovered': True,
                'unique_unarchived_anchor_preserved': True}

    def concurrent(self):
        root, budget = self.work / 'concurrent-store', 256 * 1024
        processes = []
        for i in range(5):
            files = {'data': random.Random(200 + i).randbytes(102400)}
            ref, archive = self.archive('concurrent-' + str(i), files)
            expected = self.expected('concurrent-' + str(i), files)
            argv = [sys.executable, str(self.frozen / 'acquire.py'), '--root', str(root),
                    '--budget', str(budget), 'run', str(ref), str(archive), '--', sys.executable,
                    str(Path(__file__).resolve()), '--consumer', str(expected)]
            processes.append(subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE))
        samples = []
        try:
            while any(p.poll() is None for p in processes):
                # The path walk races renames; retry only that observation.
                try:
                    used = allocation(root)
                    samples.append(used)
                    assert used <= budget, ('simultaneous admission exceeded budget', used)
                except FileNotFoundError:
                    pass
                time.sleep(.005)
            for proc in processes:
                out, err = proc.communicate(timeout=5)
                assert proc.returncode == 0, (proc.returncode, out, err)
                assert b'"independent_consumer": 1' in out, out
        finally:
            for proc in processes:
                if proc.poll() is None:
                    proc.kill(); proc.communicate(timeout=5)
        return {'simultaneous_scoped_consumers': len(processes),
                'sampled_peak_allocated': max(samples), 'samples': len(samples), 'budget': budget,
                'all_expected_payloads_read': True}

    def kill_publication(self):
        rows = []
        for hook in ('after-stage', 'after-object', 'after-catalog'):
            root, budget = self.work / ('kill-' + hook + '-store'), 512 * 1024
            old = {'old': random.Random(20).randbytes(32768)}
            new = {'new': random.Random(21).randbytes(32768)}
            old_ref, old_archive = self.archive('kill-' + hook + '-old', old)
            ref, archive = self.archive('kill-' + hook + '-new', new)
            self.cli('acquire', root, budget, 'acquire', old_ref, old_archive, '--pin', 'old-accepted')
            argv = [sys.executable, str(self.frozen / 'acquire.py'), '--root', str(root),
                    '--budget', str(budget), 'acquire', str(ref), str(archive)]
            proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                    env=dict(os.environ, SIXDB_STORAGE_TEST_STOP=hook))
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                status = Path('/proc') / str(proc.pid) / 'status'
                if status.exists() and '\nState:\tT' in status.read_text():
                    break
                assert proc.poll() is None, proc.communicate()
                time.sleep(.01)
            else:
                raise AssertionError('candidate did not reach hook: ' + hook)
            os.kill(proc.pid, signal.SIGKILL); proc.communicate(timeout=5)
            before = allocation(root)
            self.cli('store', root, budget, 'stats')
            assert (root / 'objects' / sha(old['old'])).read_bytes() == old['old']
            assert not list((root / 'staging').iterdir()) and not (root / 'pending.json').exists()
            present = (root / 'objects' / sha(new['new'])).exists()
            assert present == (hook == 'after-catalog')
            if present:
                assert (root / 'objects' / sha(new['new'])).read_bytes() == new['new']
            rows.append({'hook': hook, 'allocated_before_restart': before,
                         'allocated_after_restart': allocation(root), 'new_published': present,
                         'old_accepted_preserved': True})
        return rows

    def reader_kill(self):
        files = {'data': random.Random(31).randbytes(65536)}
        ref, archive = self.archive('reader', files)
        root, budget = self.work / 'reader-store', 256 * 1024
        expected = self.expected('reader', files)
        ready, release = self.fixtures / 'reader-ready', self.fixtures / 'reader-release'
        argv = [sys.executable, str(self.frozen / 'acquire.py'), '--root', str(root), '--budget', str(budget),
                'run', str(ref), str(archive), '--', sys.executable, str(Path(__file__).resolve()),
                '--consumer', str(expected), '--ready', str(ready), '--release', str(release)]
        reader = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        pressure = None
        try:
            deadline = time.monotonic() + 10
            while not ready.exists():
                assert reader.poll() is None, reader.communicate()
                assert time.monotonic() < deadline, 'consumer did not become ready'
                time.sleep(.01)
            payload = self.fixtures / 'reader-pressure.bin'
            payload.write_bytes(random.Random(32).randbytes(131072))
            pressure = subprocess.Popen([sys.executable, str(self.frozen / 'store.py'), '--root', str(root),
                    '--budget', str(budget), 'fetch', 'pressure', str(payload), '--sha256', sha(payload.read_bytes())],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            time.sleep(.15)
            assert pressure.poll() is None, 'admission crossed active reader scope'
            assert (root / 'objects' / sha(files['data'])).read_bytes() == files['data']
            os.kill(reader.pid, signal.SIGKILL); reader.communicate(timeout=5)
            out, err = pressure.communicate(timeout=10)
            assert pressure.returncode == 0, (out, err)
            assert not (root / 'objects' / sha(files['data'])).exists()
            return {'active_scope_blocked_pressure': True, 'wrapper_kill_released_scope': True,
                    'pressure_then_evicted_target': True, 'allocated': allocation(root), 'budget': budget}
        finally:
            for proc in (reader, pressure):
                if proc is not None and proc.poll() is None:
                    proc.kill(); proc.communicate(timeout=5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--work', type=Path)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--consumer', type=Path)
    parser.add_argument('--ready', type=Path)
    parser.add_argument('--release', type=Path)
    parser.add_argument('--case', action='append')
    args = parser.parse_args()
    if args.consumer:
        consumer(args.consumer, args.ready, args.release)
        return 0
    assert args.work is not None
    review = Review(args.work, args.source)
    prior = args.work / 'results.json'
    if prior.exists():
        assert args.case, 'use a fresh --work directory for a full run'
        review.results = json.loads(prior.read_text())['cases']
        assert not set(args.case).intersection(row['case'] for row in review.results), 'case already ran here'
    cases = [('ordinary-shared-slim', review.ordinary), ('selected-full-subdirectory', review.selection),
             ('wide-map-offline-hint', review.wide_and_offline),
             ('manifest-authentication', review.manifest_authentication), ('malformed-archives', review.malformed),
             ('recovery-mutation-and-recover', review.recovery_mutation), ('local-corruption', review.local_corruption),
             ('corrupt-backend-healthy-alternative', review.alternative_recovery),
             ('metadata-retirement', review.metadata_churn), ('simultaneous-admission', review.concurrent),
             ('killed-publication', review.kill_publication), ('killed-reader', review.reader_kill)]
    for name, function in cases:
        if not args.case or name in args.case:
            review.case(name, function)
    return int(any(not row['ok'] for row in review.results))


if __name__ == '__main__':
    sys.exit(main())
