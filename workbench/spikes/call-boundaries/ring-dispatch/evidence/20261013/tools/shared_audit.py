#!/usr/bin/env python3
"""Validate final local/shared Cohort factorial receipts without running code."""
import argparse
import csv
import difflib
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('retained_comparisons',HERE/'aggregate.py')
shared = importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
ARMS = shared.ARMS
SIZES, CHAINS = shared.SIZES, shared.CHAINS
FINAL_COMMIT = '7fe87873b1adf6d18301510131c7803b8927b5ab'
FINAL_DIGEST = 'cb9412ab78c0ce163e5199405ed446d7becd04aa3df347cf0a7a788c16b700b0'
FINAL_ARCHIVE = '11e426dc085ee0d945387393fe8d672a9638d3b48923c52535e366ad99acb7ad'
FINAL_JOBS = ['20261013T032148Z-e3b5156c','20261013T032148Z-bb9261c2','20261013T032148Z-3f771bfa']
PATTERN = re.compile(r'^(neon|avx2|avx512)-(shared_)?adapter_(cold_)?grain([14])-(\d+)-(\d+)\.csv$')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text())


def function(text,name):
    match = re.search(r'\b'+name+r'\([^;]*?\)\s*(?:const\s*)?\{',text,re.S)
    assert match is not None,name
    begin, i, depth = match.start(),match.end(),1
    while depth:
        depth+=(text[i]=='{')-(text[i]=='}');i+=1
    return text[begin:i]


def source_audit(root):
    local_path, common_path = root/'controls/adapter/ring.cpp',root/'controls/shared/ring.cpp'
    local,common = local_path.read_text(),common_path.read_text()
    provenance = load(root/'controls/source-provenance.json')['shared_context_control']
    assert sha(local_path)==provenance['local_source_sha256']
    assert sha(common_path)==provenance['shared_source_sha256']
    patch = ''.join(difflib.unified_diff(local.splitlines(keepends=True),common.splitlines(keepends=True),fromfile='local-context/ring.cpp',tofile='shared-prefix/ring.cpp'))
    assert patch==(root/'controls/shared-driver.patch').read_text()
    section_hashes={}
    for name in ['metadata_body','payload_body','manual','complete','hint_window','qualify_positive_windows']:
        a,b=function(local,name),function(common,name)
        assert a==b,(name,'local/shared semantic difference')
        section_hashes[name]=hashlib.sha256(a.encode()).hexdigest()
    windows=function(local,'hint_window')
    assert '#if LOOM_HINT_WINDOW_COLD_POSITIVE\n    assert(bytes>0);\n#else\n    require(bytes>0);\n#endif' in windows
    assert 'emit(begin);' in windows and 'last=first+bytes-1' in windows
    assert 'next<=last' in windows
    map_text=(root/'workbench/spikes/hashtable/map.h').read_text()
    primary,candidate=function(map_text,'primary_window'),function(map_text,'candidate_windows')
    assert 'emit(p.bucket,p_.header+16);' in primary
    assert 'emit(p_.suffix_at(p.bucket,i),p_.suffix_bytes+8);' in candidate
    assert 'unsigned slots, suffix_bits, suffix_bytes, fingerprint_bits, lanes, header, stride, entry_start;' in map_text
    proof=function(local,'qualify_positive_windows')
    assert 'map.primary_window(probe' in proof and 'map.candidate_windows(probe,word{1}' in proof
    assert proof.count('require(bytes>0)')==2
    assert 'require(primary_count==1 && candidate_count==1)' in proof
    assert 'qualify_positive_windows(packed,keys.front());' in local and 'qualify_positive_windows(packed,keys.front());' in common
    # Exact source hash + reconstructed diff bound the prefix factor; equal
    # helper hashes independently check that no semantic kernel was substituted.
    assert 'using Scheduled=loom::Ring<Cohort<Map>*,const Context<Map>*>;' in local
    assert 'using Scheduled=typename loom::Ring<Cohort<Map>*>::template WithShared<const Context<Map>*>;' in common
    assert 'Context<Packed> context{{worker.hints,*demand},&packed};' in common
    assert 'Context<Packed> context{{ring.hint_policy(),*demand},&packed};' in local
    storage=(root/'loom/src/ring.cpp').read_text()
    assert '.alignment = std::max(std::size_t(64), alignment)' in storage
    return {'qualification':'exact patch reconstruction plus independently identical kernels/manual/window/proof bodies',
        'local_source_sha256':sha(local_path),'shared_source_sha256':sha(common_path),
        'shared_patch_sha256':sha(root/'controls/shared-driver.patch'),'helper_body_sha256':section_hashes,
        'hint_window_factor':{'mode0':'per_window_require retained','mode1':'NDEBUG assert after cold positive-size proof; applies to manual and Ring',
                             'proof':'frozen unsigned immutable primary geometry gives header+16 and suffix_bytes+8; addresses are unchanged',
                             'original_map_sha256':sha(root/'workbench/spikes/hashtable/map.h')},
        'prefix_factor':'per-Slot restored Context pointer becomes configure-time Shared prefix; kernels/window/policy are identical',
        'predicted_saved_argument_bytes':{'local':16,'shared':8,'basis':'two native pointer locals versus one; prefix not saved per Slot'},
        'slot_geometry_analysis':{'local_slot_bytes':64,'shared_slot_bytes':56,'bank_alignment_bytes':64,
            'shared_slot_offsets_mod64':[0,56,48,40,32,24,16,8],
            'shared_24_byte_function_next_local_prefix_crosses_cacheline_at_offsets':[48,56],
            'shared_16_byte_function_next_pair_crosses_cacheline_at_offsets':[56],
            'scope':'static 64-byte-line model from typed Slot layout; no measured penalty or performance win inferred'}}


