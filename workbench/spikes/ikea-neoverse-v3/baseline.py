"""Build once on C8g, run the identical checked binaries on both generations."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile

sys.path.insert(0, str(Path('workbench/tools').resolve()))
from worker_context import GroupContext

ctx = GroupContext.from_env()
out = Path(os.environ['SIXDB_RESULTS'])
package = out / 'baseline'
package.mkdir(exist_ok=True)

def run(args, log):
    print(' '.join(map(str, args)), flush=True)
    with (out / log).open('w') as stream:
        subprocess.run(list(map(str, args)), stdout=stream, stderr=subprocess.STDOUT, check=True)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

benches = {
    'seriespack': ('ikea_seriespack_bench', ['--suite=all']),
    'tuplepack': ('ikea_tuplepack_bench', []),
    'words': ('ikea_tuplepack_word_bench', []),
    'bec256': ('ikea_bec256_bench', []),
}
checks = ['ikea_bec256_check', 'ikea_bec256_composition_check',
          'ikea_tuplepack_packets_check', 'ikea_tuplepack_gpr_check']
if ctx.member == 'c8g':
    build = Path('build/ikea-neoverse-v3/baseline')
    ctx.progress('building-baseline')
    run(['cmake', '-S', '.', '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
         '-DSIXDB_BENCHMARKS=seriespack;tuplepack;bec256',
         '-DSIXDB_MARCH=armv8.2-a+simd', '-DSIXDB_TUNE=neoverse-v2'], 'configure.log')
    run(['cmake', '--build', build, '--target', *[v[0] for v in benches.values()], *checks,
         '-j', os.environ.get('SIXDB_BUILD_JOBS', '2')], 'build.log')
    for module, (name, _) in benches.items():
        suite = 'tuplepack' if module == 'words' else module
        shutil.copy2(build / 'workbench/benchmarks' / suite / name, package / name)
    for name in checks:
        shutil.copy2(build / 'ikea' / name, package / name)
    shutil.copy2(build / 'compile_commands.json', package / 'compile_commands.json')
    run(['clang++-21', '--version'], 'compiler.txt')
    archive = out / 'baseline-binaries.tar.gz'
    with tarfile.open(archive, 'w:gz') as tar:
        tar.add(package, arcname='baseline')
    uri = ctx.uri + '/baseline-binaries.tar.gz'
    ctx.aws.upload(archive, uri, timeout=120)
    ctx.publish('binaries', {'uri': uri, 'sha256': digest(archive)})
else:
    ctx.progress('waiting-for-baseline-binaries')
    ref = ctx.wait_values('binaries', ['c8g'], timeout=1500)['c8g']
    archive = out / 'baseline-binaries.tar.gz'
    run(['aws', 's3', 'cp', ref['uri'], archive, '--only-show-errors'], 'download.log')
    if digest(archive) != ref['sha256']:
        raise RuntimeError('binary archive hash mismatch')
    with tarfile.open(archive) as tar:
        tar.extractall(out, filter='data')
(out / 'binary-hashes.json').write_text(json.dumps({p.name: digest(p) for p in package.iterdir()}, indent=2)+'\n')
run(['lscpu', '-J'], 'lscpu.json')
run(['cat', '/proc/cpuinfo'], 'cpuinfo.txt')
run(['uname', '-a'], 'uname.txt')
for name in checks:
    ctx.progress('checking', case=name)
    run(['taskset', '-c', os.environ['SIXDB_CPU'], package / name], name + '.txt')
for module, (name, extra) in benches.items():
    ctx.progress('measuring-baseline', case=module)
    run(['taskset', '-c', os.environ['SIXDB_CPU'], package / name, *extra,
         '--benchmark_min_time=0.05s', '--benchmark_repetitions=5',
         '--benchmark_enable_random_interleaving=false', '--benchmark_out_format=json',
         '--benchmark_out=' + str(out / (module + '.json'))], module + '.log')
ctx.progress('baseline-complete')
