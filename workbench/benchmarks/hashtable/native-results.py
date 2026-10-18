#!/usr/bin/env python3
"""Audit every maintained native cell and retain medians, MADs and all losses."""
import argparse
import csv
import itertools
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--rmw', type=Path, help='RMW-only supplement after restoring the inline payload mixer')
a = parser.parse_args()
root = a.root
manifest = json.loads((root / 'cases.json').read_text())
job = json.loads((root / 'job.json').read_text())
cpu = manifest['cpu']
assert '21.1.8' in (root / 'compiler.txt').read_text()
assert (root / 'complete.txt').read_text().strip() == f'{cpu} maintained complete'
assert manifest['seeds'] == [713, 3191, 76543] and manifest['repetitions'] == 4
assert manifest['source_conditioning'] == 'incoming_entry_prewalk for accumulation'
seeds, repetitions = manifest['seeds'], manifest['repetitions']
native, tune = {'zen5': ('znver5', 'znver5'),
                'granite-rapids': ('graniterapids', 'graniterapids'),
                'neoverse-v2': ('armv8-a+simd', 'neoverse-v2')}[cpu]
assert manifest['isas'] == (['native'] if cpu == 'neoverse-v2' else ['native', 'avx2'])
cells = defaultdict(lambda: defaultdict(list))
expected = defaultdict(set)
rows_count = 0


def read(path):
    expected[path.parent].add(path.name)
    with path.open() as f:
        rows = list(csv.DictReader(f))
    assert rows, path
    return rows


def positive(row, fields):
    for field in fields:
        assert math.isfinite(float(row[field])) and float(row[field]) > 0, (row, field)


def stats(values):
    median = statistics.median(values)
    return dict(median=median, mad=statistics.median(abs(x-median) for x in values))


def core(isa, name, k, v, owner, seed, rep, load, fresh, miss, condition, phases):
    global rows_count
    rows = read(root / isa / name)
    assert Counter(r['phase'] for r in rows) == Counter(phases), name
    for r in rows:
        for field, want in dict(implementation=owner, W=k+(18 if condition else 10), b=18 if condition else 10,
                                K=k, V=v, seed=seed, rep=rep, occupancy=load, new_percent=fresh,
                                miss=miss, conditioning=condition).items():
            assert r[field] == str(want), (name, field, r[field], want)
        positive(r, ['ns', 'operations'])
        assert int(r['bytes']) >= 0
        assert (int(r['bytes']) == 0) == (r['phase'] == 'destroy')
        key = ('core', isa, k, v, r['phase'], load, fresh, miss, condition)
        cells[key][owner].append({f: float(r[f]) for f in ('ns', 'bytes')})
        rows_count += 1


for isa in manifest['isas']:
    out = root / isa
    assert '100% tests passed' in (out / 'build-check.log').read_text()
    assert 'headers compile independently' in (out / 'headers.txt').read_text()
    commands = json.loads((out / 'compile.json').read_text())
    for suffix in ['operations.cpp', 'raw_hash_set.cc'] + (['accumulation.cpp'] if isa == 'native' else []):
        chosen = [c for c in commands if c['file'].endswith('/'+suffix)]
        assert chosen, suffix
        for c in chosen:
            cmd = c.get('command', ' '.join(c.get('arguments', [])))
            assert all(x in cmd for x in (f'-march={native if isa == "native" else "x86-64-v3"}', f'-mtune={tune}', '-DNDEBUG'))
    for k, v, seed, rep in itertools.product([14, 30], [0, 8, 64], seeds, range(repetitions)):
        for owner in ['ikea', 'abseil']:
            for miss in [0, 50]:
                core(isa, f'read-k{k}-v{v}-{owner}-s{seed}-r{rep}-m{miss}.csv', k,v,owner,seed,rep,85,0,miss,0,['independent','prefetch32'])
            for load, fresh in itertools.product([50,85],[0,50,100]):
                for strategy in [owner]+(['ikea_split'] if owner == 'ikea' else []):
                    phases = ['build'] + (['erase','upsert'] if fresh else ['upsert'])*4 + ['destroy']
                    core(isa, f'write-k{k}-v{v}-{strategy}-s{seed}-r{rep}-l{load}-n{fresh}.csv',k,v,strategy,seed,rep,load,fresh,0,0,phases)
                if v:
                    core(isa, f'modify-k{k}-v{v}-{owner}-s{seed}-r{rep}-l{load}-n{fresh}.csv',k,v,owner,seed,rep,load,fresh,0,0,['modify_or_insert' if fresh else 'modify']*4)
            core(isa,f'grow-k{k}-v{v}-{owner}-s{seed}-r{rep}.csv',k,v,owner,seed,rep,85,0,0,0,['grow'])
        core(isa,f'grow-k{k}-v{v}-ikea_descriptor-s{seed}-r{rep}.csv',k,v,'ikea_descriptor',seed,rep,85,0,0,0,['grow'])
    for v,seed,rep,owner,miss in itertools.product([0,8,64],seeds,range(repetitions),['ikea','abseil'],[0,50]):
        core(isa,f'cold-v{v}-{owner}-s{seed}-r{rep}-m{miss}.csv',30,v,owner,seed,rep,85,0,miss,536870912,['independent','prefetch32'])

