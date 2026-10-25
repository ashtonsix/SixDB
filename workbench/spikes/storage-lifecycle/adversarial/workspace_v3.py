#!/usr/bin/env python3
"""Independent, bounded reliability review of workspace.py (Linux via OrbStack).

Uses only fresh fixture regular images under the selected output directory. No
real devices, real caches, builds, network or external services are used.
"""
import argparse
import array
import hashlib
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import threading
import time

sys.dont_write_bytecode = True

MIB = 1024 ** 2
EXPECTED = '5ed5a1c768964e832389c0e48f942d9e1498105748f0dc21d7948ade16d08ccf'
ROOT = Path(__file__).resolve().parents[4]
WRAPPER = ROOT / 'workbench/spikes/storage-lifecycle/workspace.py'


def run(argv, timeout=30):
    p = subprocess.run([str(x) for x in argv], text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=timeout)
    return {'code': p.returncode, 'stdout': p.stdout, 'stderr': p.stderr}


def privileged(code, *args):
    return run(['sudo', '-n', 'python3', '-c', code, *args])


def image_state(pool):
    r = privileged("""import os,json,sys,stat,subprocess
p=sys.argv[1]; result=[]
if os.path.exists(p):
 for name in sorted(os.listdir(p)):
  q=p+'/'+name;s=os.lstat(q)
  item={'name':name,'bytes':s.st_size,'allocated':s.st_blocks*512,'mode':oct(stat.S_IMODE(s.st_mode)),'uid':s.st_uid}
  if name.endswith(('.img','.new')):
   assert stat.S_ISREG(s.st_mode)
   item['loops']=subprocess.run(['losetup','--associated',q],capture_output=True,text=True).stdout
  result.append(item)
print(json.dumps(result))""", str(pool))
    assert r['code'] == 0, r
    return json.loads(r['stdout'])


def mounted_inspect(image, paths=()):
    # Explicitly verifies a fresh fixture regular image before mounting it.
    code = """import os,sys,stat,json,subprocess,tempfile
p=sys.argv[1];s=os.lstat(p)
assert stat.S_ISREG(s.st_mode) and s.st_nlink==1 and s.st_size<=16*1024**2
m=tempfile.mkdtemp(prefix='workspace-v3-ro-')
subprocess.run(['mount','-t','ext4','-o','loop,ro,noload,nodev,nosuid',p,m],check=True)
try:
 out={'fs':dict(zip(('bsize','frsize','blocks','bfree','bavail','files','ffree','favail','flag','namemax'),os.statvfs(m))), 'mount':subprocess.run(['findmnt','-n','-o','SOURCE,FSTYPE,OPTIONS',m],capture_output=True,text=True).stdout, 'files':{}}
 for rel in sys.argv[2:]:
  q=m+'/'+rel
  out['files'][rel]=open(q).read() if os.path.isfile(q) else None
 print(json.dumps(out))
finally:
 subprocess.run(['umount',m],check=True);os.rmdir(m)
"""
    return run(['sudo', '-n', 'unshare', '--mount', '--propagation', 'private',
                'python3', '-c', code, str(image), *paths])


def process_tree(pid):
    result = privileged("""import os,sys,json
root=int(sys.argv[1]); all={}
for entry in os.listdir('/proc'):
 if not entry.isdigit():continue
 try:
  raw=open('/proc/'+entry+'/stat').read();rest=raw[raw.rindex(')')+2:].split()
  all[int(entry)]={'pid':int(entry),'ppid':int(rest[1]),'start':rest[19], 'state':rest[0], 'command':open('/proc/'+entry+'/cmdline','rb').read().replace(b'\\0',b' ').decode(errors='replace'), 'namespace':os.readlink('/proc/'+entry+'/ns/pid')}
 except (OSError,ValueError):pass
selected={root}; changed=True
while changed:
 old=len(selected);selected.update(p for p,v in all.items() if v['ppid'] in selected);changed=len(selected)>old
print(json.dumps([all[p] for p in sorted(selected) if p in all]))""", str(pid))
    assert result['code'] == 0, result
    return json.loads(result['stdout'])


def still_live(processes):
    result = privileged("""import os,sys,json
live=[]
for p in json.loads(sys.argv[1]):
 try:
  raw=open('/proc/'+str(p['pid'])+'/stat').read();r=raw[raw.rindex(')')+2:].split()
  if r[19]==p['start']:live.append({'pid':p['pid'],'state':r[0]})
 except OSError:pass
print(json.dumps(live))""", json.dumps(processes))
    return json.loads(result['stdout'])


