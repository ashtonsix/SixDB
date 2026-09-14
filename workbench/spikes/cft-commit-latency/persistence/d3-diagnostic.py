#!/usr/bin/env python3
"""Trace FUA/flush requests on a fresh D3 study disk; measure an extra flush."""
import json
import os
from pathlib import Path
import sys

sys.path.insert(0,str(Path(__file__).resolve().parent))
from run import OUT,BINARY,command,inventory,receipt,require

require(os.geteuid()==0 and os.environ['SIXDB_WORKER_REUSED']=='0','fresh root worker')
manifest=json.loads(Path(os.environ['SIXDB_DEVICES']).read_text());receipt('storage-manifest.json',manifest)
entry=manifest['instance_store'][0];device=str(Path(entry['device']).resolve())
require(device not in [str(Path(p).resolve()) for p in manifest['root_devices']],'refuse root disk')
block=json.loads(command(['lsblk','-b','-J','-o','PATH,TYPE,MOUNTPOINTS,SIZE',device])['stdout'])['blockdevices']
require(len(block)==1 and block[0]['type']=='disk' and not block[0].get('children') and not any(block[0]['mountpoints'] or []),'whole unmounted data disk')
require(block[0]['size']>=16*1024**3,'bounded test range')
receipt('before.json',inventory(device,'D3 diagnostic'))
events=command(['trace-cmd','list','-e'],check=False);receipt('trace-events.json',events)
selected=[name for name in ['block:block_rq_issue','nvme:nvme_setup_cmd','nvme:nvme_complete_rq'] if name in events['stdout']]
mount=Path('/mnt/sixdb-cft-d3');mount.mkdir(exist_ok=True)
results=[]
for layout in ['raw','initialized']:
    if layout=='initialized':
        receipt('format.json',command(['mkfs.ext4','-F','-b','4096','-E','lazy_itable_init=0,lazy_journal_init=0',device,'2097152']))
        command(['mount','-t','ext4','-o','data=writeback,noatime,max_batch_time=0',device,str(mount)])
    try:
        path=device if layout=='raw' else str(mount/'wal.dat')
        for method in ['direct-dsync','direct-fdatasync','direct-dsync-fdatasync','uring-direct-dsync']:
            key=layout+'-'+method
            base=['taskset','-c',os.environ['SIXDB_CPU'],str(BINARY),path,method,layout]
            for repeat in range(3):
                output=OUT/f'{key}-r{repeat}.csv'
                result=command(base+['2000','4096',str(output),str(1024**3 if layout=='raw' else 0)],timeout=180)
                results.append({'layout':layout,'method':method,'repeat':repeat,'file':output.name,**json.loads(result['stdout'])})
                receipt('diagnostic-cases.json',results)
            traced=base+['10','4096',str(OUT/(key+'-traced.csv')),str(1024**3 if layout=='raw' else 0)]
            receipt(key+'-syscalls.json',command(['strace','-ttt','-T','-yy','-o',str(OUT/(key+'-strace.txt')),
                '-e','trace=openat,pwrite64,fdatasync,fsync,io_uring_enter',*traced],check=False,timeout=180))
            if selected:
                args=['trace-cmd','record','-o',str(OUT/(key+'.dat'))]
                for event in selected:args+=['-e',event]
                result=command(args+['--',*traced],check=False,timeout=180);receipt(key+'-trace-command.json',result)
                if result['returncode']==0:
                    report=command(['trace-cmd','report','-i',str(OUT/(key+'.dat'))],check=False)
                    (OUT/(key+'-trace.txt')).write_text(report['stdout']+report['stderr'])
    finally:
        if layout=='initialized':command(['umount',str(mount)])
receipt('after.json',inventory(device,'D3 diagnostic'))
