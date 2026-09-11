#!/usr/bin/env python3
"""One existing ordinary short-region store seam, exact restored GNR callers."""
from pathlib import Path
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
BUILD = ROOT/'build/validation/seriespack/granite-rapids/avx512/Release'
FILTER = (r'^(runtime/|boundary/|head-placement/|'
          r'bulk/(series/(avx2|avx512)|predecessor|predecessor-region32|calico)/.*/u64/decode$|'
          r'resident/(series/(avx2|avx512)|predecessor|predecessor-region32|calico)/(local|striped)/k(1|2|3|4|5|6|7|12|56)/h0/u64/(point|get16)$)')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def candidate(text):
    old = '            Ops::template store<L, L, N>(reinterpret_cast<std::uint8_t*>(output + offset), value);'
    assert text.count(old) == 1
    new = '''#if defined(__AVX512BW__) && defined(__AVX512VBMI__)
            if constexpr (Ops::target == execution_target::avx512 && L * N == 64) {
                auto* destination = reinterpret_cast<__m256i*>(output + offset);
                _mm256_storeu_si256(destination, _mm512_castsi512_si256(value));
                asm volatile("" ::: "memory"); // Experimental anti-remerging barrier.
                _mm256_storeu_si256(destination + 1, _mm512_extracti64x4_epi64(value, 1));
            } else
#endif
''' + old
    return text.replace(old, new)


