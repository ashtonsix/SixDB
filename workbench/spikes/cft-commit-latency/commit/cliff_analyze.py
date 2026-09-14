#!/usr/bin/env python3
"""Reconstruct cliff distributions, temporal stages and resource deltas from raw traces."""
import argparse,csv,gzip,json
from pathlib import Path
import numpy as np

SAMPLE=np.dtype([('arrival','<u8'),('prepared','<u8'),('commit','<u8'),('batch','<u8'),('records','<u4'),('padding','<u4')])
DETAIL=np.dtype([('prepare_start','<u8'),('local','<u8'),('ack0','<u8'),('ack1','<u8')])
PERCENTILES=[.5,.9,.99,.999,1]

def binary(path,dtype):
    if path.exists():return np.fromfile(path,dtype=dtype)
    with gzip.open(str(path)+'.gz','rb') as f:return np.frombuffer(f.read(),dtype=dtype)

def quantiles(data):
    return {name:float(value)/1e6 for name,value in zip(['p50_ms','p90_ms','p99_ms','p999_ms','max_ms'],np.quantile(data,PERCENTILES))}

def write_csv(path,rows):
    if not rows:return
    keys=list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w',newline='') as out:
        writer=csv.DictWriter(out,fieldnames=keys);writer.writeheader();writer.writerows(rows)

