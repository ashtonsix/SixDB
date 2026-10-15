#!/usr/bin/env python3
"""Summarize paired complete-table timings; no cache-residence inference."""
import argparse
import csv
from collections import defaultdict
import json
from pathlib import Path
from statistics import median


def lifecycle(path,rows):
    groups=defaultdict(list)
    axes=('arm','K','V','entries','generation','input','operation')
    counters=('primary_entries','secondary_entries','chain_entries','payload_bytes',
              'known_backing_bytes','known_metadata_bytes','shared_preparation_bytes',
              'static_zero_image_bytes')
    peers={}
    for row in rows:
        groups[tuple(row[k] for k in axes)].append(row)
        peers[(row['arm'],*(row[k] for k in axes[1:]),row['repetition'])]=row
    out=[]
    for key,values in sorted(groups.items()):
        if len({v['repetition'] for v in values})!=len(values):
            raise ValueError(f'duplicate repetitions: {path}: {key}')
        times=[float(v['ns_per_op']) for v in values]
        for v in values:
            for baseline in ('abseil_native','abseil_exact'):
                peer=peers.get((baseline,*key[1:],v['repetition']))
                if peer and peer['checksum']!=v['checksum']:
                    raise ValueError(f'unmatched checksum: {path}: {key}')
        out.append(dict(zip(axes,key),evidence=path.parent.name,file=path.name,
                        samples=len(values),median_ns=median(times),minimum_ns=min(times),maximum_ns=max(times),
                        **{c:median(int(v[c]) for v in values) for c in counters}))
    return out


def staged(path,rows):
    axes=('entries','concurrency','turnovers','seed','queries',
          'primary_buckets','primary_stride','secondary_buckets','secondary_stride')
    by_case=defaultdict(dict)
    for row in rows:
        key=tuple(row[k] for k in axes)
        sample=(row['execution'],row['repetition'])
        if sample in by_case[key]:raise ValueError(f'duplicate sample: {path}: {key}: {sample}')
        by_case[key][sample]=row
    out=[]
    for key,samples in sorted(by_case.items()):
        for arm in sorted({a for a,r in samples}):
            values=[v for (a,r),v in samples.items() if a==arm]
            times=[float(v['ns_per_lookup']) for v in values]
            ratios={}
            for baseline in ('abseil','abseil_prefetch'):
                paired=[]
                for v in values:
                    b=samples[baseline,v['repetition']]
                    for field in ('requests','checksum','primary_entries','secondary_entries','chain_entries'):
                        if v[field]!=b[field]:raise ValueError(f'unmatched {field}: {path}: {key}')
                    paired.append(float(b['ns_per_lookup'])/float(v['ns_per_lookup']))
                ratios[baseline+'_paired_speedup']=median(paired)
            counters=('requests','payload_bytes','backing_bytes','known_metadata_bytes',
                      'shared_preparation_bytes','abseil_payload_bytes','ring_storage_bytes',
                      'job_state_bytes','primary_entries','secondary_entries','chain_entries',
                      'primary_visits','secondary_visits','chain_directory_visits',
                      'chain_body_visits','chain_payload_visits','staged_hint_line_targets')
            out.append(dict(zip(axes,key),execution=arm,evidence=path.parent.name,file=path.name,
                            samples=len(values),median_ns=median(times),minimum_ns=min(times),maximum_ns=max(times),
                            **ratios,**{c:median(int(v[c]) for v in values) for c in counters}))
    return out


def record_values(path,rows):
    groups=defaultdict(dict)
    for row in rows:
        key=(row['entries'],row['operation'],row['operations'])
        sample=(row['arm'],row['repetition'])
        if sample in groups[key]:raise ValueError(f'duplicate sample: {path}: {key}: {sample}')
        groups[key][sample]=row
    out=[]
    for key,samples in sorted(groups.items()):
        for arm in sorted({a for a,r in samples}):
            values=[v for (a,r),v in samples.items() if a==arm]
            ratios=[]
            for v in values:
                baseline=samples['abseil',v['repetition']]
                if v['checksum']!=baseline['checksum']:raise ValueError(f'unmatched checksum: {path}: {key}')
                ratios.append(float(baseline['ns_per_operation'])/float(v['ns_per_operation']))
            times=[float(v['ns_per_operation']) for v in values]
            counters=[k for k in values[0] if k.startswith('final_') or k=='shared_preparation_bytes']
            out.append(dict(zip(('entries','operation','operations'),key),arm=arm,evidence=path.parent.name,file=path.name,
                            samples=len(values),median_ns=median(times),minimum_ns=min(times),maximum_ns=max(times),
                            abseil_paired_speedup=median(ratios),**{c:median(int(v[c]) for v in values) for c in counters}))
    return out


def summarize(paths):
    pairs=defaultdict(list)
    complete=[]
    for path in paths:
        rows=list(csv.DictReader(path.open()))
        if not rows:
            continue
        if 'staged_hint_line_targets' in rows[0]:
            complete.extend(staged(path,rows));continue
        if 'final_payload_bytes' in rows[0]:
            complete.extend(record_values(path,rows));continue
        if 'arm' in rows[0]:
            complete.extend(lifecycle(path,rows));continue
        if len(rows)%2:
            raise ValueError(f"unpaired rows: {path}")
        for candidate,baseline in zip(rows[::2],rows[1::2]):
            # The frozen historical driver emits balanced arm order to stdout.
            if candidate['table']=='abseil':
                candidate,baseline=baseline,candidate
            if baseline['table']!='abseil' or candidate['table']=='abseil':
                raise ValueError(f"unpaired tables: {path}")
            for field in ('n','operation','rep'):
                if candidate[field]!=baseline[field]:
                    raise ValueError(f"unpaired {field}: {path}")
            key=(str(path.parent.name),path.name,candidate['table'],candidate.get('key_bits','64'),
                 candidate.get('value_bits','64'),candidate['n'],candidate.get('slots'),
                 candidate.get('layout'),candidate.get('rounding'),candidate.get('primary_address_bits'),
                 candidate.get('primary_stride'),candidate.get('permutation','original'),candidate['operation'])
            a=float(candidate['ns_per_operation']); b=float(baseline['ns_per_operation'])
            pairs[key].append((a,b,b/a))
    names=('evidence','file','table','key_bits','value_bits','n','slots','layout','rounding','primary_address_bits','primary_stride','permutation','operation')
    return complete+[dict(zip(names,k),samples=len(v),candidate_ns=median(x[0] for x in v),
                 abseil_ns=median(x[1] for x in v),paired_speedup=median(x[2] for x in v))
            for k,v in sorted(pairs.items())]


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('paths',nargs='+',type=Path)
    args=parser.parse_args()
    print(json.dumps(summarize(args.paths),indent=2))
