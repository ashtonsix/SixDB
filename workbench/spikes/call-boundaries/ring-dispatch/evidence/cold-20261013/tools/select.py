#!/usr/bin/env python3
"""Select the complete bounded six-factor comparison; do not archive or promote."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys

HERE=Path(__file__).resolve().parent
COMMIT='8d640b9e3a2a384eaad733f61a92c1c1e019a879'
DIGEST='e1294dfb1e593aa7d0338d1502678aa8731dcef9498f65d88907d135b5704d19'
MODES=('reference','sync','tail','vector','snapshot','slim')
MEASUREMENT=re.compile(r'^(neon|avx512)-cold_(reference|sync|tail|vector|snapshot|slim)_grain([14])-(128|4194304)-(1|32)-pass([01])\.csv$')
SYMBOL=re.compile(r'^([0-9a-f]+) <(.+)>:$')
INSTRUCTION=re.compile(r'^\s*([0-9a-f]+):\s*\t(.+)$')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(path):return json.loads(path.read_text())
def check(condition,message):
    if not condition:raise ValueError(message)
def dump(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,indent=2)+'\n')

def parse_assembly(path):
    bodies={};current=None
    for line_number,line in enumerate(path.read_text().splitlines(),1):
        match=SYMBOL.fullmatch(line)
        if match:
            current={'address':match[1],'symbol':line,'line':line_number,'instructions':[]}
            bodies[line]=current
        elif current:
            match=INSTRUCTION.match(line)
            if match:current['instructions'].append({'address':match[1],'line':line_number,'text':line})
    return bodies

def excerpts(result,isa,collection,source_hashes):
    codegen=load(result/f'cold-codegen-{isa}.json')
    compact={'isa':isa,'limit':'Exact Hint1 prologue, stack/call and native handoff excerpts; dynamic execution and total nested stack cannot be inferred from static counts. Full ASM remains in the named worker archive.','policies':{}}
    for policy,record in codegen.items():
        for path,digest in record['selected_headers'].items():
            check(source_hashes.get(path)==digest,f'wrong included header {policy}/{path}')
        member=f'assembly-{isa}-{policy}.txt';path=result/member
        check(sha(path)==record['assembly_sha256']==collection['members'][member]['sha256'],f'assembly identity {path}')
        bodies=parse_assembly(path);selected=[]
        for body in record['bodies']:
            if '(loom::Hint)1' not in body['symbol']:continue
            actual=bodies[body['symbol']];ins=actual['instructions']
            check(len(ins)==body['emitted_instructions'],f'instruction count {path}/{body["phase"]}')
            keep=set(range(min(12,len(ins))))
            calls=[];tails=[]
            rbp=any(re.search(r'\bmovq\s+%rsp, %rbp\b',i['text']) for i in ins[:14])
            stack=[]
            for k,instruction in enumerate(ins):
                text=instruction['text'].split('\t',1)[-1]
                if re.match(r'\s*(bl|blr|callq?)\b',text):
                    calls.append(k);keep.update(range(max(0,k-3),min(len(ins),k+4)))
                if re.match(r'\s*(br\b|jmpq?\s+\*)',text):
                    tails.append(k);keep.update(range(max(0,k-15),min(len(ins),k+2)))
                arm_frame=any(re.search(r'\b(?:mov|add)\s+x29, sp\b',i['text']) for i in ins[:14])
                is_stack=(bool(re.search(r'\bsp\b',text)) or arm_frame and bool(re.search(r'\[x29\b',text)) if isa=='neon' else '%rsp' in text or bool(re.match(r'\s*(pushq|popq)\b',text)) or rbp and '(%rbp)' in text)
                if is_stack:stack.append(k);keep.add(k)
            allocation=0
            for i in ins[:14]:
                text=i['text'].split('\t',1)[-1]
                if isa=='neon':
                    match=re.search(r'\[sp, #-0x([0-9a-f]+)\]!',text) or re.search(r'\bsub\s+sp, sp, #0x([0-9a-f]+)',text)
                    if match:allocation+=int(match[1],16)
                else:
                    if re.match(r'\s*pushq\b',text):allocation+=8
                    match=re.search(r'\bsubq\s+\$0x([0-9a-f]+), %rsp',text)
                    if match:allocation+=int(match[1],16)
            phase=body['phase']
            if record['mode'] in ('tail','vector','snapshot','slim') and phase in ('metadata','payload'):check(not calls,f'ordinary common call {policy}/{phase}')
            selected.append({'phase':phase,'address':actual['address'],'assembly_line':actual['line'],
                'emitted_all_path_instructions':len(ins),'ordinary_call_sites':len(calls),
                'entry_frame_bytes_excluding_red_zone':allocation,'stack_instruction_count':len(stack),
                'red_zone_memory_instruction_count':sum(bool(re.search(r'-0x[0-9a-f]+\(%rsp\)',ins[k]['text'])) for k in stack),
                'stack_accounting':'SP plus proved x29 frame base on ARM; RSP/push/pop plus proved RBP frame base on x86; no nested callee sum',
                'arm_x29_is_frame_base':arm_frame if isa=='neon' else None,
                'native_tail_sites':[ins[k]['address'] for k in tails],
                'excerpt_format':'Actual address/mnemonic/operands with verbose symbol annotations omitted; line numbers plus SHA256 of raw selected lines joined with newline (no trailing newline) recover exact text.',
                'raw_selected_lines_sha256':hashlib.sha256('\n'.join(ins[k]['text'] for k in sorted(keep)).encode()).hexdigest(),
                'selected_instructions':[{'line':ins[k]['line'],'text':ins[k]['text'].partition(' <')[0]} for k in sorted(keep)]})
        expected={'metadata','payload'}
        if record['mode'] in ('tail','vector','snapshot','slim'):expected.add('payload_cold')
        if record['mode']=='snapshot':expected.add('finish_cold')
        check({b['phase'] for b in selected}==expected,f'Hint1 phase coverage {policy}')
        compact['policies'][policy]={'mode':record['mode'],'configured_grain':record['configured_grain'],
            'assembly_member':member,'assembly_sha256':record['assembly_sha256'],
            'reported_codegen_member':f'cold-codegen-{isa}.json','reported_codegen_sha256':sha(result/f'cold-codegen-{isa}.json'),
            'selected_headers':record['selected_headers'],'bodies':selected}
    return compact

def select(args):
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    index={'format':1,'status':'selected only, no archive/promotion/commit performed','source_commit':COMMIT,'source_digest':DIGEST,
        'scope':{'modes':MODES,'grains':[1,4],'sizes':[128,4194304],'chains':[1,32],'passes':[0,1],'repetitions_per_pass':7,
                 'arms':['abseil','abseil-prefetch','packed','direct','manual','manual-hints','ring','ring-hints'],
                 'selection':'Every qualified main CSV/context retained, including singleton and no-hint negatives. Warm probes excluded. Selected Hint1 common/cold codegen only; full source/binaries/ASM recovered from worker archives.'},
        'jobs':{},'failed_attempts':{},'payloads':[],
        'related_owner_evidence':{'Ikea_currentTable':{'results':'workbench/spikes/hashtable/RESULTS.md:684','evidence':'workbench/spikes/hashtable/evidence/table-ring-zen5-avx2',
            'artifact_json_sha256':'5664b9f2b14cd121c0ae0194562474a51ef38d7881cb91a414198114ea015d7f',
            'full_bundle_sha256':'691baa9010b33198c54c54684fedff3ea0956bbcb933e4b1227f524fc0bcc5e0',
            'capture':'7a2b737','relationship':'Independent owner comparison referenced, not copied or combined with this campaign.'}},
        'limits':['All comparisons are within this frozen bounded driver; no public API or mixed-family policy contract is established.',
            'Normal4M timed population is4,190,897 primary+3,407 secondary+0 overflow; forced oracle supplies80 real overflow. Normal timing is not a chain-heavy workload.',
            'Frame adaptation, cold tail, prepared kernels/fact hoisting, snapshot helper and completion fusion are distinct factors.',
            'Vector carriers reduce scalar register pressure but extraction/spills and cold code still matter; homogeneous Map/family facts cannot become a global mixed-family assumption.',
            'Snapshot adds8 bytes per Frame and an ordinary cold helper; native wrapper frame alone is not total cold cost.',
            'Slot56 with an8-byte restored local argument can straddle hot prefixes; footprint reduction alone does not imply throughput benefit.',
            'Native visits are measured; useful stage work is derived for equal-length chains. No PMU/stall/cache-tier causation is claimed.',
            'Per-pass factors and actual manual controls must be inspected before merged medians; drift flags are descriptive, not significance tests.',
            'Original unpinned old-reference VM abort has no recovered originating stack; the separate forced CPU mismatch and debugger core do not resolve its cause. Native CPU0 qualifications are separate.']}
    def copy_checked(path,destination,expected=None):
        actual=sha(path)
        if expected:check(actual==expected,f'changed input {path}')
        target=output/destination;target.parent.mkdir(parents=True,exist_ok=True)
        if target.exists():check(sha(target)==actual,f'refuse changed previously selected raw bytes {target}')
        else:shutil.copyfile(path,target)
        return {'path':str(destination),'sha256':actual,'bytes':path.stat().st_size}
    for job_id in args.job:
        directory=args.workers/job_id;result=directory/'results'
        collection=load(directory/'collection.json');job=load(directory/'job.json');status=load(result/'worker-result.json')
        check(status['state']=='complete' and status['script_returncode']==0,f'unqualified worker {job_id}')
        check(job['source_commit']==COMMIT and job['source']['digest']==DIGEST,f'wrong source {job_id}')
        check(collection['members']['source.tar.gz']['sha256']==job['source']['sha256'],f'source archive identity {job_id}')
        sources=load(result/'dispatch-sources.json');provenance=load(result/'cold-capture-provenance.json')
        check(sources['fixture_commit']==COMMIT,f'wrong fixture {job_id}')
        hashes=sources['tracked_source_sha256'];identity=dict(hashes);identity['controls/cold-source-manifest.json']=sources['manifest_sha256']
        check(hashlib.sha256(json.dumps(identity,sort_keys=True).encode()).hexdigest()==DIGEST,f'source content {job_id}')
        for p,digest in provenance['source_identities'].items():check(hashes[p]==digest,f'factor source identity {p}')
        receipt={'job_id':job_id,'worker_uri':job['uri'],'source':job['source'],'script':job['script'],
            'result_archive':collection['artifact'],'source_archive_member':'source.tar.gz','source_archive_sha256':collection['members']['source.tar.gz']['sha256'],
            'worker_result':status,'isas':{},'retained_members':{},'referenced_members':{},
            'local_job_sha256':sha(directory/'job.json'),'collection_sha256':sha(directory/'collection.json')}
        def reference(name,retain=False,destination=None):
            entry=collection['members'][name];path=result/name
            if path.exists():check(sha(path)==entry['sha256'],f'worker archive member changed {job_id}/{name}')
            item={k:entry[k] for k in ('sha256','bytes')}
            if retain:
                item['retained_as']=str(destination or Path('workers')/job_id/name)
                copy_checked(path,Path(item['retained_as']),entry['sha256']);receipt['retained_members'][name]=item
            else:receipt['referenced_members'][name]=item
            return path
        for name in ('worker-result.json','job.json','cold-capture-provenance.json','cold-source-receipt.json','prepared-capture-inputs.json','compiler.txt','lscpu.json','kernel.txt','meminfo.txt','executable-order.csv'):
            reference(name,True)
        source_index={'fixture_commit':COMMIT,'content_digest':DIGEST,'manifest_files':len(identity),
            'dispatch_sources_member':'dispatch-sources.json','dispatch_sources_sha256':sha(result/'dispatch-sources.json'),
            'source_manifest_member':'source-manifest.json','source_manifest_sha256':sha(result/'source-manifest.json'),
            'selected_source_identities':{p:d for p,d in hashes.items() if p.startswith(('controls/','loom/include/','loom/src/','ikea/include/ikea/tuplepack/')) or p in ('run-cold.sh','workbench/spikes/hashtable/CMakeLists.txt','workbench/spikes/hashtable/map.h','workbench/spikes/hashtable/baseline.h','ikea/include/ikea/pipelines/native_chain.h')},
            'factor_costs':provenance['factors'],
            'native_prefix_layout':{'pointer_modes':{'modes':['reference','sync','tail','slim'],'shared_bytes':8,'saved_local_argument_bytes':8},
                'prepared_modes':{'modes':['vector','snapshot'],'shared_bytes':56,'vectors_128bit':3,'scalar_line_bytes':8,'saved_local_argument_bytes':8},
                'slot_bytes':56,'limit':'Static native signature/storage facts, not a claim that all arguments remain live or spill-free throughout callback bodies.'}}
        dump(output/'workers'/job_id/'source-index.json',source_index)
        for name in ('source.tar.gz','source-manifest.json','dispatch-sources.json','host.json','cold-local-preflight.json','slim-source-codegen.json','slim-source-qualification.json'):
            reference(name)
        order=list(csv.DictReader(reference('executable-order.csv').open()))
        isas=sorted({r['isa'] for r in order})
        for isa in isas:
            check(isa in ('neon','avx512'),f'unexpected ISA {isa}')
            native=reference(f'loom-validation-{isa}.txt',True);san=reference(f'asan-qualification-{isa}.txt',True)
            check(re.search(r'0 tests failed out of 16\b',native.read_text()),'native16 missing')
            check(re.search(r'0 tests failed out of 6\b',san.read_text()),'sanitizer6 missing')
            for name in (f'hardware-{isa}.json',f'binary-sha256-{isa}.txt',f'library-sha256-{isa}.txt'):
                reference(name,True)
            for mode in MODES[1:]:
                for grain in (1,4):
                    target=f'hashtable_ring_cold_{mode}_grain{grain}'
                    for prefix in ('forced','asan-forced'):
                        stdout=reference(f'{prefix}-{isa}-{target}.txt',True);reference(f'{prefix}-{isa}-{target}.context',True)
                        text=stdout.read_text();check('PASS' in text,f'forced failed {target}')
                        for token in (f'grain={grain}','primary_hits=16','primary_misses=1','downstream_hits=112','downstream_misses=1','real_secondary=32','real_overflow=80'):
                            check(token in text,f'forced coverage {target}/{token}')
                        stderr=(result/f'{prefix}-{isa}-{target}.context').read_text()
                        for case in ('ragged0_cancel0','ragged1_cancel0','ragged1_cancel1'):
                            check(f'cold_semantics_case={case}' in stderr,f'forced lifecycle {target}/{case}')
            compile_receipt={'isa':isa,'members':{}}
            for prefix in ('compile','compile-asan'):
                name=f'{prefix}-{isa}.json';commands=load(reference(name))
                selected=[r for r in commands if 'hashtable_ring_cold_' in r['output'] or '/loom/' in r['file']]
                check(sum('hashtable_ring_cold_' in r['output'] for r in selected)==12,'compile target coverage')
                compile_receipt['members'][name]={'sha256':sha(result/name),'selected_commands':selected}
            dump(output/'workers'/job_id/f'compile-selected-{isa}.json',compile_receipt)
            codegen=excerpts(result,isa,collection,hashes)
            dump(output/'workers'/job_id/f'codegen-selected-{isa}.json',codegen)
            reference(f'cold-codegen-{isa}.json')
            for policy in codegen['policies']:reference(f'assembly-{isa}-{policy}.txt')
            measurements=[r for r in order if r['isa']==isa];check(len(measurements)==96,'measurement coverage')
            row_count=0
            for row in measurements:
                name=f"{isa}-{row['policy']}-{row['n']}-{row['chains']}-pass{row['pass']}.csv"
                check(MEASUREMENT.fullmatch(name),'measurement name')
                path=reference(name,True);reference(name.replace('.csv','.context'),True)
                data=list(csv.DictReader(path.open()));check(len(data)==56,'sample count');row_count+=len(data)
            receipt['isas'][isa]={'measurement_csvs':len(measurements),'rows':row_count,'native_checks':16,'sanitizer_checks':6,'forced_release':10,'forced_sanitized':10}
        index['jobs'][job_id]=receipt
    for job_id in args.failed:
        directory=args.workers/job_id;collection=load(directory/'collection.json');result=directory/'results'
        status=load(directory/'status.json');job=load(directory/'job.json')
        timing_files=[p.name for p in result.glob('*.csv') if MEASUREMENT.fullmatch(p.name)]
        check(not timing_files,f'failed attempt has timings {job_id}')
        diagnostic={'job_id':job_id,'status':status,'job_source':job['source'],'result_archive':collection['artifact'],'admitted_timing_cells':0,'referenced_members':{}}
        for name in ('job.json','worker-result.json'):
            if name in collection['members']:copy_checked(result/name,Path('failed')/job_id/name,collection['members'][name]['sha256'])
        for name in ('build-asan-neon.log','script.log'):
            if name in collection['members']:
                entry=collection['members'][name];check(sha(result/name)==entry['sha256'],'failed log changed')
                diagnostic['referenced_members'][name]={k:entry[k] for k in ('sha256','bytes')}
                if name=='build-asan-neon.log':
                    check('Killed' in (result/name).read_text(),'missing compiler killed')
                    copy_checked(result/name,Path('failed')/job_id/'build-asan-neon.txt',entry['sha256'])
                    diagnostic['referenced_members'][name]['retained_as']=f'failed/{job_id}/build-asan-neon.txt'
        dump(output/'failed'/job_id/'diagnosis.json',diagnostic);index['failed_attempts'][job_id]=diagnostic
    (output/'tools').mkdir(exist_ok=True)
    for name in ('select.py','report.py'):
        shutil.copyfile(HERE/name,output/'tools'/name)
    shutil.copyfile(args.analyzer,output/'tools/analyze.py')
    for name in ('prelaunch-audit.json','worker-input-audit.json'):
        copy_checked(args.source_audit/name,Path('source-audit')/name)
    if (args.source_audit/'x86-interpretation.md').exists():
        copy_checked(args.source_audit/'x86-interpretation.md',Path('independent-interpretation/x86-interpretation.md'))
    for name in ('all-hosts-interpretation.md', 'v2-source-native-audit.json'):
        if (args.source_audit/name).exists():
            copy_checked(args.source_audit/name,Path('independent-interpretation')/name)
    index['independent_interpretation']={'author_path':str(args.source_audit/'x86-interpretation.json'),
        'sha256':sha(args.source_audit/'x86-interpretation.json') if (args.source_audit/'x86-interpretation.json').exists() else None,
        'retained_finding':'independent-interpretation/x86-interpretation.md','scope':'Full parsed static instruction JSON stays in ignored owner output; selected ASM excerpts and raw worker archives independently support the retained compact finding.'}
    index['counts']={'qualified_jobs':len(index['jobs']),'qualified_rows':sum(v['rows'] for j in index['jobs'].values() for v in j['isas'].values()),
                     'qualified_isa_cases':sum(len(j['isas']) for j in index['jobs'].values()),'failed_zero_timing_attempts':len(index['failed_attempts'])}
    index['payloads']=[{'path':str(p.relative_to(output)),'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(output.rglob('*')) if p.is_file() and p.name not in ('recovery.json','retention-arguments.json','x86-interpretation.json') and 'reports' not in p.relative_to(output).parts]
    index['counts']['payload_files']=len(index['payloads']);index['counts']['payload_bytes']=sum(p['bytes'] for p in index['payloads'])
    dump(output/'recovery.json',index)
    dump(output/'retention-arguments.json',{'selection':sorted(p['path'] for p in index['payloads'])+['recovery.json'],
        'intended_report_owner':'workbench/spikes/call-boundaries/ring-dispatch/cold-report.py',
        'regenerate':['python3','workbench/spikes/call-boundaries/ring-dispatch/cold-report.py','--evidence','{evidence}'],
        'note':'Parent owns destination, archive verification/retention and report promotion. This selection script does not archive or alter main evidence.'})
    print(json.dumps(index['counts']))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--job',action='append',required=True)
    parser.add_argument('--failed',action='append',default=[])
    parser.add_argument('--workers',type=Path,default=Path('build/workers'))
    parser.add_argument('--output',type=Path,default=HERE/'selected')
    parser.add_argument('--analyzer',type=Path,default=HERE/'analyze.py' if (HERE/'analyze.py').exists() else HERE.parent/'cold-native-analysis-20261013/analyze.py')
    parser.add_argument('--source-audit',type=Path,default=HERE.parent/'source-audit' if (HERE.parent/'source-audit').exists() else HERE.parent/'cold-native-analysis-20261013')
    select(parser.parse_args())
