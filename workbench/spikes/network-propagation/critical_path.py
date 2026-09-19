#!/usr/bin/env python3
"""Replay the retained bulk result and explain its actual tail objects.

The last-completing chunk's timeline is an additive accounting path. CPU and
stream queue waits include preceding work; they are not independent extra
penalties to add to the pipeline's other chunks or branches.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

from model import Simulator, Topology, wire
from run import quantile, write_json


class Isolated(Simulator):
    """Retain each object's jitter identity but run it alone at time zero."""
    def release(self, mid, time):
        super().release(self.object_id, time)


def completion_chain(sim, mid, terminal):
    """Follow the latest actual prerequisite, including earlier chunks/jobs.

    Network durations remain observed elapsed service under fair sharing, not
    independent link costs. This is a dependency trace for this fixed replay,
    not a prediction of the gain from removing a task or changing a capacity.
    """
    events = {}
    def add(key, time, parents=(), **label):
        events[key] = dict(time=time, parents=list(parents), **label)
        return key
    transfers = sim.transfer_audit
    incoming = {(t['message'],t['dst'],t['chunk']):i for i,t in enumerate(transfers) if t['kind']=='data'}
    cpu_keys = {}
    for m,v in sim.messages.items(): add(('release',m),v['release_us'])
    for i,t in enumerate(transfers):
        label={k:t[k] for k in ('message','edge','kind','chunk')}
        if t['kind']=='data' and t['src']!=sim.spec['source']:
            trigger=('receive_end',incoming[t['message'],t['src'],t['chunk']])
        elif t['kind']=='receipt':
            candidates=[j for (m,n,c),j in incoming.items() if (m,n)==(t['message'],t['src'])]
            trigger=('receive_end',max(candidates,key=lambda j:transfers[j]['delivered_us']))
        else: trigger=('release',t['message'])
        add(('send_queued',i),t['created_us'],[trigger])
        for phase,prefix in (('send_cpu','send'),('receive_cpu','receive')):
            cpu=t[phase]
            cpu_keys[id(cpu)]=(prefix,i)
            add((prefix+'_start',i),cpu['start_us'],[(prefix+'_queued',i)],**label,stage='CPU queue gate')
            add((prefix+'_end',i),cpu['end_us'],[(prefix+'_start',i)],**label,stage=prefix+' CPU service')
        add(('network_start',i),t['start_us'],[('send_end',i)],**label,stage='stream gate')
        add(('network_end',i),t['wire_end_us'],[('network_start',i)],**label,stage='network service with sharing')
        add(('receive_queued',i),t['arrival_us'],[('network_end',i)],**label,stage='propagation')
    for node in sim.topo.nodes:
        jobs=sorted((c for c in sim.cpu_audit if c['node']==node),key=lambda c:c['start_us'])
        for previous,current in zip(jobs,jobs[1:]):
            pp,pi=cpu_keys[id(previous)];cp,ci=cpu_keys[id(current)]
            events[cp+'_start',ci]['parents'].append((pp+'_end',pi))
    streams={}
    for i,t in sorted(enumerate(transfers),key=lambda it:it[1]['start_us']):
        stream=(t['message'],t['edge'],t['kind'])
        if stream in streams: events['network_start',i]['parents'].append(('network_end',streams[stream]))
        streams[stream]=i
    last=max((i for i,t in enumerate(transfers) if t['kind']=='data' and t['message']==mid and t['dst']==terminal),
             key=lambda i:transfers[i]['delivered_us'])
    key=('receive_end',last)
    release=sim.messages[mid]['release_us']
    reverse=[]
    while events[key]['time']>release:
        event=events[key]
        assert event['parents'], 'Unexplained dependency after target release'
        parent=max(event['parents'],key=lambda k:events[k]['time'])
        start=max(release,events[parent]['time'])
        if event.get('stage') in ('CPU queue gate','stream gate'):
            assert abs(start-event['time'])<1e-6, (key,start,event)
        if event['time']>start+1e-8:
            reverse.append(dict(start_us=start-release,end_us=event['time']-release,
                                **{k:v for k,v in event.items() if k not in ('time','parents')}))
        key=parent
    chain=list(reversed(reverse))
    assert abs(sum(c['end_us']-c['start_us'] for c in chain)-(transfers[last]['delivered_us']-release))<1e-6
    return chain


