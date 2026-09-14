#!/usr/bin/env python3
"""Recompute throughput frontiers from raw arrivals and commits on three nodes."""
import argparse,json,sys
from pathlib import Path
from throughput_summary import read,summarize,summarize_recorded
from network_summary import analyze as network_analyze
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize import stats,write_csv
FIELDS=['transport','window','batch','rate','wait_us','count','bytes','mtu','device','method','layout','placement']


def operating_points(pooled):
 knees=[]
 # Report each transport and their observed union; use only longer repeated points.
 for transport in ['tcp','udp','either']:
  values=[r for r in pooled if r['stage']=='tail' and r['stable_all_passes'] and (transport=='either' or r['transport']==transport)]
  if not values:
   for percentile in ['p50','p90','p99','p999']:
    for label in ['within_10pct','within_25pct','below_1ms']:
     knees.append(dict(transport=transport,percentile=percentile,criterion=label,observed=False,reason='No repeated stable point observed'))
   continue
  gmax=max(r['goodput_rps'] for r in values)
  for percentile in ['p50','p90','p99','p999']:
   key=percentile+'_us';baseline=min(r[key] for r in values)
   for label,limit in [('within_10pct',baseline*1.10),('within_25pct',baseline*1.25),('below_1ms',1000)]:
    eligible=[r for r in values if r[key]<=limit] if label!='below_1ms' else [r for r in values if r[key]<limit]
    if not eligible:
     knees.append(dict(transport=transport,percentile=percentile,criterion=label,observed=False,baseline_us=baseline,limit_us=limit,maximum_observed_stable_rps=gmax));continue
    best=max(eligible,key=lambda r:(r['goodput_rps'],-r[key]))
    knees.append(dict(transport=transport,percentile=percentile,criterion=label,observed=True,baseline_us=baseline,limit_us=limit,
                     maximum_observed_stable_rps=gmax,goodput_rps=best['goodput_rps'],throughput_sacrificed_pct=100*(1-best['goodput_rps']/gmax),
                     latency_us=best[key],latency_repeat_min_us=best[percentile+'_repeat_min_us'],latency_repeat_max_us=best[percentile+'_repeat_max_us'],repetitions=best['repetitions'],
                     chosen_transport=best['transport'],window=best['window'],batch=best['batch'],wait_us=best['wait_us'],rate=best['rate'],samples=best['samples']))
 return knees


