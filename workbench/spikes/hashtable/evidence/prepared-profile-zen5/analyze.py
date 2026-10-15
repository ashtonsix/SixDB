#!/usr/bin/env python3
"""Relocate unchanged-ELF perf samples; only audited timed-hit PCs enter attribution.
Usage: analyze.py WORKER_RESULTS ORIGINAL_ELF ORIGINAL_EVIDENCE OUTPUT
Reads perf.data records independently and checks the native perf-script output.
No precise-IP or per-phase enabled/running information is invented.
"""
import collections,csv,hashlib,json,re,statistics,struct,sys
from pathlib import Path
source,elf,original,out=map(Path,sys.argv[1:]);out.mkdir(parents=True,exist_ok=True)
profile=json.loads((source/'profile.json').read_text());receipt=json.loads((source/'profile-input.json').read_text());binary=elf.read_bytes()
assert hashlib.sha256(binary).hexdigest()==receipt['elf_sha256']=='9907b313adb693150c7717f758150bc2b981b1a566a672eeb588d59743f0a6ac'
assert binary[:6]==b'\x7fELF\x02\x01'
phoff=struct.unpack_from('<Q',binary,32)[0];phsize,phnum=struct.unpack_from('<HH',binary,54)
loads=[]
for i in range(phnum):
 p=struct.unpack_from('<IIQQQQQQ',binary,phoff+i*phsize)
 if p[0]==1:loads.append(dict(offset=p[2],vaddr=p[3],filesz=p[5],flags=p[1]))
regions={
 'set-composed':[(0x8003f5,0x800451,'next_key_hash'),(0x800451,0x800490,'primary_fingerprint'),(0x800490,0x8004c4,'primary_suffix'),(0x8004c4,0x800510,'primary_tail_fingerprint'),(0x800510,0x800550,'primary_tail_suffix'),(0x800550,0x800670,'secondary'),(0x800670,0x800739,'overflow')],
 'set-direct':[(0x81360f,0x81365e,'next_key_hash'),(0x81365e,0x8136a0,'primary_fingerprint'),(0x8136a0,0x8136d1,'primary_suffix'),(0x8136d1,0x813710,'primary_tail_fingerprint'),(0x813710,0x813750,'primary_tail_suffix'),(0x813750,0x813890,'secondary'),(0x8138c0,0x8138d0,'success_join'),(0x813890,0x813993,'overflow')],
 'mm-exact':[(0x9574c4,0x957513,'next_key_hash'),(0x957513,0x957570,'primary_fingerprint'),(0x957570,0x9575a3,'primary_suffix'),(0x9575a3,0x95766f,'secondary'),(0x95766f,0x957728,'overflow'),(0x957728,0x957752,'primary_value_tag'),(0x957752,0x957851,'bag_resolution_consumer'),(0x957851,0x957897,'overflow_value_tag'),(0x957897,0x9578a4,'inline_value_consumer'),(0x9578a4,0x9578c3,'secondary_value_tag')],
 'mm-wide':[(0x9f01d8,0x9f0232,'next_key_hash'),(0x9f0232,0x9f0280,'primary_fingerprint'),(0x9f0280,0x9f02a9,'primary_suffix'),(0x9f02a9,0x9f036f,'secondary'),(0x9f036f,0x9f0428,'overflow'),(0x9f0428,0x9f0452,'primary_value_tag'),(0x9f0452,0x9f0551,'bag_resolution_consumer'),(0x9f0551,0x9f057e,'overflow_value_tag'),(0x9f057e,0x9f058b,'inline_value_consumer'),(0x9f058b,0x9f05aa,'secondary_value_tag')]
}
armkind={'set-composed':(0,0),'set-direct':(0,1),'mm-exact':(2,0),'mm-wide':(2,6)}
summary=[];allhits=[];allhist=[];allregions=[];phases=[]
def write(name,rows):
 with (out/name).open('w') as f:
  w=csv.DictWriter(f,fieldnames=list(dict.fromkeys(k for r in rows for k in r)));w.writeheader();w.writerows(rows)
