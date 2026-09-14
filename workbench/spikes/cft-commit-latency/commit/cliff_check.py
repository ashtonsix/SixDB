#!/usr/bin/env python3
"""Verify live extent preparation, changing batch sizes, and binary stage traces."""
import json,os,resource,signal,socket,subprocess,sys,tempfile,time
from pathlib import Path
import numpy as np
binary=str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='cft-cliff-check-') as temporary:
    root=Path(temporary)
    for index,(protocol,layout,adaptive) in enumerate([('tcp','initialized',False),('tcp','ahead',False),('tcp','ahead',True),('udp','ahead',True)]):
        count=70001;run=6841+index;processes=[];logs=[]
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        env=os.environ|{'CFT_PROBE':'1','CFT_BINARY':'1','CFT_BASE_BATCH':'4','CFT_ADAPT_BATCH':str(int(adaptive)),'CFT_PREP_ADAPT':str(int(adaptive))}
        def args(role,who):
            ips=['127.0.0.2','127.0.0.3'] if role=='leader' else [f'127.0.0.{who+2}','-']
            return [binary,role,protocol,*ips,str(port),str(count),'16','64','50000','50',str(root/f'{index}-{who}.wal'),layout,str(root/f'{index}-{who}.bin'),str(run),'917']
        try:
            for who in [0,1]:
                log=(root/f'{index}-{who}.log').open('w+');logs.append(log)
                processes.append(subprocess.Popen(args('follower',who),env=env,stdout=log,stderr=log))
            deadline=time.monotonic()+30
            while not all((root/f'{index}-{who}.bin.ready').exists() for who in [0,1]):
                assert all(p.poll() is None for p in processes) and time.monotonic()<deadline
                time.sleep(.01)
            result=subprocess.run(args('leader','leader'),capture_output=True,text=True,env=env,timeout=120)
            assert not result.returncode,result.stderr
            assert json.loads(result.stdout)['committed']==count
            for p in processes:assert p.wait(timeout=10)==0
            for who in [0,1,'leader']:
                result=subprocess.run([binary,'recover',str(root/f'{index}-{who}.wal'),layout,str(count),str(run)],capture_output=True,text=True)
                assert not result.returncode,result.stderr
                if layout=='ahead':
                    prep=json.loads((root/f'{index}-{who}.bin.preparation.json').read_text())
                    assert prep['prepared_bytes']==prep['consumed_bytes']==count*4096
                    assert prep['initial_bytes']<prep['consumed_bytes']
            data=np.fromfile(root/f'{index}-leader.bin',dtype='<u8').reshape(-1,5)
            detail=np.fromfile(root/f'{index}-leader.bin.detail',dtype='<u8').reshape(-1,4)
            assert len(data)==count and len(detail)==count
            assert np.all(data[:,0]<=detail[:,0]) and np.all(detail[:,0]<=data[:,1])
            assert np.all(detail[:,1]>=data[:,1]) and np.all(detail[:,2:]>=data[:,1,None])
            assert np.all(data[:,2]>=np.maximum(detail[:,1],np.minimum(detail[:,2],detail[:,3])))
            batches=data[:,4]&0xffffffff
            assert np.max(batches)<=64
            if adaptive:assert np.max(batches)>4
            print(json.dumps({'protocol':protocol,'layout':layout,'adaptive':adaptive,'records':count,'all_three_readback':True,'max_batch':int(np.max(batches))}),flush=True)
        finally:
            for p in processes:
                if p.poll() is None:p.kill();p.wait()
            for log in logs:log.close()
    def limit_file():
        signal.signal(signal.SIGXFSZ,signal.SIG_IGN)
        resource.setrlimit(resource.RLIMIT_FSIZE,(70*1024**2,70*1024**2))
    path=root/'failure';log=root/'failure.wal'
    result=subprocess.run([binary,'local','tcp','-','-','43001','40001','64','4','104400','50',str(log),'ahead',str(path),'1','9137'],
                          env=os.environ|{'CFT_PROBE':'1','CFT_BINARY':'1'},capture_output=True,text=True,timeout=10,preexec_fn=limit_file)
    assert result.returncode!=0 and 'File too large' in result.stderr,result.stderr
    assert Path(str(path)+'.preparation.error.txt').exists() and Path(str(path)+'.partial.json').exists()
    print(json.dumps({'preparation_failure':'preserved underlying error and partial accounting'}),flush=True)
