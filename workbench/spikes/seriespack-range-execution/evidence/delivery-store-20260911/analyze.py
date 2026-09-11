#!/usr/bin/env python3
"""Audit the ordinary-store pair and its actual function bodies/relocations."""
from pathlib import Path
import argparse
from collections import Counter
import csv
import hashlib
import json
import re
import statistics
import struct
import subprocess
import sys
import tarfile
ROOT=Path(__file__).resolve().parents[5]
sys.path.insert(0,str(ROOT/'workbench/tools'))
import evidence
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'delivery-baseline'))
from analyze import section_sizes


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,value):path.write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
def csv_out(path,rows):
    with path.open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)


def functions(path):
    data=path.read_bytes();assert data[:6]==b'\x7fELF\x02\x01'
    off=struct.unpack_from('<Q',data,40)[0];stride,count,strings=struct.unpack_from('<HHH',data,58)
    headers=[struct.unpack_from('<IIQQQQIIQQ',data,off+i*stride) for i in range(count)]
    names=data[headers[strings][4]:headers[strings][4]+headers[strings][5]]
    sections=[{'name':names[h[0]:].split(b'\0',1)[0].decode(),'type':h[1],'flags':h[2],
               'bytes':data[h[4]:h[4]+h[5]] if h[1]!=8 else b'', 'link':h[6], 'info':h[7], 'stride':h[9]} for h in headers]
    si=next(i for i,s in enumerate(sections) if s['type']==2);symtab=sections[si]
    strings=sections[symtab['link']]['bytes'];symbols=[]
    for i in range(0,len(symtab['bytes']),symtab['stride']):
        name,info,other,section,value,size=struct.unpack_from('<IBBHQQ',symtab['bytes'],i)
        symbols.append({'name':strings[name:].split(b'\0',1)[0].decode(),'type':info&15,'section':section,'value':value,'size':size})
    relocs={}
    for s in sections:
        if s['type']!=4 or s['link']!=si:continue
        for i in range(0,len(s['bytes']),s['stride']):
            offset,info,addend=struct.unpack_from('<QQq',s['bytes'],i);target=symbols[info>>32]
            target_name=target['name'] or (sections[target['section']]['name'] if target['section']<len(sections) else '')
            relocs.setdefault(s['info'],[]).append((offset,info&0xffffffff,target_name,addend))
    result={}
    for symbol in symbols:
        if symbol['type']!=2 or not symbol['size'] or symbol['section']>=len(sections):continue
        sec=sections[symbol['section']];start=symbol['value'];end=start+symbol['size']
        body=sec['bytes'][start:end];assert len(body)==symbol['size']
        rs=[(o-start,t,n,a) for o,t,n,a in relocs.get(symbol['section'],[]) if start<=o<end]
        result[symbol['name']]={'size':len(body),'body_sha256':hashlib.sha256(body).hexdigest(),
                               'relocations':rs,'relocations_sha256':hashlib.sha256(json.dumps(rs).encode()).hexdigest()}
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--job',type=Path,required=True)
    p.add_argument('--original',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output;out.mkdir(parents=True,exist_ok=False)
    r=a.job/'results';receipt=json.loads((r/'receipt.json').read_text())
    assert receipt['status']=='passed'
    assert json.loads((a.job/'status.json').read_text())['script_returncode']==0
    assert receipt['verified_original_sources']==601
    original_manifest=json.loads((a.original/'source-manifest.json').read_text())
    new_manifest=json.loads((a.job/'source-manifest.json').read_text())
    assert len(original_manifest)==601 and all(new_manifest[n]==h for n,h in original_manifest.items())
    with tarfile.open(a.job/'source.tar.gz') as archive:
        actual={m.name.removeprefix('./'):hashlib.sha256(archive.extractfile(m).read()).hexdigest() for m in archive.getmembers() if m.isfile()}
    assert actual==new_manifest
    compiled=json.loads((a.original/'results/seriespack/avx512/compiled.json').read_text())
    expected_objects={str(Path(x['directory'])/x['output']):x['object_sha256'] for x in compiled}
    for row in receipt['reconstructed_objects']:
        assert row['actual']==expected_objects[row['path']]
    assert all(x['expected']==x['actual'] for x in receipt['reconstructed_objects'])
    for v,h in receipt['binaries'].items():assert sha(r/(v+'.bin'))==h
    base=a.original/'results/seriespack/avx512'
    assert sha(r/'base.bin')==sha(base/'ikea_seriespack_bench')
    assert sha(r/'native.cpp')==receipt['source']['candidate']
    members={}
    for label,lib in [('base',base/'libikea_seriespack.a'),('candidate',r/'libikea_seriespack.a')]:
        members[label]={n:hashlib.sha256(subprocess.check_output(['llvm-ar-21','p',lib,n])).hexdigest()
                        for n in subprocess.check_output(['llvm-ar-21','t',lib],text=True).splitlines()}
        obj=out/(label+'-native.o');obj.write_bytes(subprocess.check_output(['llvm-ar-21','p',lib,'native.cpp.o']))
    assert [n for n in members['base'] if members['base'][n]!=members['candidate'][n]]==['native.cpp.o']
    fs={v:functions(out/(v+'-native.o')) for v in ['base','candidate']}
    common=set(fs['base'])&set(fs['candidate']);changed=[]
    for n in sorted(common):
        x,y=fs['base'][n],fs['candidate'][n]
        if x!=y:changed.append({'symbol':n,'base_bytes':x['size'],'candidate_bytes':y['size'],
                               'instructions_equal':x['body_sha256']==y['body_sha256'],
                               'relocations_equal':x['relocations_sha256']==y['relocations_sha256']})
    csv_out(out/'changed-functions.csv',changed)
    save(out/'function-audit.json',{'common':len(common),'changed':len(changed),
        'added':sorted(set(fs['candidate'])-common),'removed':sorted(set(fs['base'])-common),
        'changed_avx2':sum('avx2_ops' in c['symbol'] for c in changed),
        'changed_avx512':sum('avx512_ops' in c['symbol'] for c in changed),
        'scope':'Exact per-function object bytes and relocation target/addend lists; padding/code placement of linked callers is separate'})
    save(out/'function-fingerprints.json',fs)
    blocks=[];inputs=[]
    for i,v in enumerate(receipt['order']):
        path=r/(str(i)+'-'+v+'.json');inputs.append((str(i)+'-'+v,path))
        rows=json.loads(path.read_text())['benchmarks'];d={}
        for row in rows:
            if row.get('run_type')!='iteration':continue
            assert not row.get('error_occurred');d.setdefault(row['run_name'],[]).append(1e9/row['items_per_second'])
        assert len(d)==receipt['cases'] and all(len(x)==3 for x in d.values());blocks.append(d)
    original_rows=json.loads((base/'samples.json').read_text())['benchmarks']
    expected_cases={x['run_name'] for x in original_rows if x.get('run_type')=='iteration' and re.search(receipt['filter'],x['run_name'])}
    assert set(blocks[0])==expected_cases
    pairs=[]
    for case in blocks[0]:
        before=blocks[0][case]+blocks[3][case];after=blocks[1][case]+blocks[2][case]
        ratio=statistics.median(after)/statistics.median(before)
        pairs.append({'case':case,'base_ns':statistics.median(before),'candidate_ns':statistics.median(after),
                      'ratio':ratio,'base_samples':json.dumps(before),'candidate_samples':json.dumps(after),
                      'edge_ratios':json.dumps([statistics.median(blocks[c][case])/statistics.median(blocks[b][case]) for b,c in [(0,1),(3,2)]]),
                      'observation':'separated faster' if max(after)<min(before) else 'separated slower' if min(after)>max(before) else 'sample ranges overlap'})
    csv_out(out/'paired.csv',pairs)
    costs=[]
    for v,lib in [('base',base/'libikea_seriespack.a'),('candidate',r/'libikea_seriespack.a')]:
        for scope,path in [('library',lib),('native',out/(v+'-native.o')),('benchmark',r/(v+'.bin'))]:
            costs.append({'variant':v,'scope':scope,**section_sizes(path,out/(v+'-'+scope+'-sections.txt'))})
    csv_out(out/'code-costs.csv',costs)
    summaries={}
    for family in sorted({p['case'].split('/')[0] for p in pairs}):
        group=[p for p in pairs if p['case'].startswith(family+'/')]
        summaries[family]={'cases':len(group),'observations':dict(Counter(p['observation'] for p in group)),
                          'median_ratio':statistics.median(p['ratio'] for p in group)}
    save(out/'summary.json',summaries)
    evidence.summarize(inputs,out/'samples',counters=['logical_values','queries_per_iteration','query_hash_lo','query_hash_hi',
        'output_mod64','output_mod4096','payload_stride','head0_stride','head1_stride','values_per_item'])
    save(out/'audit.json',{'passed':True,'cases':len(pairs),'samples':len(pairs)*12,'members':members,
        'original_sources':601,'reconstructed_caller_objects':len(receipt['reconstructed_objects']),
        'baseline_relink_identical':True,'binary_hashes':receipt['binaries'],
        'source':receipt['source'],'candidate_compile_seconds':next(c['elapsed_seconds'] for c in receipt['commands'] if c['label']=='compile-candidate')})
    print(json.dumps(summaries,indent=2))

if __name__=='__main__':main()
