#!/usr/bin/env python3
"""Build an H16/u64 native.cpp overlay against the coherent checkpoint."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

NATIVE = Path('ikea/src/seriespack/native.cpp')
BASE_SHA = '1abf641b246af9c075e8234a9e1b3390c412a8410af9353d84d5c52cd57350da'
def sha(data): return hashlib.sha256(data).hexdigest()
def write(path, data):
    if path.exists() and path.read_bytes() != data:
        raise RuntimeError(f'refusing changed existing output: {path}')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = (args.root / NATIVE).resolve()
    original = source.read_bytes()
    assert sha(original) == BASE_SHA, 'native.cpp differs from coherent checkpoint'
    out = args.output.resolve()
    assert out not in source.parents and out / NATIVE != source
    text = original.decode()
    marker = 'template<class Ops, unsigned Shift, unsigned T, class U>\nvoid encode_head'
    assert text.count(marker) == 1
    text = text.replace(marker, Path(__file__).with_name('projection.inc').read_text() + marker)
    old = '    else if (h == 16) {\n        encode_head<Ops, W + 8, T>(heads[0], n, input);\n        encode_head<Ops, W, T>(heads[1], n, input);\n    }\n'
    new = '    else if (h == 16) {\n#if defined(__AVX2__)\n        if constexpr (sizeof(U) == 8 && W <= 48) {\n            encode_x86_head16<Ops, W, T>(input, n,\n                reinterpret_cast<std::uint8_t*>(heads[0].bytes.data()), heads[0].stride,\n                reinterpret_cast<std::uint8_t*>(heads[1].bytes.data()), heads[1].stride);\n        } else\n#endif\n        {\n            encode_head<Ops, W + 8, T>(heads[0], n, input);\n            encode_head<Ops, W, T>(heads[1], n, input);\n        }\n    }\n'
    assert text.count(old) == 1
    candidate = text.replace(old, new).encode()
    patch = ''.join(difflib.unified_diff(original.decode().splitlines(True), candidate.decode().splitlines(True),
        fromfile='a/' + str(NATIVE), tofile='b/' + str(NATIVE))).encode()
    write(out / NATIVE, candidate)
    write(out / 'head16.patch', patch)
    receipt = {'format': 1, 'baseline_native_sha256': BASE_SHA,
        'candidate_native_sha256': sha(candidate), 'patch_sha256': sha(patch),
        'scope': 'H16/u64, every legal payload W 0..48; AVX2 compact16 or AVX512 compact32, independent head strides, unchanged payload and all other carriers/H8'}
    write(out / 'receipt.json', (json.dumps(receipt, indent=2) + '\n').encode())
    assert source.read_bytes() == original
    print(json.dumps(receipt, indent=2))
if __name__ == '__main__': main()
