#!/usr/bin/env python3
"""Static models for bounded witnesses; not measured throughput or latency."""
import pathlib,re,subprocess,sys
source=pathlib.Path(sys.argv[1]).read_text()
cpu={'zen5':'znver5','granite-rapids':'graniterapids','neoverse-v2':'neoverse-v2'}[sys.argv[2]]
out=pathlib.Path(sys.argv[3]);out.mkdir(parents=True,exist_ok=True)
for name in ('series8_equal','fused8_equal','series3_equal','packed3_equal','series64_less'):
    match=re.search(r'^'+name+r':.*?^\.Lfunc_end\d+:',source,re.M|re.S)
    if not match: raise RuntimeError('missing witness '+name)
    asm=out/(name+'.s');asm.write_text('\n'.join(line for line in match.group().splitlines() if '.cfi_' not in line))
    result=subprocess.run(['llvm-mca-21','-mcpu='+cpu,'-iterations=100',str(asm)],capture_output=True,text=True)
    (out/(name+'.txt')).write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError('mca failed '+name)
