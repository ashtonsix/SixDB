#!/usr/bin/env bash
# Compare frozen sources through the existing Loom allocation consumer.
# Usage: run-public.sh BASELINE_CAPTURE CANDIDATE_CAPTURE [OUTPUT]
set -euo pipefail
if [[ $# -lt 2 || $# -gt 3 ]]; then
  echo "Usage: $0 BASELINE_CAPTURE CANDIDATE_CAPTURE [OUTPUT]" >&2
  exit 2
fi
python3 - "$1" "$2" "${3:-${SIXDB_RESULTS:-build/allocation-palettes}/public}" <<'PY'
import csv, hashlib, json, os, platform, shutil, statistics, subprocess, sys
from pathlib import Path

sources = dict(zip(('baseline', 'candidate'), (Path(p).resolve() for p in sys.argv[1:3])))
out = Path(sys.argv[3]).resolve()
out.mkdir(parents=True, exist_ok=False)
driver = Path('workbench/spikes/allocation-reuse-temperature/allocator.cpp')
target = 'allocation_reuse_temperature_allocator'
relative_binary = Path('workbench/spikes/allocation-reuse-temperature') / target
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
assert sha(sources['baseline'] / driver) == sha(sources['candidate'] / driver), 'consumer sources differ'

cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0))))
assert cpu in os.sched_getaffinity(0), 'requested CPU is outside allowed affinity'
page = os.sysconf('SC_PAGESIZE')
expected_page = int(os.environ.get('PALETTE_PUBLIC_EXPECTED_PAGE', page))
assert page == expected_page, f'expected native page {expected_page}, observed {page}'
defaults = {'x86_64': ('znver5', 'zen5'), 'aarch64': ('armv8.2-a+simd', 'neoverse-v2')}
default_march, default_tune = defaults[platform.machine()]
march = os.environ.get('PALETTE_PUBLIC_MARCH', default_march)
tune = os.environ.get('PALETTE_PUBLIC_TUNE', default_tune)
trials = int(os.environ.get('PALETTE_PUBLIC_TRIALS', 3))
rounds = int(os.environ.get('PALETTE_PUBLIC_ROUNDS', 8))
operations = int(os.environ.get('PALETTE_PUBLIC_OPERATIONS', 65536))
slots = int(os.environ.get('PALETTE_PUBLIC_SLOTS', 256))
warmup = int(os.environ.get('PALETTE_PUBLIC_WARMUP', 3))
workloads = os.environ.get('PALETTE_PUBLIC_WORKLOADS', 'finite-control,finite-fallback,hot-loop,mixed-sizes').split(',')
widths = os.environ.get('PALETTE_PUBLIC_TOUCH', '64,full').split(',')
assert min(trials, rounds, operations, slots) > 0 and warmup >= 0
assert workloads and all(w in ('finite-control', 'finite-fallback', 'hot-loop', 'mixed-sizes') for w in workloads)
assert widths and all(w in ('64', 'full') for w in widths)

def inputs(source):
    # Inventory both owner modules and build settings. Test sources are recorded
    # but this recipe builds only the existing consumer and its libraries.
    files = [source / 'CMakeLists.txt', source / driver]
    for folder in ('cmake', 'orbital', 'loom'):
        files += [p for p in (source / folder).rglob('*') if p.is_file() and
                  (p.suffix in ('.cpp', '.h', '.hpp', '.inc', '.cmake') or p.name == 'CMakeLists.txt')]
    return {str(p.relative_to(source)): sha(p) for p in sorted(set(files))}

context = dict(kind='Existing public Loom allocate/touch/release consumer', native_page=page, cpu=cpu,
    system=platform.platform(), compiler=subprocess.check_output(['clang++-21', '--version'], text=True),
    march=march, tune=tune, trials=trials, rounds=rounds, operations=operations, slots=slots, warmup=warmup,
    workloads=workloads, touch_widths=widths, driver_sha256=sha(sources['baseline'] / driver),
    sources={}, binaries={}, executions=[],
    limits=['Block means are not individual-operation latency percentiles',
            'Same consumer, toolchain and CPU; platform results are not pooled',
            'Window controls use unchanged 3KiB finite cutoff',
            'Compiler input inventory includes unbuilt owner tests'])
