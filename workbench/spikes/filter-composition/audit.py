#!/usr/bin/env python3
"""Classify actual consumers and CPS stages; does not establish cache residence."""
import json,pathlib,re,sys
source=pathlib.Path(sys.argv[1]); functions={}; name=''
for line in source.read_text().splitlines():
    found=re.match(r'^[0-9a-f]+ <(.*)>:',line)
    if found:name=found[1];functions[name]=[]
    elif name:functions[name].append(line)
report={'file':source.name,'inline_consumers':0,'stage_functions':0,'unexpected_inline_calls':[],
        'ordinary_stage_calls':[],'stage_stack_accesses':[],
        'scope':'Actual measurement loops and stage bodies. Dynamic entry, tail branches and context traffic are charged by timings.'}
for name,lines in functions.items():
    inline='runtime::measure<' in name and any(x in name for x in ('run_case<','run_tuple<','run_pair<','run_extra<'))
    stage='::stage<' in name and ('runtime::' in name or 'extras::' in name)
    if inline:report['inline_consumers']+=1
    if stage:report['stage_functions']+=1
    for line in lines:
        call=bool(re.search(r'\t(?:bl|blr|callq?)\s',line))
        if inline and call and not any(x in line for x in ['steady_clock','printf','entry','shared_entry']):
            report['unexpected_inline_calls'].append({'function':name,'instruction':line.strip()})
        if stage and call:report['ordinary_stage_calls'].append({'function':name,'instruction':line.strip()})
        if stage and re.search(r'\[(?:sp|x29)(?:,|\])|\(%rsp\)',line):
            report['stage_stack_accesses'].append({'function':name,'instruction':line.strip()})
pathlib.Path(sys.argv[2]).write_text(json.dumps(report,indent=2)+'\n')
if not report['inline_consumers']:raise SystemExit('no measured inline consumers found')
if report['unexpected_inline_calls'] or report['ordinary_stage_calls']:raise SystemExit('unexpected outlining: inspect report')