def analyze(inputs,output):
 output.mkdir(parents=True,exist_ok=True)
 rounds={};groups={};passes=[];nodes=[]
 for worker,folder in sorted(inputs.items()):
  assert not (folder/'failure.json').exists(),folder
  nodes.append(dict(worker=worker,peers=json.loads((folder/'peers.json').read_text()),storage=json.loads((folder/'storage-manifest.json').read_text())))
  for row in json.loads((folder/'throughput-cases.json').read_text()):
   rounds.setdefault(row['key'],[]).append(row)
   assert row['recovered_records']==row['count']
   if row['role']!='leader':assert row['durable_records']==row['count'];continue
   assert row['offered']==row['committed']==row['count']
   key=tuple([row['stage'],*(row[k] for k in FIELDS)])
   groups.setdefault(key,[]).append((worker,folder,row))
 for key,values in rounds.items():
  assert len(values)==3 and sum(r['role']=='leader' for r in values)==1,key
  assert len({r['run'] for r in values})==1 and len({r['count'] for r in values})==1,key
 pooled=[]
 for key,entries in sorted(groups.items()):
  # Keep raw samples for one configuration at a time, not the entire sweep.
  g=dict(latencies=[],queues=[],passes=[])
  for worker,folder,detail in entries:
   raw=read(folder/detail['file']);summary=summarize(raw,detail)
   expected=summary if 'all_followers_drain_seconds' in detail else summarize_recorded(raw,detail)
   for field,value in expected.items():
    if isinstance(value,float):assert abs(value-detail[field])<=max(1e-8,abs(value)*1e-10),(field,value,detail[field])
    else:assert value==detail[field],field
   passes.append(dict(worker=worker)|detail|summary)
   start=1e9 if detail['rate'] else raw[len(raw)//10]['arrival_ns']
   selected=[r for r in raw if r['arrival_ns']>=start]
   g['latencies'] += [(r['commit_ns']-r['arrival_ns'])/1000 for r in selected]
   g['queues'] += [(r['prepared_ns']-r['arrival_ns'])/1000 for r in selected]
   g['passes'].append(detail|summary)
   del raw,selected
  ps=g['passes'];assert len(ps)==(3 if key[0]=='tail' else 1),(key,len(ps))
  row=dict(zip(['stage',*FIELDS],key))|stats(g['latencies'])
  row.update(repetitions=len(ps),stable_all_passes=all(p['stable_observed'] for p in ps),
             goodput_rps=sum(p['goodput_rps']*p['measurement_seconds'] for p in ps)/sum(p['measurement_seconds'] for p in ps),
             goodput_min_rps=min(p['goodput_rps'] for p in ps),goodput_max_rps=max(p['goodput_rps'] for p in ps),
             offered_rps=sum(p['offered_rps']*p['measurement_seconds'] for p in ps)/sum(p['measurement_seconds'] for p in ps),
             measurement_seconds=sum(p['measurement_seconds'] for p in ps),
             below_1ms_pct=100*sum(v<1000 for v in g['latencies'])/len(g['latencies']),
             retried_packets=sum(p['retried_packets'] for p in ps),
             max_drain_seconds=max(p['drain_seconds'] for p in ps),
             max_quorum_drain_seconds=max(p['quorum_drain_seconds'] for p in ps),
             max_all_followers_drain_seconds=max(p['all_followers_drain_seconds'] for p in ps),
             recorded_stable_all_passes=all(p['recorded_stable_observed'] for p in ps),
             max_sampled_queue=max(p['max_sampled_queue'] for p in ps),
             max_follower_lag_batches=max(p['max_follower_lag_batches'] for p in ps),
             max_sampled_backlog=max(p['max_sampled_backlog'] for p in ps),
             first_quarter_mean_queue=sum(p['first_quarter_mean_queue'] for p in ps)/len(ps),
             last_quarter_mean_queue=sum(p['last_quarter_mean_queue'] for p in ps)/len(ps),
             leader_cpu_us_per_record=1e6*sum(p['process_cpu_seconds'] for p in ps)/sum(p['count'] for p in ps),
             mean_batch_records=sum(p['count'] for p in ps)/sum(p['batches'] for p in ps),
             record_weighted_batch_records=sum(p['average_batch_records']*p['measured_records'] for p in ps)/sum(p['measured_records'] for p in ps))
  row.update({'queue_'+k:v for k,v in stats(g['queues']).items()})
  for percentile in ['p50','p90','p99','p999']:
   row[percentile+'_repeat_min_us']=min(p[percentile+'_us'] for p in ps)
   row[percentile+'_repeat_max_us']=max(p[percentile+'_us'] for p in ps)
  pooled.append(row)
 knees=operating_points(pooled)
 passes.sort(key=lambda r:(r['started'],r['key']))
 write_csv(output/'throughput.csv',pooled);write_csv(output/'throughput-passes.csv',passes);write_csv(output/'knees.csv',knees)
 (output/'throughput.json').write_text(json.dumps(pooled,indent=2)+'\n')
 (output/'throughput-nodes.json').write_text(json.dumps(nodes,indent=2)+'\n')
 network_analyze(inputs,output)
 print(json.dumps(dict(configurations=len(pooled),passes=len(passes),records=sum(p['count'] for p in passes),verified_cohort_rounds=len(rounds))))


if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',nargs='+');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
 analyze({name:Path(path) for name,path in [v.split('=',1) for v in a.input]},a.output)
