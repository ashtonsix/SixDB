#!/usr/bin/env python3
"""Audit fusion of actual timed functions; source dependency also needs inspection."""
import collections,hashlib,json,pathlib,re,sys
source=pathlib.Path(sys.argv[1]);name='';functions=set();calls=[]
setup=collections.Counter();digest=hashlib.sha256()
for raw in source.open('rb'):
    digest.update(raw)
    line=raw.decode()
    found=re.match(r'^[0-9a-f]+ <(.*)>:',line)
    if found:name=found[1]
    early=name.startswith('void early::measure<')
    if not early and not name.startswith('void measure<'):continue
    functions.add(name)
    if re.search(r'\t(?:bl|callq?|blr)\s',line) and not any(x in line for x in ('steady_clock','printf')):
        # Early-exit measurement functions first check command-line case filters.
        # Their string calls are outside timing; inspect that placement separately.
        if early and re.search(r'<(?:strlen|bcmp|memcmp)@plt>',line):
            setup[line[line.index('<'):].strip()]+=1
        else:calls.append({'function':name,'instruction':line.strip()})
report={'file':source.name,'sha256':digest.hexdigest(),
        'measurement_functions':len(functions),'unexpected_calls':calls,
        'option_filter_calls':dict(setup),
        'scope':'All actual measure functions. This checks fusion, not cache residence, latency dataflow, or placement of allowed setup calls.'}
pathlib.Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
if not functions or calls:raise SystemExit('fusion audit failed')
