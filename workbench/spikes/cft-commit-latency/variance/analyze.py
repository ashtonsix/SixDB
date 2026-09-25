#!/usr/bin/env python3
"""Fixed-tuple repeatability and predeclared training/holdout selection."""
import argparse
import csv
import json
from pathlib import Path
import statistics
import sys

ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'oneway'))
from analyze import write_csv


def read(path):
    return list(csv.DictReader(path.open()))


def correlation(a,b):
    try:
        return statistics.correlation(a,b)
    except statistics.StatisticsError:
        return None


def main():
    p=argparse.ArgumentParser()
    p.add_argument('campaign',type=Path)
    p.add_argument('--request-rank',action='store_true')
    p.add_argument('--inputs',type=Path,help='recovered raw folders named by worker member')
    args=p.parse_args()
    folder=args.campaign;out=folder/'summary'
    refs=json.loads((folder/'workers.json').read_text())
    names={};meta={};ports={};hosts={}
    configs=set();case_keys=set();captured={}
    recovered_config=json.loads((folder/'capture-config.json').read_text()) if args.inputs else None
    for name,member in refs['members'].items():
        result=args.inputs/name if args.inputs else ROOT/'build/workers'/member['job']/'results'
        ident=json.loads((result/'identity.json').read_text());n=ident['node']
        config=recovered_config[name] if args.inputs else json.loads((result.parent/'job.json').read_text())['config']['env']
        captured[name]={k:v for k,v in config.items() if k.startswith('ONEWAY_')}
        configs.add((int(config['ONEWAY_NODES']),int(config['ONEWAY_FLOWS']),int(config.get('ONEWAY_ROUNDS','4')),config['ONEWAY_LAYOUT'],config.get('ONEWAY_PORT_MODE','both')))
        names[n]=name.removeprefix('use1-');hosts[n]=ident
        for case in json.loads((result/'cases.json').read_text()):
            a,b=sorted((case['src'],case['dst']))
            key=(a,b,case['repeat'])
            value=(case['round'],case['flow'],case['src'])
            assert (n,*key) not in case_keys,'duplicate host case'
            case_keys.add((n,*key))
            assert case['role']==('client' if n==case['src'] else 'server')
            assert key not in meta or meta[key]==value
            meta[key]=value
            ports.setdefault((a,b,case['flow'],n),set()).add(case['local_port'])
    assert all(len(v)==1 for v in ports.values()), 'endpoint tuple changed between rounds'
    assert len(configs)==1
    (out/'capture-config.json').write_text(json.dumps(captured,indent=2)+'\n')
    nodes,flow_count,round_count,layout,port_mode=next(iter(configs))
    assert set(hosts)==set(range(nodes))
    expected_pairs={(a,b) for a in range(nodes) for b in range(a+1,nodes)
                    if layout=='full' or a<nodes//2<=b}
    expected_meta={(a,b,t*flow_count+f) for a,b in expected_pairs for t in range(round_count) for f in range(flow_count)}
    assert set(meta)==expected_meta,'incomplete or unexpected flow/round grid'
    assert case_keys=={(n,a,b,w) for a,b,w in expected_meta for n in (a,b)}
    for (a,b,wire),(round_,flow,initiator) in meta.items():
        assert wire==round_*flow_count+flow and 0<=flow<flow_count and 0<=round_<round_count
        assert initiator==(a if round_%2==0 else b)
    tuples=[]
    for a,b,f in sorted({k[:3] for k in ports}):
        tuples.append(dict(node_a=a,node_b=b,host_a=names[a],host_b=names[b],flow=f,
                           ip_a=hosts[a]['ip'],port_a=next(iter(ports[a,b,f,a])),
                           ip_b=hosts[b]['ip'],port_b=next(iter(ports[a,b,f,b]))))
        assert tuples[-1]['port_a']==48000+f
        assert tuples[-1]['port_b']==48100+(f if port_mode=='both' else 0)
    assert len({(r['ip_a'],r['port_a'],r['ip_b'],r['port_b']) for r in tuples})==len(tuples),'flow IDs alias the same physical tuple'
    write_csv(out/'tuples.csv',tuples)
    rtts={(int(r['node_a']),int(r['node_b']),int(r['repeat'])):r for r in read(out/'pair-blocks.csv')}
    blocks=[];groups={};block_keys=set()
    for r in read(out/'blocks.csv'):
        a,b=int(r['src']),int(r['dst']);wire=int(r['repeat'])
        key=(*sorted((a,b)),wire)
        assert (a,b,wire) not in block_keys,'duplicate directed summary block'
        block_keys.add((a,b,wire))
        round_,flow,initiator=meta[key]
        row=dict(r,host_src=names[a],host_dst=names[b],round=round_,flow=flow,request_role=int(a==initiator),
                 rtt_p50_us=float(rtts[key]['rtt_p50_us']),rtt_p99_us=float(rtts[key]['rtt_p99_us']))
        blocks.append(row);groups.setdefault((a,b,flow),[]).append(row)
    assert block_keys=={(x,y,w) for a,b,w in expected_meta for x,y in ((a,b),(b,a))}
    write_csv(out/'flow-blocks.csv',blocks)
    candidates=[]
    for (a,b,flow),rows in sorted(groups.items()):
        training=[r for r in rows if r['round']<3]
        validation=[r for r in rows if r['round']>=3]
        rank=[r for r in training if r['request_role']] if args.request_rank else training
        assert {r['round'] for r in rows}==set(range(round_count)) and len(rows)==round_count
        assert len(training)==3 and rank and len(validation)==round_count-3
        request=[r for r in validation if r['request_role']]
        row=dict(src=a,dst=b,host_src=names[a],host_dst=names[b],az_src=rows[0]['az_src'],az_dst=rows[0]['az_dst'],flow=flow,
                 rank_mode='request' if args.request_rank else 'all-legs',training_rank_blocks=len(rank),
                 holdout_request_blocks=len(request))
        for field in ('p50_us','p90_us','p99_us','p50_lo_us','p50_hi_us'):
            row['train_worst_'+field]=max(float(r[field]) for r in rank)
            row['holdout_worst_'+field]=max(float(r[field]) for r in validation)
            row['holdout_request_'+field]=max((float(r[field]) for r in request),default=None)
        row['holdout_rtt_max_p50_us']=max(r['rtt_p50_us'] for r in validation)
        row['same_role_0_2_abs_change_us']=abs(float(next(r for r in rows if r['round']==0)['p50_us'])-float(next(r for r in rows if r['round']==2)['p50_us']))
        candidates.append(row)
    mode='request' if args.request_rank else 'all-legs'
    write_csv(out/f'flow-candidates-{mode}.csv',candidates)
    # Winners are chosen using training alone. Holdout never participates in sort.
    rank_key=lambda r:(r['train_worst_p50_us'],r['train_worst_p90_us'],r['src'],r['dst'],r['flow'])
    winners=[]
    for azpair in sorted({(r['az_src'],r['az_dst']) for r in candidates if r['az_src']!=r['az_dst']}):
        rows=[r for r in candidates if (r['az_src'],r['az_dst'])==azpair]
        winners.append(dict(min(rows,key=rank_key),candidates_considered=len(rows)))
    write_csv(out/f'selected-az-directions-{mode}.csv',winners)
    pair_winners=[]
    for a,b in sorted({key[:2] for key in groups}):
        choices=[r for r in candidates if (r['src'],r['dst'])==(a,b)]
        selected=min(choices,key=rank_key)
        baseline=next(r for r in choices if r['flow']==0)
        request_gain=(baseline['holdout_request_p50_us']-selected['holdout_request_p50_us']
                      if selected['holdout_request_p50_us'] is not None else None)
        pair_winners.append(dict(selected,baseline_flow=0,
            baseline_holdout_worst_p50_us=baseline['holdout_worst_p50_us'],
            baseline_holdout_request_p50_us=baseline['holdout_request_p50_us'],
            holdout_worst_gain_vs_flow0_us=baseline['holdout_worst_p50_us']-selected['holdout_worst_p50_us'],
            holdout_request_gain_vs_flow0_us=request_gain))
    write_csv(out/f'selected-host-pairs-{mode}.csv',pair_winners)
    variance=[]
    for a,b in sorted({key[:2] for key in groups}):
        flows=sorted(f for x,y,f in groups if (x,y)==(a,b))
        indexed={(r['flow'],r['round']):r for r in blocks if int(r['src'])==a and int(r['dst'])==b}
        rounds=sorted({t for f,t in indexed})
        row=dict(src=a,dst=b,host_src=names[a],host_dst=names[b],az_src=indexed[flows[0],0]['az_src'],az_dst=indexed[flows[0],0]['az_dst'],flows=len(flows))
        for field in ('p50_us','rtt_p50_us'):
            values=lambda t:[float(indexed[f,t][field]) for f in flows]
            spans=[max(values(t))-min(values(t)) for t in rounds]
            same_role=[(t,t+2) for t in rounds if t+2 in rounds]
            changes=[abs(float(indexed[f,x][field])-float(indexed[f,y][field])) for f in flows for x,y in same_role]
            row[field+'_flow_span_median']=statistics.median(spans)
            row[field+'_flow_span_max']=max(spans)
            row[field+'_same_role_change_median']=statistics.median(changes)
            row[field+'_same_role_change_max']=max(changes)
            for x,y in same_role:
                row[field+f'_flow_correlation_{x}_{y}']=correlation(values(x),values(y))
        variance.append(row)
    write_csv(out/'pair-variance.csv',variance)
    info=dict(tuple_count=len(tuples),directions=len(groups),rounds=rounds,
              training_rounds=[0,1,2],holdout_rounds=[r for r in rounds if r>=3],
              rank_mode='request' if args.request_rank else 'all-legs',
              tuple_stability_verified=True,clock_bounds_are_not_confidence_intervals=True,
              iid_or_stationarity_assumed=False,
              same_role_round_comparisons=same_role,
              independent_clock_fit_uses_full_capture=True,
              selected_directions=len(winners),
              selected_point_train_p50_le_110=sum(r['train_worst_p50_us']<=110 for r in winners),
              selected_point_holdout_request_p50_le_110=sum(r['holdout_request_p50_us'] is not None and r['holdout_request_p50_us']<=110 for r in winners))
    (out/f'variance-checks-{mode}.json').write_text(json.dumps(info,indent=2)+'\n')
    print(json.dumps(info,indent=2))


if __name__=='__main__':main()