for name in ['accumulate','guarded','modify','rebuild_storage','descriptor']:
    text = (root / f'san-ikea_hashtable_{name}_check.txt').read_text()
    assert text.strip(), name
    if name == 'accumulate':
        assert 'PASS ordered fixed-capacity mutable accumulation histories' in text

basic = [(k,b,n) for k,b in [(14,10),(30,10),(30,15)] for n in [2,8]]
wide = [(k,b,n) for k,b in [(31,10),(47,10),(47,17),(55,9)] for n in [2,8]]
checksums = {}
for panel, values, cases in [('basic',[0,8,64],basic),('wide',[0,8],wide)]:
    for v,(k,b,n),overlap,seed in itertools.product(values,cases,[0,50,100],seeds):
        where = root / 'native'
        name = f'accum-{panel}-v{v}-k{k}-b{b}-n{n}-o{overlap}-s{seed}.csv'
        rows = read(where/name)
        assert len(rows) == 2*repetitions
        assert [r['owner'] for r in rows] == [['ikea','abseil'][(i+rep+seed%2)%2] for rep in range(repetitions) for i in range(2)]
        pop = 2**b * (8*((63 if k<=30 else 95)//(k+1))) *85//100
        union = pop*overlap//100+n*(pop-pop*overlap//100)
        db = b+(0 if overlap==100 else 1 if n==2 else 2 if k==47 else 3)
        dk = k-(db-b); bucket = 64 if dk<=30 else 96
        primary = 2**db*(bucket+8*((bucket-1)//(dk+1))*v)
        checksums[panel,k,v,b,n,overlap,seed] = rows[0]['checksum']
        for r in rows:
            for field,want in dict(K=k,V=v,b=b,inputs=n,overlap=overlap,seed=seed,destination_b=db,incoming=(n-1)*pop,union=union,workspace_bytes=0).items():
                assert int(r[field]) == want, (name,field)
            assert r['checksum'] == rows[0]['checksum'] and int(r['rep']) in range(repetitions)
            positive(r,['prepare_ns','merge_ns','total_ns'])
            assert abs(float(r['prepare_ns'])+float(r['merge_ns'])-float(r['total_ns'])) < 2e-6
            if r['owner']=='ikea': assert int(r['primary_bytes'])==primary
            cells['accumulation',panel,k,v,b,n,overlap][r['owner']].append({f:float(r[f]) for f in ['prepare_ns','merge_ns','total_ns','primary_bytes','overflow_bytes','spills']})
            rows_count += 1

if manifest['compare_capture']:
    for panel,cases in [('basic',basic),('wide',wide)]:
        for (k,b,n),o,seed in itertools.product(cases,[0,50,100],seeds):
            rows = read(root/f'before-{panel}'/f'k{k}-b{b}-n{n}-o{o}-s{seed}.csv')
            assert len(rows)==4*repetitions
            assert Counter(r['owner'] for r in rows)==dict.fromkeys(['accumulate','kway','routed_before','abseil'],repetitions)
            for r in rows:
                assert r['checksum']==checksums[panel,k,8,b,n,o,seed]
                positive(r,['prepare_ns','merge_ns','total_ns'])
                cells['before',panel,k,8,b,n,o][r['owner']].append({f:float(r[f]) for f in ['prepare_ns','merge_ns','total_ns']})
                rows_count += 1
    for isa in manifest['isas']:
        proof=json.loads((root/'codegen'/isa/'comparison.json').read_text())
        assert proof['cpu']==cpu
        # Script fails on missing symbols; every compared stream must match.
        for group in ['consumers','outlined']:
            assert proof[group] and all(row['identical'] for row in proof[group])
        assert len(proof['consumers']) in (17, 21) and len(proof['outlined']) >= 6

rmw_source = None
if a.rmw:
    assert (a.rmw / 'complete.txt').read_text().strip() == f'{cpu} RMW verified'
    supplement = json.loads((a.rmw / 'cases.json').read_text())
    assert supplement == dict(cpu=cpu, isas=manifest['isas'], seeds=seeds,
                              repetitions=repetitions, only='RMW mixer inline repair')
    assert '21.1.8' in (a.rmw / 'compiler.txt').read_text()
    rmw_source = json.loads((a.rmw / 'job.json').read_text())['source']
    # Audit original rows too, but replace only the affected RMW measurements.
    for key in list(cells):
        if key[0] == 'core' and key[4] in ('modify', 'modify_or_insert'):
            del cells[key]
    original_root, root = root, a.rmw
    for isa in manifest['isas']:
        proof = json.loads((root/'codegen'/isa/'comparison.json').read_text())
        assert len(proof['consumers']) == 21
        assert all(r['identical'] for r in proof['consumers'] + proof['outlined'])
        commands = json.loads((root/isa/'compile.json').read_text())
        for suffix in ('operations.cpp', 'raw_hash_set.cc'):
            chosen = [c for c in commands if c['file'].endswith('/'+suffix)]
            assert chosen
            for c in chosen:
                cmd = c.get('command', ' '.join(c.get('arguments', [])))
                assert all(x in cmd for x in (f'-march={native if isa == "native" else "x86-64-v3"}', f'-mtune={tune}', '-DNDEBUG'))
        for k,v,seed,rep,load,fresh,owner in itertools.product([14,30],[8,64],seeds,range(repetitions),[50,85],[0,50,100],['ikea','abseil']):
            core(isa, f'modify-k{k}-v{v}-{owner}-s{seed}-r{rep}-l{load}-n{fresh}.csv',
                 k,v,owner,seed,rep,load,fresh,0,0,['modify_or_insert' if fresh else 'modify']*4)
    root = original_root

for directory,names in expected.items():
    assert {p.name for p in directory.glob('*.csv')}==names, directory
summary=[]
for key,owners in sorted(cells.items()):
    measures={owner:{field:stats([r[field] for r in records]) for field in records[0]} for owner,records in owners.items()}
    row=dict(dimensions=key, samples={owner:len(rs) for owner,rs in owners.items()},measurements=measures)
    field='ns' if key[0]=='core' else 'merge_ns'
    if 'ikea' in measures:
        row['ikea_abseil_ratio']=measures['ikea'][field]['median']/measures['abseil'][field]['median']
    summary.append(row)
result=dict(cpu=cpu,job=job.get('id',root.parent.name),source=job['source'],manifest=manifest,
            rmw_source=rmw_source,
            hardware=json.loads((root/'lscpu.json').read_text()),rows=rows_count,summary=summary,
            comparison_limit='Captured before uses four controls; maintained uses two. Same-host runs, not paired same-binary timings.',
            growth_control='Abseil copy/reserve; original growth qualification also retains reserved insertion',
            reducer='byte fold; copy/add and partial-value performance not qualified')
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(result,indent=2)+'\n')
print(f'Audited {rows_count} rows / {len(summary)} cells, complete grids, module/header/sanitizer checks and actual Abseil flags')
