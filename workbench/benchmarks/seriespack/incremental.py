#!/usr/bin/env python3
"""Measure rebuild scope after representative edits, without changing source bytes."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
from build_evidence import build_summary

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
parser.add_argument('--isa', choices=['neon', 'avx2', 'avx512'], required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--jobs', type=int, default=2)
args = parser.parse_args()
root = Path(__file__).resolve().parents[3]
build = args.build.resolve()
output = args.output.resolve()
targets = ['ikea_seriespack_check', 'ikea_seriespack_ownership_check', 'ikea_examples', 'ikea_seriespack_bench']
command = ['cmake', '--build', str(build), '--target', *targets, '-j', str(args.jobs)]
subprocess.run(command, cwd=root, check=True)
artifacts = {
    'ikea_seriespack_check': 'ikea/ikea_seriespack_check',
    'ikea_seriespack_ownership_check': 'ikea/ikea_seriespack_ownership_check',
    'ikea_seriespack_bench': 'workbench/benchmarks/seriespack/ikea_seriespack_bench',
    'libikea_seriespack.a': 'ikea/libikea_seriespack.a',
}
def hashes():
    return {name: hashlib.sha256((build / relative).read_bytes()).hexdigest()
            for name, relative in artifacts.items()}
baseline = hashes()
results = []
for label, relative in [
    ('unchanged', None),
    ('admission-rule', 'src/seriespack/admission.cpp'),
    ('native-write', f'include/ikea/seriespack/detail/native/{args.isa}/write.h'),
    ('integration-adapter', 'examples/seriespack/integration.cpp')]:
    case = output / label
    case.mkdir(parents=True, exist_ok=False)
    shutil.copyfile(build / '.ninja_log', case / 'build-before.ninja_log')
    (case / 'build-state.txt').write_text(f'Warm maintained targets; touch {relative}\n')
    source = root / 'ikea' / relative if relative else None
    stat = source.stat() if source else None
    try:
        if source:
            # Only mtime changes. Restore it even when a build fails.
            os.utime(source, ns=(stat.st_atime_ns, time.time_ns()))
        with (case / 'build.log').open('w') as log:
            result = subprocess.run([
                '/usr/bin/time', '-f',
                'elapsed_seconds=%e\nuser_seconds=%U\nsystem_seconds=%S\nmax_process_rss_kib=%M\nexit_status=%x',
                '-o', str(case / 'build.resources'), *command], cwd=root, stdout=log,
                stderr=subprocess.STDOUT)
    finally:
        if source:
            os.utime(source, ns=(stat.st_atime_ns, stat.st_mtime_ns))
    shutil.copyfile(build / '.ninja_log', case / 'build-after.ninja_log')
    build_summary(case)
    row = json.loads((case / 'build-summary.json').read_text())
    row.update(scenario=label, source=relative, returncode=result.returncode,
               artifact_hashes=hashes())
    row['artifacts_unchanged'] = row['artifact_hashes'] == baseline
    results.append(row)
    (output / 'summary.json').write_text(json.dumps({
        'method': 'One warm rebuild per touched file; no source-byte change. Wall time includes relinks; '
                  'RSS is max single process, not concurrent total. Not a paired pre-refactor comparison.',
        'jobs': args.jobs, 'targets': targets, 'baseline_hashes': baseline,
        'scenarios': results}, indent=2) + '\n')
    if result.returncode:
        raise SystemExit(f'{label} failed; see {case}')
print(output / 'summary.json')
