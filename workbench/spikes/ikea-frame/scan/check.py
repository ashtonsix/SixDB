#!/usr/bin/env python3
"""Independent answers across the private boundary; no timing claim."""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
from bridge import Library

p=argparse.ArgumentParser();p.add_argument('library',type=Path);p.add_argument('--output',type=Path)
a=p.parse_args();lib=Library(a.library.resolve());random=np.random.default_rng(1729)
checks=0;inventories=[]
for blocks in [1,17,65]:
    n=blocks*256
    cases=[np.full(n,(1<<64)-1,dtype=np.uint64),np.arange(n,dtype=np.uint64),
        np.repeat(np.arange((n+18)//19,dtype=np.uint64),19)[:n]]
    for bits in [8,16,32,64]:
        cases.append(random.integers(0,np.iinfo(np.uint64).max,size=n,dtype=np.uint64)>>np.uint64(64-bits))
    for values in cases:
        with lib.image(values) as image:
            stats=image.stats();assert stats['rows']==n
            inventories.append(stats)
            for selected in [list(range(blocks)),[],list(range(0,blocks,2)),list(range(min(3,blocks)))]:
                words=np.zeros((blocks+63)//64,dtype=np.uint64)
                for b in selected:words[b//64]|=np.uint64(1)<<np.uint64(b%64)
                image.mask(words)
                wanted=[int(v) for b in selected for v in values[b*256:(b+1)*256]]
                for implementation in [0,1,2]:
                    assert image.query(implementation,0)==sum(wanted);checks+=1
                    for threshold in [0,1,int(values[n//2]),(1<<64)-1]:
                        assert image.query(implementation,1,threshold)==sum(v>=threshold for v in wanted)
                        checks+=1
            for implementation in [0,1,2]:
                ranges=image.ranges(implementation)
                assert ranges and all(address and size>0 for address,size,_ in ranges)
            bad=np.full((blocks+63)//64,np.uint64((1<<64)-1))
            if blocks%64:
                try:image.mask(bad)
                except ValueError:checks+=1
                else:raise AssertionError('out-of-domain selection admitted')
result={'checks':checks,'images':len(inventories),'inventories':inventories,
        'library_sha256':hashlib.sha256(a.library.read_bytes()).hexdigest(),
        'cache_maintenance_available':bool(lib.flush_supported()),'timing_claim':False}
if a.output:a.output.write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='inventories'}))
