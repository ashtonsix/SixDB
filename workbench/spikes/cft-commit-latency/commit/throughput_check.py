#!/usr/bin/env python3
"""Check pipelined durable prefixes, partial batches, arrivals, loss and recovery."""
import csv,json,os,socket,subprocess,sys,tempfile,time
from pathlib import Path
binary=str(Path(sys.argv[1]).resolve())
cases=[(p,w,b,r,False) for p in ['tcp','udp'] for w,b,r in [(1,1,0),(4,4,0),(64,64,0),(4,16,20000),(16,64,2000),(4,1,100000)]]
cases += [('udp',4,16,20000,True),('udp',64,64,0,True)]
if os.environ.get('CFT_STRESS_ONLY'):cases=[('udp',64,16,0,False)]
with tempfile.TemporaryDirectory(prefix='cft-pipeline-check-') as temp:
 root=Path(temp)
 for index,(proto,window,batch,rate,loss) in enumerate(cases):
  with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  count=int(os.environ.get("CFT_RECORDS",max(513,window*batch*3+7)));run=91726+index;processes=[];logs=[]
  def args(role,who):
   ips=['127.0.0.2','127.0.0.3'] if role=='leader' else [f'127.0.0.{who+2}','-']
   return [binary,role,proto,*ips,str(port),str(count),str(window),str(batch),str(rate),'50',str(root/f'{index}-{who}.wal'),'initialized',str(root/f'{index}-{who}.csv'),str(run),'482']
  try:
   for who in [0,1]:
    log=(root/f'{index}-{who}.log').open('w+');logs.append(log)
    env=os.environ|({'CFT_DROP_FRAGMENT_EVERY':'17','CFT_DROP_ACK_EVERY':'19'} if loss else {})
    processes.append(subprocess.Popen(args('follower',who),stdout=log,stderr=log,env=env))
   deadline=time.monotonic()+10
   while not all((root/f'{index}-{who}.csv.ready').exists() for who in [0,1]):
    assert all(p.poll() is None for p in processes)
    assert time.monotonic()<deadline
    time.sleep(.01)
   result=subprocess.run(args('leader','leader'),capture_output=True,text=True,timeout=40)
   assert not result.returncode,result.stderr
   detail=json.loads(result.stdout);assert detail['committed']==count
   if loss:assert detail['retried_packets']>0
   for p in processes:assert not p.wait(timeout=5)
   for who in [0,1,'leader']:
    result=subprocess.run([binary,'recover',str(root/f'{index}-{who}.wal'),'initialized',str(count),str(run)],capture_output=True,text=True)
    assert not result.returncode,result.stderr
   rows=list(csv.DictReader((root/f'{index}-leader.csv').open()));assert len(rows)==count
   assert all(int(r['arrival_ns'])<=int(r['prepared_ns'])<=int(r['commit_ns']) for r in rows)
   assert all(int(a['commit_ns'])<=int(b['commit_ns']) for a,b in zip(rows,rows[1:]))
   assert max(int(r['batch_records']) for r in rows)<=batch
   print(json.dumps(dict(proto=proto,window=window,batch=batch,rate=rate,loss=loss,verified=True)),flush=True)
  except Exception as error:
   if isinstance(error,subprocess.TimeoutExpired):print(str(error.stderr)[-5000:],file=sys.stderr)
   for log in logs:log.seek(0);print(log.read(),file=sys.stderr)
   raise
  finally:
   for p in processes:
    if p.poll() is None:p.kill();p.wait()
   for log in logs:log.close()
