#!/usr/bin/env python3
"""Select comparable static bodies and qualification receipts from a raw bundle."""
import argparse
import hashlib
import json
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    args = parser.parse_args()
    root = args.bundle
    raw = json.loads((root/'codegen/receipt.json').read_text())
    selected = {'compiler': raw['compiler'], 'mca': raw['mca'],
                'source_digest': raw['source_digest'],
                'raw_receipt_sha256': digest(root/'codegen/receipt.json'),
                'analysis_recipe_sha256': raw['analysis_recipe_sha256'],
                'parser_controls': raw['parser_controls'],
                'units': {'bytes': 'Object symbol size; alignment padding is excluded.',
                          'instruction_sites': 'Disassembler sites; may include boundary padding.',
                          'load_sites_lexical': 'Conservative lexical load count; excludes some memory-source ALU instructions. Not physical loads.',
                          'stack_sites': 'Explicit stack-address sites and register push/pop; not a spill count.',
                          'cacheline64_span': 'Minimum/maximum alignment bound for code, not cache misses.',
                          'mca': 'LLVM21 CPU-model resource/latency assumptions, exact successful found-row tail including its saves/restores but excluding return. Not whole-query cycles or hardware timing.'},
                'limits': raw['limits'] + [
                    'Repeated MCA regions reuse argument/clobbered registers; Total Cycles is not independent-call latency.',
                    'The actual native Driver already sees inline bodies. The deliberately outlined ordinary fixture is a hypothetical new seam, not the current baseline.',
                    'The current Driver contains native/direct/manual routes and setup; its whole-symbol size is not comparable to one fixture query.'],
                'targets': {}}
    for target, rows in raw['targets'].items():
        bodies, models, objects = [], [], []
        for row in rows:
            objects.append({key: row[key] for key in ('source', 'source_sha256', 'object_sha256', 'compiler_assembly_sha256', 'argv')})
            for f in row['functions']:
                if f['name'].startswith('critical_paths::') and not any(
                        cold in f['name'] for cold in ('Scope::qualify', 'Scope::~Scope')):
                    f = dict(f)
                    f['load_sites_lexical'] = f.pop('load_sites')
                    bodies.append(f)
            models.extend({key: m[key] for key in ('symbol', 'status', 'region_sha256', 'assembled_region_sha256', 'summary')}
                          for m in row['mca'])
        selected['targets'][target] = {'objects': objects, 'bodies': bodies, 'found_row_tail_models': models}
    checks = {}
    for name, member in [('native_point', 'checks/native/receipt.json'),
                         ('asan_ubsan_point', 'checks/asan/receipt.json'),
                         ('native_wire', 'checks/native-wire.json'),
                         ('asan_ubsan_wire', 'checks/asan-wire.json'),
                         ('qemu_x86_wire', 'checks/x86-wire/receipt.json')]:
        path = root/member
        receipt = json.loads(path.read_text())
        assert receipt.get('run_status', receipt.get('status')) == 0, name
        result = receipt.get('result', receipt.get('checks'))
        assert result['passed'], name
        checks[name] = {'receipt_sha256': digest(path), **receipt, 'result': result}
    qualification = {'checks': checks,
                     'limits': ['Native runs are local AArch64; V2 objects are target models, not a V2 hardware run.',
                                'The x86 wire law uses Clang21 x86-64-v3 and QEMU max for functional coverage of BMI2/SIMD arms, not Zen5 execution or timing.',
                                'Point-query libraries are frozen v3 bytes; corrected final/check.cpp adds owner-message polling during teardown. No query/library byte changed.',
                                'The retained Scope keeps physical backing. Immutable Page/Schema/Bindings/directory metadata is fixture-owned and outlives the Scope; this does not qualify MVCC snapshots.']}
    (root/'static-summary.json').write_text(json.dumps(selected, indent=2)+'\n')
    (root/'qualification.json').write_text(json.dumps(qualification, indent=2)+'\n')
    print('Selected complete bodies, five found-row tails per CPU model, and all five qualification receipts.')


if __name__ == '__main__':
    main()
