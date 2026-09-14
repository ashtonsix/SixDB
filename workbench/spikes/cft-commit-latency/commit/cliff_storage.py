#!/usr/bin/env python3
"""Isolate local durable-write service with the same arrival, buffer and ring machinery."""
import gzip,json,os,shutil,subprocess,sys,threading,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[4];OUT=Path(os.environ['SIXDB_RESULTS'])
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));sys.path.insert(0,str(ROOT/'workbench/tools'))
from persistence.run import command,require,inventory
from worker_context import GroupContext
from cliff_nvme import read as read_nvme
ctx=GroupContext.from_env();cpus=sorted(os.sched_getaffinity(0));os.sched_setaffinity(0,{cpus[1]})
binary=ROOT/'build/clang/dev/workbench/spikes/cft-commit-latency/cft_throughput_bench'
def save(name,value):(OUT/name).write_text(json.dumps(value,indent=2)+'\n')
require(os.geteuid()==0 and os.environ['SIXDB_WORKER_REUSED']=='0','fresh disposable local probe')
manifest=json.loads(Path(os.environ['SIXDB_DEVICES']).read_text());save('storage-manifest.json',manifest)
device=str(Path(manifest['instance_store'][0]['device']).resolve())
require(device not in [str(Path(p).resolve()) for p in manifest['root_devices']],'refuse root device')
blocks=json.loads(command(['lsblk','-b','-J','-o','PATH,TYPE,MOUNTPOINTS,SIZE',device])['stdout'])['blockdevices']
require(len(blocks)==1 and blocks[0]['type']=='disk' and not blocks[0].get('children') and not any(blocks[0]['mountpoints'] or []),'unmounted whole disposable local disk')
save('device-before.json',inventory(device,'nvme'));rows=[]
env=os.environ|{'CFT_PROBE':'1','CFT_BINARY':'1','CFT_SKIP_INITIALIZE':'1'}
os.environ.update(env)
cases=[(90000,4),(104400,4),(120000,4),(104400,16)]
for index,(rate,batch) in enumerate(cases):
    count=rate*40;key=f'{index:02d}-local-{rate}-b{batch}';path=OUT/(key+'.bin');cookie=628947+index
    ctx.progress('measuring',case=key,completed=index,total=len(cases));stop=threading.Event()
    def monitor():
        with (OUT/(key+'.monitor.jsonl')).open('w') as out:
            while not stop.is_set():
                value=read_nvme(device);value['disk']=Path(f'/sys/class/block/{Path(device).name}/stat').read_text()
                out.write(json.dumps(value)+'\n');out.flush();stop.wait(1.1)
    thread=threading.Thread(target=monitor);thread.start()
    args=['taskset','-c',str(cpus[0]),str(binary),'local','tcp','-','-','43001',str(count),'64',str(batch),str(rate),'50',device,'raw',str(path),str(cookie),'9137']
    try:result=command(args,timeout=240)
    finally:stop.set();thread.join()
    row={'key':key,'name':key,'rate':rate,'batch':batch,'window':64,'count':count,'seconds':40,'layout':'raw','run':cookie,'seed':9137,'command':args,'local_only':True,'skip_initialize':True}|json.loads(result['stdout'])
    ctx.progress('verifying',case=key,completed=index,total=len(cases))
    recovery=command(['taskset','-c',str(cpus[0]),str(binary),'recover',device,'raw',str(count),str(cookie)],timeout=240)
    row['verified_records']=json.loads(recovery['stdout'])['verified_records'];require(row['verified_records']==count,'local direct readback')
    rows.append(row);save('cliff-cases.json',rows);ctx.publish('latest',row)
ctx.progress('compressing',completed=len(cases),total=len(cases))
for path in OUT.iterdir():
    if path.name.endswith(('.bin','.detail','.jsonl')):
        with path.open('rb') as src,gzip.open(str(path)+'.gz','wb',compresslevel=1) as dst:shutil.copyfileobj(src,dst)
        path.unlink()
save('device-after.json',inventory(device,'nvme'));ctx.progress('complete',completed=len(cases),total=len(cases))
