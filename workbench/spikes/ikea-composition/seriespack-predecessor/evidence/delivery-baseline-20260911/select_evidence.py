from pathlib import Path
import csv,json,shutil,sys,hashlib
root=Path.cwd();sys.path.insert(0,str(root/'workbench/tools'))
from experiment import Run
base=root/'build/workspaces/seriespack-delivery-current-20260911'
old=root/'build/successor-delivery/retained-all-r2'
out=root/'build/successor-delivery/selected-baseline'
run=Run(base,out,{'scope':'Post-study decision selection from verified complete current baseline; not another measurement','production_installed':False,'full_analysis_recovery':'full-analysis-artifact.json'})
files=[]
for n in ['audit.json','inventory.csv','checked-point.csv','code-costs.csv','large-plan-qualification.json','large-source/cases.csv','large-source/provenance.json','same-wire-comparisons.csv','workers/zen-artifact.json','workers/gnr-artifact.json','workers/v2-artifact.json']:
 dest=out/n;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(old/n,dest);files.append(n)
export=root/'workbench/spikes/ikea-composition/validation/evidence/delivery-baseline-20260911'
shutil.copy2(export/'artifact.json',out/'full-analysis-artifact.json');files.append('full-analysis-artifact.json')
context=json.loads((old/'baseline/provenance.json').read_text())
context['full_table_recovery']={'reference':'full-analysis-artifact.json','member':'baseline/cases.csv','sha256':context['files_sha256']['cases.csv']}
(out/'baseline-inputs.json').write_text(json.dumps(context,indent=2)+'\n');files.append('baseline-inputs.json')
with (old/'comparisons.csv').open() as f:rows=list(csv.DictReader(f))
choices={}
for v in rows:
 if v['profile']!=('neon' if v['host']=='v2' else 'avx512') or '/calico/' not in v['control']:continue
 if not v['case'].startswith('bulk/') or not v['case'].endswith(('/u64/encode','/u64/decode','/u8/encode','/u8/decode')):continue
 key=(v['host'],'/'.join(v['case'].split('/')[3:]))
 previous=choices.get(key)
 if previous is None or (float(v['series_ns']),float(v['control_ns']))<(float(previous['series_ns']),float(previous['control_ns'])):choices[key]=v
with (out/'calico-bulk-selection.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(choices.values())
files.append('calico-bulk-selection.csv')
with (old/'baseline/cases.csv').open() as f:rows=list(csv.DictReader(f))
selected=[]
for v in rows:
 c=v['case'];profile=v['input']
 if profile not in ['zen-avx512','gnr-avx512','v2-neon']:continue
 if c.startswith('runtime/') or c.startswith('boundary/') or c.startswith('head-placement/'):
  selected.append(v)
 elif c.startswith('casing/') and '/local/k12/' in c and '/n8193/' in c:selected.append(v)
with (out/'range-placement-witnesses.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(selected)
files.append('range-placement-witnesses.csv')
shutil.copy2(Path(__file__),out/'select_evidence.py');files.append('select_evidence.py')
(out/'summary.md').write_text('''# Current SeriesPack decision evidence

All seven profiles pass the source/object/archive/binary, feature-applicability
and measurement audit: 21,305 cases, 63,915 baseline repetitions, all206 bulk
and76 unheaded access descriptions per profile. The scalar-x86 aggregate and
public examples pass. The separate checked-get pair has36 cases/432 timings;
the Zen large-source pilot has24 cases/72 timings.

This selection keeps available same-wire comparisons, current Calico bulk
alternatives, representative range/placement/casing witnesses, the checked-get
pair, exact large-source evidence and actual code/build costs. Individual
repetitions and ordering edges are retained where applicable. Calico bulk
selection chooses the fastest measured native family per actual SeriesPack
format and fastest available Calico plane representation; it is not runtime
dispatch or a same-wire comparison. Significant losses remain in these tables.

The complete21,305-case CSV and full comparison table are in the verified
`full-analysis-artifact.json` bundle, with their original sample provenance.
`baseline-inputs.json` identifies that full CSV hash and all profile contexts.
Original worker references recover measured sources, binaries, libraries,
flags and full JSON. This is post-study selection, not another hardware run.
The all-profile composition review is retained separately alongside its cases.
See ../../delivery-reconciliation.md for the overall judgment.
''');files.append('summary.md')
run.compact(files,[]);run.finish()
print('selected',len(files),'files',sum((out/n).stat().st_size for n in files),'bytes',len(choices),'Calico cases',len(selected),'range/placement/casing witnesses')
