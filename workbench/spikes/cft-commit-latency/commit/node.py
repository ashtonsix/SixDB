#!/usr/bin/env python3
"""Four-host coordination for measured durable three-voter commit rounds."""
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

PREFIX=os.environ['CFT_RUN_URI'];NODE=int(os.environ['CFT_NODE'])
BINARY=ROOT/'build/clang/dev/workbench/spikes/cft-commit-latency/cft_commit_bench'
DEADLINE=time.monotonic()+7000
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
            ok=self.condition.wait_for(lambda:len(peers)==4 or self.failure,timeout=900) and not self.failure
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
              'cpu':['cat','/proc/stat'],'interrupts':['cat','/proc/interrupts']}
    for name,args in commands.items():data[name]=command(args,check=False)
    save(key+'.json',data)


def base_case(placement,transport,mtu,size,**settings):
    return {'placement':placement,'transport':transport,'mtu':mtu,'bytes':size,
            'packet_bytes':1472 if mtu==1500 else 8973,'retry_us':2000,
            'device':'nvme','method':'direct-dsync','layout':'initialized','filesystem':'ordered-tuned',
            'surviving_only':False,**settings}


def identity(case):return hashlib.sha256(json.dumps(case,sort_keys=True).encode()).hexdigest()[:16]


def screen_cases():
    cases=[]
    for placement,size in itertools.product(['good','bad'],[512,4096,8192,65536]):
        for transport,mtu,packet in [('tcp',1500,1472),('tcp',9001,8973),('udp',1500,1472),('udp',9001,1472),('udp',9001,8973)]:
            cases.append(base_case(placement,transport,mtu,size,packet_bytes=packet))
    for placement,transport,size in itertools.product(['good','bad'],['tcp','udp'],[4096,65536]):
        for settings in [dict(layout='raw',filesystem='none'),
                         dict(method='buffered-fdatasync',layout='extend',filesystem='ordered'),
                         dict(method='direct-fdatasync'),dict(device='gp3'),dict(device='io2'),
                         dict(device='gp3',method='buffered-fdatasync',layout='extend',filesystem='ordered')]:
            cases.append(base_case(placement,transport,9001,size,**settings))
        cases.append(base_case(placement,'tcp',1500,size,device='gp3',method='buffered-fdatasync',layout='extend',filesystem='ordered'))
    return list({identity(c):c for c in cases}.values())


def prepare(case,key):
    label=case['device'];device=DEVICES[label]['device'];mount=Path('/mnt/sixdb-cft-'+label)
    mount.mkdir(exist_ok=True)
    mode=case['filesystem']
    if label in mounted and mounted[label]!=mode:
        command(['umount',str(mount)]);del mounted[label]
    if case['layout']=='raw':
        require(label not in mounted,'raw disk must be unmounted');formatted.discard(label);return device
    if label not in formatted:
        save(key+'-format.json',command(['mkfs.ext4','-F','-b','4096','-E','lazy_itable_init=0,lazy_journal_init=0',device,'2097152']))
        formatted.add(label)
    if label not in mounted:
        options={'ordered':'data=ordered','ordered-tuned':'data=ordered,noatime,max_batch_time=0'}[mode]
        command(['mount','-t','ext4','-o',options,device,str(mount)]);mounted[label]=mode
    save(key+'-mount.json',command(['findmnt','-J',str(mount)]))
    return str(mount/'wal.dat')


