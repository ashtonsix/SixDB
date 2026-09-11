#!/usr/bin/env python3
"""Prepare a Local6-only loop-grain overlay for a paired public-endpoint build."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

HEADER = Path('ikea/include/ikea/seriespack/native_avx512.h')
HEADER_SHA = '62562fc9199c6d81bf5d3f7794866822b296bcf91a6acf761a9cdf325d8d3d3a'
NATIVE = Path('ikea/src/seriespack/native.cpp')
NATIVE_SHA = '1abf641b246af9c075e8234a9e1b3390c412a8410af9353d84d5c52cd57350da'


def sha(data): return hashlib.sha256(data).hexdigest()


def write_identical(path,data):
    if path.exists():
        if path.read_bytes()!=data: raise RuntimeError(f'refusing to replace different output: {path}')
    else:
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--grain',type=int,choices=[64,256,512],required=True)
    args=parser.parse_args()
    source=(args.root/HEADER).resolve();native=(args.root/NATIVE).resolve()
    output=args.output.resolve()
    baseline=source.read_bytes();native_bytes=native.read_bytes()
    if sha(baseline)!=HEADER_SHA or sha(native_bytes)!=NATIVE_SHA:
        raise RuntimeError('header/native.cpp differs from the coherent baseline; do not mix checkpoints')
    destination=output/'ikea/seriespack/native_avx512.h'
    if destination.resolve()==source or output in source.parents:
        raise RuntimeError('output would replace the production source')
    old='''        for (; tiles - tile >= 8; tile += 8)
            _mm512_storeu_si512(out + tile * 8, read_local_region64<W>(p + tile * W));
'''
    pragma='unroll(disable)' if args.grain==64 else f'unroll_count({args.grain//64})'
    new='''#if defined(__GFNI__)
        if constexpr (W == 6) {
#pragma clang loop '''+pragma+'''
            for (; tiles - tile >= 8; tile += 8)
                _mm512_storeu_si512(out + tile * 8, read_local_region64<W>(p + tile * W));
        } else
#endif
        {
'''+old+'''        }
'''
    text=baseline.decode()
    if text.count(old)!=1: raise RuntimeError('dense loop insertion is ambiguous')
    candidate=text.replace(old,new).encode()
    patch=''.join(difflib.unified_diff(text.splitlines(True),candidate.decode().splitlines(True),
        fromfile='a/'+str(HEADER),tofile='b/'+str(HEADER))).encode()
    receipt={'baseline_header_sha256':HEADER_SHA,'native_cpp_sha256':NATIVE_SHA,
        'grain_values':args.grain,'candidate_header_sha256':sha(candidate),'patch_sha256':sha(patch),
        'scope':'GFNI AVX512 Local6/u8 dense decoder only; byte leaf, accesses, other widths, carriers and boundaries unchanged',
        'build':'Rebuild this same captured native.cpp with the overlay include directory first; retain every other captured object and profile flag, then relink the same public benchmark.'}
    write_identical(destination,candidate);write_identical(output/'local6.patch',patch)
    write_identical(output/'receipt.json',(json.dumps(receipt,indent=2)+'\n').encode())
    if source.read_bytes()!=baseline or native.read_bytes()!=native_bytes:
        raise RuntimeError('baseline changed concurrently during preparation')
    print(json.dumps(receipt,indent=2))


if __name__=='__main__':main()
