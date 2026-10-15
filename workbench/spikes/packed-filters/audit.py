#!/usr/bin/env python3
"""Audit fusion of actual timed functions; source dependency also needs inspection."""
import json,pathlib,re,sys
source=pathlib.Path(sys.argv[1]);name='';functions=set();calls=[]
for line in source.read_text().splitlines():
    found=re.match(r'^[0-9a-f]+ <(.*)>:',line)
    if found:name=found[1]
    if not name.startswith('void measure<'):continue
    functions.add(name)
    if re.search(r'\t(?:bl|callq?|blr)\s',line) and not any(x in line for x in ('steady_clock','printf')):
        calls.append({'function':name,'instruction':line.strip()})
report={'file':source.name,'measurement_functions':len(functions),'unexpected_calls':calls,
        'scope':'All actual measure functions. This checks fusion, not cache residence or latency dataflow.'}
pathlib.Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
if not functions or calls:raise SystemExit('fusion audit failed')
