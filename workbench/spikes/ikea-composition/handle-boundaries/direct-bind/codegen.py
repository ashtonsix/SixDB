#!/usr/bin/env python3
"""Check that adding direct binding preserves the existing qualified TP callers.

Reuses exact source/commands and strict function/constant/unwind records from
the final-v5 projection study. Only its filter header changes. This is an
unlinked old-caller check, not native timing or a proof about all Ikea callers.
"""
import argparse
import difflib
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location('handle_run', HERE.parent/'run.py')
run = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(run)
HEADER = 'ikea/include/ikea/tuplepack/filter.h'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--parent', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    parent = args.parent.resolve()
    old = json.loads((parent/'receipt.json').read_text())
    out = args.output.resolve()
    tree = out/'source'
    receipt_path = out/'receipt.json'
    if receipt_path.exists():
        raise RuntimeError('choose fresh output; never overwrite qualified objects')
    records = [(Path(path), row) for path, row in old['objects'].items()
               if '/after/' in path and Path(path).stem in ('producer', 'caller', 'extra_tuple_2')]
    assert len(records) == 9
    dependencies = {}
    for path, row in records:
        assert run.sha(path) == row['object_sha256']
        for name, digest in row['dependencies'].items():
            assert run.sha(Path(name)) == digest, name
            dependencies[name] = digest
    before_root = Path(records[0][1]['command'][records[0][1]['command'].index('-I')+1]).parents[1]
    # Copy only the actual project dependency closure, keeping paths and source
    # basenames identical. Current unrelated Frame edits cannot enter this proof.
    for name, digest in dependencies.items():
        source = Path(name)
        if source.is_relative_to(before_root):
            target = tree/source.relative_to(before_root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    before = (tree/HEADER).read_text()
    (tree/HEADER).write_bytes(args.header.read_bytes())
    hashes = {str(p.relative_to(tree)): run.sha(p) for p in tree.rglob('*') if p.is_file()}
    receipt = {'scope': __doc__, 'complete': False, 'objects': {}, 'comparisons': {},
               'compiler': run.base.run(['clang++-21', '--version']),
               'parent_receipt_sha256': run.sha(parent/'receipt.json'),
               'source_sha256': hashes,
               'source_manifest_sha256': hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest(),
               'patch': ''.join(difflib.unified_diff(before.splitlines(True), args.header.read_text().splitlines(True),
                                                      fromfile='projection/filter.h', tofile='direct/filter.h'))}
    run.save(receipt_path, receipt)
    for path, row in records:
        profile, name = row['profile'], path.stem
        output = out/profile/(name+'.o')
        run.compile_object(tree, row['source'], profile, output, 'final16', receipt, configured_release=True)
        lhs = json.loads(path.with_suffix('.record.json').read_text())
        rhs = json.loads(output.with_suffix('.record.json').read_text())
        result = {kind: {'same': len(lhs[kind]),
                         'different': [k for k in lhs[kind].keys() | rhs[kind].keys()
                                       if lhs[kind].get(k) != rhs[kind].get(k)]}
                  for kind in ('functions', 'constants', 'unwind')}
        receipt['comparisons'][profile+'/'+name] = {'parent_object_sha256': row['object_sha256'],
                                                   'object_sha256': run.sha(output), 'records': result}
        run.save(receipt_path, receipt)
        assert all(not v['different'] for v in result.values()), (profile, name, result)
        print(profile, name, {k: v['same'] for k,v in result.items()}, flush=True)
    receipt['complete'] = True
    run.save(receipt_path, receipt)


if __name__ == '__main__':
    main()
