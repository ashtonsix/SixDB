#!/usr/bin/env python3
"""Independently validate frozen invoke/direct header-factor worker receipts."""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import shlex
import statistics

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('shared_audit',HERE/'shared_audit.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
base=shared.shared
JOBS=['20261013T040929Z-4ddeb41f','20261013T040929Z-46a68be2','20261013T040929Z-5a6e2967']
COMMIT='2a32da161d3a7eacbafad869f7362927d358a0c6'
DIGEST='34ebaf4d3d3a8957864828d3bde7c74043cf14ab90f03081423d023e756cec95'
ARCHIVE='45d3f759fb6b1f43a47d155a2cfffb96b2090f8c2fba93c87f9c67a778c09c0b'
PATTERN=re.compile(r'^(neon|avx512)-cohort_(invoke|direct)_grain([14])-(128|4194304)-(1|32)\.csv$')
SIZES,CHAINS=(128,4194304),(1,32)


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def load(path):return json.loads(path.read_text())


def source_audit(source):
    provenance=load(source/'controls/cohort-direct-provenance.json')
    assert provenance['base_shared_fixture_commit']==shared.FINAL_COMMIT
    for name,digest in provenance['sha256'].items():assert sha(source/name)==digest
    assert sha(source/'controls/cohort-direct-local-preflight.json')==provenance['local_preflight_sha256']
    old=(source/'controls/invoke/loom/cohort.h').read_text()
    new=(source/'controls/new/loom/cohort.h').read_text()
    # Invert exactly the authorized helper + stable pointer snapshot changes;
    # API, binding, masks, iteration order and completion update must match.
    begin=new.index('  // A std::invoke wrapper can remain outlined')
    end=new.index('\npublic:',begin)
    helper=new[begin:end]
    assert 'if constexpr (std::is_member_pointer_v<std::remove_cvref_t<Function>>)' in helper
    assert 'return std::invoke(function, state);' in helper and 'return function(state);' in helper
    assert 'static decltype(auto) invoke(Function& function, State& state) noexcept' in helper
    inverse=new[:begin]+new[end+1:]
    inverse=inverse.replace('    auto* const states = states_;\n','')
    inverse=inverse.replace('invoke(function, states[i])','std::invoke(function, states_[i])')
    assert inverse==old,'Header has changes outside direct-call/member fallback and states-pointer snapshot'
    return {'qualification':'Exact header delta inverted to original, unchanged ABI/binding/mask/order/completion semantics verified',
            'header_sha256':{'invoke':sha(source/'controls/invoke/loom/cohort.h'),'direct':sha(source/'controls/new/loom/cohort.h')},
            'root_header_sha256':sha(source/'loom/include/loom/cohort.h'),
            'driver_sha256':sha(source/'controls/shared/ring.cpp'),
            'ring_sha256':sha(source/'controls/new/loom/ring.h'),
            'shared_window_source_audit':shared.source_audit(source),
            'semantics':'Function stays lvalue under existing Function& noexcept/result constraints; member pointers retain std::invoke; states snapshot follows existing no Cohort mutation/reentry contract.'}


def audit_job(jobdir,source,checksums,source_fact):
    j=load(jobdir/'job.json');root=jobdir/'results';manifest=load(jobdir/'source-manifest.json')
    assert j['source_commit']==COMMIT and j['source']['digest']==DIGEST and j['source']['sha256']==ARCHIVE
    assert j['script']=='run-cohort-direct.sh'
    provenance=load(source/'controls/cohort-direct-provenance.json')
    assert all(manifest[name]==digest for name,digest in provenance['sha256'].items())
    result={'job_id':jobdir.name,'source':j['source'],'source_commit':j['source_commit'],'machine':j['config']['machine'],
            'instance_type':j['config']['instance_type'],'files':{},'cells':[],'measurement_files':[],'hardware_profiles':{},
            'ring_sha256':source_fact['ring_sha256'],'cohort_header_sha256':source_fact['header_sha256']}
    if not (root/'worker-result.json').exists():
        result.update(worker_status={'state':'pending'},qualification='pending; no timing admitted');return result
    status=load(root/'worker-result.json');result['worker_status']=status
    if status['state']!='complete' or status['script_returncode']!=0:
        result['qualification']='failed job; no timing admitted';return result
    runtime=load(root/'dispatch-sources.json')
    assert all(manifest[name]==digest==sha(source/name) for name,digest in runtime.items())
    assert sha(root/'source-manifest.json')==sha(jobdir/'source-manifest.json')
    result['source_files']=runtime
    order,repaired=base.executable_order(root/'executable-order.csv');assert repaired==0
    result['executable_order']=order
    for path in sorted(root.glob('*.csv')):
        m=PATTERN.fullmatch(path.name)
        if not m:continue
        isa,call,grain,n,c=m.groups();grain,n,c=map(int,(grain,n,c));effective=min(c,grain);groups=(c+grain-1)//grain
        hardware_path=root/f'hardware-{isa}.json';hardware=load(hardware_path);ctx_path=path.with_suffix('.context');ctx=base.context(ctx_path)
        assert not hardware['calibration_enabled'] and hardware['reported']['selected_cpu']==0
        assert ctx['hardware_context']==hardware['behavior_context']
        expected=dict(calibration_enabled='false',policy_revision='0',pressure='0',line_bytes='64',demand_code='0',selected_hint='1',
            configured_grain=str(grain),effective_grain=str(effective),groups=str(groups),prepared_map_fact='true',
            logical_visits_origin='derived_equal_lengths',ragged_check='passed',context_transfer='shared_prefix',
            hint_window_contract='cold_immutable_geometry_positive',positive_window_proof='header_plus_16_and_suffix_bytes_plus_8')
        assert all(ctx[k]==v for k,v in expected.items())
        assert int(ctx['page_bytes'])==hardware['reported']['pages']['base_bytes']
        rows=list(csv.DictReader(path.open()));assert len(rows)==56
        assert {(int(r['rep']),r['execution']) for r in rows}=={(rep,arm) for rep in range(7) for arm in base.ARMS}
        forward=['abseil','abseil-prefetch','packed','direct','manual','manual-hints','ring','ring-hints']
        for rep in range(7):assert [r['execution'] for r in rows if int(r['rep'])==rep]==(forward[::-1] if rep%2 else forward)
        fixed={k:int(rows[0][k]) for k in base.FIXED}
        assert fixed['checksum']==checksums[n] and fixed['key_input_bytes']==8*n
        assert fixed['rounds']==max(1,262144//n) and fixed['hint_code']==int(ctx['selected_hint'])
        assert fixed['packed_requested_bytes']==fixed['direct_requested_bytes']
        assert sum(fixed[k] for k in ['primary_keys','secondary_keys','overflow_keys'])==n
        for row in rows:
            assert int(row['n'])==n and int(row['chains'])==c
            assert {k:int(row[k]) for k in base.FIXED}==fixed
            assert math.isfinite(float(row['ns_per_lookup'])) and float(row['ns_per_lookup'])>0
            assert [int(row[k]) for k in ['configured_grain','effective_grain','groups']]==[grain,effective,groups]
            useful=2*n*fixed['rounds'] if row['execution'] in ('ring','ring-hints') else 0
            assert int(row['ring_visits'])==useful//effective and int(row['useful_item_stage_visits'])==useful
            assert row['useful_item_stage_visits_origin']=='derived_equal_lengths'
            assert int(row['ring_storage_bytes'])==int(rows[0]['ring_storage_bytes'])
        result['measurement_files'].append({'measurement':path.name,'isa':isa,'call_factor':call,'configured_grain':grain,
            'effective_grain':effective,'groups':groups,'n':n,'chains':c,'rows':56,'csv_sha256':sha(path),'context_sha256':sha(ctx_path),
            'hardware_sha256':sha(hardware_path),'context':{k:v for k,v in ctx.items() if k!='hardware_context'}})
        for arm in sorted(base.ARMS):
            selected=[r for r in rows if r['execution']==arm];samples=[float(r['ns_per_lookup']) for r in selected]
            result['cells'].append({'isa':isa,'call_factor':call,'configured_grain':grain,'effective_grain':effective,'groups':groups,
                'n':n,'chains':c,'execution':arm,'samples':samples,'ns_per_lookup_median':statistics.median(samples),
                'ns_per_lookup_min':min(samples),'ns_per_lookup_max':max(samples),'ring_storage_bytes':int(selected[0]['ring_storage_bytes']),
                'ring_visits':int(selected[0]['ring_visits']),'useful_item_stage_visits':int(selected[0]['useful_item_stage_visits']),**fixed})
        result['hardware_profiles'].setdefault(isa,{'file':hardware_path.name,'sha256':sha(hardware_path),'hardware':hardware})
    isas=sorted(result['hardware_profiles']);assert isas
    expected={(isa,call,g,n,c) for isa in isas for call in ['invoke','direct'] for g in [1,4] for n in SIZES for c in CHAINS}
    actual={(m['isa'],m['call_factor'],m['configured_grain'],m['n'],m['chains']) for m in result['measurement_files']}
    assert expected==actual
    assert len(order)==len(expected)
    assert {(o['isa'],o['policy'],int(o['n']),int(o['chains'])) for o in order}=={(isa,f'cohort_{call}_grain{g}',n,c) for isa,call,g,n,c in expected}
    assert all(int(o['window_mode'])==1 and int(o['reps'])==7 for o in order)
    for isa in isas:
        for n in SIZES:
            for c in CHAINS:
                cells=[x for x in result['cells'] if (x['isa'],x['n'],x['chains'])==(isa,n,c)]
                assert len({tuple(x[k] for k in base.FIXED) for x in cells})==1
                for g in [1,4]:assert len({x['ring_storage_bytes'] for x in cells if x['configured_grain']==g})==1
        for kind,count in [('loom-validation',16),('asan-qualification',6)]:
            path=root/f'{kind}-{isa}.txt';text=path.read_text()
            assert f'100% tests passed, 0 tests failed out of {count}' in text
            for name in ['loom_ring_check','loom_ring_rotation_check','loom_ring_shared_check','loom_cohort_check','loom_hint_check','loom_example_hints']:assert name in text
            result['files'][path.name]=sha(path)
        compiled=load(root/f'compile-{isa}.json');codegen=load(root/f'cohort-codegen-{isa}.json')
        assert set(codegen)=={f'cohort_{call}_grain{g}' for call in ['invoke','direct'] for g in [1,4]}
        for call in ['invoke','direct']:
            for g in [1,4]:
                policy=f'cohort_{call}_grain{g}';receipt=codegen[policy]
                assert receipt['driver_sha256']==source_fact['driver_sha256']
                assert receipt['cohort_header_sha256']==source_fact['header_sha256'][call]
                assert receipt['included_cohort_header']==f"controls/{'invoke' if call=='invoke' else 'new'}/loom/cohort.h"
                assert receipt['cohort_include_trace']==['/opt/sixdb/source/'+receipt['included_cohort_header']]
                assert receipt['window_mode']=='cold_positive' and receipt['call_factor']==call and receipt['configured_grain']==g
                assert '-E' in receipt['preprocess_command'] and '-H' in receipt['preprocess_command']
                matches=[x for x in compiled if 'CMakeFiles/hashtable_ring_'+policy+'.dir/' in x['output']];assert len(matches)==1
                command=shlex.split(matches[0]['command']);assert command==receipt['compile_command']
                assert matches[0]['file']=='/opt/sixdb/source/controls/shared/ring.cpp'
                assert '-DNDEBUG' in command and f'-DLOOM_COHORT_GRAIN={g}' in command and '-DLOOM_HINT_WINDOW_COLD_POSITIVE=1' in command
                assert '-O3' in command and '-fno-vectorize' in command and '-fno-slp-vectorize' in command and not any(x.startswith('-flto') for x in command)
                if call=='invoke':assert command.index('-I/opt/sixdb/source/controls/invoke')<command.index('-I/opt/sixdb/source/controls/new')
                assert command.index('-I/opt/sixdb/source/controls/new')<command.index('-I/opt/sixdb/source/loom/include')
                callbacks=receipt['callbacks'];assert len(callbacks)==8
                assert receipt['invoke_impl_callback_calls']==sum(len(x['invoke_impl_calls']) for x in callbacks)
                assert receipt['invoke_impl_callback_calls']==(0 if call=='direct' else 4*g)
        result.setdefault('reported_codegen',{})[isa]=codegen
        for name in [f'compile-{isa}.json',f'compile-asan-{isa}.json',f'cohort-codegen-{isa}.json',f'binary-sha256-{isa}.txt',f'hardware-{isa}.json']:
            result['files'][name]=sha(root/name)
    for name in ['worker-result.json','dispatch-sources.json','source-manifest.json','cohort-direct-provenance.json','cohort-direct-local-preflight.json','source-provenance.json','executable-order.csv','compiler.txt']:
        result['files'][name]=sha(root/name)
    result.update(qualification='independent checksum/source/header-precedence/native/sanitizer/full-factor accounting checks pass; actual assembly audit remains separate',
                  isas=isas,measurement_rows=sum(m['rows'] for m in result['measurement_files']))
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=HERE.parent/'native-adapter-source')
    parser.add_argument('--workers',type=Path,default=Path('build/workers'))
    parser.add_argument('--jobs',nargs='+',default=JOBS)
    parser.add_argument('--output',type=Path,default=HERE/'cohort-factor-measurements.json')
    parser.add_argument('--source-only',action='store_true')
    args=parser.parse_args();source=source_audit(args.source)
    result={'format':1,'source_audit':source,'source_identity':{'commit':COMMIT,'digest':DIGEST,'archive_sha256':ARCHIVE},'jobs':[],
            'limits':['Primary comparison is invoke/direct within this job; previous host reuse does not make earlier binaries source-equivalent.',
                      'Cold-positive windows, Shared Context, same Ring core/ABI and same driver are fixed. Header factor includes direct invocation and states-pointer snapshot together.',
                      'Native visits measured; useful item-stage visits analytically derived for equal lengths. Setup/push/drain charged.',
                      'Zero reported invoke calls will also be checked against independently parsed actual assembly; no cache-tier/PMU inference.']}
    if not args.source_only:
        sums=base.expected_checksums();result['independent_key_set_checksums']=sums
        result['jobs']=[audit_job(args.workers/j,args.source,sums,source) for j in args.jobs]
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(args.output,[(j['job_id'],j['worker_status']['state'],j.get('measurement_rows',0)) for j in result['jobs']])


if __name__=='__main__':main()