def round_case(case,stage,repeat,index):
    global process
    key=f'{stage}-{identity(case)}-r{repeat}'
    cookie=int.from_bytes(hashlib.sha256((key+PREFIX).encode()).digest()[:8],'little')
    leader,followers=(2,[1,0]) if case['placement']=='good' else (3,[2,1])
    if case['surviving_only']:followers=followers[1:]
    participants=[leader,*followers]
    role='leader' if NODE==leader else ('follower' if NODE in followers else 'idle')
    count=100 if stage=='preflight' else (2000 if stage=='screen' else (40000 if case['bytes']<=4096 else 12000))
    row={**case,'key':key,'node':NODE,'role':role,'stage':stage,'repeat':repeat,'run':cookie,'samples':count,
         'leader_az':PEERS[leader]['az_id'],'follower_azs':[PEERS[i]['az_id'] for i in followers]}
    barrier(key+'-begin')
    command(['ip','link','set','dev',NETWORK,'mtu',str(case['mtu'])])
    path=prepare(case,key) if role!='idle' else None
    prefix=['taskset','-c',str(CPUS[0]),str(BINARY)]
    if role!='idle':
        peer_ips=[PEERS[i]['ip'] for i in followers]+['-']
        args=[role,case['transport'],*(peer_ips[:2] if role=='leader' else [ME['ip'],'-']),
              '43001',str(case['bytes']),str(count),path,case['method'],case['layout'],str(OUT/(key+'.csv')),
              str(cookie),str(case['packet_bytes']),str(case['retry_us'])]
        row['command']=prefix+args;row['device_path']=DEVICES[case['device']]['device']
    if role=='follower':
        with (OUT/(key+'.stderr')).open('w') as errors,(OUT/(key+'.stdout')).open('w') as stdout:
            process=subprocess.Popen(prefix+args,stdout=stdout,stderr=errors)
        def ready():
            require(process.poll() is None,'follower exited before readiness')
            return True if (OUT/(key+'.csv.ready')).exists() else None
        wait_for(ready)
    barrier(key+'-servers-ready')
    started=time.time()
    if role=='leader':
        result=command(prefix+args,timeout=900);row.update(json.loads(result['stdout']))
        output=OUT/(key+'.csv')
        with output.open() as f:values=[int(v['commit_ns'])/1000 for v in csv.DictReader(f)]
        require(len(values)==count,'all requested commit samples present');row.update(stats(values))
        with gzip.open(str(output)+'.gz','wb') as f:f.write(output.read_bytes())
        output.unlink();row['file']=output.name+'.gz'
    elif role=='follower':
        require(process.wait(timeout=910)==0,'follower process failed');process=None
        row.update(json.loads((OUT/(key+'.stdout')).read_text()))
    row.update(started=started,ended=time.time())
    if role!='idle':
        result=command(prefix+['recover',path,case['layout'],str(case['bytes']),str(count+64),str(cookie)],timeout=900)
        row['recovered_records']=json.loads(result['stdout'])['verified_records']
        require(row['recovered_records']==count+64,'complete durable prefix recovered')
    rows.append(row);save('commit-cases.json',rows)
    barrier(key+'-finished')
    if NODE==leader:print(json.dumps({k:row[k] for k in ['key','placement','device','method','layout','transport','bytes','p50_us','p90_us','p99_us','p999_us']}),flush=True)


def summary():
    grouped={}
    for row in rows:
        if row['role']!='leader' or row['stage']!='screen':continue
        case={k:row[k] for k in screen_cases()[0]}
        entry=grouped.setdefault(identity(case),{'case':case,'values':[]})
        with gzip.open(OUT/row['file'],'rt') as f:entry['values'] += [int(v['commit_ns'])/1000 for v in csv.DictReader(f)]
    return [{'case':value['case'],**stats(value['values'])} for value in grouped.values()]


def tail_cases(summaries):
    selected={}
    for placement,size in itertools.product(['good','bad'],[4096,65536]):
        candidates=[r for r in summaries if r['case']['placement']==placement and r['case']['bytes']==size]
        for percentile in ['p50_us','p99_us']:
            case=min(candidates,key=lambda r:r[percentile])['case'];selected[identity(case)]=case
        for case in [base_case(placement,'tcp',9001,size),base_case(placement,'udp',9001,size),
                     base_case(placement,'tcp',1500,size,device='gp3',method='buffered-fdatasync',layout='extend',filesystem='ordered')]:
            selected[identity(case)]=case
        if size==4096:
            case=min(candidates,key=lambda r:r['p99_us'])['case']|{'surviving_only':True};selected[identity(case)]=case
    return list(selected.values())


