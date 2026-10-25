#!/usr/bin/env python3
"""Replay tiny ordinary SixDB storage workflows without cloud or live-cache writes.

Only the executable AWS backend is fake. Public helper source is copied unchanged
into a disposable Git fixture so its repository-relative output paths are private.
Assertions use expected bytes, command results and filesystem stat, never a helper's
accounting report. See ../PUBLIC-WORKFLOWS.md for comparison limits.
"""
from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import resource
import shlex
import shutil
import signal
import subprocess
import struct
import sys
import time

MIB = 1024 ** 2
PAYLOAD = 64 * 1024
LOCAL_BUDGET = 128 * 1024
WORKER_BUDGET = 96 * 1024
MAX_BYTES = 16 * MIB


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def measure(root):
    """Unique-inode allocation, including directories; no library inventory oracle."""
    seen, logical, allocated, count = set(), 0, 0, 0
    if not root.exists():
        return dict(logical=0, allocated=0, files=0)
    for p in [root, *root.rglob('*')]:
        s = p.lstat()
        if (s.st_dev, s.st_ino) in seen:
            continue
        seen.add((s.st_dev, s.st_ino))
        allocated += s.st_blocks * 512
        if p.is_file() and not p.is_symlink():
            logical += s.st_size
            count += 1
    return dict(logical=logical, allocated=allocated, files=count)


def extents(root):
    """Kernel physical extent union for regular files; unknown/inline bytes excluded.

    This is a lower bound, not filesystem metadata or compression accounting.
    It avoids claiming reflink copies occupy st_blocks independently.
    """
    ranges, unknown = [], []
    candidates=([root] if root.is_file() else list(root.rglob('*'))) if isinstance(root,Path) else list(root)
    for path in sorted(candidates):
        if not path.is_file() or path.is_symlink(): continue
        try:
            request=bytearray(32+256*56)
            struct.pack_into('=QQIIII',request,0,0,2**64-1,1,0,256,0)
            with path.open('rb') as handle: fcntl.ioctl(handle.fileno(),0xC020660B,request,True)
            count=struct.unpack_from('=I',request,20)[0]
            if count==256: raise RuntimeError('extent fixture capacity exhausted')
            for index in range(count):
                _,physical,length=struct.unpack_from('=QQQ',request,32+index*56)
                flags=struct.unpack_from('=I',request,32+index*56+40)[0]
                if flags & (2|4|8|128|256|512|1024): unknown.append(str(path)); continue
                ranges.append((physical,physical+length))
        except OSError: unknown.append(str(path))
    total,end=0,0
    for start,stop in sorted(ranges):
        total+=max(0,stop-max(start,end)); end=max(end,stop)
    return dict(physical_extent_lower_bound=total,unknown_files=len(set(unknown)))


FAKE_AWS = r'''#!/usr/bin/env python3
import hashlib,json,os,sys,time
from pathlib import Path
a=sys.argv[1:]; base=Path(os.environ['PUBLIC_FAKE_AWS'])
with (base/'calls.jsonl').open('a') as f: f.write(json.dumps(a)+'\n')
def option(k): return a[a.index(k)+1]
def obj(bucket,key): return base/'objects'/hashlib.sha256((bucket+'\n'+key).encode()).hexdigest()
def missing():
    print('An error occurred (NoSuchKey) when calling GetObject: absent fixture object',file=sys.stderr); sys.exit(1)
if a[:2]==['s3api','put-object']:
    p=obj(option('--bucket'),option('--key')); p.parent.mkdir(exist_ok=True)
    if p.exists(): print('PreconditionFailed',file=sys.stderr); sys.exit(1)
    p.write_bytes(Path(option('--body')).read_bytes()); print('{}')
elif a[:2]==['s3api','get-object']:
    p=obj(option('--bucket'),option('--key'))
    if not p.exists(): missing()
    target=Path(a[a.index('--key')+2]); data=p.read_bytes()
    if os.environ.get('PUBLIC_PAUSE_GET')=='1' and option('--key').endswith('.tar.gz'):
        target.write_bytes(data[:len(data)//2]); (base/'paused').write_text(str(target)); time.sleep(30)
    target.write_bytes(data); print('{}')
elif a[:2]==['s3','cp']:
    i=next(i for i,v in enumerate(a) if v.startswith('s3://'))
    bucket,_,key=a[i][5:].partition('/'); p=obj(bucket,key)
    if not p.exists(): missing()
    if a[i+1]=='-': sys.stdout.buffer.write(p.read_bytes())
    else: Path(a[i+1]).write_bytes(p.read_bytes())
else:
    print('FAKE BACKEND DENIED: '+repr(a),file=sys.stderr); sys.exit(97)
'''


