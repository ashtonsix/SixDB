#!/usr/bin/env python3
"""Prepare a Local1/u8 region4 encode overlay, without modifying production."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

HEADER=Path('ikea/include/ikea/seriespack/native_avx512.h')
HEADER_SHA='62562fc9199c6d81bf5d3f7794866822b296bcf91a6acf761a9cdf325d8d3d3a'
NATIVE=Path('ikea/src/seriespack/native.cpp')
NATIVE_SHA='1abf641b246af9c075e8234a9e1b3390c412a8410af9353d84d5c52cd57350da'


def sha(data):return hashlib.sha256(data).hexdigest()


def write_identical(path,data):
    if path.exists():
        if path.read_bytes()!=data:raise RuntimeError(f'refusing different existing output: {path}')
    else:
        path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    source=(args.root/HEADER).resolve();native=(args.root/NATIVE).resolve()
    baseline=source.read_bytes();native_bytes=native.read_bytes()
    if sha(baseline)!=HEADER_SHA or sha(native_bytes)!=NATIVE_SHA:
        raise RuntimeError('baseline header/native.cpp differs from the coherent checkpoint')
    output=args.output.resolve();destination=output/'ikea/seriespack/native_avx512.h'
    if destination.resolve()==source or output in source.parents:
        raise RuntimeError('overlay would replace production')
    old='''    std::size_t tile = 0;
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7 &&
                  (W != 1 || sizeof(UInt) < 4)) {
'''
    new='''    std::size_t tile = 0;
#if defined(__GFNI__) && defined(__AVX512VBMI2__)
    if constexpr (G == geometry::local8 && W == 1 && sizeof(UInt) == 1) {
        // Four exact movemask regions share the enclosing loop. The existing
        // region and tile entries retain all shorter-run boundary handling.
#pragma clang loop unroll(disable)
        for (; tiles - tile >= 32; tile += 32) {
            detail::static_for<4>([&](auto part) {
                write_local_region64<1>(p + tile + part * 8,
                    _mm512_loadu_si512(in + tile * 8 + part * 64));
            });
        }
    }
#endif
    if constexpr (G == geometry::local8 && W >= 1 && W <= 7 &&
                  (W != 1 || sizeof(UInt) < 4)) {
'''
    text=baseline.decode()
    if text.count(old)!=1:raise RuntimeError('encode loop insertion point is ambiguous')
    candidate=text.replace(old,new).encode()
    patch=''.join(difflib.unified_diff(text.splitlines(True),candidate.decode().splitlines(True),
        fromfile='a/'+str(HEADER),tofile='b/'+str(HEADER))).encode()
    receipt={'baseline_header_sha256':HEADER_SHA,'native_cpp_sha256':NATIVE_SHA,
        'candidate_header_sha256':sha(candidate),'patch_sha256':sha(patch),
        'scope':'Local1/u8 encode_low_tiles only when GFNI and AVX512VBMI2 are enabled; four 64-value regions per loop, unchanged leaf and shorter-run paths',
        'build':'Recompile only the coherent native.cpp object with this include overlay first; relink all other captured objects unchanged.'}
    write_identical(destination,candidate);write_identical(output/'local1.patch',patch)
    write_identical(output/'receipt.json',(json.dumps(receipt,indent=2)+'\n').encode())
    if source.read_bytes()!=baseline or native.read_bytes()!=native_bytes:
        raise RuntimeError('baseline changed concurrently during preparation')
    print(json.dumps(receipt,indent=2))


if __name__=='__main__':main()
