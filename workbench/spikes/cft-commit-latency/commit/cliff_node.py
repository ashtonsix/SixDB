#!/usr/bin/env python3
"""Time-aligned, same-host interventions for the repeated CFT latency cliff."""
import gzip,hashlib,http.server,json,os,shutil,struct,subprocess,sys,threading,time,urllib.request
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[4]
OUT=Path(os.environ['SIXDB_RESULTS'])
sys.path.insert(0,str(ROOT/'workbench/tools'))
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from worker_context import GroupContext
from persistence.run import command,inventory,require

ctx=GroupContext.from_env()
NODE=int(os.environ['CFT_NODE']);PROFILE=os.environ['CFT_PROFILE']
BINARY=ROOT/'build/clang/dev/workbench/spikes/cft-commit-latency/cft_throughput_bench'
CPUS=sorted(os.sched_getaffinity(0));os.sched_setaffinity(0,{CPUS[1]})
NETWORK=json.loads(command(['ip','-j','route','show','default'])['stdout'])[0]['dev']
rows=[];process=None;sampler_stop=threading.Event();mounted=False

def save(name,value):
    (OUT/name).write_text(json.dumps(value,indent=2)+'\n')

class Barrier(http.server.BaseHTTPRequestHandler):
    condition=threading.Condition();arrivals={};failure=False
    def do_GET(self):
        key,node=self.path.strip('/').rsplit('/',1)
        with self.condition:
            if key=='failure':type(self).failure=True
            peers=self.arrivals.setdefault(key,set());peers.add(node);self.condition.notify_all()
            ok=self.condition.wait_for(lambda:len(peers)==3 or self.failure,timeout=600) and not self.failure
        self.send_response(200 if ok else 503);self.end_headers();self.wfile.write(b'ready' if ok else b'failed')
    def log_message(self,*args):pass

def barrier(key):
    with urllib.request.urlopen(f'http://{PEERS[0]}:43000/{key}/{NODE}',timeout=610) as response:
        require(response.read()==b'ready','probe barrier')

def sample(key):
    last_nvme=0
    with (OUT/(key+'.monitor.jsonl')).open('w') as out:
        while not sampler_stop.is_set():
            before=time.monotonic_ns()
            result=command(['ethtool','-S',NETWORK],check=False,timeout=5)
            counters={}
            for line in result['stdout'].splitlines():
                k,sep,v=line.strip().partition(': ')
                if sep and v.isdigit():counters[k]=int(v)
            record={'steady_ns':before,'utc_ns':time.time_ns(),'end_ns':time.monotonic_ns(),'ena':counters}
            # AWS documents at most one vendor-statistics request per second.
            if before-last_nvme>=1100000000:
                last_nvme=before
                probe=subprocess.run(['nvme','get-log',DEVICE,'--log-id=0xd0','--log-len=8192','--namespace-id=1','--raw-binary'],capture_output=True,timeout=5)
                record['nvme_result']=probe.returncode;record['nvme_end_ns']=time.monotonic_ns()
                if probe.returncode==0 and len(probe.stdout)==8192:
                    magic,version=struct.unpack_from('<II',probe.stdout)
                    record['nvme_magic']=magic;record['nvme_version']=version
                    if magic==0xec2c0d7e:
                        names=['read_ops','write_ops','read_bytes','write_bytes','read_time_us','write_time_us','volume_iops_exceeded_us','volume_tp_exceeded_us','instance_iops_exceeded_us','instance_tp_exceeded_us','queue_length']
                        record['nvme']=dict(zip(names,struct.unpack_from('<11Q',probe.stdout,8)))
                    else:record['nvme_error']='unexpected vendor log magic'
                else:record['nvme_error']=probe.stderr.decode(errors='replace')[:1000]
            for name,path in [('disk',f'/sys/class/block/{Path(DEVICE).name}/stat'),('vm','/proc/vmstat'),('cpu','/proc/stat'),('net','/proc/net/dev'),('memory','/proc/meminfo')]:
                record[name]=Path(path).read_text()
            if process and process.poll() is None:
                for name in ['schedstat','io','stat']:
                    try:record['process_'+name]=Path(f'/proc/{process.pid}/{name}').read_text()
                    except FileNotFoundError:pass
            out.write(json.dumps(record)+'\n');out.flush()
            sampler_stop.wait(max(0,.1-(time.monotonic_ns()-before)/1e9))

