#!/usr/bin/env python3
"""One captured BEC consumer, shared allocations, sequential paired reader blocks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import statistics
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'workbench/tools'))
from experiment import Run
import datasets


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--target', choices=['zen5', 'granite-rapids', 'neoverse-v2'], required=True)
    args = parser.parse_args()
    output = Path(os.environ['SIXDB_RESULTS']) / 'bec-metadata'
    run = Run(ROOT, output, vars(args), workspace=ROOT / 'build/bec-metadata-workspace')
    error = None
    try:
        cpu = int(os.environ['SIXDB_CPU'])
        if cpu != 0 or cpu not in os.sched_getaffinity(0):
            raise ValueError('CPU0 required')
        run.receipt['pinned_cpu'] = cpu
        run.receipt['reader_order'] = ['specialized', 'native', 'materialized', 'materialized', 'native', 'specialized']
        run.save()
        data = run.input('inputs/windows', datasets.restore(json.loads((HERE / 'windows.json').read_text())))
        save(output / 'prepared.json', datasets.verify(data))
        shutil.copy2(data / 'lineage.json', output / 'lineage.json')
        run.step('compiler', ['clang++-21', '--version'], 'compiler.txt')
        if '21.1.8' not in (output / 'compiler.txt').read_text():
            raise ValueError('pinned compiler required')
        run.step('cpu', ['lscpu'], 'cpu.txt')
        (output / 'cpuinfo.txt').write_text(Path('/proc/cpuinfo').read_text())
        cache = Path('/sys/devices/system/cpu/cpu0/cache')
        save(output / 'cache.json', {str(p): p.read_text().strip() for p in cache.rglob('*')
            if p.is_file() and p.name in {'size', 'level', 'type', 'coherency_line_size', 'shared_cpu_list'}})
        arm = args.target == 'neoverse-v2'
        flags = ['-DSIXDB_MARCH=armv8-a', '-DCMAKE_CXX_FLAGS=-mcpu=neoverse-v2'] if arm else [
            '-DSIXDB_MARCH=x86-64-v4', '-DCMAKE_CXX_FLAGS=-mavx512vbmi -mavx512vbmi2 -mgfni -mavx512vpopcntdq -mavx512bitalg']
        run.step('configure', ['cmake', '-S', run.source_root, '-B', run.build_dir, '-G', 'Ninja',
            '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=clang++-21',
            '-DSIXDB_SPIKES=bec-packed-metadata', '-DSIXDB_TUNE=' + args.target, *flags])
        targets = ['ikea_bec_metadata_bench', 'ikea_bec_metadata_check', 'ikea_bitsets_check',
                   'ikea_integer_check', 'ikea_heterogeneous_check', 'ikea_seriespack_range_bounds_check']
        run.step('build', ['cmake', '--build', run.build_dir, '--target', *targets, '-j', '1'])
        shutil.copy2(run.build_dir / 'compile_commands.json', output / 'compile_commands.json')
        shutil.copy2(run.build_dir / '.ninja_log', output / 'ninja-log.txt')
        compiled = []
        for row in json.loads((run.build_dir / 'compile_commands.json').read_text()):
            argv = shlex.split(row['command'])
            obj = Path(row['directory']) / argv[argv.index('-o') + 1]
            if obj.is_file():
                compiled.append(dict(row, source_sha256=sha(Path(row['file'])), object_sha256=sha(obj)))
        save(output / 'compiled.json', compiled)
        probe = run.build_dir / 'workbench/spikes/ikea-composition'
        binaries = {
            'bench': run.build_dir / 'workbench/spikes/bec-packed-metadata/ikea_bec_metadata_bench',
            'adapter': run.build_dir / 'workbench/spikes/bec-packed-metadata/ikea_bec_metadata_check',
            'bitsets': probe / 'probes/ikea-blocks/ikea_bitsets_check',
            'integers': probe / 'probes/ikea-integers/ikea_integer_check',
            'combined': probe / 'probes/ikea-heterogeneous/ikea_heterogeneous_check',
            'ranges': run.build_dir / 'ikea/ikea_seriespack_range_bounds_check'}
        for name, binary in binaries.items():
            shutil.copy2(binary, output / (name + '.bin'))
            if name != 'bench':
                run.step(name + '-check', ['taskset', '-c', '0', binary,
                    *(['--all-available'] if name == 'ranges' else [])], name + '-checks.txt')
        bounds = (output / 'ranges-checks.txt').read_text()
        for target in (['scalar', 'neon'] if arm else ['scalar', 'avx2', 'avx512']):
            if '(' + target + ')' not in bounds or 'skipped' in bounds.lower():
                raise ValueError('missing production range target: ' + target)
        run.step('link-command', ['ninja', '-C', run.build_dir, '-t', 'commands', 'ikea_bec_metadata_bench'], 'link-command.txt')
        link = next(line for line in (output / 'link-command.txt').read_text().splitlines()
                    if ' -o ' in line and 'ikea_bec_metadata_bench ' in line and ' -c ' not in line)
        linked = {}
        for arg in shlex.split(link):
            if Path(arg).suffix not in {'.o', '.a'}: continue
            source = run.build_dir / arg
            destination = output / 'link-inputs' / arg
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, destination)
            linked[arg] = sha(source)
        save(output / 'link-inputs.json', linked)
        archive = run.build_dir / 'workbench/spikes/bec-packed-metadata/libikea_bec_metadata_adapter.a'
        run.step('symbols', ['llvm-nm-21', '-S', '--size-sort', '-C', archive], 'symbols.txt')
        run.step('assembly', ['llvm-objdump-21', '-dr', '-C', archive], 'assembly.txt')
        chosen_target = 'neon' if arm else 'avx512'
        if not arm:
            # Bounded target discriminator: nine refill contexts (including final
            # logical tail), both phases per process, then reversed target order.
            # Choose by median of the nine per-context AVX2/AVX512 ratios; this
            # screens a control, not a runtime width/size policy or whole-count claim.
            screen = {}
            for block, target in enumerate(['avx2', 'avx512', 'avx512', 'avx2']):
                path = output / f'screen-{block}-{target}.json'
                run.step(f'screen-{block}-{target}', ['env', 'BEC_METADATA_DATA=' + str(data),
                    'BEC_METADATA_TARGET=' + target, 'taskset', '-c', '0', binaries['bench'],
                    '--benchmark_filter=^bec/[23]-materialized/(structural|random_half|structural_tail129)/refill/',
                    '--benchmark_min_time=0.03s', '--benchmark_repetitions=3',
                    '--benchmark_enable_random_interleaving=false', '--benchmark_out_format=json',
                    '--benchmark_out=' + str(path)])
                rows = [r for r in json.loads(path.read_text())['benchmarks'] if r['run_type'] == 'iteration']
                if len(rows) != 54 or any(r.get('error_occurred') for r in rows):
                    raise ValueError('target screen inventory/status')
                for row in rows:
                    case = row['name'].split('/', 2)[2]
                    screen.setdefault(case, {}).setdefault(target, []).append(row['cpu_time'] / 256)
            ratios = {case: statistics.median(v['avx2']) / statistics.median(v['avx512']) for case, v in screen.items()}
            if len(ratios) != 9: raise ValueError('target screen cases')
            if statistics.median(ratios.values()) < 1: chosen_target = 'avx2'
            save(output / 'target-screen.json', dict(samples=screen, avx2_over_avx512=ratios, chosen=chosen_target,
                rule='median of nine refill median ratios, AVX2 if below1 else AVX512'))
        run.receipt['materialized_target'] = chosen_target; run.save()
        bench = ['env', 'BEC_METADATA_DATA=' + str(data), 'BEC_METADATA_TARGET=' + chosen_target,
                 'taskset', '-c', '0', binaries['bench'],
                 '--benchmark_filter=^bec/', '--benchmark_enable_random_interleaving=false']
        run.step('preflight', [*bench, '--benchmark_min_time=1x', '--benchmark_repetitions=1',
            '--benchmark_out_format=json', '--benchmark_out=' + str(output / 'preflight.json')])
        run.step('timing', [*bench, '--benchmark_min_time=0.03s', '--benchmark_repetitions=3',
            '--benchmark_out_format=json', '--benchmark_out=' + str(output / 'timings.json')])
        run.step('report', ['python3', HERE / 'report.py', output], 'report.txt')
        if any(sha(Path(row['file'])) != row['source_sha256'] for row in compiled):
            raise ValueError('compiled source changed')
        if any(sha(run.build_dir / name) != h for name, h in linked.items()):
            raise ValueError('linked input changed')
        # Analysis verifies the complete case/sample inventory and all counters.
        run.compact(['compiler.txt', 'cpu.txt', 'cache.json', 'adapter-checks.txt',
            'bitsets-checks.txt', 'integers-checks.txt', 'combined-checks.txt', 'ranges-checks.txt',
            'symbols.txt', 'prepared.json', 'lineage.json', 'samples.csv', 'paired.csv', 'summary.json',
            *([] if arm else ['target-screen.json'])], [])
    except Exception as exc:
        error = exc
    error = run.finish(error)
    print(output, flush=True)
    if error: raise error


if __name__ == '__main__':
    main()
