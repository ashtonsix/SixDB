#!/usr/bin/env python3
"""Summarize genuine CPU-clock samples without conflating oracles with mutation."""
import argparse
import hashlib
import json
import re
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
records={}
for log in sorted(a.root.glob('*.record.txt')):
    stem=log.name.removesuffix('.record.txt');text=log.read_text()
    sample=re.search(r'\((\d+) samples\)',text)
    result={'record_sha256':hashlib.sha256(log.read_bytes()).hexdigest()}
    symbols=a.root/(stem+'.symbols.txt');lines=a.root/(stem+'.lines.txt')
    if not sample or not symbols.exists():
        result.update(status='unavailable',diagnostic=text);records[stem]=result;continue
    result.update(status='sampled',samples=int(sample[1]),event='cpu-clock:u')
    lost=re.search(r'Total Lost Samples:\s*(\d+)',symbols.read_text());assert lost
    result['lost_samples']=int(lost[1]);result['top_symbols']=[];result['mutation_lines']=[]
    for line in symbols.read_text().splitlines():
        if not re.match(r'^\s*\d+\.\d+%\s',line):continue
        fields=re.split(r'\s{2,}',line.strip())
        if len(result['top_symbols'])<16:result['top_symbols'].append(dict(percent=float(fields[0][:-1]),symbol=fields[3]))
        if 'candidate::visit<' in fields[3] and 'candidate::apply(' in fields[3]:result['mutation_percent']=float(fields[0][:-1])
    for line in lines.read_text().splitlines():
        if not re.match(r'^\s*\d+\.\d+%\s',line) or 'candidate::visit<' not in line:continue
        fields=re.split(r'\s{2,}',line.strip())
        result['mutation_lines'].append(dict(percent=float(fields[0][:-1]),line=fields[2]))
    result['symbols_sha256']=hashlib.sha256(symbols.read_bytes()).hexdigest()
    result['lines_sha256']=hashlib.sha256(lines.read_bytes()).hexdigest()
    records[stem]=result
assert records
out={'scope':'Whole process includes both contenders, four routing/skew histories, preparation/oracles and coordinator. Percentages are CPU-clock self samples, not elapsed-time shares of the timed mutation phase, PMU cycles, cache misses or serial latency. Inlined source-line attribution is approximate.', 'profiles':records}
a.output.write_text(json.dumps(out,indent=2)+'\n')
print(f'{sum(x["status"]=="sampled" for x in records.values())}/{len(records)} profiles sampled; unavailable diagnostics retained')
