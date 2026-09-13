#!/usr/bin/env python3
"""Exact Boolean refinement and modeled line unions at equal four-byte budgets."""
import argparse
import csv
import hashlib
import itertools
import json
from pathlib import Path
import random

EXPRESSIONS={
    '(A|C)&(B|D)':lambda v: bool(((v&1) or (v&4)) and ((v&2) or (v&8))),
    '(A&B)|C':lambda v: bool(((v&1) and (v&2)) or (v&4))}
GROUPINGS={'one4':[(0,1,2,3)],'AC_BD':[(0,2),(1,3)],
           'AB_CD':[(0,1),(2,3)],'A_C_B_D':[(0,),(2,),(1,),(3,)]}


def evaluate(values,expression,planes,grain,placement):
    # Bounds enumerate all completions of unknown fields; these are exact
    # Boolean values, not probabilistic fingerprint evidence.
    known=0
    unresolved=set(range(len(values)))
    matches=set()
    lines=set()
    logical_bytes=groups_visited=rows_refined=0
    base=0
    for plane in planes:
        groups=sorted({r//grain for r in unresolved})
        groups_visited+=len(groups)
        known |= sum(1<<f for f in plane)
        by_value={v:{expression(x) for x in range(16) if (x&known)==(v&known)}
                  for v in range(16)}
        for group in groups:
            first=group*grain;last=min(first+grain,len(values))
            logical_bytes+=(last-first)*len(plane)
            if placement=='planes':
                lo=base+first*len(plane);hi=base+last*len(plane)-1
                lines.update(range(lo//64,hi//64+1))
            else:
                for r in range(first,last):
                    lines.update((4*r+f)//64 for f in plane)
            for r in range(first,last):
                if r not in unresolved:continue
                rows_refined+=1
                truth=by_value[values[r]]
                if len(truth)==1:
                    unresolved.remove(r)
                    if True in truth:matches.add(r)
        base+=len(values)*len(plane)
    assert not unresolved
    assert matches=={r for r,v in enumerate(values) if expression(v)}
    return {'matches':len(matches),'loaded_field_bytes':logical_bytes,
            'visited_groups':groups_visited,'refined_rows':rows_refined,
            'modeled_distinct_64B_lines':len(lines)}


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--rows',type=int,default=65536)
    ap.add_argument('--seed',type=int,default=192837)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args()
    assert args.rows>0 and args.rows%256==0
    rng=random.Random(args.seed)
    values=[sum((rng.random()<.1)<<f for f in range(4)) for _ in range(args.rows)]
    rows=[]
    for order,data in [('iid',values),('clustered_same_values',sorted(values))]:
        for name,expression in EXPRESSIONS.items():
            for grouping,planes in GROUPINGS.items():
                for grain,placement in itertools.product([16,64,256],['planes','interleaved']):
                    rows.append(dict(order=order,expression=name,grouping=grouping,grain=grain,
                        placement=placement,**evaluate(data,expression,planes,grain,placement)))
    args.output.mkdir(parents=True,exist_ok=True)
    with (args.output/'counts.csv').open('w') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    meta={'kind':'exact logical counts and modeled address unions; no CPU timing',
        'rows':args.rows,'seed':args.seed,'field_true_probability':.1,'logical_bytes_per_row':4,
        'script_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'value_sequence_sha256':hashlib.sha256(bytes(values)).hexdigest(),
        'assumptions':['one byte per Boolean field; all group rows loaded for an unresolved group',
          'filter-only result, including certified matches; no later field projection',
          'aligned synthetic plane origins; field-byte traffic is distinct from line demand',
          'line union is a cold-demand geometry model, not measured bandwidth or prefetch',
          'clustered fixture is the same multiset sorted by complete field state']}
    (args.output/'provenance.json').write_text(json.dumps(meta,indent=2)+'\n')
    print(f'{len(rows)} configurations independently checked against complete Boolean evaluation.')


if __name__=='__main__':main()
