#!/usr/bin/env python3
"""Run power-safe storage methods on explicit disposable worker devices."""
import gzip
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import time

OUT=Path(os.environ['SIXDB_RESULTS'])
ROOT=Path(__file__).resolve().parents[4]
BINARY=ROOT/'build/clang/dev/workbench/spikes/cft-commit-latency/cft_persistence_bench'
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize import stats


def command(args, *, check=True, timeout=300):
    p=subprocess.run(args,capture_output=True,text=True,timeout=timeout)
    if check and p.returncode:
        raise RuntimeError(f'{args}: {p.stdout}\n{p.stderr}')
    return {'args':args,'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr}


def receipt(name,data):
    (OUT/name).write_text(json.dumps(data,indent=2)+'\n')


def require(condition,message):
    if not condition:raise RuntimeError(message)


def inventory(device,label):
    name=Path(device).name
    paths=['queue/write_cache','queue/fua','queue/logical_block_size','queue/physical_block_size','queue/rotational','queue/scheduler','queue/nr_requests','queue/io_poll','stat']
    data={'device':device,'label':label,'utc':time.time(),'sysfs':{}}
    for suffix in paths:
        p=Path('/sys/class/block')/name/suffix
        if p.exists():data['sysfs'][suffix]=p.read_text().strip()
    data['lsblk']=command(['lsblk','-b','-J','-O',device],check=False)
    data['udev']=command(['udevadm','info','--query=all','--name='+device],check=False)
    if name.startswith('nvme'):
        data['nvme_identify']=command(['nvme','id-ctrl',device,'-o','json'],check=False)
        data['nvme_namespace']=command(['nvme','id-ns',device,'-o','json'],check=False)
        data['nvme_write_cache']=command(['nvme','get-feature',device,'-f','6','-H'],check=False)
        data['nvme_smart']=command(['nvme','smart-log',device,'-o','json'],check=False)
    else:
        data['hdparm']=command(['hdparm','-I',device],check=False)
    return data


def main():
    require(os.geteuid()==0,'root required')
    require(os.environ['SIXDB_WORKER_REUSED']=='0','only fresh study workers may format storage')
    manifest=json.loads(Path(os.environ['SIXDB_DEVICES']).read_text())
    receipt('storage-manifest.json',manifest)
    devices=[(name,entry) for name,entry in manifest['ebs'].items()]
    if manifest['instance_store']:
        devices.append(('instance-store-0',manifest['instance_store'][0]))
    require(devices,'no study data device')
    mount=Path('/mnt/sixdb-cft-write');mount.mkdir(exist_ok=True)
    before=command(['findmnt','-J']);receipt('mounts-before.json',before)
    rows=[]
    for label,entry in devices:
        device=str(Path(entry['device']).resolve())
        require(device not in [str(Path(p).resolve()) for p in manifest['root_devices']],f'root device refused: {device}')
        block=json.loads(command(['lsblk','-b','-J','-o','PATH,TYPE,MOUNTPOINTS,SIZE,LOG-SEC',device])['stdout'])['blockdevices']
        require(len(block)==1 and block[0]['type']=='disk' and not block[0].get('children'),f'whole unpartitioned disk required: {block}')
        require(not any(block[0]['mountpoints'] or []),'never format a mounted device')
        require(block[0]['size']>=16*1024**3,'study data disk smaller than expected')
        receipt(label+'-before.json',inventory(device,label))
        hdd=os.environ.get('DEVICE_CLASS')=='hdd'
        count=500 if hdd else 2000
        sizes=[n for n in [512,4096,16384,65536] if n>=block[0]['log-sec']]
        methods=['direct-fdatasync','direct-dsync','uring-direct-fdatasync','uring-direct-dsync']
        phases=[('raw',[(m,'raw') for m in methods]),
                ('ordered',[('buffered-fdatasync',x) for x in ['extend','fallocate','initialized']]+[(m,'initialized') for m in methods]),
                ('ordered-tuned',[(m,'initialized') for m in ['buffered-fdatasync','direct-dsync']]),
                ('writeback-tuned',[(m,'initialized') for m in ['buffered-fdatasync','direct-dsync']])]
        plan=[(phase,[(m,l,n) for m,l in cases for n in sizes],'screen') for phase,cases in phases]
        screen_values={}
        phase_index=0
        while phase_index<len(plan):
            phase,cases,stage=plan[phase_index]
            phase_index+=1
            print(json.dumps({'device':label,'phase':phase,'stage':stage,'utc':time.time()}),flush=True)
            if phase!='raw':
                args=['mkfs.ext4','-F','-b','4096','-E','lazy_itable_init=0,lazy_journal_init=0']
                args += [device,'2097152'] # A bounded 8 GiB filesystem on this study-owned disk.
                receipt(label+'-'+stage+'-'+phase+'-format.json',command(args))
                options={'ordered':'data=ordered','ordered-tuned':'data=ordered,noatime,max_batch_time=0',
                         'writeback-tuned':'data=writeback,noatime,max_batch_time=0'}[phase]
                command(['mount','-t','ext4','-o',options,device,str(mount)])
                receipt(label+'-'+stage+'-'+phase+'-mount.json',command(['findmnt','-J',str(mount)]))
            try:
                for repeat in range(3):
                    order=list(cases)
                    random.Random(3987+repeat).shuffle(order)
                    for method,layout,size in order:
                        sample_count=count if stage=='screen' else (10000 if hdd else (40000 if size==4096 else 12000))
                        key=f'{label}-{stage}-{phase}-{method}-{layout}-{size}-r{repeat}'
                        path=device if phase=='raw' else str(mount/'wal.dat')
                        output=OUT/(key+'.csv')
                        start=time.time()
                        measured=command(['taskset','-c',os.environ['SIXDB_CPU'],str(BINARY),path,method,layout,str(sample_count),str(size),str(output),str(1024**3 if phase=='raw' else 0)],timeout=900)
                        detail=json.loads(measured['stdout'])
                        row={'device':label,'device_path':device,'phase':phase,'stage':stage,'method':method,'layout':layout,'bytes':size,
                             'repeat':repeat,'started':start,'ended':time.time(),'file':output.name+'.gz',**detail}
                        values=[int(line.split(',')[0])/1000 for line in output.read_text().splitlines()[1:]]
                        assert len(values)==sample_count
                        row.update(stats(values))
                        if stage=='screen':screen_values.setdefault((phase,method,layout,size),[]).extend(values)
                        rows.append(row)
                        with gzip.open(str(output)+'.gz','wb') as f:f.write(output.read_bytes())
                        output.unlink()
                        receipt('storage-cases.json',rows)
                if phase!='raw':
                    receipt(label+'-'+stage+'-'+phase+'-filesystem.json',command(['dumpe2fs','-h',device],check=False))
            finally:
                if phase!='raw':command(['umount',str(mount)])
            if stage=='screen' and phase_index==len(phases):
                selected=set()
                for size in [4096,65536]:
                    candidates={key:stats(values) for key,values in screen_values.items() if key[3]==size}
                    # Selection uses screening only; independent longer passes measure its tails.
                    for percentile in ['p50_us','p99_us']:
                        selected.add(min(candidates,key=lambda key:candidates[key][percentile]))
                    selected.add(('ordered','buffered-fdatasync','extend',size))
                receipt(label+'-tail-selection.json',[list(key) for key in sorted(selected)])
                for chosen_phase in dict.fromkeys(key[0] for key in sorted(selected)):
                    plan.append((chosen_phase,[(m,l,n) for p,m,l,n in sorted(selected) if p==chosen_phase],'tail'))
        receipt(label+'-after.json',inventory(device,label))
    print(json.dumps({'completed_cases':len(rows),'devices':len(devices)}),flush=True)

if __name__=='__main__':main()