def quick(path):
    data=np.fromfile(path,dtype=np.dtype([('arrival','<u8'),('prepared','<u8'),('commit','<u8'),('batch','<u8'),('records','<u4'),('padding','<u4')]))
    require(np.all(data['commit']>=data['prepared']) and np.all(data['prepared']>=data['arrival']),'probe timestamps')
    selected=data[data['arrival']>=1000000000]
    result={'measured_records':len(selected),'measurement_seconds':float((data['arrival'][-1]-1000000000)/1e9)}
    for name,values in [('latency',selected['commit']-selected['arrival']),('queue_prepare',selected['prepared']-selected['arrival']),('post_prepare',selected['commit']-selected['prepared'])]:
        result[name+'_ms']=np.quantile(values,[.5,.9,.99,.999,1]).tolist();result[name+'_ms']=[x/1e6 for x in result[name+'_ms']]
    result['below_1ms_pct']=100*float(np.mean(selected['commit']-selected['arrival']<1000000))
    result['goodput_rps']=int(np.count_nonzero((data['commit']>=1000000000)&(data['commit']<=data['arrival'][-1])))/result['measurement_seconds']
    return result

def run(case,index):
    global process,mounted
    key=f'{index:02d}-{case["name"]}';cookie=int.from_bytes(hashlib.sha256((ctx.uri+key).encode()).digest()[:8],'little')
    seed=9137+case.get('repeat',0);count=round(case['rate']*case['seconds'])
    layout=case.get('layout','raw');log=DEVICE
    if layout!='raw':
        mount=Path('/mnt/cft-cliff');mount.mkdir(exist_ok=True)
        if not mounted:
            save('format.json',command(['mkfs.ext4','-F','-b','4096','-E','lazy_itable_init=0,lazy_journal_init=0',DEVICE,'16777216']))
            command(['mount','-t','ext4','-o','data=ordered,noatime,max_batch_time=0',DEVICE,str(mount)]);mounted=True
            save('filesystem.json',command(['findmnt','-J',str(mount)]))
        log=str(mount/'append.wal')
    else:require(not mounted,'raw writes after mounting forbidden')
    row=case|{'key':key,'node':NODE,'run':cookie,'seed':seed,'count':count,'layout':layout,'device':DEVICE,'log':log}
    output=OUT/(key+'.bin');env=os.environ|{'CFT_PROBE':'1','CFT_BINARY':'1','CFT_BASE_BATCH':str(case.get('base_batch',case['batch'])),'CFT_ADAPT_BATCH':str(int(case.get('adaptive',False))),'CFT_SKIP_INITIALIZE':str(int(case.get('skip_initialize',False))),'CFT_PREP_ADAPT':str(int(case.get('prep_adaptive',False))),'CFT_PREP_CPU':str(CPUS[min(2,len(CPUS)-1)]),'CFT_BURST':str(int(case.get('burst',False)))}
    prefix=['taskset','-c',str(CPUS[0]),str(BINARY)]
    ips=[PEERS[1],PEERS[0]] if NODE==2 else [PEERS[NODE],'-']
    args=['leader' if NODE==2 else 'follower',case.get('transport','tcp'),*ips,'43001',str(count),str(case.get('window',64)),str(case['batch']),str(case['rate']),str(case.get('wait_us',50)),log,layout,str(output),str(cookie),str(seed)]
    row['command']=prefix+args;row['environment']={k:v for k,v in env.items() if k.startswith('CFT_')}
    ctx.progress('preparing',case=key,completed=index,total=len(PLAN));barrier(key+'-begin')
    logs=[(OUT/(key+'.stdout')).open('w'),(OUT/(key+'.stderr')).open('w')]
    if NODE!=2:
        process=subprocess.Popen(prefix+args,env=env,stdout=logs[0],stderr=logs[1])
        deadline=time.monotonic()+300
        while not Path(str(output)+'.ready').exists():
            require(process.poll() is None,'follower setup failed');require(time.monotonic()<deadline,'follower preparation deadline');time.sleep(.05)
    barrier(key+'-ready');ctx.progress('measuring',case=key,completed=index,total=len(PLAN))
    sampler_stop.clear();monitor=threading.Thread(target=sample,args=(key,));monitor.start();row['started']=time.time()
    try:
        if NODE==2:process=subprocess.Popen(prefix+args,env=env,stdout=logs[0],stderr=logs[1])
        require(process.wait(timeout=360)==0,'native probe failed')
    finally:
        sampler_stop.set();monitor.join();[f.close() for f in logs]
    row['ended']=time.time();process=None
    row.update(json.loads((OUT/(key+'.stdout')).read_text()))
    ctx.progress('verifying',case=key,completed=index,total=len(PLAN))
    recovered=command(prefix+['recover',log,layout,str(count),str(cookie)],timeout=300)
    row['verified_records']=json.loads(recovered['stdout'])['verified_records'];require(row['verified_records']==count,'all records direct-readback')
    if NODE==2:row.update(quick(output));print(json.dumps(row),flush=True)
    rows.append(row);save('cliff-cases.json',rows)
    ctx.publish('latest',row);barrier(key+'-done')

