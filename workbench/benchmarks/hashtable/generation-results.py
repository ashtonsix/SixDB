#!/usr/bin/env python3
"""Audit both Zen generation transfers and retain every read cell."""
import argparse,csv,json,math,statistics
from collections import defaultdict
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('zen4',type=Path);p.add_argument('zen5',type=Path);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=[];receipts=[]
for cpu,root in [('zen4',a.zen4),('zen5',a.zen5)]:
    assert (root/'complete.txt').read_text().strip()==f'{cpu} transferred-Zen5-recipe complete'
    assert '21.1.8' in (root/'compiler.txt').read_text()
    hardware=json.loads((root/'lscpu.json').read_text());fields={r['field']:r['data'] for r in hardware['lscpu']}
    assert fields['Vendor ID:']=='AuthenticAMD'
    assert fields['CPU family:']==('25' if cpu=='zen4' else '26')
    receipts.append({'cpu':cpu,'model':fields['Model name:'],'job':json.loads((root/'job.json').read_text())})
    for isa in ('avx512','avx2-gfni','avx2'):
        for k in (14,30):
            where=root/isa/f'k{k}';compile=json.loads((where/'compile.json').read_text())
            tune='znver4' if cpu=='zen4' else 'znver5'
            for domain in ('reads.cpp','raw_hash_set.cc'):
                command=next(e.get('command',' '.join(e.get('arguments',[]))) for e in compile if e['file'].endswith(domain))
                assert '-mtune='+tune in command
                assert '-march='+('x86-64-v3' if isa!='avx512' else tune) in command
                assert ('-mgfni' in command)==(isa=='avx2-gfni')
            check=list(csv.DictReader((where/'check.csv').open()));assert len(check)==12
            rows=[]
            for seed in (713,3191,76543):
                for miss in (0,50,100):
                    data=list(csv.DictReader((where/f'read-{seed}-{miss}.csv').open()));assert len(data)==60
                    for r in data:
                        assert int(r['seed'])==seed and int(r['miss_percent'])==miss and int(r['K'])==k and int(r['b'])==10
                        assert int(r['requests'])==1048576 and int(r['rep']) in range(5)
                        assert math.isfinite(float(r['ns'])) and float(r['ns'])>0
                    rows.extend(data)
            groups=defaultdict(lambda:defaultdict(list))
            for r in rows:groups[(r['mode'],int(r['V']),int(r['miss_percent']))][r['implementation']].append(r)
            for (mode,v,miss),owners in sorted(groups.items()):
                measurements={}
                for owner,rs in owners.items():
                    assert len(rs)==15
                    assert {(int(r['seed']),int(r['rep'])) for r in rs}=={(seed,rep) for seed in (713,3191,76543) for rep in range(5)}
                    values=[float(r['ns']) for r in rs];med=statistics.median(values)
                    measurements[owner]={'ns':med,'mad':statistics.median(abs(x-med) for x in values),'samples':len(values),'bytes':statistics.median(int(r['bytes']) for r in rs),'E':sorted({int(r['E']) for r in rs}),'kernel':sorted({int(r['kernel']) for r in rs})}
                x=measurements['ikea']['ns'];z=measurements['abseil']['ns']
                out.append({'cpu':cpu,'isa':isa,'K':k,'V':v,'mode':mode,'miss_percent':miss,'measurements':measurements,'ikea_throughput_factor':z/x})
result={'scope':'Existing Zen5 construction recipes transferred unchanged to native Zen4/Zen5 compiler tunes; all V0/8/64 and 0/50/100% misses retained. No Zen4 encoding search or new measured profile claimed.','normal_rows':6480,'receipts':receipts,'summary':out}
a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(result,indent=2)+'\n')
print('Audited 6,480 rows, native AMD families, matching candidate/Abseil march/mtune, 12 functional rows per geometry/ISA')
