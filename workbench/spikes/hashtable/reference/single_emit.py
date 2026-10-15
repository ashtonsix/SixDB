#!/usr/bin/env python3
"""Bind affine byte-field solver layouts to the first single-bucket consumer.

The solver owns every physical offset. This adapter admits an execution family;
it does not rank plans from storage metrics or interpret fragments per lookup.
"""
import argparse
from pathlib import Path
import layout


def emit():
    out = ['#pragma once', '// Generated from layout.py; no hand-authored physical offsets.',
           '#include "single_bucket.h"', 'namespace hashtable_spike::single {']
    for k, v in ((8, 8), (16, 8), (24, 0), (32, 8), (40, 2), (40, 8), (48, 8), (56, 8), (64, 8)):
        for n in (16, 32):
            for placement in ('rows', 'planes'):
                g = layout.Geometry(8, k-8, v, 'map' if v else 'set', 'neon', role='single')
                plan = layout.construct(g, n, placement, 'byte', 'global', 1, 8)
                fields = {f.name: f for f in plan.fields if f.slot == -1}
                assert set(fields) == {'occupancy'}
                count = fields['occupancy']
                assert count.start % 8 == 0
                assert len(plan.fp_groups) == n//8
                fp = plan.fp_groups[0]['offset']
                assert all(p['offset'] == fp+p['first_slot'] for p in plan.fp_groups)
                def affine(name, bits):
                    fs = sorted((f for f in plan.fields if f.name == name), key=lambda f: f.slot)
                    if not bits:
                        assert not fs
                        return '{0,0}'
                    assert len(fs) == n and all(f.bits == bits and f.start % 8 == 0 for f in fs)
                    first, step = fs[0].start//8, (fs[1].start-fs[0].start)//8
                    assert all(f.start == 8*(first+i*step) for i,f in enumerate(fs))
                    return '{'+f'{first},{step}'+'}'
                suffix, value = affine('suffix', k-8), affine('value', v*8)
                name = f'k{k}v{v*8}_{placement}{n}'
                out.append(f'inline constexpr plan {name}'+'{'+f'{k},{v*8},{n},{plan.stride},{count.start//8},{count.bits},{fp},{suffix},{value}'+'};')
                out.append(f'// Solver identity: {plan.identity()}')
    out.append('} // namespace hashtable_spike::single')
    return '\n'.join(out)+'\n'


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args(); a.output.write_text(emit())
