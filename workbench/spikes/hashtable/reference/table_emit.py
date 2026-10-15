#!/usr/bin/env python3
"""Bind solver byte/tight layouts to complete-table bucket execution.

This family admits contiguous byte fingerprints and periodic field locations.
Physical transfer windows, including any required tail byte, are charged here.
It neither scores layouts by an invented cycle model nor selects a preset.
"""
import argparse
from pathlib import Path
import layout


def emit_plan(k, v, a, n, role, placement, packing, max_entries):
    kb=min(8,k-a)
    geometry=layout.Geometry(kb,k-a-kb,v//8,'map' if v else 'set','neon',
                             max_descendants=max_entries,role=role)
    selected=layout.construct(geometry,n,placement,packing,'global',1,8)
    headers={f.name:f for f in selected.fields if f.slot==-1}
    def header(name):
        f=headers.get(name)
        return (f.start,f.bits) if f else (0,0)
    count,desc,ref=(header(name) for name in ('occupancy','descendants','overflow_ref'))
    fields=[]
    end=selected.stride
    for name,bits in [('suffix',geometry.kc),('value',v)]:
        fs=sorted((f for f in selected.fields if f.name==name),key=lambda f:f.slot)
        if not bits:
            assert not fs
            fields.append((0,0,0,1));continue
        if placement=='key_planes' and name=='suffix':
            first=[f for f in fs if f.logical_bit==0]
            offset=first[0].start
            lanes=(n+7)//8*8
            assert all(f.start==offset+f.logical_bit//8*lanes*8+f.slot*8 for f in fs)
            fields.append((offset,8,n*8,n));continue
        group={'rows':n,'planes':n,'groups8':8,'groups16':16,'key_planes':n,'key_words':n}[placement]
        offset=fs[0].start
        step=fs[1].start-offset if n>1 else 0
        stride=fs[group].start-offset if n>group else group*step
        assert all(f.bits==bits and f.start==offset+(i//group)*stride+(i%group)*step for i,f in enumerate(fs))
        phases={f.start%8 for f in fs}
        window=max((bits+p+7)//8 for p in phases)
        end=max(end,max(f.start//8+window for f in fs))
        fields.append((offset,step,stride,group))
    for offset,bits in (count,desc,ref):end=max(end,offset//8+(bits+offset%8+7)//8)
    fp=selected.fp_groups[0]['offset']
    fp_stride=4 if placement=='key_words' else 1
    assert all(g['offset']==fp+g['first_slot']*fp_stride for g in selected.fp_groups)
    suffix,value=fields
    values=[k,a,kb,v,n,end,*count,*desc,*ref,fp]
    plane_stride=(n+7)//8*8 if placement=='key_planes' else 0
    aggregate='{'+','.join(map(str,values))+',{'+','.join(map(str,suffix))+'},{'+','.join(map(str,value))+'},'+str(plane_stride)
    aggregate+=',key_search::exact_raw,4}' if placement=='key_words' else '}'
    name=f'k{k}v{v}_a{a}_n{n}_{role}_{placement}_{packing}'
    return name,aggregate,selected.identity(),end-selected.stride


def emit():
    out=['#pragma once','#include "table_bucket.h"','namespace hashtable_spike::tiered {']
    # Definitions do not instantiate execution. The complete consumer chooses a
    # small comparison set; other widths/geometries remain available to it.
    for k,v in [(8,8),(16,16),(24,0),(32,64),(40,16),(48,64),(56,64),(64,64)]:
        for a in range(1,min(k-3,19)):
            for n in (8,14,16,32):
                if n>2**(k-a):continue
                for role in ('primary','secondary'):
                    for placement,packing in [('rows','byte'),('planes','byte'),('groups8','tight')]:
                        name,data,identity,pad=emit_plan(k,v,a,n,role,placement,packing,2**32-1)
                        out.append(f'inline constexpr bucket_plan {name}{data};')
                        out.append(f'// Solver {identity}; physical transfer tail: {pad} bytes.')
    # Bounded cold-lookup panel: full values carry the next request, including
    # miss transitions, without an external trace on the timed dependency path.
    for a in (5,8,12,16,20):
        for role,aa in [('primary',a),('secondary',max(1,a-3))]:
            for placement in ('rows','key_planes'):
                name,data,identity,pad=emit_plan(40,64,aa,16,role,placement,'byte',2**32-1)
                out.append(f'inline constexpr bucket_plan {name}{data};')
                out.append(f'// Solver {identity}; physical transfer tail: {pad} bytes.')
    # One real wider-record consumer. Tight rows are a guarded correctness case,
    # not an additional timing panel; each full-word projection can cross bytes.
    for a in (5,8,18):
        for role,aa in [('primary',a),('secondary',max(1,a-3))]:
            for packing in ('byte','tight'):
                name,data,identity,pad=emit_plan(40,192,aa,16,role,'rows',packing,2**32-1)
                out.append(f'inline constexpr bucket_plan {name}{data};')
                out.append(f'// Solver {identity}; physical transfer tail: {pad} bytes.')
    name,data,identity,pad=emit_plan(40,64,8,16,'primary','key_words','byte',2**32-1)
    out.append(f'inline constexpr bucket_plan {name}{data};')
    out.append(f'// Solver {identity}; physical transfer tail: {pad} bytes.')
    # Online growth: one list -> 64 primary slots -> 128 primary slots. A
    # single secondary bucket is a real pow2 capacity, not an omitted special case.
    for n,a,role in [(16,2,'primary'),(16,3,'primary'),(16,0,'secondary'),
                     (8,3,'primary'),(8,4,'primary'),(8,0,'secondary'),(8,1,'secondary')]:
        name,data,identity,pad=emit_plan(40,64,a,n,role,'rows','byte',2**32-1)
        out.append(f'inline constexpr bucket_plan {name}{data};')
        out.append(f'// Solver {identity}; physical transfer tail: {pad} bytes.')
    name,data,identity,pad=emit_plan(64,64,0,8,'secondary','rows','byte',2**32-1)
    out.append(f'inline constexpr bucket_plan {name}{data};')
    out.append(f'// Solver {identity}; physical transfer tail: {pad} bytes.')
    out.append('} // namespace hashtable_spike::tiered')
    return '\n'.join(out)+'\n'


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();args.output.write_text(emit())