def analyze(root,output):
    cases=json.loads((root/'cliff-cases.json').read_text())
    summary=[];temporal=[];network=[];storage=[];prep=[]
    for row in cases:
        key=row['key'];path=root/(key+'.bin')
        if not path.exists() and not Path(str(path)+'.gz').exists():continue
        a=binary(path,SAMPLE);d=binary(Path(str(path)+'.detail'),DETAIL)
        assert len(a)==row['count'] and len(d)==len(a)
        assert np.all(a['arrival']<=d['prepare_start']) and np.all(d['prepare_start']<=a['prepared'])
        assert np.all(d['local']>=a['prepared']) and np.all(d['ack0']>=a['prepared']) and np.all(d['ack1']>=a['prepared'])
        assert np.all(a['commit']>=np.maximum(d['local'],np.minimum(d['ack0'],d['ack1'])))
        assert np.all(a['commit'][1:]>=a['commit'][:-1])
        selected=a['arrival']>=1e9
        intervals={'latency':a['commit']-a['arrival'],'queue':d['prepare_start']-a['arrival'],'prepare':a['prepared']-d['prepare_start'],
                   'local':d['local']-a['prepared'],'ack0':d['ack0']-a['prepared'],'ack1':d['ack1']-a['prepared'],
                   'post_prepare':a['commit']-a['prepared'],'all_durable':np.maximum(d['local'],np.maximum(d['ack0'],d['ack1']))-a['prepared']}
        finish=int(a['arrival'][-1]);seconds=(finish-1e9)/1e9
        result={k:row[k] for k in ['key','name','rate','seconds','count','batch','layout']}
        result.update(repeat=row.get('repeat',0),transport=row.get('transport','tcp'),window=row.get('window',64),local_only=row.get('local_only',False),adaptive=row.get('adaptive',False),prep_adaptive=row.get('prep_adaptive',False),burst=row.get('burst',False),skip_initialize=row.get('skip_initialize',False),measured_records=int(selected.sum()),measurement_seconds=seconds,
                      goodput_rps=int(((a['commit']>=1e9)&(a['commit']<=finish)).sum())/seconds,
                      deadline_goodput_rps=int((selected&(intervals['latency']<1e6)&(a['commit']<=finish)).sum())/seconds,
                      below_1ms_pct=100*float(np.mean(intervals['latency'][selected]<1e6)),
                      all_offered=len(a),all_committed=len(a),rejected=0,unfinished=0,
                      final_queued=int(np.count_nonzero(a['prepared']>finish)),final_uncommitted=int(np.count_nonzero(a['commit']>finish)),
                      drain_ms=max(0,int(a['commit'][-1])-finish)/1e6,
                      all_follower_drain_ms=max(0,row['drained_ns']-finish)/1e6)
        for label,values in intervals.items():result.update({label+'_'+k:v for k,v in quantiles(values[selected]).items()})
        local_prefix=np.maximum.accumulate(d['local'])
        quorum_ready=np.maximum(local_prefix,np.minimum(d['ack0'],d['ack1']))
        assert np.all(a['commit']>=quorum_ready)
        local_last=(local_prefix>=d['ack0'])&(local_prefix>=d['ack1'])
        masks={'queued_over_1ms':selected&(a['prepared']-a['arrival']>1e6),
               'slowest_1pct':selected&(intervals['latency']>=np.quantile(intervals['latency'][selected],.99))}
        for label,mask in masks.items():
            result[label+'_records']=int(mask.sum());result[label+'_local_last_pct']=100*float(np.mean(local_last[mask])) if mask.any() else ''
        result['commit_observation_gap_p99_us']=float(np.quantile(a['commit'][selected]-quorum_ready[selected],.99))/1000
        for threshold in [1,10,50,100]:result[f'above_{threshold}ms_pct']=100*float(np.mean(intervals['latency'][selected]>=threshold*1e6))
        # Fit only the late saturated cumulative service slope; do not infer a capacity from healthy runs.
        committed_times,first=np.unique(a['commit'],return_index=True);counts=np.r_[first[1:],len(a)]
        fit=(committed_times>=max(14e9,finish-5e9))&(committed_times<=finish)
        if fit.sum()>2:
            slope,intercept=np.polyfit(committed_times[fit]/1e9,counts[fit],1)
            result.update(late_service_rps=float(slope),late_intercept_records=float(intercept))
        summary.append(result)
        for second in range(int(np.ceil(finish/1e9))):
            left=second*1000000000;right=min(finish,(second+1)*1000000000)
            lo,hi=np.searchsorted(a['arrival'],[left,right],side='left')
            if hi<=lo:continue
            line={'key':key,'second':second,'records':int(hi-lo),'offered_rps':(hi-lo)/((right-left)/1e9),
                  'committed_rps':int(((a['commit']>=left)&(a['commit']<right)).sum())/((right-left)/1e9),
                  'below_1ms_pct':100*float(np.mean(intervals['latency'][lo:hi]<1e6))}
            for label,values in intervals.items():line.update({label+'_'+k:v for k,v in quantiles(values[lo:hi]).items()})
            temporal.append(line)
        anchor=json.loads(Path(str(path)+'.clock.json').read_text());start=anchor['steady_start_ns']
        for node_root in sorted(root.parent.iterdir()):
            mon=node_root/(key+'.monitor.jsonl');opener=open
            if not mon.exists():mon=Path(str(mon)+'.gz');opener=gzip.open
            if not mon.exists():continue
            # Remote clocks are aligned through each sample's realtime/monotonic anchor; precision is NTP-level.
            previous=None;previous_nvme=None
            with opener(mon,'rt') as f:
                for text in f:
                    sample=json.loads(text)
                    elapsed=(sample['utc_ns']-anchor['realtime_ns']+anchor['steady_now_ns']-start)/1e9
                    if previous:
                        dt=(sample['steady_ns']-previous['steady_ns'])/1e9
                        item={'key':key,'node':node_root.name,'second':elapsed,'interval_seconds':dt,'read_duration_ms':(sample['end_ns']-sample['steady_ns'])/1e6}
                        if 'disk' in sample and 'disk' in previous:
                            current_disk=list(map(int,sample['disk'].split()));previous_disk=list(map(int,previous['disk'].split()))
                            item['disk_write_MBps']=(current_disk[6]-previous_disk[6])*512/dt/1e6
                            item['disk_write_ops_s']=(current_disk[4]-previous_disk[4])/dt
                            item['disk_inflight']=current_disk[8]
                            item['disk_write_time_ms']=current_disk[7]-previous_disk[7]
                        for k,v in sample.get('ena',{}).items():
                            if k in previous.get('ena',{}) and ('allowance' in k or k.startswith('ena_srd') or k.endswith(('_bytes','_cnt'))):item[k]=v if k in ['ena_srd_mode','ena_srd_resource_utilization','conntrack_allowance_available'] else v-previous['ena'][k]
                        network.append(item)
                    previous=sample
                    if 'nvme' in sample:
                        if previous_nvme:
                            item={'key':key,'node':node_root.name,'second':elapsed,'interval_seconds':(sample['steady_ns']-previous_nvme['steady_ns'])/1e9}
                            item.update({k:v if k=='queue_length' else v-previous_nvme['nvme'][k] for k,v in sample['nvme'].items()});storage.append(item)
                        previous_nvme=sample
            meta=node_root/(key+'.bin.preparation.json')
            if meta.exists():
                detail=json.loads(meta.read_text());events=np.genfromtxt(str(meta).replace('.preparation.json','.preparation.csv'),names=True,delimiter=',')
                detail.update(prepare_calls=int(np.count_nonzero(events['action']==1)),pause_event_samples=int(np.count_nonzero(events['action']==2)),
                              all_range_prepared_and_consumed=detail['prepared_bytes']==detail['consumed_bytes']==detail['total_bytes'])
                prep.append({'key':key,'node':node_root.name}|detail)
        print(json.dumps({k:result[k] for k in ['key','goodput_rps','latency_p99_ms','latency_p999_ms','local_p99_ms','ack0_p99_ms','ack1_p99_ms','below_1ms_pct']}),flush=True)
    output.mkdir(parents=True,exist_ok=True)
    for name,rows in [('cases',summary),('seconds',temporal),('network',network),('nvme',storage),('preparation',prep)]:write_csv(output/(name+'.csv'),rows)
    compact={}
    for row in network:
        second=int(np.floor(row['second']));key=(row['key'],row['node'],second)
        item=compact.setdefault(key,{'key':key[0],'node':key[1],'second':second,'interval_seconds':0.,'write_MB':0.,'write_ops':0.,'disk_inflight_max':0})
        dt=row['interval_seconds'];item['interval_seconds']+=dt
        item['write_MB']+=row.get('disk_write_MBps',0)*dt;item['write_ops']+=row.get('disk_write_ops_s',0)*dt
        item['disk_inflight_max']=max(item['disk_inflight_max'],row.get('disk_inflight',0))
        for k,v in row.items():
            if k.endswith('allowance_exceeded') or (k.startswith('ena_srd') and k not in ['ena_srd_mode','ena_srd_resource_utilization']):item[k]=item.get(k,0)+v
            elif k=='ena_srd_mode':item[k]=v
            elif k=='ena_srd_resource_utilization':item[k]=max(item.get(k,0),v)
    write_csv(output/'resource-seconds.csv',list(compact.values()))
    (output/'analysis.json').write_text(json.dumps({'raw_root':str(root),'cases':len(summary),'sample_dtype':SAMPLE.descr,'detail_dtype':DETAIL.descr,'quantiles':'NumPy linear/type 7; arrival cohort after first scheduled second','remote_alignment':'realtime anchors, NTP-level precision; leader stages use its one steady clock','local_only':'local control aliases ACK detail fields to local completion solely for trace format compatibility; it performs no replication'},indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('leader',type=Path);p.add_argument('--output',required=True,type=Path);a=p.parse_args();analyze(a.leader,a.output)
