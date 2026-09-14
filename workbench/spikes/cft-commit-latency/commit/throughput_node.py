#!/usr/bin/env python3
"""Three-host coordination for latency/throughput frontiers."""
import csv
import gzip
import hashlib
import http.server
import itertools
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import threading
import time
import urllib.request

OUT=Path(os.environ['SIXDB_RESULTS'])
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from persistence.run import command,inventory,require
from summarize import stats
from throughput_summary import read,summarize

PREFIX=os.environ['CFT_RUN_URI'];NODE=int(os.environ['CFT_NODE']);PROFILE=os.environ.get('CFT_PROFILE','small');SCALED=PROFILE!='small'
MTU=8900 if SCALED else 9001
os.environ['CFT_PACKET_BYTES']=str(MTU-28)
BINARY=ROOT/'build/clang/dev/workbench/spikes/cft-commit-latency/cft_throughput_bench'
DEADLINE=time.monotonic()+5200
rows=[];mounted={};formatted=set();process=None


def save(name,value):
    (OUT/name).write_text(json.dumps(value,indent=2)+'\n')


def put(key,value):
    path=OUT/('mailbox-'+key.replace('/','-'));save(path.name,value)
    command(['aws','s3','cp','--only-show-errors',str(path),PREFIX+'/'+key],timeout=30)


def get(key):
    result=command(['aws','s3','cp','--only-show-errors',PREFIX+'/'+key,'-'],check=False,timeout=30)
    return json.loads(result['stdout']) if result['returncode']==0 else None


def wait_for(fn,interval=.1):
    while time.monotonic()<DEADLINE:
        value=fn()
        if value is not None:return value
        time.sleep(interval)
    raise TimeoutError('study deadline')


def metadata(path):
    base='http://169.254.169.254/latest/'
    req=urllib.request.Request(base+'api/token',method='PUT',headers={'X-aws-ec2-metadata-token-ttl-seconds':'60'})
    token=urllib.request.urlopen(req,timeout=3).read().decode()
    req=urllib.request.Request(base+path,headers={'X-aws-ec2-metadata-token':token})
    return urllib.request.urlopen(req,timeout=3).read().decode()


class Barrier(http.server.BaseHTTPRequestHandler):
    condition=threading.Condition();arrivals={};failure=False

    def do_GET(self):
        key,node=self.path.strip('/').rsplit('/',1)
        with self.condition:
            if key=='failure':type(self).failure=True
            peers=self.arrivals.setdefault(key,set());peers.add(node);self.condition.notify_all()
            ok=self.condition.wait_for(lambda:len(peers)==3 or self.failure,timeout=900) and not self.failure
        self.send_response(200 if ok else 503);self.end_headers();self.wfile.write(b'ready' if ok else b'failed')

    def log_message(self,*args):pass


def barrier(key):
    with urllib.request.urlopen(f"http://{PEERS[0]['ip']}:43000/{key}/{NODE}",timeout=910) as reply:
        require(reply.read()==b'ready','cohort barrier')


def diagnostic(key):
    data={'utc':time.time(),'node':NODE}
    commands={'link':['ip','-j','-d','link','show','dev',NETWORK],
              'driver':['ethtool','-i',NETWORK],'offloads':['ethtool','-k',NETWORK],
              'counters':['ethtool','-S',NETWORK],'protocols':['cat','/proc/net/snmp'],
              'rings':['ethtool','-g',NETWORK],'channels':['ethtool','-l',NETWORK],'coalescing':['ethtool','-c',NETWORK],
              'tcp-settings':['sysctl','net.ipv4.tcp_limit_output_bytes','net.ipv4.tcp_autocorking','net.ipv4.tcp_congestion_control','net.ipv4.tcp_rmem','net.ipv4.tcp_wmem'],
              'cpu':['cat','/proc/stat'],'interrupts':['cat','/proc/interrupts']}
    for name,args in commands.items():data[name]=command(args,check=False)
    data['byte_queue_limits']={str(p):p.read_text().strip() for p in sorted(Path('/sys/class/net',NETWORK,'queues').glob('tx-*/byte_queue_limits/limit_min'))}
    save(key+'.json',data)
    return data


def identity(case):return hashlib.sha256(json.dumps(case,sort_keys=True).encode()).hexdigest()[:16]


def case(transport,window,batch,rate=0,wait_us=50,seconds=7):
    count=min(2000000,max(3000,round(rate*seconds))) if rate else min(250000,max(6000,window*batch*2000))
    return dict(transport=transport,window=window,batch=batch,rate=rate,wait_us=wait_us,count=count,bytes=4096,mtu=MTU,
                device='nvme',method='direct-dsync',layout='raw',placement='good',profile=PROFILE)


