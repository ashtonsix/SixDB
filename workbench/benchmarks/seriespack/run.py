#!/usr/bin/env python3
"""SeriesPack hardware checks and optional measurements; worker.py captures source/host."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[3]
PROFILES = {
    'avx2': ('x86-64-v3', [], {'__AVX2__'}, {'__AVX512F__', '__GFNI__'}, {'avx2'}),
    'avx512bw': ('x86-64-v4', [], {'__AVX2__', '__AVX512F__', '__AVX512BW__'},
                 {'__AVX512VBMI__', '__AVX512VBMI2__', '__GFNI__'},
                 {'avx2', 'avx512f', 'avx512bw', 'avx512vl', 'avx512dq', 'avx512cd'}),
    'avx512': ('x86-64-v4', ['-mavx512vbmi', '-mavx512vbmi2', '-mgfni'],
               {'__AVX2__', '__AVX512F__', '__AVX512BW__', '__AVX512VBMI__',
                '__AVX512VBMI2__', '__GFNI__'}, set(),
                {'avx2', 'avx512f', 'avx512bw', 'avx512vl', 'avx512dq', 'avx512cd',
                'avx512vbmi', 'avx512_vbmi2', 'gfni'}),
    'neon': ('armv8-a', [], {'__ARM_NEON'}, {'__ARM_FEATURE_SVE'}, {'asimd'}),
}
TUNES = {'zen5': 'znver5', 'granite-rapids': 'graniterapids', 'neoverse-v2': 'neoverse-v2'}


def save(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n')


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def check_applicability(profile, target, text):
    """Interpret this suite's optional checks without claiming absent native coverage."""
    aggregate = target == 'ikea_seriespack_check'
    full = profile == 'avx512'
    optional = {
        'deferred_sum': 'deferred sum: native checks skipped (AVX512 BW/VBMI is not enabled)',
        'grouped_composition': 'SKIP: grouped composition requires AVX-512 BW/VBMI',
    }
    allowed = set() if full else set(optional.values())
    lines = [line.strip() for line in text.splitlines()]
    skips = {line for line in lines if re.search(r'\bskip(?:ped)?\b|no enabled native target', line, re.I)}
    if skips - allowed:
        raise RuntimeError('unexpected native skip: ' + repr(sorted(skips - allowed)))
    if skips and not aggregate:
        raise RuntimeError('requested check is unavailable: ' + '; '.join(sorted(skips)))
    required = ['scalar'] + (['neon'] if profile == 'neon' else
                            ['avx2', 'avx512'] if full else ['avx2'])
    if aggregate:
        for native in required:
            for family in ['SeriesPack public wire', 'SeriesPack range bounds']:
                if not any(line.startswith(f'{family} ({native}):') and line.endswith('passed') for line in lines):
                    raise RuntimeError(f'missing actual target validation: {family} {native}')
        executor = {'neon': 'NEON', 'avx512': 'AVX-512'}.get(profile, 'AVX2')
        if not any(line.startswith(f'{executor} native composition:') and line.endswith('passed') for line in lines):
            raise RuntimeError(f'missing native composition validation: {executor}')
        if full and (not any(line.startswith('PASS: grouped composition ') for line in lines) or
                     not any(re.match(r'PASS: \d+ deferred-sum checks;', line) for line in lines)):
            raise RuntimeError('missing grouped/deferred validation for the full AVX-512 profile')
    return {'required_targets': required if aggregate else [],
            'allowed_unavailable_messages': sorted(allowed),
            'observed_unavailable_messages': sorted(skips),
            'optional_checks': {name: {'applicable': full, 'unavailable_observed': message in skips}
                                for name, message in optional.items()}}