def main():
    assert str(ROOT) == '/opt/sixdb/source', 'Preserve original source/build paths'
    out = Path(os.environ['SIXDB_RESULTS'])
    os.environ.update(SERIESPACK_BULK_VALUES='8192', SERIESPACK_RESIDENT_BYTES='4096')
    old = ROOT/'build/recovered/seriespack-gnr-store-original'
    receipt = {'status':'running', 'scope':'finish_region full AVX512 output stores only; no production edits',
               'order':['base','candidate','candidate','base'], 'filter':FILTER,
               'commands':[], 'reconstructed_objects':[]}
    def save():
        (out/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    def run(label, argv, cwd=ROOT):
        record={'label':label,'argv':list(map(str,argv)),'cwd':str(cwd)}
        receipt['commands'].append(record);save();print(label,flush=True)
        start=time.monotonic()
        with (out/(label+'.txt')).open('w') as log:
            subprocess.run(argv,cwd=cwd,stdout=log,stderr=subprocess.STDOUT,check=True)
        record['elapsed_seconds']=time.monotonic()-start;save()
    selected=['source-manifest.json','seriespack/validation.json','checked-point-pair/receipt.json',
              *['seriespack/avx512/'+n for n in ['compile_commands.json','compiled.json',
                 'libikea_seriespack.a','libikea_seriespack_prior.a','ikea_seriespack_bench']]]
    run('recover',[sys.executable,ROOT/'workbench/tools/artifacts.py','fetch',HERE/'base-artifact.json',old,
                   *[a for n in selected for a in ['--file',n]]])
    source=json.loads((old/'source-manifest.json').read_text())
    assert all(sha(ROOT/n)==h for n,h in source.items())
    receipt['verified_original_sources']=len(source)
    validation=json.loads((old/'seriespack/validation.json').read_text())
    profile=next(p for p in validation['profiles'] if p['name']=='avx512')
    argv=next(c['argv'] for c in profile['commands'] if c['name']=='configure')
    run('configure',argv[argv.index('cmake'):])
    run('build-benchmark-dependency',['cmake','--build',BUILD,'--target','benchmark','-j1'])
    targets=['ikea_seriespack_bench','ikea_seriespack_operations_check',
             'ikea_seriespack_physical_operations_check','ikea_seriespack_range_bounds_check']
    links={}
    for target in targets:
        lines=subprocess.check_output(['ninja','-C',BUILD,'-t','commands',target],text=True).splitlines()
        argv=shlex.split(next(l for l in reversed(lines) if ' -o ' in l and ' -c ' not in l))
        if argv[:2]==[':','&&']:argv=argv[2:]
        if argv[-2:]==['&&',':']:argv=argv[:-2]
        links[target]=argv
    inputs={str(BUILD/a) for argv in links.values() for a in argv if a.endswith(('.o','.a'))}
    compile_rows=json.loads((old/'seriespack/avx512/compile_commands.json').read_text())
    compiled={r['file']:r for r in json.loads((old/'seriespack/avx512/compiled.json').read_text())}
    for path in sorted(inputs):
        if not path.endswith('.o'):continue
        row=next(r for r in compile_rows if str(Path(r['directory'])/r['output'])==path)
        expected=compiled[row['file']]
        assert sha(Path(row['file']))==expected['source_sha256']
        Path(path).parent.mkdir(parents=True,exist_ok=True)
        run('compile-caller-'+str(len(receipt['reconstructed_objects'])),shlex.split(row['command']),Path(row['directory']))
        actual=sha(Path(path))
        receipt['reconstructed_objects'].append({'path':path,'expected':expected['object_sha256'],'actual':actual})
        save();assert actual==expected['object_sha256'],path
    library=BUILD/'ikea/libikea_seriespack.a'
    prior_library=BUILD/'workbench/benchmarks/seriespack/libikea_seriespack_prior.a'
    for destination in [library,prior_library]:
        destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(old/'seriespack/avx512'/destination.name,destination)
    expected_inputs={r['path']:r['sha256'] for link in json.loads((old/'checked-point-pair/receipt.json').read_text())['link_inputs'] for r in link['common_inputs']}
    for path,digest in expected_inputs.items():
        if path in inputs:assert sha(Path(path))==digest,path
    receipt['common_inputs']={p:sha(Path(p)) for p in sorted(inputs)}
    original=ROOT/'ikea/src/seriespack/native.cpp'
    assert sha(original)=='1b93ca5a3c29c0830394970a66dde88d2a6c4c1cba98e9e47f29a4b25febe61c'
    src=out/'native.cpp';src.write_text(candidate(original.read_text()))
    receipt['source']={'base':sha(original),'candidate':sha(src)}
    row=next(r for r in compile_rows if Path(r['file'])==original)
    argv=shlex.split(row['command']);obj=out/'native.cpp.o'
    argv[argv.index('-o')+1]=str(obj);argv[argv.index('-c')+1]=str(src)
    argv+=['-iquote',str(original.parent)]
    run('compile-candidate',argv,BUILD)
    replacement=out/'libikea_seriespack.a';shutil.copy2(library,replacement)
    run('archive-candidate',['llvm-ar-21','r',replacement,obj])
    members=subprocess.check_output(['llvm-ar-21','t',library],text=True).splitlines()
    receipt['archive_members']=[]
    for member in members:
        hashes=[hashlib.sha256(subprocess.check_output(['llvm-ar-21','p',lib,member])).hexdigest() for lib in [library,replacement]]
        assert (hashes[0]!=hashes[1])==(member=='native.cpp.o')
        receipt['archive_members'].append({'name':member,'base':hashes[0],'candidate':hashes[1]})
    for variant,lib in [('base',library),('candidate',replacement)]:
        for target,link in links.items():
            argv=link.copy();binary=out/(variant+'.bin' if target=='ikea_seriespack_bench' else variant+'-'+target)
            argv[argv.index('-o')+1]=str(binary)
            argv[next(i for i,a in enumerate(argv) if BUILD/a==library)]=str(lib)
            for i,a in enumerate(argv):
                if a.startswith('--dependency-file='):argv[i]='--dependency-file='+str(out/(variant+'-'+target+'.d'))
            run('link-'+variant+'-'+target,argv,BUILD)
            if target!='ikea_seriespack_bench':
                run('check-'+variant+'-'+target,['taskset','-c',os.environ['SIXDB_CPU'],binary,
                     *(['--all-available'] if target.endswith(('physical_operations_check','range_bounds_check')) else [])])
    assert sha(out/'base.bin')==sha(old/'seriespack/avx512/ikea_seriespack_bench'),'baseline relink mismatch'
    receipt['binaries']={v:sha(out/(v+'.bin')) for v in ['base','candidate']}
    save()
    inventories={}
    for i,variant in enumerate(receipt['order']):
        name=str(i)+'-'+variant;sample=out/(name+'.json')
        run(name,['taskset','-c',os.environ['SIXDB_CPU'],out/(variant+'.bin'),
             '--benchmark_filter='+FILTER,'--benchmark_min_time=0.03s','--benchmark_repetitions=3',
             '--benchmark_enable_random_interleaving=false','--benchmark_out_format=json','--benchmark_out='+str(sample)])
        rows=json.loads(sample.read_text())['benchmarks']
        assert not any(r.get('error_occurred') for r in rows)
        actual={r['run_name'] for r in rows if r.get('run_type')=='iteration'}
        assert sum(r.get('run_type')=='iteration' for r in rows)==len(actual)*3
        inventories[name]=sorted(actual)
        if i:assert inventories[name]==inventories['0-base']
        receipt['cases']=len(actual);save()
    receipt['status']='passed';save()


if __name__=='__main__':main()