DRIVER = r'''import json,os,subprocess,sys,time
from pathlib import Path
root=Path(sys.argv[1]); sys.path.insert(0,str(root/'workbench/tools'))
from experiment import Run
output,workspace=map(Path,sys.argv[3:5]); mode=sys.argv[2]
run=Run(root,output,{'fixture':mode},workspace=workspace)
if mode=='interrupt':
    (output/'unique.bin').write_bytes(bytes(range(256))*256)
    (output/'paused').touch(); time.sleep(30)
elif mode=='input':
    prepared=Path(sys.argv[5]); run.input('inputs',prepared)
    (output/'measurement.csv').write_text('value\n42\n'); run.finish()
else:
    run.step('configure',['cmake','-S',str(run.source_root),'-B',str(run.build_dir),'-G','Ninja'])
    run.step('build',['cmake','--build',str(run.build_dir),'--target','fixture'])
    run.step('execute',[str(run.build_dir/'ikea/fixture')]); run.finish()
'''


class Replay:
    def __init__(self, source, work):
        self.source, self.work = source.resolve(), work.resolve()
        self.repo = self.work / 'fixture'
        self.tools = self.repo / 'workbench/tools'
        self.records, self.cases = [], []
        self.env = dict(os.environ)
        self.env.update(PYTHONDONTWRITEBYTECODE='1', XDG_CACHE_HOME=str(self.work / 'xdg'),
                        TMPDIR=str(self.work / 'tmp'), PUBLIC_FAKE_AWS=str(self.work / 'backend'),
                        SIXDB_DATA_CACHE=str(self.repo / 'build/shared-datasets'),
                        SIXDB_MIN_FREE_GIB='0', SIXDB_LOCAL_BUILD_GIB=str(LOCAL_BUDGET / 1024 ** 3),
                        SIXDB_WORKER_CACHE_GIB=str(WORKER_BUDGET / 1024 ** 3),
                        GIT_CONFIG_NOSYSTEM='1', GIT_CONFIG_GLOBAL='/dev/null',
                        AWS_EC2_METADATA_DISABLED='true')
        self.env.pop('SIXDB_RESULTS', None)
        self.env.pop('SIXDB_HOST_STORAGE_PATH', None)
        for p in (self.tools, self.work / 'tmp', self.work / 'backend/objects', self.work / 'bin'):
            p.mkdir(parents=True)
        fake = self.work / 'bin/aws'
        fake.write_text(FAKE_AWS); fake.chmod(0o755)
        self.env['PATH'] = str(fake.parent) + os.pathsep + self.env['PATH']
        self.versions = {}
        for p in sorted((self.source / 'workbench/tools').rglob('*')):
            relative=p.relative_to(self.source / 'workbench/tools')
            if p.is_file() and (p.suffix=='.py' or len(relative.parts)==1 and p.suffix in {'.json','.sh'}):
                target=self.tools/relative; target.parent.mkdir(parents=True,exist_ok=True)
                shutil.copy2(p,target)
                self.versions[str(p.relative_to(self.source))] = digest(p)
        shutil.copy2(self.source / 'CMakeLists.txt', self.repo)
        shutil.copytree(self.source / 'cmake', self.repo / 'cmake')
        for p in [self.repo / 'CMakeLists.txt', *(self.repo / 'cmake').glob('*')]:
            if p.is_file(): self.versions[str(p.relative_to(self.repo))] = digest(p)
        for module in ('ikea', 'orbital', 'loom', 'engine', 'shore', 'workbench'):
            (self.repo / module).mkdir(exist_ok=True)
            (self.repo / module / 'CMakeLists.txt').write_text('')
        (self.repo / 'ikea/CMakeLists.txt').write_text('add_executable(fixture main.cpp)\nsixdb_target(fixture)\n')
        (self.repo / 'ikea/main.cpp').write_text('int main() { return 0; }\n')
        # Ignore helper transport and dataset fixture recipes as capture inputs.
        (self.repo / '.gitignore').write_text('/build/\n/workbench/tools/\n/workbench/datasets/\n')
        (self.repo / 'source.bin').write_bytes(self.payload('source'))
        dataset = self.repo / 'workbench/datasets/fixture'
        dataset.mkdir(parents=True)
        (dataset / 'dataset.json').write_text('{"sources":{}}\n')
        (dataset / 'prepare.py').write_text('''import hashlib,time
DEFAULTS={'variant':0,'pause':False}
def prepare(paths,output,variant,pause):
    data=b''.join(hashlib.sha256(str(i).encode()).digest() for i in range(2048))
    (output/'data.bin').write_bytes(data)
    if pause:
        (output/'paused').touch(); time.sleep(30)
    return {'rows':65536}
''')
        self.driver = self.work / 'run_fixture.py'
        self.driver.write_text(DRIVER)
        self.run(['git', 'init', '-q', str(self.repo)])
        self.run(['git', 'add', '.'])
        self.run(['git', '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid',
                  '-c', 'core.hooksPath=/dev/null', 'commit', '-q', '-m', 'tiny public workflow fixture'])

    @staticmethod
    def payload(name, size=PAYLOAD):
        return b''.join(hashlib.sha256((name+str(i)).encode()).digest() for i in range((size+31)//32))[:size]

    def guard(self):
        usage = measure(self.work)
        if usage['allocated'] > MAX_BYTES or usage['logical'] > MAX_BYTES:
            raise RuntimeError(f'private fixture exceeded 16 MiB guard: {usage}')

    def run(self, argv, *, ok=True, extra=None, timeout=30):
        argv = [str(x) for x in argv]
        result = subprocess.run(argv, cwd=self.repo, env=self.env | (extra or {}),
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout)
        self.records.append(dict(argv=argv, cwd=str(self.repo), env=extra or {},
                                 returncode=result.returncode, stdout=result.stdout[-4000:], stderr=result.stderr[-4000:]))
        self.guard()
        if ok and result.returncode:
            raise RuntimeError(f'{shlex.join(argv)} failed: {result.stdout}\n{result.stderr}')
        return result

    def cli(self, name, *args, **kwargs):
        return self.run([sys.executable, self.tools / name, *args], **kwargs)

    def last_path(self, result):
        return Path(result.stdout.strip().splitlines()[-1])

    def record(self, name, success, **details):
        value = dict(name=name, success=bool(success), **details)
        self.cases.append(value)
        print(json.dumps(value, sort_keys=True), flush=True)

    def object(self, bucket, key, data):
        path = self.work / 'backend/objects' / hashlib.sha256((bucket+'\n'+key).encode()).hexdigest()
        path.write_bytes(data)

    def interrupt(self, argv, ready, *, extra=None):
        argv = [str(x) for x in argv]
        proc = subprocess.Popen(argv, cwd=self.repo, env=self.env | (extra or {}),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
        deadline = time.monotonic() + 10
        while not ready() and proc.poll() is None and time.monotonic() < deadline:
            time.sleep(.02)
        was_ready = ready()
        if proc.poll() is None:
            os.killpg(proc.pid, signal.SIGKILL)
        out, err = proc.communicate(timeout=5)
        self.records.append(dict(argv=argv, cwd=str(self.repo), env=extra or {}, returncode=proc.returncode,
                                 interrupted_after_ready=was_ready, stdout=out[-2000:], stderr=err[-2000:]))
        self.guard()
        if not was_ready: raise RuntimeError('interruption fixture did not reach write phase: '+err)

    def execute(self):
        source = self.repo / 'build/artifact-source'; source.mkdir(parents=True)
        data = self.payload('artifact')
        (source / 'data.bin').write_bytes(data)
        (source / 'summary.csv').write_text('value\n42\n')
        ref = self.repo / 'build/artifact.json'
        self.cli('artifacts.py', 'put', source, ref)
        # Ordinary immutable input helper should reuse exactly one path.
        inputs = [self.last_path(self.cli('artifacts.py', 'input', ref)) for _ in range(3)]
        self.record('artifact-input-reuse', len(set(inputs)) == 1 and (inputs[0]/'data.bin').read_bytes() == data,
                    paths=list(map(str,inputs)))
        destinations = [self.repo / f'build/recovery/{i}' for i in range(4)]
        before = measure(self.repo / 'build')
        for p in destinations: self.cli('artifacts.py', 'fetch', ref, p)
        after = measure(self.repo / 'build')
        self.record('artifact-ordinary-fetch-local-budget', after['allocated'] <= LOCAL_BUDGET,
                    budget=LOCAL_BUDGET, before=before, after=after,
                    expected_bytes_preserved=all((p/'data.bin').read_bytes()==data for p in destinations))
        # Explicit --cache must stay authoritative and outside SIXDB_RESULTS.
        declared = self.work / 'declared-data'
        route = self.last_path(self.cli('artifacts.py','input',ref,'--cache',declared))
        self.record('artifact-declared-cache-route', route.is_relative_to(declared) and (route/'data.bin').read_bytes()==data,
                    path=str(route))
        refused = self.cli('artifacts.py','input',ref,'--cache',self.work/'results/cache',ok=False,
                           extra={'SIXDB_RESULTS':str(self.work/'results')})
        self.record('artifact-input-results-route-refused', refused.returncode != 0 and not (self.work/'results').exists())
        # A killed download leaves actual ordinary .fetch staging behind.
        interrupted_base = self.repo / 'build/interrupted-artifact'; interrupted_base.mkdir()
        for i in range(5):
            marker = self.work/'backend/paused'; marker.unlink(missing_ok=True)
            self.interrupt([sys.executable,self.tools/'artifacts.py','fetch',ref,interrupted_base/str(i)],
                           marker.exists,extra={'PUBLIC_PAUSE_GET':'1'})
        debris = [str(p.relative_to(interrupted_base)) for p in interrupted_base.rglob('bundle.tar.gz')]
        self.record('artifact-interrupted-staging-bounded', measure(interrupted_base)['allocated']<=LOCAL_BUDGET,
                    abandoned_archives=debris, retained=measure(interrupted_base))
        self.cli('artifacts.py','fetch',ref,interrupted_base/'retry')
        self.record('artifact-interrupt-retry-bytes', (interrupted_base/'retry/data.bin').read_bytes()==data)
        # Actual catalog CLI variants; no source downloader or recipe is mocked.
        variants=[]
        for i in range(4): variants.append(self.last_path(self.cli('datasets.py','get','fixture','--param',f'variant={i}')))
        variant_again = self.last_path(self.cli('datasets.py','get','fixture','--param','variant=0'))
        self.record('dataset-variant-reuse', variant_again==variants[0] and len(set(variants))==4 and
                    len({digest(p/'data.bin') for p in variants})==1)
        routed_cache=self.work/'declared-dataset-cache'
        routed=self.last_path(self.cli('datasets.py','get','fixture','--param','variant=5',
                                      extra={'SIXDB_DATA_CACHE':str(routed_cache)}))
        self.record('dataset-declared-cache-route',routed.is_relative_to(routed_cache) and
                    (routed/'data.bin').read_bytes()==(variants[0]/'data.bin').read_bytes(),path=str(routed))
        prepared = self.repo/'build/shared-datasets/prepared'
        self.record('dataset-variants-local-budget', measure(prepared)['allocated'] <= LOCAL_BUDGET,
                    budget=LOCAL_BUDGET, actual=measure(prepared), variants=len(variants))
        for i in range(3):
            self.interrupt([sys.executable,self.tools/'datasets.py','get','fixture','--param',f'variant={20+i}',
                            '--param','pause=true'], lambda: bool(list(prepared.glob('tmp*/prepared/paused'))))
            # Keep earlier witnesses but make the readiness test distinguish the next invocation.
            for p in prepared.glob('tmp*/prepared/paused'): p.rename(p.with_name('paused-observed'))
        incomplete = list(prepared.glob('tmp*/prepared/data.bin'))
        self.record('dataset-interrupted-staging-bounded', sum(p.stat().st_size for p in incomplete)<=LOCAL_BUDGET,
                    abandoned_preparations=len(incomplete), retained_bytes=sum(p.stat().st_size for p in incomplete))
        # Capture writes and receipts occur through the ordinary CLI into fixture-only Git worktrees.
        captures = []
        for i in range(3):
            if i: (self.repo/'source.bin').write_bytes(self.payload(f'capture-source-{i}'))
            p=self.repo/f'build/captures/{i}'; self.cli('capture.py','create',p,'--base',self.repo)
            self.cli('capture.py','verify',p); captures.append(p)
        initial = (captures[0]/'source.bin').read_bytes()
        (self.repo/'source.bin').write_bytes(self.payload('changed-source'))
        self.cli('capture.py','verify',captures[0])
        self.record('capture-byte-isolation', initial==self.payload('source') and (captures[0]/'source.bin').read_bytes()==initial)
        capusage = measure(self.repo/'build/captures')
        capextents=extents(self.repo/'build/captures')
        self.record('capture-local-budget', capextents['physical_extent_lower_bound'] <= LOCAL_BUDGET,
                    budget=LOCAL_BUDGET,actual=capusage,extents=capextents,
                    qualification='extent union lower bound excludes filesystem metadata and unknown/inline data')
        # Pinned Linux CMake rules, real Clang, useful incremental workspace.
        workspace=self.repo/'build/workspaces/fixture'
        for i in range(2):
            self.run([sys.executable,self.driver,self.repo,'build',self.repo/f'build/experiments/build-{i}',workspace])
        buildusage=measure(workspace/'build')
        self.record('actual-cmake-build-local-budget',buildusage['allocated'] <= LOCAL_BUDGET,
                    budget=LOCAL_BUDGET,actual=buildusage,executable_exists=(workspace/'build/ikea/fixture').is_file())
        self.record('actual-cmake-incremental-reuse', 'no work to do' in
                    (self.repo/'build/experiments/build-1/build.stdout').read_text().lower())
        for i in range(3):
            output=self.repo/f'build/experiments/interrupted-{i}'
            self.interrupt([sys.executable,self.driver,self.repo,'interrupt',output,workspace],(output/'paused').exists)
        abandoned=list((self.repo/'build/experiments').glob('interrupted-*/unique.bin'))
        self.record('experiment-interrupted-local-budget',sum(p.stat().st_size for p in abandoned)<=LOCAL_BUDGET,
                    budget=LOCAL_BUDGET,unique_bytes=sum(p.stat().st_size for p in abandoned),outputs=len(abandoned))
        # Published prepared dependency; recover two normal run bundles through public artifact CLI.
        self.cli('datasets.py','publish','fixture','--param','variant=0')
        study=self.repo/'build/experiments/prepared-input'
        self.run([sys.executable,self.driver,self.repo,'input',study,workspace,variants[0]])
        runref=self.repo/'build/input-run-artifact.json'; self.cli('artifacts.py','put',study,runref)
        restored=[self.repo/f'build/recovered-runs/{i}' for i in range(3)]
        for p in restored: self.cli('artifacts.py','fetch',runref,p)
        dbytes=sum((p/'inputs/data.bin').stat().st_size for p in restored)
        inputextents=[extents(p/'inputs') for p in restored]
        recoveryextents=extents([p/'inputs/data.bin' for p in restored])
        self.record('artifact-run-input-recovery-shared',recoveryextents['physical_extent_lower_bound']<=PAYLOAD,
                    per_result_data_bytes=dbytes,expected_data_bytes=PAYLOAD,
                    extents=recoveryextents,per_result_extents=inputextents,
                    data_correct=all((p/'inputs/data.bin').read_bytes()==(variants[0]/'data.bin').read_bytes() for p in restored))
        # Real worker CLI collection, with local objects representing already-completed remote jobs.
        reference=json.loads(ref.read_text())
        bundle=source.parent/'worker-source'; bundle.mkdir()
        (bundle/'measurement.csv').write_text('value\n42\n')
        (bundle/'input.bin').write_bytes(data)
        (bundle/'source.tar.gz').write_bytes(self.payload('launch-source',32*1024))
        nested=bundle/'study'; nested.mkdir(); (nested/'source.tar.gz').write_bytes(self.payload('nested-source',32*1024))
        workerref=self.repo/'build/worker-artifact.json'; self.cli('artifacts.py','put',bundle,workerref)
        reference=json.loads(workerref.read_text()); jobs=[]
        for i in range(4):
            jobid=f'fixture-{i}'; jobdir=self.repo/'build/workers'/jobid; jobdir.mkdir(parents=True)
            prefix='sixdb/workers/'+jobid
            job={'format':2,'id':jobid,'prefix':prefix,'uri':'s3://fixture/'+prefix,
                 'config':{'bucket':'fixture','region':'fixture','env':{}}}
            (jobdir/'job.json').write_text(json.dumps(job))
            (jobdir/'source.tar.gz').write_bytes((bundle/'source.tar.gz').read_bytes())
            self.object('fixture',prefix+'/status.json',json.dumps({'state':'complete','artifact':reference}).encode())
            self.cli('worker.py','fetch',jobid); jobs.append(jobdir)
        workerusage=measure(self.repo/'build/workers')
        self.record('worker-ordinary-fetch-budget',workerusage['allocated']<=WORKER_BUDGET,budget=WORKER_BUDGET,
                    actual=workerusage,expected_measurements=all((p/'results/measurement.csv').read_text()=='value\n42\n' for p in jobs))
        self.record('worker-default-source-omissions',all(not (p/'source.tar.gz').exists() and
                    not (p/'results/source.tar.gz').exists() and not (p/'results/study/source.tar.gz').exists() for p in jobs))
        self.cli('worker.py','fetch',jobs[0].name,'--file','study/source.tar.gz')
        self.record('worker-selected-source-recovery',(jobs[0]/'results/study/source.tar.gz').read_bytes()==
                    (nested/'source.tar.gz').read_bytes())
        (jobs[0]/'results/measurement.csv').write_text('my edited measurement\n')
        self.cli('worker.py','fetch',jobs[0].name)
        witnesses=list(jobs[0].glob('replaced-results-*/measurement.csv'))
        self.record('worker-repair-preserves-edit',any(p.read_text()=='my edited measurement\n' for p in witnesses) and
                    (jobs[0]/'results/measurement.csv').read_text()=='value\n42\n')
        for i in range(2):
            jobid=f'fixture-interrupt-{i}'; jobdir=self.repo/'build/workers'/jobid; jobdir.mkdir()
            prefix='sixdb/workers/'+jobid
            job={'format':2,'id':jobid,'prefix':prefix,'uri':'s3://fixture/'+prefix,
                 'config':{'bucket':'fixture','region':'fixture','env':{}}}
            (jobdir/'job.json').write_text(json.dumps(job))
            self.object('fixture',prefix+'/status.json',json.dumps({'state':'complete','artifact':reference}).encode())
            marker=self.work/'backend/paused'; marker.unlink(missing_ok=True)
            self.interrupt([sys.executable,self.tools/'worker.py','fetch',jobid],marker.exists,
                           extra={'PUBLIC_PAUSE_GET':'1'})
        worker_abandoned=list((self.repo/'build/workers').glob('fixture-interrupt-*/.collection-*/.fetch-*/bundle.tar.gz'))
        self.record('worker-interrupted-staging-bounded',sum(p.stat().st_size for p in worker_abandoned)<=WORKER_BUDGET,
                    abandoned_archives=len(worker_abandoned),retained_bytes=sum(p.stat().st_size for p in worker_abandoned))
        self.cli('worker.py','cache')
        self.cli('local_cache.py')
        self.record('declared-budget-gap-persists-after-report',measure(self.repo/'build/workers')['allocated']<=WORKER_BUDGET and
                    measure(self.repo/'build')['allocated']<=LOCAL_BUDGET,
                    worker_actual=measure(self.repo/'build/workers'),local_with_workers=measure(self.repo/'build'))
        (jobs[0]/'.keep-local').touch()
        unique=self.repo/'build/unique-evidence.bin'; unique.write_bytes(self.payload('unique-evidence'))
        self.cli('worker.py','cache','--prune','--older-hours','0')
        self.record('public-prune-preserves-pin-edit-and-unique',
                    (jobs[0]/'results/input.bin').read_bytes()==data and unique.read_bytes()==self.payload('unique-evidence') and
                    any(p.exists() and p.read_text()=='my edited measurement\n' for p in witnesses))
        self.record('public-prune-cannot-enforce-budget',measure(self.repo/'build/workers')['allocated']<=WORKER_BUDGET,
                    budget=WORKER_BUDGET,actual=measure(self.repo/'build/workers'),
                    expected_behavior='preserve pinned/edited/unique bytes, refuse subsequent over-cap admission safely')
        # Route must have stayed private: all command paths are from this fixture.
        self.record('fixture-size-guard',measure(self.work)['allocated']<=MAX_BYTES,actual=measure(self.work),limit=MAX_BYTES)

    def save(self, error=None):
        report=dict(format=1,source=str(self.source),work=str(self.work),versions=self.versions,
                    replay_sha256=digest(Path(__file__)),
                    cases=self.cases,commands=self.records,error=str(error) if error else None,
                    budgets=dict(local=LOCAL_BUDGET,worker=WORKER_BUDGET),fixture=measure(self.work))
        (self.work/'report.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
        return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[4])
    parser.add_argument('--work',type=Path,required=True,help='new directory; never an existing cache or checkout')
    parser.add_argument('--expect-bounded',action='store_true',help='exit 1 when an external boundedness expectation fails')
    args=parser.parse_args()
    if sys.platform!='linux': parser.error('run on Linux (orb -m ubuntu from the Mac)')
    if args.work.exists(): parser.error('choose a new work directory; existing work is preserved')
    # Each fixture file is tiny, including interrupted writers; this is an additional per-file stop.
    resource.setrlimit(resource.RLIMIT_FSIZE,(2*MIB,2*MIB))
    replay=Replay(args.source,args.work)
    try:
        replay.execute()
    except BaseException as error:
        replay.save(error); raise
    report=replay.save()
    failed=[c['name'] for c in report['cases'] if not c['success']]
    print(json.dumps({'report':str(args.work/'report.json'),'failed_expectations':failed},sort_keys=True))
    return int(args.expect_bounded and bool(failed))


if __name__=='__main__': sys.exit(main())
