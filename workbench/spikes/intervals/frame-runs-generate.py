#!/usr/bin/env python3
"""Bind a projection-only counterfactual of the actual Frame point readers.

Current Frame uses one common getter template with curated source aliases. Bind
those actual programs directly. Older captured epochs use a projection-only
class transform with an exact inverse check. Both borrow the same owning image;
no getter is hand reimplemented and no Frame file is edited.
"""
import argparse, hashlib, json, re
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('frame',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
a.output.mkdir(parents=True,exist_ok=True);receipt={}
for file,cls,stop in [('point_read.h','point_reader','\n// Restore a dictionary edition'),
                      ('compact_read.h','compact_reader','\n} // namespace frame_points')]:
    source=a.frame/'point'/file;text=source.read_text()
    if 'struct '+cls+' {' not in text:
        program=cls+'_program';alias='unpredictable_'+cls
        for name,source_name in [(cls,'fe::run_ends'),(alias,'ikea::intervals::run_end_source')]:
            pattern=r'using\s+'+name+r'\s*=\s*'+program+r'\s*<\s*'+re.escape(source_name)+r'\s*>\s*;'
            assert re.search(pattern,text),'curated Frame source binding changed: '+name
        assert text.count('ikea::intervals::upper_bound(RunSource{')==1
        dest=a.output/(cls+'_native_runs.h')
        dest.write_text('#pragma once\n#include "'+file+'"\nnamespace frame_points { using '+cls+'_native_runs = '+alias+'; }\n')
        receipt[file]={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                       'generated_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
                       'binding':'actual curated Frame programs over one common getter template',
                       'ordinary_alias':cls,'native_alias':alias}
        continue
    start=text.index('struct '+cls+' {');end=text.index(stop,start)
    original=text[start:end]
    assert original.count('ikea::intervals::upper_bound(fe::run_ends{')==1
    candidate=re.sub(r'\b'+cls+r'\b',cls+'_native_runs',original)
    candidate=candidate.replace('ikea::intervals::upper_bound(fe::run_ends{',
                                'ikea::intervals::upper_bound(ikea::intervals::run_end_source{')
    restored=re.sub(r'\b'+cls+r'_native_runs\b',cls,candidate).replace(
        'ikea::intervals::upper_bound(ikea::intervals::run_end_source{',
        'ikea::intervals::upper_bound(fe::run_ends{')
    assert restored==original,'counterfactual changed more than the named seam'
    dest=a.output/(cls+'_native_runs.h')
    dest.write_text('#pragma once\n#include "'+file+'"\nnamespace frame_points {\n'+candidate+'\n}\n')
    receipt[file]={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
                   'class_sha256':hashlib.sha256(original.encode()).hexdigest(),
                   'generated_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),
                   'inverse_transform_exact':True}
(a.output/'frame-runs-transform.json').write_text(json.dumps(receipt,indent=2)+'\n')