old=list(csv.DictReader((original/'formats.csv').open()))
for run in profile['runs']:
 name=run['name'];data=(source/(name+'.data')).read_bytes();assert hashlib.sha256(data).hexdigest()==run['data_sha256']
 assert data[:8]==b'PERFILE2';h=struct.unpack_from('<13Q',data);_,header_size,attr_size,attr_offset,attr_bytes,offset,length,*_=h
 assert header_size==104 and attr_bytes==attr_size==160
 typ,size,config,freq,sample_type,read_format,flags=struct.unpack_from('<IIQQQQQ',data,attr_offset)
 assert (typ,config,freq,sample_type)==(0,0,997,391)
 assert flags&(1<<10) and not(flags&(1<<1));assert (flags>>15)&3==0
 records=collections.Counter();samples=[];maps=[];at=offset
 while at<offset+length:
  t,misc,n=struct.unpack_from('<IHH',data,at);assert n>=8 and at+n<=offset+length;records[t]+=1
  if t==9:
   assert n==48,n
   ip,pid,tid,time,cpu,res,period=struct.unpack_from('<QIIQIIQ',data,at+8);samples.append(dict(ip=ip,pid=pid,tid=tid,time=time,cpu=cpu,period=period,misc=misc))
  if t==10:
   pid,tid,start,span,pgoff=struct.unpack_from('<IIQQQ',data,at+8)
   # mmap2 fixed header is72bytes, including the union containing build ID.
   path=data[at+72:at+n].split(b'\0',1)[0].decode(errors='replace')
   if path.endswith('/hashtable-prepared-profile-source/formats'):
    buildid_size=data[at+40];buildid=data[at+44:at+44+buildid_size].hex();maps.append(dict(pid=pid,tid=tid,start=start,span=span,pgoff=pgoff,path=path,buildid=buildid))
  at+=n
 assert at==offset+length and len(maps)==1,maps
 mapping=maps[0];assert mapping['buildid']=='1d99b3271bbae9ab6ab16b4104062cdac4a512d2'
 load=next(p for p in loads if p['offset']==mapping['pgoff'] and p['flags']&1);bias=mapping['start']-load['vaddr']
 text=(source/(name+'-events.txt')).read_text();parsed=re.findall(r'^\s*\S+\s+(\d+)/(\d+)\s+([\d.]+):\s+(\d+) cycles:u:\s+([0-9a-f]+)',text,re.M)
 assert [(int(p),int(t),int(w),int(ip,16)) for p,t,tm,w,ip in parsed]==[(s['pid'],s['tid'],s['period'],s['ip']) for s in samples]
 assert all(abs(float(tm)*1e9-s['time'])<1100 for (_,_,tm,_,_),s in zip(parsed,samples))
 native_mmap=re.findall(r'PERF_RECORD_MMAP2 (\d+)/(\d+): \[0x([0-9a-f]+)\(0x([0-9a-f]+)\) @ 0x([0-9a-f]+) <([0-9a-f]+)>\]: r-xp (.*hashtable-prepared-profile-source/formats)',text)
 assert len(native_mmap)==1
 m=native_mmap[0];assert tuple(int(x,16) for x in m[2:5])==(mapping['start'],mapping['span'],mapping['pgoff']) and m[5]==mapping['buildid']
 log=(source/(name+'.log')).read_text();assert int(re.search(r'\((\d+) samples\)',log)[1])==len(samples)
 lo,hi=map(lambda x:int(x,16),run['elf_hit_range']);kind,arm=armkind[name]
 witness=original/f'K40-kind{kind}-arm{arm}-hit.asm';asm={int(m[1],16):m[2] for m in re.finditer(r'^([0-9a-f]+): (.*)$',witness.read_text(),re.M)}
 hits=[];mapped=0
 for s in samples:
  if s['pid']!=mapping['pid'] or not mapping['start']<=s['ip']<mapping['start']+mapping['span']:continue
  mapped+=1;address=s['ip']-bias
  if not lo<=address<hi:continue
  assert address in asm,(name,hex(address));assert s['cpu']==profile['cpu'] and (s['misc']&7)==2
  region=next((n for a,b,n in regions[name] if a<=address<b),'loop_control_setup_result')
  hits.append(dict(run=name,time_ns=s['time'],pid=s['pid'],tid=s['tid'],cpu=s['cpu'],raw_ip=hex(s['ip']),elf_ip=hex(address),period=s['period'],region=region,instruction=asm[address]));allhits.extend(hits[-1:])
 total_period=sum(x['period'] for x in hits)
 histogram=collections.defaultdict(lambda:[0,0])
 for x in hits:histogram[x['elf_ip']][0]+=1;histogram[x['elf_ip']][1]+=x['period']
 for ip,(count,weight) in sorted(histogram.items(),key=lambda p:p[1][0],reverse=True):allhist.append(dict(run=name,elf_ip=ip,samples=count,period_sum=weight,sample_fraction=count/len(hits),period_fraction=weight/total_period,instruction=asm[int(ip,16)]))
 for region in sorted({x['region'] for x in hits}):
  chosen=[x for x in hits if x['region']==region];weight=sum(x['period'] for x in chosen);allregions.append(dict(run=name,region=region,samples=len(chosen),period_sum=weight,sample_fraction=len(chosen)/len(hits),period_fraction=weight/total_period))
 rows=list(csv.DictReader((source/(name+'.csv')).open()));assert len(rows)==9000
 assert {(int(r['K']),int(r['kind']),int(r['A']),int(r['arm'])) for r in rows}=={(40,kind,10,arm)}
 for phase in sorted({r['phase'] for r in rows}):
  rr=[r for r in rows if r['phase']==phase];assert sorted(int(x['rep']) for x in rr)==list(range(1000));v=sorted(float(x['ns'])/int(x['operations']) for x in rr)
  prev=[r for r in old if (int(r['K']),int(r['kind']),int(r['A']),int(r['arm']),r['phase'])==(40,kind,10,arm,phase)];original_median=statistics.median(float(x['ns'])/int(x['operations']) for x in prev)
  for field in ['operations','n','requested','metadata','primary_slots','secondary_slots','build_primary_rows','build_secondary_rows','build_overflow_rows']:assert {r[field] for r in rr}=={r[field] for r in prev},(name,phase,field)
  phases.append(dict(run=name,phase=phase,repetitions=len(rr),median_ns_per_operation=statistics.median(v),minimum=v[0],p10=v[99],p90=v[899],maximum=v[-1],original_median=original_median,ratio_to_original=statistics.median(v)/original_median,total_timed_ns=sum(float(x['ns']) for x in rr)))
 hitphase=next(r for r in phases if r['run']==name and r['phase']=='hit')
 summary.append(dict(run=name,input_sha256={suffix:hashlib.sha256((source/(name+suffix)).read_bytes()).hexdigest() for suffix in ['.data','-events.txt','.csv','.log']},native_record_log_samples=len(samples),records=dict(records),samples=len(samples),samples_in_original_elf=mapped,hit_samples=len(hits),hit_samples_fraction=len(hits)/len(samples),hit_sample_period_min=min(x['period'] for x in hits),hit_sample_period_median=statistics.median(x['period'] for x in hits),hit_sample_period_max=max(x['period'] for x in hits),hit_sample_period_sum=total_period,hit_cpu_counts=dict(collections.Counter(x['cpu'] for x in hits)),lost_records=records[2]+records[13],throttle_records=records[5],unthrottle_records=records[6],precise_ip=0,frequency_hz=freq,read_format=read_format,per_phase_enabled_running_available=False,mapping=mapping,load_bias=hex(bias),hit_range=run['elf_hit_range'],hit_timed_seconds=hitphase['total_timed_ns']/1e9,hit_sampling_density_per_timed_second=len(hits)/(hitphase['total_timed_ns']/1e9)))
write('hit-samples.csv',allhits);write('hit-addresses.csv',allhist);write('hit-regions.csv',allregions);write('replay.csv',phases)
result=dict(original_elf_sha256=receipt['elf_sha256'],profile_job='20261013T201821Z-34e68e97',interpretation='Only audited timed-hit addresses; arbitrary-skid cycles:u samples are locations, not causal instruction or region cycle fractions.',original_elf_source_manifest=receipt['source_manifest'],runs=summary)
(out/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
for r in summary:
 print(r['run'],'samples',r['hit_samples'],'/',r['samples'],'period min/median/max',r['hit_sample_period_min'],r['hit_sample_period_median'],r['hit_sample_period_max'],'lost/throttle',r['lost_records'],r['throttle_records'])
 print('regions',[(x['region'],x['samples'],round(x['period_fraction'],3)) for x in allregions if x['run']==r['run']]);print('top',[(x['elf_ip'],x['samples'],x['instruction']) for x in allhist if x['run']==r['run']][:6])