def run(case,stage,repeat):
    global process
    key=f'{stage}-{identity(case)}-r{repeat}'
    cookie=int.from_bytes(hashlib.sha256((PREFIX+key).encode()).digest()[:8],'little')
    # The same independent arrival trace is used across parameter settings at a given rate/repeat.
    seed=int.from_bytes(hashlib.sha256(f"{case['rate']}:{case['count']}:{repeat}".encode()).digest()[:8],'little')
    row=case|dict(key=key,node=NODE,role='leader' if NODE==2 else 'follower',stage=stage,repeat=repeat,run=cookie,seed=seed)
    barrier(key+'-begin')
    if NODE==2:put('running-case.json',row|{'utc':time.time()})
    prefix=['taskset','-c',str(CPUS[0]),str(BINARY)]
    ips=[PEERS[i]['ip'] for i in [1,0]] if NODE==2 else [ME['ip'],'-']
    args=[row['role'],case['transport'],*ips,'43001',str(case['count']),str(case['window']),str(case['batch']),str(case['rate']),str(case['wait_us']),DEVICE,'raw',str(OUT/(key+'.csv')),str(cookie),str(seed)]
    row['command']=prefix+args
    if NODE!=2:
        with (OUT/(key+'.stderr')).open('w') as errors,(OUT/(key+'.stdout')).open('w') as stdout:
            process=subprocess.Popen(prefix+args,stdout=stdout,stderr=errors)
        def ready():
            require(process.poll() is None,'pipeline follower exited before ready')
            return True if (OUT/(key+'.csv.ready')).exists() else None
        wait_for(ready)
    barrier(key+'-ready');row['started']=time.time()
    if NODE==2:
        result=command(prefix+args,timeout=900);row.update(json.loads(result['stdout']))
        path=OUT/(key+'.csv');row.update(summarize(read(path),row))
        with gzip.open(str(path)+'.gz','wb') as f:f.write(path.read_bytes())
        path.unlink();row['file']=path.name+'.gz'
    else:
        require(process.wait(timeout=910)==0,'pipeline follower failed');process=None
        row.update(json.loads((OUT/(key+'.stdout')).read_text()))
    row['ended']=time.time()
    result=command(prefix+['recover',DEVICE,'raw',str(case['count']),str(cookie)],timeout=900)
    row['recovered_records']=json.loads(result['stdout'])['verified_records'];require(row['recovered_records']==case['count'],'all pipeline records recovered')
    rows.append(row);save('throughput-cases.json',rows)
    barrier(key+'-finished')
    if NODE==2:
        print(json.dumps({k:row[k] for k in ['key','transport','window','batch','rate','goodput_rps','p50_us','p90_us','p99_us','p999_us','stable_observed']}),flush=True)
        put('latest.json',row)


def choose_open():
    closed=[r for r in rows if r['stage']=='closed']
    selected=[case(p,16,1,1000,0) for p in ['tcp','udp']]
    for transport in ['tcp','udp']:
        candidates=[r for r in closed if r['transport']==transport]
        # Unbatched candidate, highest observed capacity, and an intermediate batch candidate.
        best=max(candidates,key=lambda r:r['goodput_rps'])
        unbatched=max([r for r in candidates if r['batch']==1],key=lambda r:r['goodput_rps'])
        middle=max([r for r in candidates if r['batch']==4],key=lambda r:r['goodput_rps'])
        for row in { (r['window'],r['batch']):r for r in [best,unbatched,middle] }.values():
            capacity=row['goodput_rps']
            for fraction in ([.05,.5,.85,1.05] if SCALED else [.05,.25,.5,.7,.85,1.0,1.15]):
                rate=max(1000,round(capacity*fraction/100)*100)
                selected.append(case(transport,row['window'],row['batch'],rate,50))
        # Matched rates across batch caps expose fill-time/capacity effects at the same workload.
        for fraction in ([.7] if SCALED else [.5,.85]):
            rate=max(1000,round(best['goodput_rps']*fraction/100)*100)
            for batch_size in [1,4,16,64]:selected.append(case(transport,16,batch_size,rate,50))
        # Batch-wait sensitivity on the highest-capacity configuration.
        rate=max(1000,round(best['goodput_rps']*.7/100)*100)
        for wait in ([0,100] if SCALED else [0,20,100,250]):selected.append(case(transport,best['window'],best['batch'],rate,wait))
    return list({identity(c):c for c in selected}.values())


def choose_tail():
    candidates=[r for r in rows if r['stage']=='open' and r['stable_observed']]
    selected={}
    for transport in ['tcp','udp']:
        values=[r for r in candidates if r['transport']==transport]
        if not values:continue
        chosen=[max(values,key=lambda r:r['goodput_rps'])]
        for percentile in ['p50_us','p90_us','p99_us','p999_us']:
            baseline=min(values,key=lambda r:r[percentile]);chosen.append(baseline)
            for allowance in [1.10,1.25]:
                eligible=[r for r in values if r[percentile]<=allowance*baseline[percentile]]
                chosen.append(max(eligible,key=lambda r:r['goodput_rps']))
            subms=[r for r in values if r[percentile]<1000]
            if subms:chosen.append(max(subms,key=lambda r:r['goodput_rps']))
        for row in chosen:
            c=case(transport,row['window'],row['batch'],row['rate'],row['wait_us'],seconds=16)
            selected[identity(c)]=c
    return list(selected.values())


