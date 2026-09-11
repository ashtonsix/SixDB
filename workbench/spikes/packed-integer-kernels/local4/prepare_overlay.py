#!/usr/bin/env python3
"""Create a hash-checked, AVX2-only Local4 header overlay; never edit the source tree."""
import argparse
import difflib
import hashlib
import json
from pathlib import Path

BASE_SHA256 = '3e78b77bcea0a0229f18b8dd2fbf27a01696b00931703db1610750445501f9f9'
HELPER_FILE_SHA256 = '231aece22a0302845c1336658c4312a6ba432a77722ac4cc2053e04fd6011e2e'
RELATIVE = Path('ikea/include/ikea/seriespack/native_avx2.h')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write_new_or_identical(path, data):
    if path.exists():
        if path.read_bytes() != data:
            raise RuntimeError(f'refusing to replace different existing output: {path}')
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    source = (args.root / RELATIVE).resolve()
    output = args.output.resolve()
    if output == args.root.resolve() or output in source.parents:
        raise RuntimeError('output must be a separate overlay directory')
    baseline = source.read_bytes()
    if digest(baseline) != BASE_SHA256:
        raise RuntimeError('production header differs from the approved diagnostic baseline')
    helper_file = Path(__file__).with_name('projected.h').read_bytes()
    if digest(helper_file) != HELPER_FILE_SHA256:
        raise RuntimeError('isolated projected helper differs from its retained hash')
    helper_text = helper_file.decode()
    begin, end = '// BEGIN LOCAL4 PROJECTED HELPER\n', '// END LOCAL4 PROJECTED HELPER'
    if helper_text.count(begin) != 1 or helper_text.count(end) != 1:
        raise RuntimeError('helper markers are ambiguous')
    helper = helper_text.split(begin)[1].split(end)[0]
    text = baseline.decode()
    needle = 'template<unsigned R>\n[[gnu::always_inline]] inline void write_local_region32('
    if text.count(needle) != 1:
        raise RuntimeError('region writer insertion point is ambiguous')
    text = text.replace(needle, '#if !defined(__GFNI__) && !defined(__AVX512F__)\n' + helper + '#endif\n\n' + needle)
    old = '    } else detail::avx2::encode_body<R, 8>(p, native_detail::transpose(values));\n'
    new = '''    }
#if !defined(__GFNI__) && !defined(__AVX512F__)
    else if constexpr (R == 4)
        detail::avx2::encode_body<4, 8>(p, local4_projected_transpose(values));
#endif
    else detail::avx2::encode_body<R, 8>(p, native_detail::transpose(values));
'''
    if text.count(old) != 1:
        raise RuntimeError('region writer replacement is ambiguous')
    candidate = text.replace(old, new).encode()
    overlay_header = output / 'ikea/seriespack/native_avx2.h'
    if overlay_header.resolve() == source:
        raise RuntimeError('overlay would replace production')
    patch = ''.join(difflib.unified_diff(baseline.decode().splitlines(True), candidate.decode().splitlines(True),
        fromfile='a/' + str(RELATIVE), tofile='b/' + str(RELATIVE)))
    receipt = {
        'baseline_header': str(RELATIVE), 'baseline_sha256': BASE_SHA256,
        'helper_file_sha256': HELPER_FILE_SHA256, 'candidate_sha256': digest(candidate),
        'patch_sha256': digest(patch.encode()),
        'include_prefix': str(output),
        'scope': 'AVX2-only write_local_region32<4>; no loop-grain, other-width, GFNI or AVX512 change',
        'build': 'Add this overlay include directory before ikea/include and rebuild the captured native.cpp object, then relink the same captured benchmark dependencies. Do not mix native.cpp/header checkpoints.',
    }
    write_new_or_identical(overlay_header, candidate)
    write_new_or_identical(output / 'local4.patch', patch.encode())
    write_new_or_identical(output / 'receipt.json', (json.dumps(receipt, indent=2)+'\n').encode())
    # Re-read the source to prove this preparation did not modify it.
    if source.read_bytes() != baseline:
        raise RuntimeError('production source changed concurrently during overlay preparation')
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
