#!/usr/bin/env python3
"""Independent local composed-workflow review, frozen 2026-10-25 candidate.

Run from Linux: python3 .../composed_workflow_v5.py [--phase NAME].
Only the private FIXTURE below may be mutated. No network, real cache or build.
The 32 MiB main image and 16 MiB minimal reproduction image are never mounted
outside the candidate's private scope; read-only debugfs and host stat are the
external storage/byte oracles. Main phases run ordinary, preservation, then disk.
"""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tarfile

REPO = Path(__file__).resolve().parents[4]
FIXTURE = REPO / 'build/reviews/storage-20261024/fresh-composed-workflow-v5'
CANDIDATE = FIXTURE / 'candidate'
POOL = FIXTURE / 'pool'
IMAGE = POOL / 'research.img'
MIB = 1024 * 1024
SIZE, BUDGET, CACHE = 32 * MIB, 33 * MIB, 4 * MIB
SHAS = {
    'store.py': 'b0a4c22be48aba7a19bd9bd61d86e30b90392bcc7999cd7a824175753122b771',
    'acquire.py': '7ff36f2abf2ef6e84d9fdaf544af89793aa6851ba51852b8519dc49927d32970',
    'workspace.py': 'fb2cc7522d873bdb90273e639c15212a65ecc2839870c59aeb4d7cc3252b542f',
    'flow.py': '122e1b75a5ece57c396d993a9e554108f0b704c0d1c660cd6d5a88af6004cfd6',
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def payload(label, size):
    # Deterministic, compressible fixtures whose expanded identities are checked
    # independently of both the archive parser and the candidate catalog.
    block = b''.join(hashlib.sha256((label + str(n)).encode()).digest() for n in range(512))
    return (block * ((size + len(block) - 1) // len(block)))[:size]


def write_json(path, value):
    path.write_text(json.dumps(value, sort_keys=True, indent=2) + '\n')


def execute(argv, *, limit=90):
    done = subprocess.run([str(a) for a in argv], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, timeout=limit)
    return {'code': done.returncode, 'stdout': done.stdout.decode(errors='replace'),
            'stderr': done.stderr.decode(errors='replace')}


def debug(path):
    done = subprocess.run(['sudo', '-n', 'debugfs', '-R', 'cat ' + path, str(IMAGE)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
    if 'File not found' in done.stderr.decode(errors='replace'):
        raise FileNotFoundError(path)
    return done.stdout


def image_facts():
    info = IMAGE.stat()
    assert info.st_size == SIZE and info.st_blocks * 512 == SIZE
    loops = execute(['sudo', '-n', 'losetup', '--associated', IMAGE])
    assert loops['code'] == 0 and not loops['stdout'].strip(), loops
    mounts = execute(['findmnt', '-n', '-S', IMAGE])
    assert mounts['code'] == 1 and not mounts['stdout'].strip(), mounts
    ls = execute(['sudo', '-n', 'debugfs', '-R', 'ls -l /work/cache/objects', IMAGE])
    objects = re.findall(r'\b[0-9a-f]{64}\b', ls['stdout'])
    return {'image_bytes': info.st_size, 'allocated_bytes': info.st_blocks * 512,
            'object_names': sorted(objects), 'loop_clear': True, 'mount_clear': True}


def workspace(command):
    return execute([sys.executable, CANDIDATE / 'workspace.py', '--pool', POOL,
                    '--budget', BUDGET, 'run', 'research', '--size', SIZE, '--', *command])


def flow(label, *, artifact='base', flags=(), expected=None, command=None):
    command = command or [sys.executable, FIXTURE / 'consumer.py',
                          json.dumps(expected, sort_keys=True)]
    result = execute([sys.executable, CANDIDATE / 'flow.py', '--pool', POOL,
                      '--budget', BUDGET, '--size', SIZE, '--cache-budget', CACHE,
                      'run', 'research', FIXTURE / (artifact + '.json'),
                      FIXTURE / (artifact + '.tgz'), '--output', label, *flags,
                      '--', *command])
    result['image'] = image_facts()
    if expected is not None and result['code'] == 0:
        receipt = json.loads(debug('/work/output/' + label + '/receipt.json'))
        assert receipt['members'] == expected, (label, receipt, expected)
        result['receipt'] = receipt
    return result


def manifest(members, policies=()):
    return {name: {'bytes': len(data), 'sha256': digest(data),
                   'rebuildable': name in policies} for name, data in members.items()}


def expected(members):
    return {name: digest(data) for name, data in members.items()}


def archive(label, members, policies=(), **reference_fields):
    out = io.BytesIO()
    with tarfile.open(fileobj=out, mode='w', format=tarfile.PAX_FORMAT) as tar:
        for name, data in members.items():
            item = tarfile.TarInfo(name)
            item.size = len(data)
            item.mtime = 0
            tar.addfile(item, io.BytesIO(data))
    compressed = gzip.compress(out.getvalue(), mtime=0)
    (FIXTURE / (label + '.tgz')).write_bytes(compressed)
    ref = dict(format=1, sha256=digest(compressed), bytes=len(compressed),
               files=len(members), **reference_fields)
    write_json(FIXTURE / (label + '.json'), ref)
    write_json(FIXTURE / (label + '.manifest.json'), {'artifact': ref,
               'members': manifest(members, policies)})


def initialize():
    FIXTURE.mkdir(parents=True, exist_ok=True)
    for name, wanted in SHAS.items():
        assert digest((CANDIDATE / name).read_bytes()) == wanted, name
    (FIXTURE / 'consumer.py').write_text('''import hashlib,json,os,sys\nfrom pathlib import Path\nmapping=json.load(os.fdopen(int(os.environ["SIXDB_INPUTS_FD"])))\nseen={name:hashlib.sha256(Path(path).read_bytes()).hexdigest() for name,path in mapping.items()}\nassert seen==json.loads(sys.argv[1]),(seen,sys.argv[1])\nreceipt={"members":seen,"paths":mapping}\nPath("receipt.json").write_text(json.dumps(receipt,sort_keys=True)+"\\n")\nprint(json.dumps({"verified":len(seen),"output":str(Path.cwd())}))\n''')
    base = {'data/column.bin': payload('column', MIB // 2),
            'data/same-column.bin': payload('column', MIB // 2),
            'measurement.json': b'{"value":47,"unit":"logical"}\n',
            'nested/source.tar.gz': payload('source', MIB),
            'build/probe': b'\x7fELF' + payload('binary', MIB - 4),
            'notes.o': b'ordinary unique text with an object suffix\n'}
    archive('base', base, ('build/probe',), uri='fixture://base')
    archive('job-variant', base, ('build/probe',), uri='fixture://different-job')
    archive('scoped', {'experiment/' + k: v for k, v in base.items()},
            ('experiment/build/probe',), subdirectory='experiment')
    for n in range(5):
        archive('pressure' + str(n), {'data.bin': payload('pressure' + str(n), MIB)})
    (FIXTURE / 'unique.bin').write_bytes(payload('unique', 384 * 1024))
    (FIXTURE / 'pin.bin').write_bytes(payload('pinned', 384 * 1024))
    external_bytes = sum(p.stat().st_blocks * 512 for p in FIXTURE.rglob('*')
                         if p.is_file() and not p.is_relative_to(POOL))
    assert external_bytes < 16 * MIB, external_bytes
    return base


def run(phase):
    global POOL, IMAGE, SIZE, BUDGET, CACHE
    if phase == 'overlap':
        POOL = FIXTURE / 'overlap-pool'
        IMAGE = POOL / 'research.img'
        SIZE, BUDGET, CACHE = 16 * MIB, 17 * MIB, 2 * MIB
    base = initialize()
    results = {}
    def record(label, value, success=True):
        results[label] = value
        write_json(FIXTURE / ('results-' + phase + '.json'), results)
        print(label + ': ' + str(value.get('code', 'checked')), flush=True)
        if success:
            assert value.get('code', 0) == 0, value

    normal = {k: v for k, v in base.items() if k not in ('nested/source.tar.gz', 'build/probe')}
    if phase == 'overlap':
        shared = payload('overlap', 65536)
        archive('overlap-A', {'data.bin': shared, 'result.json': b'{"job":"A"}\n'})
        archive('overlap-B', {'data.bin': shared, 'result.json': b'{"job":"B"}\n'})
        selected = expected({'data.bin': shared})
        selection = ['--file', 'data.bin']
        hint = selection + ['--manifest', FIXTURE / 'overlap-A.manifest.json']
        record('A-online', flow('A-online', artifact='overlap-A', flags=selection, expected=selected))
        live, offline = FIXTURE / 'overlap-A.tgz', FIXTURE / 'overlap-A.offline'
        live.rename(offline)
        try:
            record('A-offline-before-B', flow('A-offline-before-B', artifact='overlap-A',
                   flags=hint, expected=selected))
        finally:
            offline.rename(live)
        record('B-online', flow('B-online', artifact='overlap-B', flags=selection, expected=selected))
        live.rename(offline)
        try:
            record('A-offline-after-B', flow('A-offline-after-B', artifact='overlap-A',
                   flags=hint, expected=selected), False)
            assert results['A-offline-after-B']['code'] != 0
            assert debug('/work/cache/objects/' + digest(shared)) == shared
            assert results['A-offline-after-B']['image']['object_names'] == [digest(shared)]
        finally:
            offline.rename(live)
    elif phase == 'ordinary':
        record('default', flow('default', expected=expected(normal)))
        first = results['default']['image']['object_names']
        for n in range(6):
            label = 'repeat-' + str(n)
            record(label, flow(label, artifact='job-variant', expected=expected(normal)))
            assert results[label]['image']['object_names'] == first
        record('selected', flow('selected', flags=['--file', 'build/probe'],
                                expected=expected({'build/probe': base['build/probe']})))
        record('full', flow('full', flags=['--full'], expected=expected(base)))
        record('cache-alias-count', {'actual': len(results['full']['image']['object_names']),
                                    'expected': len(set(expected(base).values()))})
        assert len(results['full']['image']['object_names']) == len(set(expected(base).values()))
        offline = FIXTURE / 'base.offline'
        (FIXTURE / 'base.tgz').rename(offline)
        try:
            record('offline-proven-hint', flow('offline-proven-hint',
                   flags=['--manifest', FIXTURE / 'base.manifest.json'], expected=expected(normal)))
            record('offline-without-hint', flow('offline-without-hint', expected=expected(normal)), False)
            assert results['offline-without-hint']['code'] != 0
        finally:
            offline.rename(FIXTURE / 'base.tgz')
        record('scope', flow('scope', artifact='scoped', expected=expected(normal)))
        (FIXTURE / 'base.tgz').rename(offline)
        try:
            record('offline-after-overlap', flow('offline-after-overlap',
                   flags=['--manifest', FIXTURE / 'base.manifest.json'], expected=expected(normal)), False)
            assert results['offline-after-overlap']['code'] != 0
            for data in normal.values():
                assert debug('/work/cache/objects/' + digest(data)) == data
        finally:
            offline.rename(FIXTURE / 'base.tgz')
        for n in range(5):
            record('pressure' + str(n), flow('pressure' + str(n), artifact='pressure' + str(n),
                expected=expected({'data.bin': payload('pressure' + str(n), MIB)})))
        record('recover-full', flow('recover-full', flags=['--full'], expected=expected(base)))
    elif phase == 'compatibility':
        # Exact required fields of the compact reference for ordinary job
        # 20261021T130039Z-b8b3dd4e, read independently from operations/workers.json.
        # The URI is provenance only: no real archive is read or downloaded.
        large_ref = {'format': 1, 'files': 4918, 'bytes': 29392523,
                     'sha256': 'dd117b74e334c5a2f5baa07f23e90051b86a3801580bfc3f1b5ff099ea0e6d7e'}
        write_json(FIXTURE / 'historical-count.json', large_ref)
        record('historical-count-4918', execute([sys.executable, CANDIDATE / 'acquire.py',
               'inspect', FIXTURE / 'historical-count.json', FIXTURE / 'absent-historical-payload.tgz']), False)
        assert results['historical-count-4918']['code'] != 0
        assert 'invalid v1 compressed SHA/bytes/file count' in results['historical-count-4918']['stderr']
        large_ref['files'] = 4096
        write_json(FIXTURE / 'accepted-count-control.json', large_ref)
        record('count-4096-reaches-backend', execute([sys.executable, CANDIDATE / 'acquire.py',
               'inspect', FIXTURE / 'accepted-count-control.json', FIXTURE / 'absent-historical-payload.tgz']), False)
        assert 'No such file' in results['count-4096-reaches-backend']['stderr']
    elif phase == 'preservation':
        seed = '''import os,sys\nfrom pathlib import Path\nsys.path.insert(0,sys.argv[1])\nfrom store import Store\ns=Store(Path("cache"),int(sys.argv[2]))\ntry:\n s.ingest("unique-evidence",sys.argv[3])\n s.ingest("pinned-input",sys.argv[4],expected=sys.argv[5],lease="ongoing-study")\n p=s.object_path(sys.argv[6]); p.chmod(0o644); p.write_bytes(b"researcher-edit-preserve\\n")\nfinally:s.close()\n'''
        record('seed-unique-pin-edit', workspace([sys.executable, '-c', seed, CANDIDATE,
               CACHE, FIXTURE / 'unique.bin', FIXTURE / 'pin.bin',
               digest(payload('pinned', 384 * 1024)), digest(base['data/column.bin'])]))
        for n in range(5):
            record('preserved-progress' + str(n), flow('preserved-progress' + str(n),
               artifact='pressure' + str(n), expected=expected({'data.bin': payload('pressure' + str(n), MIB)})))
            for source_label, source in [('unique', payload('unique', 384 * 1024)),
                                          ('pinned', payload('pinned', 384 * 1024))]:
                assert debug('/work/cache/objects/' + digest(source)) == source, source_label
            assert debug('/work/cache/objects/' + digest(base['data/column.bin'])) == b'researcher-edit-preserve\n'
        record('changed-member-refused', flow('changed-member-refused', expected=expected(normal)), False)
        assert results['changed-member-refused']['code'] != 0
        assert debug('/work/cache/objects/' + digest(base['data/column.bin'])) == b'researcher-edit-preserve\n'
        huge = {'too-large.bin': payload('too-large', CACHE)}
        archive('too-large', huge)
        record('oversize-refused', flow('oversize-refused', artifact='too-large', expected=expected(huge)), False)
        assert results['oversize-refused']['code'] != 0
        record('progress-after-refusal', flow('progress-after-refusal', artifact='pressure4',
               expected=expected({'data.bin': payload('pressure4', MIB)})))
    elif phase == 'disk':
        before = debug('/work/output/default/receipt.json')
        fill = '''import errno,json,os\nfrom pathlib import Path\np=Path("output/disposable-pressure");p.parent.mkdir(exist_ok=True)\nf=p.open("wb",buffering=0);count=0\ntry:\n while count<32*1024*1024:\n  f.write(b"P"*32768);count+=32768\n os.fsync(f.fileno())\nexcept OSError as e:\n assert e.errno==errno.ENOSPC,e\nfinally:f.close()\nprint(json.dumps({"written":count}))\n'''
        record('fill-private-output', workspace([sys.executable, '-c', fill]))
        record('flow-disk-full', flow('flow-disk-full', artifact='pressure3',
               expected=expected({'data.bin': payload('pressure3', MIB)})), False)
        assert results['flow-disk-full']['code'] != 0
        assert debug('/work/output/default/receipt.json') == before
        record('release-own-pressure', workspace([sys.executable, '-c',
               'from pathlib import Path;Path("output/disposable-pressure").unlink()']))
        record('useful-work-after-space-release', flow('after-space-release', artifact='pressure3',
               expected=expected({'data.bin': payload('pressure3', MIB)})))
        assert debug('/work/output/default/receipt.json') == before
    else:
        raise ValueError(phase)
    write_json(FIXTURE / ('results-' + phase + '.json'), results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=['ordinary', 'preservation', 'disk', 'compatibility', 'overlap'], default='ordinary')
    run(parser.parse_args().phase)
