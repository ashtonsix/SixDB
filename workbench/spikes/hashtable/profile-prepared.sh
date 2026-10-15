#!/bin/bash
set -euo pipefail
: "${SIXDB_RESULTS:?}" "${SIXDB_CPU:?}"
# Profile the exact retained Zen5 ELF. No hashtable compilation or source
# substitution is involved; PMU qualification builds its independent probe.
reference=workbench/spikes/hashtable/profile-prepared-input.json
recovered=build/hashtable-prepared-profile-source
python3 workbench/tools/artifacts.py fetch "$reference" "$recovered" --file formats --file job.json
python3 - "$recovered" "$SIXDB_RESULTS" "$reference" <<'PY'
import hashlib,json,sys
from pathlib import Path
source,out,reference=map(Path,sys.argv[1:])
job=json.loads((source/'job.json').read_text())
assert job['source']['digest']=='2013b46d6f86a483ebf11d70ab78e6dffcd5a5b2c702eb57ec3eba8c6756238e'
receipt={'artifact':json.loads(reference.read_text()),'source_manifest':job['source']['digest'],
         'elf_sha256':hashlib.sha256((source/'formats').read_bytes()).hexdigest(),
         'scope':'sampling of an unchanged retained ELF; profiled timings are not benchmark replacements'}
(out/'profile-input.json').write_text(json.dumps(receipt,indent=2)+'\n')
PY
bash workbench/benchmarks/orbital/pmu.sh
sudo -n apt-get update > "$SIXDB_RESULTS/perf-install.log" 2>&1
sudo -n apt-get install -y linux-tools-common linux-tools-generic >> "$SIXDB_RESULTS/perf-install.log" 2>&1
python3 - "$recovered/formats" "$SIXDB_RESULTS" <<'PY'
import json,os,subprocess,sys,hashlib,time
from pathlib import Path
exe=Path(sys.argv[1]).resolve();out=Path(sys.argv[2]).resolve()
tools=sorted(Path('/usr/lib/linux-tools').glob('*/perf'))
assert tools,'installed perf executable'
perf=str(tools[-1]);cpu=os.environ['SIXDB_CPU']
receipt={'perf':perf,'version':subprocess.check_output([perf,'--version'],text=True).strip(),
         'cpu':int(cpu),'repetitions':int(os.environ.get('HASHTABLE_PROFILE_REPEATS','1000')),'runs':[]}
# Identify permission without modifying global perf policy.
prefix=None
for identity in ([],['sudo','-n']):
 p=subprocess.run([*identity,perf,'stat','-e','cycles:u','--','true'],text=True,capture_output=True)
 (out/('perf-access-'+('root' if identity else 'user')+'.txt')).write_text(p.stdout+p.stderr)
 if p.returncode==0:prefix=identity;break
if prefix is None:
 receipt['status']='sampling_unavailable'
 (out/'profile.json').write_text(json.dumps(receipt,indent=2)+'\n');sys.exit(0)
for name,kind,arm,lo,hi in [('set-composed',0,0,0x800334,0x80073e),
                         ('set-direct',0,1,0x813583,0x81399b),
                         ('mm-exact',2,0,0x957436,0x9578f5),
                         ('mm-wide',2,6,0x9f00f3,0x9f05bb)]:
 data=out/(name+'.data')
 cmd=[*prefix,perf,'record','--no-inherit','-e','cycles:u','-F','997','--sample-cpu',
      '-o',str(data),'--','taskset','-c',cpu,str(exe),'run','40',str(kind),str(arm),str(receipt['repetitions']),'1']
 before=time.time_ns()
 with (out/(name+'.csv')).open('w') as stdout,(out/(name+'.log')).open('w') as stderr:
  result=subprocess.run(cmd,stdout=stdout,stderr=stderr)
 item={'name':name,'argv':cmd,'returncode':result.returncode,'start_ns':before,'end_ns':time.time_ns(),
       'elf_hit_range':[hex(lo),hex(hi)]}
 if result.returncode==0:
  with (out/(name+'-events.txt')).open('w') as f:
   # Preserve mmap records; sample IPs must be relocated to ELF addresses.
   subprocess.run([*prefix,perf,'script','--show-mmap-events','--show-task-events','-i',str(data),
                   '-F','comm,pid,tid,time,event,ip,sym,dso,period'],stdout=f,check=True)
  item['data_sha256']=hashlib.sha256(data.read_bytes()).hexdigest()
 receipt['runs'].append(item)
 (out/'profile.json').write_text(json.dumps(receipt,indent=2)+'\n')
 if result.returncode:break
receipt['status']='complete' if len(receipt['runs'])==4 and all(r['returncode']==0 for r in receipt['runs']) else 'sampling_failed'
(out/'profile.json').write_text(json.dumps(receipt,indent=2)+'\n')
PY
