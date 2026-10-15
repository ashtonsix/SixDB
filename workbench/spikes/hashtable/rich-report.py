#!/usr/bin/env python3
"""Derive compact paired evidence from a completed rich-feature worker archive.

Original measurements remain in that archive. context.json and paired.csv are
authored derivatives, deliberately separate from artifacts.py's byte provenance.
"""
import argparse
import collections
import csv
import gzip
import hashlib
import json
import re
import statistics
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def symbols(path):
    text = gzip.open(path, 'rt').read()
    parsed = {}
    for name, body in re.findall(r'^\S+ <([^\n]+)>:\n(.*?)(?=\n\S+ <|\Z)', text, re.M | re.S):
        body = re.sub(r'^\s*[0-9a-f]+:\s*', '', body, flags=re.M)
        body = re.sub(r'0x[0-9a-f]+ <([^\n]+)>', r'SYMBOL<\1>', body)
        parsed[name] = body
    return text, parsed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    root, out = args.results, args.destination
    summary_bytes = (root / 'summary.json').read_bytes()
    summary = json.loads(summary_bytes)
    job = json.loads((root / 'job.json').read_text())
    manifest = json.loads((root / 'source-manifest.json').read_text())
    manifest_hash = digest(json.dumps(manifest, sort_keys=True).encode())
    archive_hash = digest((root / 'source.tar.gz').read_bytes())
    assert manifest_hash == job['source']['digest']
    assert archive_hash == job['source']['sha256']
    result = json.loads((root / 'worker-result.json').read_text())
    assert result['state'] == 'complete' and result['script_returncode'] == 0

    arms = ['sp_tp_prepared', 'sp_tp_admitted', 'sp_tp_split', 'raw_fused']
    fields = ['tier', 'process', 'row_bytes', 'active', 'buckets', 'operation']
    grouped = collections.defaultdict(dict)
    for case in summary['cases']:
        assert case['repetitions'] == 8
        key = tuple(case[f] for f in fields)
        assert case['arm'] not in grouped[key]
        grouped[key][case['arm']] = case
    tiers = sorted({key[0] for key in grouped})
    fp = {case.get('fingerprint_bits', 7) for case in summary['cases']}
    assert len(fp) == 1
    fp = fp.pop()
    expected = {(tier, process, row, active, buckets, operation)
                for tier in tiers for process in range(3) for row in [6, 10]
                for active in [0, 1, 7, 8, 15, 16] for buckets in [1, 256]
                for operation in ['independent_hit', 'fingerprint_miss', 'suffix_miss', 'serial_hit', 'changed_update']}
    assert grouped.keys() == expected
    out.mkdir(parents=True, exist_ok=True)
    with (out / 'paired.csv').open('w') as stream:
        writer = csv.writer(stream)
        writer.writerow(fields + [a + '_ns' for a in arms] + ['payload_bytes', 'shared_prepared_bytes'])
        for key, cases in sorted(grouped.items()):
            assert set(cases) == set(arms)
            assert len({c['payload_bytes'] for c in cases.values()}) == 1
            writer.writerow(list(key) + [cases[a]['ns_per_op'] for a in arms] +
                            [cases['sp_tp_split']['payload_bytes'], cases['sp_tp_split']['shared_prepared_bytes']])

    ranges = []
    for tier in tiers:
        for row in [6, 10]:
            for op in ['independent_hit', 'fingerprint_miss', 'suffix_miss', 'serial_hit', 'changed_update']:
                observations = []
                for active in [1, 7, 8, 15, 16]:
                    for buckets in [1, 256]:
                        series = {a: [grouped[(tier, p, row, active, buckets, op)][a]['ns_per_op']
                                      for p in range(3)] for a in ['sp_tp_split', 'raw_fused']}
                        med = {a: statistics.median(s) for a, s in series.items()}
                        observations.append(dict(active=active, buckets=buckets,
                            ratio=med['sp_tp_split'] / med['raw_fused'], process_medians_ns=series))
                ranges.append(dict(tier=tier, row_bytes=row, operation=op,
                    minimum=min(observations, key=lambda x: x['ratio']),
                    maximum=max(observations, key=lambda x: x['ratio'])))

    codegen, parsed, flags = {}, {}, {}
    for tier in tiers:
        text, parsed[tier] = symbols(root / tier / 'codegen/rich_composition.s.gz')
        facts = json.loads((root / tier / 'codegen.json').read_text())['rich_composition']['witnesses']
        codegen[tier] = dict(gfni_instruction_count=len(re.findall(r'\tvgf', text)),
            point_witnesses={n: dict(instructions_including_alignment=v['instructions_including_alignment'],
                                    calls=len(v['calls']), stack_instructions=len(v['stack_instructions']))
                for n, v in facts.items() if 'split' in n or 'fused' in n})
        commands = json.loads((root / tier / 'compile.json').read_text())
        flags[tier] = next(x['command'] for x in commands if x['file'].endswith('/rich_composition.cpp'))
        assert all(f in flags[tier] for f in ['-fno-vectorize', '-fno-slp-vectorize', '-ffp-contract=off'])
    if 'avx2' in tiers and 'avx2-gfni' in tiers:
        names = [n for n in parsed['avx2'] if 'rich::run<' in n and
                 ('true, true' in n or 'rich::fused' in n)]
        assert len(names) == 20
        codegen['avx2_vs_gfni'] = dict(complete_strong_or_raw_loops=len(names),
            changed_normalized_bodies=[n for n in names if parsed['avx2'][n] != parsed['avx2-gfni'][n]])
    context = dict(kind='Authored derivation of completed worker evidence; not original worker bytes',
        job=job['id'], source_manifest_digest=manifest_hash, source_tar_gz_sha256=archive_hash,
        rich_composition_cpp_sha256=manifest['workbench/spikes/hashtable-design/rich_composition.cpp'],
        archived_summary_json_sha256=digest(summary_bytes), fingerprint_bits=fp,
        paired_csv_sha256=digest((out / 'paired.csv').read_bytes()),
        reproduction='python3 workbench/spikes/hashtable/rich-report.py RECOVERED_WORKER_RESULTS DESTINATION',
        report_script_sha256=digest(Path(__file__).read_bytes()),
        scope=summary['scope'] + [
            'Each paired.csv cell is one process median of eight rotated-arm repetitions; all four arms and all tails remain.',
            'Ranges use the ratio of the medians of the three process medians; ranges exclude active=0, which remains in paired.csv.',
            'sp_tp_split uses spike-only fixed controls checked against production preparation; this is not a finished public API.',
            'The 2216-byte prepared object is shared per format and used in setup; it is not per-bucket metadata or a claim about hot accessed bytes.',
            'Serial timing includes the full-value consumer and dependent query-index calculation.',
            'F7/C13 is noncanonical under the original fingerprint clamp; F8/C13 is a legal K40/A19 component. Neither measures hashing/table costs.',
            'Point instruction counts do not establish throughput: complete inlined loops can have different schedules, branches, and register spills.'],
        compile_commands=flags, codegen=codegen, strongest_to_raw_ranges=ranges)
    (out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    print(f'{out}: {len(grouped)} paired process cells, F{fp}, source {manifest_hash}')


if __name__ == '__main__':
    main()
