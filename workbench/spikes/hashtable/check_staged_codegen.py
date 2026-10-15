#!/usr/bin/env python3
"""Check actual generic/word ready callbacks and paired Abseil consumers."""
import argparse, gzip, json, re, runpy
from pathlib import Path
u=runpy.run_path(str(Path(__file__).with_name('check_ring_codegen.py')))
p=argparse.ArgumentParser(description=__doc__);p.add_argument('assembly',type=Path)
p.add_argument('--word-control',action='store_true')
p.add_argument('--report-ready-frame',action='store_true',help='Report and charge ready-path stack references; still reject outlined calls')
a=p.parse_args()
text=gzip.open(a.assembly,'rt').read() if a.assembly.suffix=='.gz' else a.assembly.read_text()
base=re.compile(r'ht::staged_test::direct<(1|32)u, (false|true), ht::staged_test::baseline_reader>')
bodies={};current=None
for line in text.splitlines():
    h=u['HEADER'].match(line.strip())
    if h:
        name=h[1];current=name if name.startswith('void ht::staged_test::visit<') or base.search(name) else None
        if current is not None:
            assert current not in bodies
            bodies[current]=[]
    elif current is not None:
        i=u['INSTRUCTION'].match(line)
        if i:bodies[current].append((i[1],i[2],line.strip()))
ready={n:b for n,b in bodies.items() if n.startswith('void ht::staged_test::visit<')}
baseline={n:b for n,b in bodies.items() if base.search(n)}
assert len(ready)==(4 if a.word_control else 2),(len(ready),a.word_control)
assert len(baseline)==4 and {base.search(n).groups() for n in baseline}=={(str(c),h) for c in (1,32) for h in ('false','true')}
report=[];failures={};saved_callbacks=0;stack_references=0
for name,body in bodies.items():
    assert body,name
    bad=[];allowed=[];callee_save=[];frame=[]
    for m,o,line in body:
        if name in ready:
            # x86 preserve_none still preserves RBP. Word suffix assembly and
            # multimap iteration can use it as a temporary. Report and charge
            # this explicit ABI cost; do not turn a frame preference into a
            # correctness gate that prevents complete-operation measurement.
            if m in ('pushq','popq') and o.strip()=='%rbp':callee_save.append(line)
            elif u['ordinary_call'](m):bad.append(line)
            elif u['STACK_REGISTER'].search(o.split(' <',1)[0]) or m.startswith(('push','pop')) or m in ('enter','leave','leaveq'):
                if a.report_ready_frame:frame.append(line)
                else:bad.append(line)
        elif u['ordinary_call'](m):
            if re.search(r'<(?:memset|bzero)(?:@[^>]*)?>',o) or re.search(r'ht::staged_test::(?:initialize|result)<',o):allowed.append(line)
            else:bad.append(line)
        elif '<' in o and re.search(r'absl::.*::(?:find|prefetch)(?:<|\()',o):bad.append(line)
    if bad:failures[name]=bad
    if callee_save:
        assert sum('pushq' in line for line in callee_save)==1
        assert any('popq' in line for line in callee_save)
        saved_callbacks+=1
    stack_references+=len(frame)
    report.append(dict(owner='word' if 'word_reader<' in name else 'generic' if name in ready else 'abseil',
        ready=name in ready,hints='visit<true' in name,instructions=len(body),allowed_outer_calls=allowed,
        rbp_preservation=callee_save,ready_frame=frame))
assert not failures,json.dumps(failures,indent=2)
print(json.dumps(dict(consumers=report,ready_calls=0,ready_other_stack_references=stack_references,
    ready_callbacks_preserving_rbp=saved_callbacks,outlined_baseline_lookup=0,
    limitation='Static calls and frame references only; not instruction quality, lifetime, traffic or performance.'),indent=2))