def main(*, legacy=False):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tune', choices=TUNES, required=True)
    parser.add_argument('--profile', choices=PROFILES, action='append')
    parser.add_argument('--calico', action=argparse.BooleanOptionalAction, default=legacy,
                        help='restore and include the optional historical Calico comparisons')
    parser.add_argument('--measure', action='store_true', help='also build and run the kernel comparisons')
    parser.add_argument('--retain-check-binaries', action='store_true',
                        help='also retain passing check executables during measurements (logs/hashes always retained)')
    parser.add_argument('--check-target', default='ikea_seriespack_check',
                        help='build/run one ikea_seriespack_*_check executable for diagnosis')
    parser.add_argument('--build-type', choices=['RelWithDebInfo', 'Release'])
    parser.add_argument('--benchmark-filter', default='^bulk/')
    parser.add_argument('--min-time', type=float, default=0.03, help='minimum seconds per benchmark repetition')
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--plan', action='store_true', help='print the feature matrix without running')
    args = parser.parse_args()
    if not (args.check_target.startswith('ikea_seriespack_') and args.check_target.endswith('_check')
            and all(c.isalnum() or c == '_' for c in args.check_target)):
        parser.error('check-target must name an ikea_seriespack_*_check target')
    arm = args.tune == 'neoverse-v2'
    profiles = args.profile or (['neon'] if arm else ['avx2', 'avx512bw', 'avx512'])
    if any((name == 'neon') != arm for name in profiles):
        parser.error('neon requires neoverse-v2 tuning; x86 profiles require zen5 or granite-rapids')
    if len(set(profiles)) != len(profiles):
        parser.error('choose each profile at most once')
    if not math.isfinite(args.min_time) or args.min_time <= 0 or args.repetitions < 1:
        parser.error('min-time and repetitions must be positive')
    build_type = args.build_type or ('Release' if args.measure else 'RelWithDebInfo')
    if args.plan:
        print(json.dumps({name: {'march': PROFILES[name][0], 'extra_flags': PROFILES[name][1],
                               'tune': args.tune, 'build_type': build_type,
                               'measure': args.measure, 'calico': args.calico,
                               'benchmark_home': 'spike compatibility' if legacy else 'recurring suite',
                               'check_target': args.check_target,
                               'benchmark_filter': args.benchmark_filter if args.measure else None}
                          for name in profiles}, indent=2))
        return 0
    if platform.system() != 'Linux' or platform.machine() != ('aarch64' if arm else 'x86_64'):
        parser.error('run on Linux hardware matching the profile; this runner does not launch an emulator')
    if 'SIXDB_RESULTS' not in os.environ:
        parser.error('set SIXDB_RESULTS to an ignored output directory, or use worker.py')
    output = Path(os.environ['SIXDB_RESULTS']).resolve() / 'seriespack'
    output.mkdir(parents=True, exist_ok=False)
    cpu = int(os.environ.get('SIXDB_CPU', min(os.sched_getaffinity(0))))
    if cpu not in os.sched_getaffinity(0):
        parser.error('SIXDB_CPU is outside the allowed affinity mask')
    context_paths = [Path('/sys/kernel/mm/transparent_hugepage') / name for name in ['enabled', 'defrag']]
    context_paths += [index / name for index in Path(f'/sys/devices/system/cpu/cpu{cpu}/cache').glob('index*')
                     for name in ['level', 'type', 'size', 'coherency_line_size', 'shared_cpu_list',
                                  'ways_of_associativity', 'number_of_sets']]
    save(output / 'cache-context.json', {str(path): path.read_text().strip()
                                        for path in context_paths if path.is_file()})
    jobs = int(os.environ.get('SIXDB_BUILD_JOBS', '1'))
    if jobs < 1:
        parser.error('SIXDB_BUILD_JOBS must be positive')
    cpuinfo = Path('/proc/cpuinfo').read_text()
    (output / 'cpuinfo.txt').write_text(cpuinfo)
    feature_sets = [set(line.split(':', 1)[1].split()) for line in cpuinfo.splitlines()
                    if line.startswith(('flags\t', 'Features\t'))]
    hardware = set.intersection(*feature_sets) if feature_sets else set()
    receipt = {'format': 1, 'scope': 'correctness, observed build costs' +
               (', and matched-contract measurements' if args.measure else '; no throughput measurements'),
               'tune': args.tune, 'cpu': cpu, 'build_jobs': jobs,
               'build_type': build_type, 'arguments': vars(args),
               'source_commit': os.environ.get('SIXDB_SOURCE_COMMIT'),
               'job': os.environ.get('SIXDB_JOB'), 'profiles': []}
    save(output / 'validation.json', receipt)
    prior = None
    if args.measure and args.calico:
        sys.path.insert(0, str(ROOT / 'workbench/tools'))
        import datasets
        reference = ROOT / 'workbench/spikes/ikea-composition/probes/ikea-integers/prior-input.json'
        prior = datasets.restore(json.loads(reference.read_text()))
        shutil.copy2(reference, output / 'prior-input.json')
        shutil.copy2(prior / 'prepared.json', output / 'prior-prepared.json')
        receipt['prior'] = {'reference': 'prior-input.json', 'prepared': 'prior-prepared.json',
                            'include_directory': str(prior)}
    if args.measure:
        receipt['measurement_environment'] = {name: os.environ.get(name) for name in
                                              ['SERIESPACK_BULK_VALUES', 'SERIESPACK_RESIDENT_BYTES']}
        save(output / 'validation.json', receipt)
    benchmark_relative = ('workbench/spikes/ikea-composition/validation' if legacy
                          else 'workbench/benchmarks/seriespack')
    failed = False
    for name in profiles:
        march, extra, required, forbidden, cpu_required = PROFILES[name]
        directory = output / name
        directory.mkdir()
        build = ROOT / 'build/validation/seriespack' / args.tune / name / build_type
        record = {'name': name, 'march': march, 'extra_flags': extra, 'status': 'running',
                  'build_directory_existed': build.exists(), 'correctness_passed': False,
                  'check_target': args.check_target,
                  'check_scope': 'aggregate' if args.check_target == 'ikea_seriespack_check' else 'executable',
                  'commands': [], 'binaries': []}
        receipt['profiles'].append(record)

        def command(label, argv):
            print(f'{name}: {label}', flush=True)
            resource_path = directory / (label + '.resources.json')
            actual = ['taskset', '-c', str(cpu), '/usr/bin/time', '-q', '-o', str(resource_path),
                      '-f', '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"max_rss_kib":%M}',
                      *map(str, argv)]
            entry = {'name': label, 'argv': actual, 'cwd': str(ROOT)}
            record['commands'].append(entry)
            save(output / 'validation.json', receipt)
            started = time.monotonic()
            with (directory / (label + '.txt')).open('w') as log:
                result = subprocess.run(actual, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            entry.update(returncode=result.returncode, elapsed_seconds=time.monotonic() - started)
            if resource_path.exists():
                entry['resources'] = json.loads(resource_path.read_text())
            save(output / 'validation.json', receipt)
            if result.returncode:
                raise RuntimeError(f'{label} exited {result.returncode}; see {name}/{label}.txt')

        try:
            missing = cpu_required - hardware
            if missing:
                raise RuntimeError('host lacks required CPU features: ' + ', '.join(sorted(missing)))
            command('compiler', ['clang++-21', '--version'])
            flags = ['-std=c++23', f'-march={march}', f'-mtune={TUNES[args.tune]}', *extra]
            command('macros', ['clang++-21', *flags, '-dM', '-E', '-x', 'c++', '/dev/null'])
            macros = {line.split()[1] for line in (directory / 'macros.txt').read_text().splitlines()
                      if line.startswith('#define ')}
            record['enabled_feature_macros'] = sorted(m for m in macros if any(
                word in m for word in ('AVX', 'BMI', 'GFNI', 'FMA', 'SSE', 'ARM_NEON', 'ARM_FEATURE')))
            if not required <= macros or forbidden & macros:
                raise RuntimeError(f'wrong compiler feature set: missing={required - macros}, '
                                   f'forbidden={forbidden & macros}')
            command('configure', ['cmake', '-S', ROOT, '-B', build, '-G', 'Ninja',
                                  f'-DCMAKE_BUILD_TYPE={build_type}',
                                  '-DSIXDB_SPIKES=' + ('ikea-composition' if args.measure and legacy else ''),
                                  '-DSIXDB_BENCHMARKS=' + ('seriespack' if args.measure and not legacy else ''),
                                  '-DSERIESPACK_PRIOR_DIR=' + (str(prior) if prior else ''),
                                  f'-DSIXDB_MARCH={march}', f'-DSIXDB_TUNE={args.tune}',
                                  '-DCMAKE_CXX_COMPILER=clang++-21',
                                  '-DCMAKE_CXX_FLAGS=' + ' '.join(extra)])
            aggregate = args.check_target == 'ikea_seriespack_check'
            label = 'build-and-check' if aggregate else 'build-check-target'
            command(label, ['cmake', '--build', build, '--target', args.check_target, '-j', str(jobs)])
            if not aggregate:
                check_args = ['--all-available'] if args.check_target == 'ikea_seriespack_physical_operations_check' else []
                command('check', [build / 'ikea' / args.check_target, *check_args])
            check_log = (directory / (label if aggregate else 'check')).with_suffix('.txt').read_text()
            record['check_applicability'] = check_applicability(name, args.check_target, check_log)
            record['correctness_passed'] = True
            if args.measure:
                command('build-benchmark', ['cmake', '--build', build, '--target',
                                             'ikea_seriespack_bench', '-j', str(jobs)])
                benchmark = build / benchmark_relative / 'ikea_seriespack_bench'
                samples = directory / 'samples.json'
                command('benchmark', [benchmark, f'--benchmark_filter={args.benchmark_filter}',
                                      f'--benchmark_min_time={args.min_time}s',
                                      f'--benchmark_repetitions={args.repetitions}',
                                      '--benchmark_enable_random_interleaving=false',
                                      '--benchmark_out_format=json', f'--benchmark_out={samples}'])
                data = json.loads(samples.read_text())
                rows = data.get('benchmarks', [])
                if not rows or any(row.get('error_occurred') for row in rows):
                    raise RuntimeError('benchmark produced no cases or reported a case error; see samples.json')
                record['measurement'] = {'output': 'samples.json', 'rows': len(rows),
                                          'context': data.get('context', {})}
            record['status'] = 'passed'
        except Exception as error:
            record.update(status='failed', error=str(error))
            failed = True
            print(f'{name}: {error}', flush=True)
        finally:
            # Keep failed-build diagnostics too. The worker bundles bulky outputs in S3.
            for filename in ['CMakeCache.txt', 'compile_commands.json', '.ninja_log']:
                if (build / filename).is_file():
                    shutil.copy2(build / filename, directory / filename)
            if args.check_target == 'ikea_seriespack_check':
                binaries = sorted((build / 'ikea').glob('ikea_seriespack_*_check'))
                library = build / 'ikea/libikea_seriespack.a'
                if library.exists():
                    binaries.append(library)
            else:
                binaries = [build / 'ikea' / args.check_target]
            if args.measure:
                bench_dir = build / benchmark_relative
                binaries.extend(p for p in [bench_dir / 'ikea_seriespack_bench',
                                           bench_dir / 'libikea_seriespack_prior.a'] if p.exists())
            for binary in binaries:
                if not binary.is_file():
                    continue
                retain = (not args.measure or not record['correctness_passed'] or
                          args.retain_check_binaries or not binary.name.endswith('_check'))
                if retain:
                    shutil.copy2(binary, directory / binary.name)
                size = subprocess.run(['llvm-size-21', '--format=berkeley', str(binary)],
                                      text=True, capture_output=True)
                (directory / (binary.name + '.size.txt')).write_text(size.stdout + size.stderr)
                totals = [0, 0, 0]
                for row in size.stdout.splitlines():
                    fields = row.split()
                    if len(fields) >= 3 and all(x.isdigit() for x in fields[:3]):
                        totals = [a + int(b) for a, b in zip(totals, fields[:3])]
                record['binaries'].append({'name': binary.name, 'bytes': binary.stat().st_size,
                                          'sha256': sha256(binary), 'size_returncode': size.returncode,
                                          'retained': retain,
                                          'checks_passed_in_this_run': record['correctness_passed'],
                                          'check_target': args.check_target,
                                          'text_data_bss': totals if size.returncode == 0 else None})
                # Static archives are retained and can be disassembled when needed;
                # their expanded text largely repeats the benchmark executable.
                if retain and binary.suffix != '.a':
                    with (directory / (binary.name + '.asm')).open('w') as assembly:
                        result = subprocess.run(['llvm-objdump-21', '--disassemble', '--no-show-raw-insn',
                                                 str(binary)], stdout=assembly, stderr=subprocess.STDOUT)
                    record['binaries'][-1]['disassembly_returncode'] = result.returncode
            save(output / 'validation.json', receipt)
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