def job_audit(base,source,checksums,expected_identity):
    root=base/'results'
    j=load(base/'job.json')
    expected_commit,expected_digest,expected_archive=expected_identity
    assert j['source_commit']==expected_commit and j['source']['digest']==expected_digest and j['source']['sha256']==expected_archive
    assert j['script']=='run-shared.sh'
    manifest=load(base/'source-manifest.json')
    source_provenance=load(source/'controls/source-provenance.json')
    relevant=['controls/adapter/ring.cpp','controls/shared/ring.cpp','controls/shared-driver.patch','controls/adapter-driver.patch',
              'controls/source-provenance.json','controls/new/loom/ring.h','loom/include/loom/ring.h','loom/include/loom/cohort.h',
              'loom/src/ring.cpp','workbench/spikes/hashtable/ring.cpp','workbench/spikes/hashtable/map.h','workbench/spikes/hashtable/CMakeLists.txt','run-shared.sh']
    assert all(sha(source/name)==manifest[name] for name in relevant)
    assert manifest['controls/new/loom/ring.h']==manifest['loom/include/loom/ring.h']==source_provenance['copied_optimized_ring_sha256']
    assert manifest['workbench/spikes/hashtable/map.h']==source_provenance['selected_manifest_verified']['workbench/spikes/hashtable/map.h']
    assert manifest['workbench/spikes/hashtable/ring.cpp']==source_provenance['selected_manifest_verified']['workbench/spikes/hashtable/ring.cpp']
    result={'job_id':base.name,'source_commit':j['source_commit'],'source':j['source'],
            'machine':j['config']['machine'],'instance_type':j['config']['instance_type'],
            'optimized_ring_sha256':manifest['loom/include/loom/ring.h'],'source_manifest_sha256':sha(base/'source-manifest.json'),
            'source_file_sha256':{name:manifest[name] for name in relevant},'cells':[],'measurement_files':[],'files':{},'hardware_profiles':{}}
    if not (root/'worker-result.json').exists():
        result.update(qualification='pending collection; no timings admitted',worker_status={'state':'pending'})
        return result
    status=load(root/'worker-result.json');result['worker_status']=status
    if status['state']!='complete' or status['script_returncode']!=0:
        result['qualification']='terminal failed job; no timing admitted';return result
    runtime=load(root/'dispatch-sources.json')
    assert all(manifest.get(name)==digest for name,digest in runtime.items())
    assert all(sha(source/name)==digest for name,digest in runtime.items())
    result['runtime_source_sha256']=runtime
    assert sha(root/'source-manifest.json')==sha(base/'source-manifest.json')
    order, repaired=shared.executable_order(root/'executable-order.csv')
    assert repaired==0
    result['executable_order']=order
    for path in sorted(root.glob('*.csv')):
        match=PATTERN.fullmatch(path.name)
        if not match:continue
        isa,common,cold,grain,n,c=match.groups()
        transfer='shared' if common else 'local';window=1 if cold else 0
        grain,n,c=map(int,(grain,n,c))
        effective,groups=min(c,grain),(c+grain-1)//grain
        ctx_path=path.with_suffix('.context');ctx=shared.context(ctx_path)
        hardware_path=root/f'hardware-{isa}.json';hardware=load(hardware_path)
        assert not hardware['calibration_enabled'] and hardware['reported']['selected_cpu']==0
        assert ctx['hardware_context']==hardware['behavior_context'] and ctx['calibration_enabled']=='false'
        expected_ctx=dict(policy_revision='0',pressure='0',line_bytes='64',demand_code='0',selected_hint='1',
                          configured_grain=str(grain),effective_grain=str(effective),groups=str(groups),prepared_map_fact='true',
                          logical_visits_origin='derived_equal_lengths',ragged_check='passed',
                          context_transfer='shared_prefix' if common else 'local_restored_pointer',
                          hint_window_contract='cold_immutable_geometry_positive' if cold else 'per_window_require',
                          positive_window_proof='header_plus_16_and_suffix_bytes_plus_8' if cold else 'not_selected')
        for key,value in expected_ctx.items():assert ctx[key]==value,(path,key,ctx[key],value)
        assert int(ctx['page_bytes'])==hardware['reported']['pages']['base_bytes']
        rows=list(csv.DictReader(path.open()))
        assert len(rows)==56 and {(int(r['rep']),r['execution']) for r in rows}=={(r,a) for r in range(7) for a in ARMS}
        for rep in range(7):
            actual=[r['execution'] for r in rows if int(r['rep'])==rep]
            forward=['abseil','abseil-prefetch','packed','direct','manual','manual-hints','ring','ring-hints']
            assert actual==(forward[::-1] if rep%2 else forward)
        fixed={key:int(rows[0][key]) for key in shared.FIXED}
        assert fixed['checksum']==checksums[n] and fixed['key_input_bytes']==8*n
        assert fixed['rounds']==max(1,262144//n) and fixed['hint_code']==1
        assert sum(fixed[k] for k in ['primary_keys','secondary_keys','overflow_keys'])==n
        assert fixed['packed_requested_bytes']==fixed['direct_requested_bytes']
        for row in rows:
            assert int(row['n'])==n and int(row['chains'])==c
            assert {key:int(row[key]) for key in shared.FIXED}==fixed
            assert math.isfinite(float(row['ns_per_lookup'])) and float(row['ns_per_lookup'])>0
            useful=2*n*fixed['rounds'] if row['execution'] in ('ring','ring-hints') else 0
            assert int(row['ring_visits'])==useful//effective
            assert int(row['useful_item_stage_visits'])==useful
            assert row['useful_item_stage_visits_origin']=='derived_equal_lengths'
            assert [int(row[k]) for k in ['configured_grain','effective_grain','groups']]==[grain,effective,groups]
            assert int(row['ring_storage_bytes'])==int(rows[0]['ring_storage_bytes'])
        item={'measurement':path.name,'isa':isa,'transfer':transfer,'window_mode':window,'configured_grain':grain,
              'effective_grain':effective,'groups':groups,'n':n,'chains':c,'rows':len(rows),
              'csv_sha256':sha(path),'context_sha256':sha(ctx_path),'hardware_sha256':sha(hardware_path),
              'hardware_context_sha256':hashlib.sha256(ctx['hardware_context'].encode()).hexdigest(),
              'context':{k:v for k,v in ctx.items() if k!='hardware_context'}}
        result['measurement_files'].append(item)
        for arm in sorted(ARMS):
            selected=[r for r in rows if r['execution']==arm];values=[float(r['ns_per_lookup']) for r in selected]
            result['cells'].append({'isa':isa,'transfer':transfer,'window_mode':window,'configured_grain':grain,'effective_grain':effective,
                'groups':groups,'n':n,'chains':c,'execution':arm,'ns_per_lookup_median':statistics.median(values),
                'ns_per_lookup_min':min(values),'ns_per_lookup_max':max(values),'samples':values,
                'ring_visits':int(selected[0]['ring_visits']),'useful_item_stage_visits':int(selected[0]['useful_item_stage_visits']),
                'ring_storage_bytes':int(selected[0]['ring_storage_bytes']),**fixed})
        if isa not in result['hardware_profiles']:
            result['hardware_profiles'][isa]={'hardware':hardware,'sha256':sha(hardware_path),'file':hardware_path.name}
    isas=sorted(result['hardware_profiles']);assert isas
    actual={(m['isa'],m['transfer'],m['window_mode'],m['configured_grain'],m['n'],m['chains']) for m in result['measurement_files']}
    expected={(isa,t,w,g,n,c) for isa in isas for t in ['local','shared'] for w in [0,1] for g in [1,4] for n in SIZES for c in CHAINS}
    assert actual==expected,(base.name,'missing/extra full campaign cells',len(expected-actual),len(actual-expected))
    expected_order={(isa,w,n,c,('shared_' if t=='shared' else '')+'adapter_'+('cold_' if w else '')+'grain'+str(g)) for isa,t,w,g,n,c in expected}
    assert len(order)==len(expected_order)
    assert {(o['isa'],int(o['window_mode']),int(o['n']),int(o['chains']),o['policy']) for o in order}==expected_order
    assert all(int(o['reps'])==7 and re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+Z',o['start_utc']) and re.fullmatch(r'\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+Z',o['end_utc']) for o in order)
    # Same-work factors preserve footprints/checksum/visits. Storage changes
    # only with grain and local/shared signature, never with the window factor.
    for isa in isas:
        for n in SIZES:
            for c in CHAINS:
                selected=[x for x in result['cells'] if (x['isa'],x['n'],x['chains'])==(isa,n,c)]
                assert len({tuple(x[k] for k in shared.FIXED) for x in selected})==1
                for g in [1,4]:
                    group=[x for x in selected if x['configured_grain']==g]
                    for t in ['local','shared']:
                        assert len({x['ring_storage_bytes'] for x in group if x['transfer']==t})==1
                    storage={t:next(x['ring_storage_bytes'] for x in group if x['transfer']==t) for t in ['local','shared']}
                    assert storage['local']-storage['shared']==8*((c+g-1)//g)
        for kind in ['loom-validation','asan-qualification']:
            path=root/f'{kind}-{isa}.txt';text=path.read_text()
            expected_tests=16 if kind=='loom-validation' else 6
            assert f'100% tests passed, 0 tests failed out of {expected_tests}' in text
            for test in ['loom_ring_check','loom_ring_rotation_check','loom_ring_shared_check','loom_cohort_check','loom_hint_check','loom_example_hints']:
                assert test in text
            result['files'][path.name]=sha(path)
        compile_path=root/f'compile-{isa}.json';compiled=load(compile_path)
        for t in ['local','shared']:
            for w in [0,1]:
                for g in [1,4]:
                    policy=('shared_' if t=='shared' else '')+'adapter_'+('cold_' if w else '')+'grain'+str(g)
                    matches=[x for x in compiled if 'CMakeFiles/hashtable_ring_'+policy+'.dir/' in x['output']]
                    assert len(matches)==1
                    command=matches[0]['command']
                    assert '/controls/'+('shared' if t=='shared' else 'adapter')+'/ring.cpp' in matches[0]['file']
                    assert f'-DLOOM_COHORT_GRAIN={g}' in command and f'-DLOOM_HINT_WINDOW_COLD_POSITIVE={w}' in command and '-DNDEBUG' in command
                    assert '-O3' in command and '-fno-vectorize' in command and '-fno-slp-vectorize' in command and '-flto' not in command
                    assert command.index('-I/opt/sixdb/source/controls/new')<command.index('-I/opt/sixdb/source/loom/include')
        for name in [compile_path.name,f'compile-asan-{isa}.json',f'binary-sha256-{isa}.txt',f'hardware-{isa}.json']:
            result['files'][name]=sha(root/name)
    for name in ['worker-result.json','source-provenance.json','source-manifest.json','dispatch-sources.json','shared-driver.patch','adapter-driver.patch','executable-order.csv','compiler.txt']:
        result['files'][name]=sha(root/name)
    result.update(qualification='full independent checksum, geometry, source, factorial accounting and native/sanitizer receipts verified',
                  measurement_rows=sum(m['rows'] for m in result['measurement_files']),isas=isas)
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,default=HERE.parent/'native-adapter-source')
    parser.add_argument('--workers',type=Path,default=Path('build/workers'))
    parser.add_argument('--jobs',nargs='+',default=FINAL_JOBS)
    parser.add_argument('--expected-commit',default=FINAL_COMMIT)
    parser.add_argument('--expected-digest',default=FINAL_DIGEST)
    parser.add_argument('--expected-archive',default=FINAL_ARCHIVE)
    parser.add_argument('--source-only',action='store_true')
    parser.add_argument('--output',type=Path,default=HERE/'shared-measurements.json')
    args=parser.parse_args()
    source=source_audit(args.source)
    result={'format':1,'source_audit':source,'jobs':[],'limits':[
        'Setup/enqueue/drain charged; hardware discovery, coherent context binding and ragged validation outside timing.',
        'Shared and window factors remain separate; no performance inference from saved bytes alone.',
        'Useful item-stage visits are equal-length derivations; native frame visits measured separately.',
        'No PMU counters collected in these uninstrumented campaigns; no cache-tier or stall cause inferred.',
        'No post-launch source edits are silently accepted: commit/digest/archive and source-manifest hashes are fixed.']}
    if not args.source_only:
        checksums=shared.expected_checksums()
        result['independent_key_set_checksums']=checksums
        identity=(args.expected_commit,args.expected_digest,args.expected_archive)
        result['expected_source_identity']={'commit':identity[0],'digest':identity[1],'source_archive_sha256':identity[2]}
        result['jobs']=[job_audit(args.workers/j,args.source,checksums,identity) for j in args.jobs]
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    cells=[{'job_id':j['job_id'],**{k:v for k,v in c.items() if k!='samples'},'samples_json':json.dumps(c['samples'])} for j in result['jobs'] for c in j['cells']]
    if cells:
        with args.output.with_suffix('.csv').open('w') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(cells[0]));writer.writeheader();writer.writerows(cells)
    print(args.output,'source-qualified','jobs',[(j['job_id'],j['worker_status']['state'],j.get('measurement_rows',0)) for j in result['jobs']])


if __name__=='__main__':
    main()