for label, source in sources.items():
    before = inputs(source)
    receipt = source.with_name(source.name + '.capture.json')
    source_context = dict(path=str(source), input_sha256=before)
    if receipt.exists():
        captured = json.loads(receipt.read_text())
        assert hashlib.sha256(json.dumps(captured['source_files_sha256'], sort_keys=True).encode()).hexdigest() == captured['source_digest'], 'invalid capture digest'
        for name, digest in before.items():
            assert captured['source_files_sha256'].get(name) == digest, f'capture changed: {label}/{name}'
        source_context.update(capture_source_digest=captured['source_digest'], capture_receipt_sha256=sha(receipt))
        shutil.copy2(receipt, out / f'{label}.capture.json')
    elif label == 'candidate' and os.environ.get('SIXDB_RESULTS'):
        manifest = Path(os.environ['SIXDB_RESULTS']) / 'source-manifest.json'
        if manifest.exists():
            captured = json.loads(manifest.read_text())
            for name, digest in before.items():
                assert captured.get(name) == digest, f'worker source changed: {name}'
            source_context.update(worker_source_digest=hashlib.sha256(json.dumps(captured, sort_keys=True).encode()).hexdigest(),
                                  worker_manifest_sha256=sha(manifest), worker_job=os.environ.get('SIXDB_JOB'))
    context['sources'][label] = source_context
    build = out / f'build-{label}'
    configure = ['cmake', '-S', str(source), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
                 '-DSIXDB_SPIKES=allocation-reuse-temperature', f'-DSIXDB_MARCH={march}', f'-DSIXDB_TUNE={tune}']
    compile = ['cmake', '--build', str(build), '--target', target, '-j', os.environ.get('SIXDB_BUILD_JOBS', '1')]
    for name, command in (('configure', configure), ('build', compile)):
        with (out / f'{label}-{name}.log').open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    assert before == inputs(source), f'{label} source changed during build'
    binary = out / f'{label}-allocator'
    shutil.copy2(build / relative_binary, binary)
    shutil.copy2(build / 'compile_commands.json', out / f'{label}-compile-commands.json')
    context['binaries'][label] = dict(path=str(binary), sha256=sha(binary), configure=configure, build=compile)
    print(f'built {label}: {context["binaries"][label]["sha256"]}', flush=True)
baseline_inputs = context['sources']['baseline']['input_sha256']
candidate_inputs = context['sources']['candidate']['input_sha256']
context['changed_inputs'] = sorted(name for name in baseline_inputs.keys() | candidate_inputs.keys()
                                  if baseline_inputs.get(name) != candidate_inputs.get(name))
(out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
rows = []
for trial in range(trials):
    order = ('baseline', 'candidate') if trial % 2 == 0 else ('candidate', 'baseline')
    for workload in workloads:
        for width in widths:
            for label in order:
                binary_context = context['binaries'][label]
                binary = Path(binary_context['path'])
                assert sha(binary) == binary_context['sha256'], 'captured executable changed'
                stem = f'{label}-{trial}-{workload}-touch{width}'
                command = [str(binary), '--cpu', str(cpu), '--slots', str(slots), '--rounds', str(rounds),
                           '--operations', str(operations), '--warmup', str(warmup),
                           '--workload', workload, '--touch-bytes', width]
                with (out / (stem + '.csv')).open('w') as output, (out / (stem + '.log')).open('w') as log:
                    subprocess.run(command, stdout=output, stderr=log, check=True)
                assert sha(binary) == binary_context['sha256'], 'captured executable changed'
                samples = list(csv.DictReader((out / (stem + '.csv')).open()))
                assert len(samples) == rounds and all(int(r['allocations']) == operations for r in samples)
                rows += [dict(policy=label, trial=trial, binary_sha256=binary_context['sha256'], **r) for r in samples]
                context['executions'].append(dict(policy=label, trial=trial, workload=workload, touch=width,
                                                  command=command, csv=stem + '.csv', log=stem + '.log'))
                with (out / 'samples.csv').open('w') as stream:
                    writer = csv.DictWriter(stream, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
                (out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
                print(f'trial{trial} {label} {workload} touch{width}', flush=True)
summaries = []
metrics = ('block_mean_ns_per_op', 'reuses', 'new_allocations', 'mapped_bytes', 'idle_bytes', 'trim_pages',
           'mmap_calls', 'munmap_calls', 'mprotect_calls')
for workload, width in sorted({(r['workload'], r['touch_bytes']) for r in rows}):
    for label in ('baseline', 'candidate'):
        matched = [r for r in rows if r['workload'] == workload and r['touch_bytes'] == width and r['policy'] == label]
        record = dict(workload=workload, touch_bytes=width, policy=label, trials=trials, samples=len(matched))
        for metric in metrics:
            trial_medians = [statistics.median(float(r[metric]) for r in matched if int(r['trial']) == trial) for trial in range(trials)]
            record[metric] = statistics.median(trial_medians)
            if metric == 'block_mean_ns_per_op':
                record['trial_median_min_ns'] = min(trial_medians)
                record['trial_median_max_ns'] = max(trial_medians)
        summaries.append(record)
for record in summaries:
    baseline = next(r for r in summaries if r['workload'] == record['workload'] and r['touch_bytes'] == record['touch_bytes'] and r['policy'] == 'baseline')
    record['ratio_to_baseline'] = record['block_mean_ns_per_op'] / baseline['block_mean_ns_per_op']
with (out / 'summary.csv').open('w') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(summaries[0])); writer.writeheader(); writer.writerows(summaries)
for label, source in sources.items():
    assert context['sources'][label]['input_sha256'] == inputs(source), f'{label} source changed during measurement'
print(f'{len(rows)} verified public allocator samples: {out}', flush=True)
PY