def helper_fd(source, work):
    """An existing outside helper can transfer a writable mount reference."""
    path=Path('/tmp')/('ws-v3-fd-'+str(os.getpid())+'.sock')
    listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
    listener.bind(str(path));listener.listen(1);listener.settimeout(3)
    errors=[]
    def helper():
        try:
            conn,_=listener.accept()
            with conn:
                fd=os.open(source/'fd-output',os.O_CREAT|os.O_WRONLY|os.O_TRUNC,0o600)
                try:conn.sendmsg([b'fd'],[(socket.SOL_SOCKET,socket.SCM_RIGHTS,array.array('i',[fd]))])
                finally:os.close(fd)
        except Exception as exc:errors.append(repr(exc))
    thread=threading.Thread(target=helper);thread.start()
    result=work(code="import socket,array,os; s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.connect("+repr(str(path))+");msg,anc,flags,addr=s.recvmsg(10,socket.CMSG_SPACE(4));a=array.array('i');a.frombytes(anc[0][2][:4]);fd=a[0];os.write(fd,b'consumer wrote via received fd');os.fsync(fd);os.close(fd);s.close()")
    thread.join(4);listener.close();path.unlink()
    return {'consumer':result,'helper_errors':errors,
            'outside_file':(source/'fd-output').read_text() if (source/'fd-output').exists() else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--resume', type=Path, help='resume only supplemental cases in an existing fixture')
    args = parser.parse_args()
    out = args.output.absolute()
    out.mkdir(parents=True, exist_ok=True)
    if not args.resume and list(out.glob('fixtures-*')):
        raise RuntimeError('existing review fixtures remain; remove only verified inactive fixtures before rerunning')
    fixtures = args.resume.absolute() if args.resume else out / ('fixtures-' + str(os.getpid()))
    if not args.resume:
        fixtures.mkdir()
    pool = fixtures / 'pool'
    source = fixtures / 'source'
    if not args.resume:
        source.mkdir()
        (source / 'keep.txt').write_text('acknowledged-source-v3\n')
    evidence = json.loads((out/'results.json').read_text()) if args.resume else {
        'sha256': hashlib.sha256(WRAPPER.read_bytes()).hexdigest(),
        'fixtures': str(fixtures), 'cases': {}}
    assert evidence['sha256'] == EXPECTED

    def record(name, result):
        evidence['cases'][name] = result
        (out / 'results.json').write_text(json.dumps(evidence, indent=2) + '\n')
        print(name, json.dumps(result)[:1200], flush=True)

    def argv(name='a', size=16*MIB, budget=49*MIB, code='pass', selected=pool):
        return [sys.executable, str(WRAPPER), '--pool', str(selected), '--budget',
                str(budget), 'run', name, '--size', str(size), '--',
                sys.executable, '-c', code]

    def work(name='a', code='pass', **kwargs):
        return run(argv(name=name, code=code, **kwargs))

    def extras():
        supplemental(out, fixtures, source, pool, evidence, record, argv, work)

    if args.resume:
        extras()
        return

    record('plain-help', run([sys.executable, str(WRAPPER), '--help']))
    record('run-help', run([sys.executable, str(WRAPPER), '--pool', str(pool),
                            '--budget', str(49*MIB), 'run', '--help']))
    record('invalid-request-boundaries', [work(size=size) for size in
        (0, 16*MIB-4096, 16*MIB+1, 128*MIB+4096)] + [work('../escape')])
    record('first', work(code="from pathlib import Path; Path('incremental').write_text('one\\n'); print('created')"))
    record('restart', work(code="from pathlib import Path; p=Path('incremental'); assert p.read_text()=='one\\n'; p.write_text(p.read_text()+'two\\n'); print(p.read_text())"))
    record('independent-first', mounted_inspect(pool/'a.img', ['work/incremental']))
    record('allocate-b', work('b'))
    record('allocate-c', work('c'))
    record('overcommit', work('d'))
    record('immutable-size', work(size=20*MIB))
    record('immutable-budget', work(budget=50*MIB))
    record('after-admission', image_state(pool))

    # Repeated reuse and independent process launches; global lock should serialize.
    record('repeated', [work(code="from pathlib import Path; p=Path('count'); n=int(p.read_text()) if p.exists() else 0; p.write_text(str(n+1))") for _ in range(4)])
    start = time.monotonic()
    processes = [subprocess.Popen(argv(name, code="import time; from pathlib import Path; p=Path('concurrent'); p.write_text(p.read_text()+'x' if p.exists() else 'x'); time.sleep(0.15)"),
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                 for name in ('a','b','a','c')]
    runs = [dict(zip(('stdout','stderr'), p.communicate(timeout=30))) | {'code':p.returncode} for p in processes]
    record('concurrent', {'seconds': time.monotonic()-start, 'runs': runs})
    record('independent-counters', mounted_inspect(pool/'a.img', ['work/count','work/concurrent']))

    containment = """import os,json,socket,tempfile,multiprocessing as mp
from pathlib import Path
blocked={}
for path in [os.environ['SIXDB_SOURCE']+'/""" + str(source.relative_to(ROOT)) + """/keep.txt', '/tmp/workspace-v3-outside-'+str(os.getpid()), os.environ['HOME']+'/../../../../outside-v3']:
 try:
  with open(path,'w') as f:f.write('BAD')
  blocked[path]='WRITTEN'
 except OSError as e:blocked[path]=e.errno
for name in ['HOME','TMPDIR','TMP','TEMP','XDG_CACHE_HOME','XDG_STATE_HOME','CARGO_TARGET_DIR','PYTHONPYCACHEPREFIX']:
 p=Path(os.environ[name]);p.mkdir(parents=True,exist_ok=True);(p/'route-v3').write_text(name)
Path('/dev/shm/shared-v3').write_text('shared')
assert os.stat('/dev/shm').st_dev==os.stat('.').st_dev
a,b=socket.socketpair();a.send(b'ipc');assert b.recv(3)==b'ipc';a.close();b.close()
s=socket.socket();s.close()
ctx=mp.get_context('fork'); q=ctx.Queue();p=ctx.Process(target=lambda:q.put('multiprocessing-ok'));p.start();assert q.get(timeout=2)=='multiprocessing-ok';p.join(); assert p.exitcode==0
print(json.dumps({'blocked':blocked,'ipc':'ok','tmp':tempfile.gettempdir()}))
"""
    record('containment-ipc-paths', work(code=containment))
    record('source-preserved', (source/'keep.txt').read_text())
    record('independent-ipc-paths', mounted_inspect(pool/'a.img',
        ['work/home/route-v3','work/tmp/route-v3','work/cache/route-v3',
         'work/state/route-v3','work/target/route-v3','work/shm/shared-v3']))

    # Acknowledged content must survive a consumer error and wrapper loss.
    record('consumer-failure', work(code="from pathlib import Path; Path('failure-state').write_text('retained'); raise SystemExit(7)"))
    record('after-consumer-failure', mounted_inspect(pool/'a.img', ['work/incremental','work/failure-state']))
    p = subprocess.Popen(argv(code="import os,time; from pathlib import Path; pid=os.fork(); Path('descendant' if pid==0 else 'leader').write_text(str(os.getpid())); time.sleep(20)"), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(0.6)
    p.kill()
    p.communicate(timeout=5)
    time.sleep(0.5)
    record('wrapper-kill-state', image_state(pool))
    record('restart-after-wrapper-kill', work(code="from pathlib import Path; assert Path('incremental').read_text()=='one\\ntwo\\n'; print('reused')"))
    record('independent-after-wrapper-kill', mounted_inspect(pool/'a.img', ['work/leader','work/descendant','work/incremental']))

    record('disk-full', work('b', code="""import os,errno
from pathlib import Path
Path('before-full').write_text('ack')
with open('fill','wb',buffering=0) as f:
 try:
  for _ in range(32):f.write(b'x'*1048576)
 except OSError as e: assert e.errno==errno.ENOSPC; print('ENOSPC',f.tell())
"""))
    record('full-independent', mounted_inspect(pool/'b.img', ['work/before-full']))
    record('full-recover', work('b', code="from pathlib import Path; Path('fill').unlink(); print('recovered'); Path('after-full').write_text('survived')"))
    record('inode-full', work('c', code="""import errno
from pathlib import Path
Path('files').mkdir(exist_ok=True)
count=0
try:
 for i in range(10000):Path('files/'+str(i)).touch();count+=1
except OSError as e: assert e.errno==errno.ENOSPC;print('inode-ENOSPC',count)
"""))
    record('inode-full-recover', work('c', code="import shutil;shutil.rmtree('files');print('inode recovered')"))
    record('output-full', work('b', code="import os; [os.write(1,b'o'*1048576) for _ in range(32)]"))
    record('after-output-full', image_state(pool))
    record('output-full-independent', mounted_inspect(pool/'b.img', ['work/after-full']))

    extras()


def supplemental(out, fixtures, source, pool, evidence, record, argv, work):
    record('signal-command', work(code="import os,signal;from pathlib import Path;Path('signal-state').write_text('before signal');os.kill(os.getpid(),signal.SIGTERM)"))
    record('signal-command-state', mounted_inspect(pool/'a.img',['work/signal-state']))
    # Source socket represents a local helper explicitly kept outside the wrapper.
    sockpath=Path('/tmp')/('ws-v3-helper-'+str(os.getpid())+'.sock')
    listener=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
    listener.bind(str(sockpath));listener.listen(1);listener.settimeout(3)
    helper_result=[]
    def helper():
        try:
            conn,_=listener.accept()
            with conn:
                msg=conn.recv(100)
                (source/'helper-output').write_bytes(msg)
                helper_result.append(msg.decode())
        except Exception as e:helper_result.append(repr(e))
    thread=threading.Thread(target=helper);thread.start()
    record('external-helper-socket', work(code="import socket; s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM); s.connect("+repr(str(sockpath))+");s.sendall(b'outside helper writes');s.close()"))
    thread.join(4);listener.close();sockpath.unlink()
    record('external-helper-observed', {'helper':helper_result,'file':(source/'helper-output').read_text() if (source/'helper-output').exists() else None})
    record('external-helper-fd', helper_fd(source,work))

    # Corruption is limited to one of our isolated images, after preservation tests.
    record('corrupt-superblock', privileged("import os,sys;f=os.open(sys.argv[1],os.O_RDWR);os.pwrite(f,b'\\x00'*1024,1024);os.fsync(f);os.close(f)", str(pool/'c.img')))
    record('corrupt-reuse', work('c'))
    record('after-corruption', image_state(pool))
    record('final-main-state', image_state(pool))

    # Validate host process identities, including daemonization, before killing
    # only the user-owned outer wrapper. PID namespace cleanup must remove all.
    process = subprocess.Popen(argv(code="import os,time; os.fork(); os.setsid() if os.getpid()!=6 else None; time.sleep(20)"),
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    time.sleep(0.5)
    tree = process_tree(process.pid)
    process.kill();process.communicate(timeout=5);time.sleep(0.4)
    record('wrapper-descendant-teardown', {'before':tree,'survivors':still_live(tree),'images':image_state(pool)})

    # Deterministic pauses alter only invocation timing, not implementation. The
    # target file remains the reviewed hash; these are setup-failure injections.
    fault_pool=fixtures/'interrupt-pool'
    for phase in ('mkfs.ext4','mount'):
        marker=fixtures/('pause-'+phase.replace('.','-'))
        code="""import importlib.util,sys,time,os
sys.dont_write_bytecode=True
from pathlib import Path
spec=importlib.util.spec_from_file_location('under_review',sys.argv[1]);w=importlib.util.module_from_spec(spec);spec.loader.exec_module(w)
original=w.tool
def paused(args,descriptors=()):
 if args[0]==sys.argv[2]:
  if args[0]=='mount':result=original(args,descriptors)
  Path(sys.argv[3]).write_text('paused')
  time.sleep(20)
  if args[0]=='mount':return result
 return original(args,descriptors)
w.tool=paused
raise SystemExit(w.inside())
"""
        request={'pool':str(fault_pool),'budget':17*MIB,'name':'f','size':16*MIB,
                 'command':[sys.executable,'-c','pass'],'source':str(ROOT),'env':dict(os.environ)}
        p=subprocess.Popen(['sudo','-n','unshare','--mount','--pid','--ipc','--mount-proc',
                            '--propagation','private','--fork','--kill-child',sys.executable,'-c',
                            code,str(WRAPPER),phase,str(marker)],stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        p.stdin.write(json.dumps(request)+'\n');p.stdin.flush()
        deadline=time.monotonic()+5
        while not marker.exists() and p.poll() is None and time.monotonic()<deadline:time.sleep(0.01)
        assert marker.exists(), 'injection barrier not reached'
        tree=process_tree(p.pid)
        supervisor=[x for x in tree if str(marker) in x['command'] and x['pid']!=p.pid
                    and x['namespace']!=os.readlink('/proc/self/ns/pid')]
        assert len(supervisor)==1, tree
        record('paused-'+phase, {'tree':tree,'images':image_state(fault_pool)})
        record('kill-'+phase, run(['sudo','-n','kill','-KILL',str(supervisor[0]['pid'])]))
        stdout,stderr=p.communicate(timeout=5)
        time.sleep(0.2)
        record('interrupted-'+phase, {'code':p.returncode,'stdout':stdout,'stderr':stderr,
                                    'survivors':still_live(tree),'images':image_state(fault_pool)})
        record('restart-'+phase, work('f',selected=fault_pool,budget=17*MIB,
               code="from pathlib import Path;Path('acknowledged').write_text('survived interrupted setup')"))
    record('interrupt-independent', mounted_inspect(fault_pool/'f.img',['work/acknowledged']))
    original_budget=str(17*MIB)+'\n'
    record('damage-budget', privileged("import sys;open(sys.argv[1],'w').write('corrupted\\n')", str(fault_pool/'budget')))
    record('damaged-budget-refusal', work('f',selected=fault_pool,budget=17*MIB))
    record('restore-budget', privileged("import sys;open(sys.argv[1],'w').write(sys.argv[2])",str(fault_pool/'budget'),original_budget))
    record('metadata-recovery', work('f',selected=fault_pool,budget=17*MIB))

    # A 32 MiB outer filesystem emulates outer ENOSPC without filling the host.
    # Peak isolated images: main 48 + interruption 16 + outer 32 = 96 MiB.
    outer=fixtures/'outer.img'
    code="""import os,stat,subprocess,sys,json
from pathlib import Path
image=Path(sys.argv[1]); m=Path(sys.argv[2]); wrapper=sys.argv[3]
fd=os.open(image,os.O_CREAT|os.O_EXCL|os.O_RDWR,0o600);os.ftruncate(fd,32*1024**2);os.posix_fallocate(fd,0,32*1024**2);os.close(fd)
subprocess.run(['mkfs.ext4','-F','-q','-m','0','-E','nodiscard,lazy_itable_init=0,lazy_journal_init=0',str(image)],check=True)
m.mkdir();subprocess.run(['mount','-t','ext4','-o','loop,nodev,nosuid',str(image),str(m)],check=True)
try:
 filler=m/'filler'
 free=os.statvfs(m).f_bavail*os.statvfs(m).f_frsize
 f=os.open(filler,os.O_CREAT|os.O_RDWR,0o600);os.posix_fallocate(f,0,free-8*1024**2);os.close(f)
 command=['setpriv','--reuid='+os.environ['SUDO_UID'],'--regid='+os.environ['SUDO_GID'],'--init-groups','python3',wrapper,'--pool',str(m/'pool'),'--budget',str(17*1024**2),'run','outer','--size',str(16*1024**2),'--','python3','-c','pass']
 def attempt():
  p=subprocess.run(command,capture_output=True,text=True,timeout=15)
  entries=[]
  for q in (m/'pool').glob('*'):
   s=q.stat();entries.append({'name':q.name,'size':s.st_size,'allocated':s.st_blocks*512})
  return {'code':p.returncode,'stdout':p.stdout,'stderr':p.stderr,'entries':entries}
 failed=attempt();filler.unlink();recovered=attempt()
 print(json.dumps({'initial_free':free,'failed':failed,'recovered':recovered}))
finally:subprocess.run(['umount',str(m)],check=True);m.rmdir()
"""
    record('outer-enospc', run(['sudo','-n','unshare','--mount','--propagation','private',
                              'python3','-c',code,str(outer),str(fixtures/'outer-mount'),str(WRAPPER)]))
    record('outer-image-independent', privileged("import os,sys,json,subprocess;s=os.stat(sys.argv[1]);print(json.dumps({'size':s.st_size,'allocated':s.st_blocks*512,'loops':subprocess.run(['losetup','--associated',sys.argv[1]],capture_output=True,text=True).stdout}))",str(outer)))
    print('results:', out/'results.json', flush=True)


if __name__ == '__main__':
    main()
