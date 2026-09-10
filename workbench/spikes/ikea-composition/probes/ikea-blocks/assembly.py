#!/usr/bin/env python3
"""Select complete functions illustrating the ABI and composition findings.

Input is a full (possibly recovered) run; output is just the small assembly
selection. Full objects, IR and disassembly remain in that run's S3 bundle.
"""
import argparse
from pathlib import Path
import re

ABI = {
    'aarch64': ['raw128', 'expected128', 'call_expected128', 'raw512', 'cps512', 'cps_chunks512'],
    'x86-sse2': ['raw128', 'raw256', 'cps256'],
    'x86-avx2': ['raw256', 'expected512', 'call_expected512', 'cps512'],
    'x86-avx512': ['raw512', 'expected512', 'call_expected512', '__regcall3__reg_tagged128',
                   '__regcall3__reg_expected128', 'call_reg_expected128', '__regcall3__reg_union128',
                   'inline_raw512', 'inline_expected_success512', 'inline_expected_dynamic512'],
}
COMPOSITION = ['ikea_comp_load', 'ikea_comp_features', 'ikea_comp_load_features',
               'ikea_comp_model', 'ikea_comp_transition_model_stage', 'ikea_comp_done']


def select(assembly, names):
    headers = list(re.finditer(r'^([0-9a-f]+) <([^>]+)>:$', assembly, re.MULTILINE))
    blocks = {}
    for i, header in enumerate(headers):
        end = headers[i + 1].start() if i + 1 < len(headers) else len(assembly)
        block = assembly[header.start():end]
        # An object/section heading is not part of the preceding function.
        block = re.split(r'\n\n(?=\S.*file format|Disassembly of section)', block)[0]
        blocks[header[2]] = block.rstrip()
    missing = set(names) - blocks.keys()
    if missing:
        raise ValueError(f'Missing assembly functions: {sorted(missing)}')
    return '\n\n'.join(blocks[name] for name in names) + '\n'


def export(kind, source, output):
    output.mkdir(parents=True, exist_ok=True)
    written = []
    if kind == 'abi':
        variants = ABI
    else:
        variants = {p.name.removesuffix('-hot.asm'): COMPOSITION for p in sorted(source.glob('*-hot.asm'))}
    if not variants:
        raise ValueError('No assembly inputs')
    for variant, names in variants.items():
        paths = ([source / f'{variant}-{unit}.asm' for unit in ('producer', 'caller', 'sink')]
                 if kind == 'abi' else [source / f'{variant}-hot.asm'])
        assembly = '\n\n'.join(path.read_text() for path in paths)
        name = f'{variant}-excerpts.asm'
        (output / name).write_text(select(assembly, names))
        written.append(name)
    return written


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('kind', choices=['abi', 'composition'])
    parser.add_argument('run', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    print('\n'.join(export(args.kind, args.run, args.output)))
