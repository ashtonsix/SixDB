#!/usr/bin/env python3
"""Scheduled-arrival latency and unique-record goodput, with visible drain/backlog."""
import bisect,csv,gzip,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from summarize import stats

def read(path):
 opener=gzip.open if str(path).endswith('.gz') else open
 with opener(path,'rt') as f:return [{k:int(v) for k,v in r.items()} for r in csv.DictReader(f)]

def summarize_recorded(rows,case):
 assert len(rows)==case['count']
 assert all(r['arrival_ns']<=r['prepared_ns']<=r['commit_ns'] for r in rows)
 assert all(a['commit_ns']<=b['commit_ns'] for a,b in zip(rows,rows[1:]))
 open_loop=case['rate']>0
 # Warmup ends after one scheduled second in open-loop; 10% of records in closed-loop.
 start=1e9 if open_loop else rows[len(rows)//10]['arrival_ns']
 selected=[r for r in rows if r['arrival_ns']>=start]
 assert len(selected)>100
 end=rows[-1]['arrival_ns'] if open_loop else rows[-1]['commit_ns']
 commits=[r['commit_ns'] for r in rows];arrivals=[r['arrival_ns'] for r in rows]
 submitted=[r['prepared_ns'] for r in rows]
 before=bisect.bisect_left(commits,start);during=bisect.bisect_right(commits,end)-before
 result=stats([(r['commit_ns']-r['arrival_ns'])/1000 for r in selected])
 result.update(measurement_seconds=(end-start)/1e9,measured_records=len(selected),goodput_rps=during/((end-start)/1e9),
               offered_rps=(len(rows)-bisect.bisect_left(arrivals,start))/((end-start)/1e9),
               backlog_start=bisect.bisect_right(arrivals,start)-bisect.bisect_right(commits,start),
               backlog_end=len(rows)-bisect.bisect_right(commits,end),
               queue_end=len(rows)-bisect.bisect_right(submitted,end),
               drain_seconds=max(0,rows[-1]['commit_ns']-end)/1e9,
               below_1ms_pct=100*sum(r['commit_ns']-r['arrival_ns']<1e6 for r in selected)/len(selected),
               average_batch_records=sum(r['batch_records'] for r in selected)/len(selected),
               max_batch_records=max(r['batch_records'] for r in selected))
 for label,values in [('queue',[(r['prepared_ns']-r['arrival_ns'])/1000 for r in selected]),
                       ('post_prepare',[(r['commit_ns']-r['prepared_ns'])/1000 for r in selected])]:
  result.update({label+'_'+k:v for k,v in stats(values).items()})
 grid=[start+(end-start)*i/100 for i in range(101)]
 queue=[bisect.bisect_right(arrivals,t)-bisect.bisect_right(submitted,t) for t in grid]
 backlog=[bisect.bisect_right(arrivals,t)-bisect.bisect_right(commits,t) for t in grid]
 result.update(max_sampled_queue=max(queue),max_sampled_backlog=max(backlog),
               first_quarter_mean_queue=sum(queue[:25])/25,last_quarter_mean_queue=sum(queue[-25:])/25)
 # A bounded finite-run criterion, not a proof of indefinitely sustainable service.
 result['stable_observed']=bool(open_loop and result['goodput_rps']>=.98*result['offered_rps'] and result['drain_seconds']<=max(.02,.01*result['measurement_seconds']) and result['last_quarter_mean_queue']<=result['first_quarter_mean_queue']+max(10,.001*case['rate']))
 return result


def summarize(rows,case):
 result=summarize_recorded(rows,case)
 result['recorded_stable_observed']=result['stable_observed']
 result['quorum_drain_seconds']=result['drain_seconds']
 # The native end includes all follower acknowledgments, pending sends and ring teardown.
 # Quorum latency remains the primary metric; finite-run stability also checks slot release.
 if 'drained_ns' in case:
  end=rows[-1]['arrival_ns'] if case['rate']>0 else rows[-1]['commit_ns']
  result['all_followers_drain_seconds']=max(0,case['drained_ns']-end)/1e9
  result['stable_observed']=bool(result['stable_observed'] and result['all_followers_drain_seconds']<=max(.02,.01*result['measurement_seconds']))
 return result