try:
    require(os.geteuid()==0 and os.environ['SIXDB_WORKER_REUSED']=='0','fresh root study worker required')
    manifest=json.loads(Path(os.environ['SIXDB_DEVICES']).read_text());save('storage-manifest.json',manifest)
    DEVICES=manifest['ebs']|{'nvme':manifest['instance_store'][0]}
    for label,entry in DEVICES.items():
        device=str(Path(entry['device']).resolve());entry['device']=device
        require(device not in [str(Path(p).resolve()) for p in manifest['root_devices']],'refuse root disk')
        block=json.loads(command(['lsblk','-b','-J','-o','PATH,TYPE,MOUNTPOINTS,SIZE',device])['stdout'])['blockdevices']
        require(len(block)==1 and block[0]['type']=='disk' and not block[0].get('children') and not any(block[0]['mountpoints'] or []),'unmounted whole study disk required')
        require(block[0]['size']>=16*1024**3,'data disk capacity');save(label+'-before.json',inventory(device,label))
    document=json.loads(metadata('dynamic/instance-identity/document'))
    ME={'node':NODE,'ip':document['privateIp'],'az':document['availabilityZone'],'az_id':metadata('meta-data/placement/availability-zone-id'),'identity':document}
    NETWORK=json.loads(command(['ip','-j','route','show','default'])['stdout'])[0]['dev']
    CPUS=sorted(os.sched_getaffinity(0));require(len(CPUS)==2,'two dedicated physical CPUs expected')
    os.sched_setaffinity(0,{CPUS[1]})
    if NODE==0:
        server=http.server.ThreadingHTTPServer(('0.0.0.0',43000),Barrier)
        threading.Thread(target=server.serve_forever,daemon=True).start()
    put(f'ready/{NODE}.json',ME)
    if NODE==0:
        peers=[wait_for(lambda i=i:get(f'ready/{i}.json'),3) for i in range(4)]
        require([p['az_id'] for p in peers]==['use1-az1','use1-az2','use1-az4','use1-az6'],'expected stable AZ identities');put('peers.json',peers)
    PEERS=wait_for(lambda:get('peers.json'),3);save('peers.json',PEERS);diagnostic('initial')
    preflight=[base_case('good','udp',9001,4096),
               base_case('bad','tcp',1500,4096,device='gp3',method='buffered-fdatasync',layout='extend',filesystem='ordered'),
               base_case('good','udp',9001,65536,device='io2')]
    for index,case in enumerate(preflight):round_case(case,'preflight',0,index)
    for stage in ['screen','tail']:
        if stage=='screen':cases=screen_cases()
        else:
            put(f'screen-summary/{NODE}.json',summary());barrier('summaries-ready')
            if NODE==0:
                summaries=sum([get(f'screen-summary/{i}.json') for i in range(4)],[])
                save('screen-summary.json',summaries);put('tail-plan.json',tail_cases(summaries))
            cases=wait_for(lambda:get('tail-plan.json'),3)
        save(stage+'-plan.json',cases)
        for repeat in range(3):
            order=list(cases);random.Random(8734+repeat).shuffle(order)
            diagnostic(stage+f'-r{repeat}-before')
            for index,case in enumerate(order):round_case(case,stage,repeat,index)
            diagnostic(stage+f'-r{repeat}-after');barrier(stage+f'-r{repeat}-diagnostics-done')
    barrier('all-measurements-done')
    for label,entry in DEVICES.items():save(label+'-after.json',inventory(entry['device'],label))
    if NODE==0:put('progress.json',{'state':'complete','utc':time.time()})
    barrier('all-results-ready')
except Exception as error:
    save('failure.json',{'error':repr(error),'utc':time.time(),'node':NODE})
    try:urllib.request.urlopen(f"http://{PEERS[0]['ip']}:43000/failure/{NODE}",timeout=3).read()
    except Exception:pass
    raise
finally:
    if process is not None and process.poll() is None:process.kill();process.wait()
    for label in list(mounted):command(['umount','/mnt/sixdb-cft-'+label],check=False)
