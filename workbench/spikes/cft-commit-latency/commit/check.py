#!/usr/bin/env python3
"""Exercise real durable quorum rounds, fragmentation, retries and recovery locally."""
import csv
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

binary=str(Path(sys.argv[1]).resolve())
cases=[(transport,4096,method,'initialized',False) for transport in ['tcp','udp']
       for method in ['buffered-fdatasync','direct-fdatasync','direct-dsync']]
cases += [('tcp',65536,'direct-dsync','initialized',False),
          ('udp',65536,'direct-dsync','initialized',True),
          ('udp',512,'buffered-fdatasync','extend',True)]
with tempfile.TemporaryDirectory(prefix='sixdb-cft-check-') as directory:
    root=Path(directory)
    for index,(transport,size,method,layout,loss) in enumerate(cases):
        with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
        common=[str(port),str(size),'20',None,method,layout,None,str(89101+index),'1472','1000']
        processes=[]
        logs=[]
        try:
            for i,ip in enumerate(['127.0.0.2','127.0.0.3']):
                args=common.copy();args[3]=str(root/f'{index}-{i}.wal');args[6]=str(root/f'{index}-{i}.csv')
                log=(root/f'{index}-{i}.log').open('w+');logs.append(log)
                env=os.environ | ({'CFT_DROP_FRAGMENT_EVERY':'17','CFT_DROP_ACK_EVERY':'19'} if loss else {})
                processes.append(subprocess.Popen([binary,'follower',transport,ip,'-',*args],stdout=log,stderr=log,env=env))
            deadline=time.monotonic()+10
            while not all((root/f'{index}-{i}.csv.ready').exists() for i in range(2)):
                assert all(p.poll() is None for p in processes),'follower exited before readiness'
                assert time.monotonic()<deadline,'follower readiness deadline'
                time.sleep(.02)
            args=common.copy();args[3]=str(root/f'{index}-leader.wal');args[6]=str(root/f'{index}-leader.csv')
            leader=subprocess.run([binary,'leader',transport,'127.0.0.2','127.0.0.3',*args],capture_output=True,text=True,timeout=30)
            assert leader.returncode==0,leader.stderr
            assert json.loads(leader.stdout)['verified_records']==84
            for process,log in zip(processes,logs):
                assert process.wait(timeout=10)==0
                log.seek(0);detail=json.loads(log.read());assert detail['verified_records']==84
                if loss:assert detail['injected_drops']>0
            for who in ['leader','0','1']:
                recovery=subprocess.run([binary,'recover',str(root/f'{index}-{who}.wal'),layout,str(size),'84',str(89101+index)],capture_output=True,text=True)
                assert recovery.returncode==0,recovery.stderr
                assert json.loads(recovery.stdout)['verified_records']==84
            rows=list(csv.DictReader((root/f'{index}-leader.csv').open()))
            assert len(rows)==20
            assert all(0<int(r['commit_ns'])<=int(r['all_followers_ns']) for r in rows)
            if loss:assert sum(int(r['retried_packets']) for r in rows)>0
            print(json.dumps({'transport':transport,'bytes':size,'method':method,'layout':layout,'injected_loss':loss,'verified':True}),flush=True)
        except Exception:
            for log in logs:log.seek(0);print(log.read(),file=sys.stderr)
            raise
        finally:
            for p in processes:
                if p.poll() is None:p.kill();p.wait()
            for log in logs:log.close()