try:
    require(os.geteuid()==0 and os.environ['SIXDB_WORKER_REUSED']=='0','fresh root throughput worker')
    manifest=json.loads(Path(os.environ['SIXDB_DEVICES']).read_text());save('storage-manifest.json',manifest)
    DEVICE=str(Path(manifest['instance_store'][0]['device']).resolve())
    require(DEVICE not in [str(Path(p).resolve()) for p in manifest['root_devices']],'refuse root throughput disk')
    blocks=json.loads(command(['lsblk','-b','-J','-o','PATH,TYPE,MOUNTPOINTS,SIZE',DEVICE])['stdout'])['blockdevices']
    require(len(blocks)==1 and blocks[0]['type']=='disk' and not blocks[0].get('children') and not any(blocks[0]['mountpoints'] or []) and blocks[0]['size']>=16*1024**3,'unmounted whole disposable throughput disk')
    save('nvme-before.json',inventory(DEVICE,'nvme'))
    if PROFILE=='express':save('ena-express-ready.json',wait_for(lambda:get('ena-express-ready.json'),3))
    document=json.loads(metadata('dynamic/instance-identity/document'))
    ME={'node':NODE,'ip':document['privateIp'],'az':document['availabilityZone'],'az_id':metadata('meta-data/placement/availability-zone-id'),'identity':document}
    NETWORK=json.loads(command(['ip','-j','route','show','default'])['stdout'])[0]['dev']
    CPUS=sorted(os.sched_getaffinity(0));require(len(CPUS)>=2,'at least two physical CPUs');os.sched_setaffinity(0,{CPUS[1]})
    diagnostic('before-tuning')
    command(['ip','link','set','dev',NETWORK,'mtu',str(MTU)])
    save('socket-tuning.json',command(['sysctl','-w','net.core.rmem_max=33554432','net.core.wmem_max=33554432']))
    if SCALED:
        # Matched settings on standard ENA and Express; UDP packets fit SRD's MTU.
        save('scaled-tcp-tuning.json',command(['sysctl','-w','net.ipv4.tcp_limit_output_bytes=4194304','net.ipv4.tcp_autocorking=0']))
        for path in Path('/sys/class/net',NETWORK,'queues').glob('tx-*/byte_queue_limits/limit_min'):path.write_text('max\n')
    if NODE==0:
        server=http.server.ThreadingHTTPServer(('0.0.0.0',43000),Barrier);threading.Thread(target=server.serve_forever,daemon=True).start()
    put(f'ready/{NODE}.json',ME)
    if NODE==0:
        peers=[wait_for(lambda i=i:get(f'ready/{i}.json'),3) for i in range(3)]
        require([p['az_id'] for p in peers]==['use1-az1','use1-az2','use1-az4'],'throughput stable AZ IDs');put('peers.json',peers)
    PEERS=wait_for(lambda:get('peers.json'),3);save('peers.json',PEERS);diagnostic('initial')
    for transport in ['tcp','udp']:
        diagnostic('preflight-'+transport+'-before')
        run(case(transport,4,4)|{'count':2000},'preflight',0)
        detail=diagnostic('preflight-'+transport+'-after')
        put(f'network-preflight-{NODE}-{transport}.json',detail)
    for stage in ['closed','open','tail']:
        if stage=='closed':
            choices=[(64,1),(64,4),(16,16),(4,64),(16,64)] if SCALED else list(itertools.product([1,4,16,64],[1,4,16,64]))
            cases=[case(p,w,b) for p in ['tcp','udp'] for w,b in choices]
        else:
            if NODE==2:put(stage+'-plan.json',choose_open() if stage=='open' else choose_tail())
            cases=wait_for(lambda:get(stage+'-plan.json'),3)
        save(stage+'-plan.json',cases)
        for repeat in range(3 if stage=='tail' else 1):
            order=list(cases);random.Random(9891+repeat).shuffle(order)
            diagnostic(f'{stage}-r{repeat}-before')
            for c in order:run(c,stage,repeat)
            diagnostic(f'{stage}-r{repeat}-after');barrier(f'{stage}-r{repeat}-done')
    save('nvme-after.json',inventory(DEVICE,'nvme'));barrier('all-done')
    if NODE==2:put('progress.json',{'state':'complete','utc':time.time()})
except Exception as error:
    save('failure.json',{'error':repr(error),'utc':time.time(),'node':NODE})
    try:urllib.request.urlopen(f"http://{PEERS[0]['ip']}:43000/failure/{NODE}",timeout=3).read()
    except Exception:pass
    raise
finally:
    if process is not None and process.poll() is None:process.kill();process.wait()