try:
    require(os.geteuid()==0 and os.environ['SIXDB_WORKER_REUSED']=='0','fresh disposable probe workers')
    manifest=json.loads(Path(os.environ['SIXDB_DEVICES']).read_text());save('storage-manifest.json',manifest)
    DEVICE=str(Path(manifest['instance_store'][0]['device']).resolve())
    require(DEVICE not in [str(Path(p).resolve()) for p in manifest['root_devices']],'refuse root disk')
    blocks=json.loads(command(['lsblk','-b','-J','-o','PATH,TYPE,MOUNTPOINTS,SIZE',DEVICE])['stdout'])['blockdevices']
    require(len(blocks)==1 and blocks[0]['type']=='disk' and not blocks[0].get('children') and not any(blocks[0]['mountpoints'] or []),'unmounted whole disposable disk')
    save('device-before.json',inventory(DEVICE,'nvme'))
    mtu=9001 if PROFILE=='small' else 8900;os.environ['CFT_PACKET_BYTES']=str(mtu-28)
    save('link-before.json',command(['ip','-j','-d','link','show','dev',NETWORK]))
    command(['ip','link','set','dev',NETWORK,'mtu',str(mtu)])
    save('tuning.json',command(['sysctl','-w','net.core.rmem_max=33554432','net.core.wmem_max=33554432']))
    if PROFILE!='small':
        save('tcp-tuning.json',command(['sysctl','-w','net.ipv4.tcp_limit_output_bytes=4194304','net.ipv4.tcp_autocorking=0']))
        for path in Path('/sys/class/net',NETWORK,'queues').glob('tx-*/byte_queue_limits/limit_min'):path.write_text('max\n')
    if PROFILE=='express':save('network-ready.json',ctx.wait_values('network-ready',['controller'],timeout=600))
    if NODE==0:
        server=http.server.ThreadingHTTPServer(('0.0.0.0',43000),Barrier);threading.Thread(target=server.serve_forever,daemon=True).start()
    ip=json.loads(command(['ip','-j','route','get','169.254.169.254'])['stdout'])[0]['prefsrc']
    ctx.ready({'ip':ip,'node':NODE,'cpus':CPUS,'device':DEVICE})
    ready=ctx.wait_ready(timeout=600);PEERS={v['node']:v['ip'] for v in ready.values()};save('peers.json',ready)
    PLAN=json.loads((ROOT/os.environ['CFT_PLAN']).read_text());save('plan.json',PLAN)
    for index,case in enumerate(PLAN):run(case,index)
    ctx.progress('compressing',completed=len(PLAN),total=len(PLAN))
    for path in OUT.iterdir():
        if path.name.endswith(('.bin','.detail','.jsonl')):
            with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
            path.unlink()
    save('device-after.json',inventory(DEVICE,'nvme'));barrier('all-done');ctx.progress('complete',completed=len(PLAN),total=len(PLAN))
except Exception as error:
    save('failure.json',{'error':repr(error),'node':NODE,'utc':time.time()})
    if process and process.poll() is None:process.kill();process.wait()
    try:urllib.request.urlopen(f'http://{PEERS[0]}:43000/failure/{NODE}',timeout=2).read()
    except Exception:pass
    raise
finally:
    if mounted:command(['umount','/mnt/cft-cliff'],check=False)