def path_rows(sim, mid):
    message = sim.messages[mid]
    release = message['release_us']
    terminal = max(sim.spec['targets'], key=lambda n: message['delivered'][n])
    transfers = [t for t in sim.transfer_audit if t['message'] == mid and t['kind'] == 'data']
    last = max((t for t in transfers if t['dst'] == terminal), key=lambda t: t['delivered_us'])
    chunk, node, reverse_path = last['chunk'], terminal, []
    while node != sim.spec['source']:
        transfer = next(t for t in transfers if t['dst'] == node and t['chunk'] == chunk)
        reverse_path.append(transfer)
        node = transfer['src']
    stages = []
    for t in reversed(reverse_path):
        s, r = t['send_cpu'], t['receive_cpu']
        stages.extend(dict(edge=t['edge'], stage=name, start_us=a-release, end_us=b-release,
                           duration_us=b-a) for name, a, b in [
            ('sender CPU queue', t['created_us'], s['start_us']),
            ('sender CPU service', s['start_us'], s['end_us']),
            ('outgoing stream queue', s['end_us'], t['start_us']),
            ('network serialization with sharing', t['start_us'], t['wire_end_us']),
            ('propagation and jitter', t['wire_end_us'], t['arrival_us']),
            ('receiver CPU queue', t['arrival_us'], r['start_us']),
            ('receiver CPU service', r['start_us'], r['end_us']),
        ])
    latency = message['delivered'][terminal]-release
    assert abs(sum(s['duration_us'] for s in stages)-latency) < 1e-6
    assert all(abs(a['end_us']-b['start_us']) < 1e-6 for a,b in zip(stages,stages[1:]))
    pipelines = []
    for edge in sorted(sim.route.values()):
        ts = [t for t in transfers if t['edge'] == edge]
        pipelines.append(dict(edge=edge, first_send_us=min(t['start_us'] for t in ts)-release,
                              last_wire_end_us=max(t['wire_end_us'] for t in ts)-release,
                              first_delivered_us=min(t['delivered_us'] for t in ts)-release,
                              full_delivered_us=max(t['delivered_us'] for t in ts)-release))
    neighbors = [dict(message=m, release_us=v['release_us']-release,
                      complete_us=max(v['delivered'].values())-release)
                 for m,v in sim.messages.items()
                 if v['release_us'] <= release+latency and max(v['delivered'].values()) >= release]
    return dict(seed=sim.seed, message=mid, terminal=terminal, chunk=chunk, latency_us=latency,
                confirmed_us=max(message['confirmed'].values())-release,
                node_completion_us={n:t-release for n,t in message['delivered'].items()},
                stages=stages, pipelines=pipelines, overlapping_objects=neighbors,
                completion_dependency_chain=completion_chain(sim,mid,terminal),
                payload_transfers=transfers, release_us=release)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', type=Path, default=Path('build/network-propagation/final'))
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    config = json.loads((args.input/'config.json').read_text())
    record = next(r for r in json.loads((args.input/'results.json').read_text())
                  if r['summary']['scenario'] == 'regional-bulk' and r['summary']['policy'] == 'balanced')
    topo_data = config['topologies'][record['summary']['topology']]
    topo, spec, route = Topology(topo_data), record['spec'], record['route']
    rows = sorted([dict(seed=seed, **r) for seed, run in zip(config['seed_list'],record['runs'])
                   for r in run['rows']], key=lambda r:r['last_us'])
    selected = rows[-2:]
    probes, audits = [], []
    for seed in sorted({r['seed'] for r in selected}):
        sim = Simulator(topo, spec, route, seed, trace=seed==config['seed_list'][0], audit=True)
        result = sim.run()
        original = record['runs'][config['seed_list'].index(seed)]
        assert result == original, 'Audit changed retained simulation outcomes'
        probes.extend(path_rows(sim,r['message']) for r in selected if r['seed']==seed)
        audits.append(dict(seed=seed, transfers=sim.transfer_audit, cpu=sim.cpu_audit))
    variants = []
    for label in ('retained', 'isolated', '4KiB same route', '4KiB chunks', 'MTU 9000', '25 Gbit/s', 'zero CPU cost'):
        ts, td = copy.deepcopy(spec), copy.deepcopy(topo_data)
        if label == '4KiB same route': ts.update(size_bytes=4096,chunk_bytes=4096)
        if label == '4KiB chunks': ts['chunk_bytes'] = 4096
        if label == 'MTU 9000': ts['mtu'] = 9000
        if label == '25 Gbit/s':
            for r in td['resources']: r['gbps'] *= 2.5
        if label == 'zero CPU cost':
            for n in td['nodes']: n.update(chunk_cpu_us=0,packet_cpu_us=0,crypto_gbps=1e100)
        if label == 'isolated':
            ts['messages'] = 1
            vs = []
            for seed in config['seed_list']:
                isolated_rows = []
                for mid in range(spec['messages']):
                    sim = Isolated(Topology(td),ts,route,seed)
                    sim.object_id = mid
                    isolated_rows.extend(sim.run()['rows'])
                vs.append(dict(rows=isolated_rows))
        else:
            vs = [Simulator(Topology(td),ts,route,seed).run() for seed in config['seed_list']]
        values = [r['last_us'] for run in vs for r in run['rows']]
        variants.append(dict(name=label, percentiles_us={str(p):quantile(values,p/100) for p in (50,90,99,99.9)},
                             tail_objects=[dict(seed=r['seed'],message=r['message'],
                                                latency_us=vs[config['seed_list'].index(r['seed'])]['rows'][r['message']]['last_us'])
                                           for r in selected]))
        print('DONE',label,variants[-1]['percentiles_us'],flush=True)
    output = args.output
    output.mkdir(parents=True,exist_ok=True)
    write_json(output/'diagnostic.json',dict(summary=record['summary'],spec=spec,route=route,
                 largest_samples=rows[-6:],tail_paths=probes,sensitivity=variants,
                 chunk_wire_bytes=wire(spec['chunk_bytes'],spec['mtu'])[0],
                 chunk_packets=wire(spec['chunk_bytes'],spec['mtu'])[1],
                 exact_replay=True, sources={p.name:hashlib.sha256(p.read_bytes()).hexdigest()
                  for p in (Path(__file__),Path(__file__).with_name('model.py'))}))
    write_json(output/'full-audit.json',audits)
    source=output/'source'
    source.mkdir(exist_ok=True)
    for name in ('critical_path.py','critical_path_report.py','model.py','run.py'):
        (source/name).write_bytes(Path(__file__).with_name(name).read_bytes())
    write_json(output/'input-reference.json',dict(directory=str(args.input),
        sha256={name:hashlib.sha256((args.input/name).read_bytes()).hexdigest()
                for name in ('config.json','results.json')},
        retained_archive='workbench/spikes/network-propagation/evidence/20260918/artifact.json'))
    from critical_path_report import report
    report(output)
    for p in probes:
        print('TAIL',p['seed'],p['message'],p['latency_us'],p['terminal'],p['chunk'])
        print('OVERLAP',p['overlapping_objects'])
        for s in p['stages']:
            print(s['edge'],s['stage'],round(s['start_us'],3),round(s['end_us'],3),round(s['duration_us'],3))
        print('PIPELINES',p['pipelines'])
    print('SENSITIVITY',variants)


if __name__ == '__main__': main()
