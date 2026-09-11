#!/usr/bin/env python3
"""Summarize SeriesPack access repetitions with matched extents and explicit controls."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


CASE = re.compile(r'resident/(?:(series)/([^/]+)|(calico|predecessor))/(local|striped)/'
                  r'k(\d+)/h(\d+)/u(\d+)/(?:((?:static-arithmetic|static-offsets|admitted-region|raw-dense))/)?'
                  r'(point|get16|dependent)$')
PROFILES = {'neon': {'neon', 'scalar'}, 'avx2': {'avx2', 'scalar'},
            'avx512bw': {'avx2', 'scalar'}, 'avx512': {'avx2', 'avx512', 'scalar'}}
EXTENTS = ('logical_values', 'encoded_bytes', 'query_bytes', 'io_bytes_per_value',
           'values_per_item', 'bulk_input_output_bytes')
UNITS = {'ns': 1, 'us': 1e3, 'ms': 1e6, 's': 1e9}
STATIC_POINTS = {'static-arithmetic', 'static-offsets'}
MATERIALIZERS = {'admitted-region', 'raw-dense'}


def identity(path, content=None):
    content = path.read_bytes() if content is None else content
    return {'path': str(path.resolve()), 'sha256': hashlib.sha256(content).hexdigest()}


def positive(value, label, integer=False, zero=False):
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            not math.isfinite(value) or value < 0 or (value == 0 and not zero) or
            (integer and value != int(value))):
        raise ValueError(f'{label}: invalid numeric value {value!r}')
    return int(value) if integer else value


def descriptor(name, context, profile):
    match = CASE.fullmatch(name)
    if not match:
        raise ValueError(f'unknown access case: {name}')
    series, target, control, layout, k, h, carrier, path, operation = match.groups()
    provider = series or control
    target = target if series else context.get(f'{"calico" if control == "calico" else "predecessor"}_target')
    # The predecessors have BW-only AVX-512 paths, while SeriesPack's AVX-512
    # execution family additionally requires VBMI/VBMI2/GFNI. Keep the family
    # labels distinct; comparison() still refuses to rank unlike targets.
    allowed = PROFILES[profile] | ({'avx512'} if control and profile == 'avx512bw' else set())
    if target not in allowed:
        raise ValueError(f'{name}: missing or incompatible target {target!r} for profile {profile}')
    k, h, carrier = int(k), int(h), int(carrier)
    if not (1 <= k <= 64 and h in (0, 8, 16) and h <= k and carrier == 64):
        raise ValueError(f'{name}: invalid access width/head/carrier')
    if provider == 'series' and layout == 'striped' and k - h not in (*range(1, 8), 10, 12, 14, 15, 20):
        raise ValueError(f'{name}: unsupported SeriesPack striped geometry')
    if ((path and provider != 'series') or
            (path in STATIC_POINTS and operation != 'point') or
            (path in MATERIALIZERS and (operation != 'get16' or h != 0 or target == 'scalar' or
                (layout == 'local' and k not in (*range(1, 8), 56)))) or
            (provider != 'series' and h != 0) or
            (provider == 'predecessor' and not (k <= 7 or (k == 56 and layout == 'local')))):
        raise ValueError(f'{name}: unsupported control or access route')
    return {'provider': provider, 'target': target, 'profile': profile, 'layout': layout,
            'width': k, 'head': h, 'carrier': carrier, 'operation': operation,
            'route': path or ('bound' if series else 'direct'),
            'point_path': None if path in MATERIALIZERS else path or ('bound' if series else 'direct')}


def read(path, profile):
    raw = path.read_bytes()
    document = json.loads(raw)
    context = document.get('context', {})
    requested = context.get('requested_encoded_resident_bytes')
    if requested is not None:
        if isinstance(requested, str) and requested.isdecimal():
            requested = int(requested)
        requested = positive(requested, 'requested encoded footprint', integer=True)
        if not 4096 <= requested <= 1 << 30:
            raise ValueError('requested encoded footprint is outside the driver contract')
    groups = defaultdict(list)
    for row in document['benchmarks']:
        name = row.get('run_name', row['name'])
        if not name.startswith('resident/'):
            continue
        if row.get('error_occurred'):
            raise ValueError(f"{name}: {row.get('error_message', 'benchmark error')}")
        if row.get('run_type', 'iteration') == 'aggregate':
            continue
        if row.get('run_type', 'iteration') != 'iteration':
            raise ValueError(f'{name}: unknown run type')
        groups[name].append(row)
    cases = {}
    for name, rows in groups.items():
        desc = descriptor(name, context, profile)
        extents, samples, indices, declared = [], [], [], set()
        for row in rows:
            extent = tuple(positive(row[key], f'{name}/{key}', integer=True,
                                    zero=key == 'bulk_input_output_bytes') for key in EXTENTS)
            n, encoded, query_bytes, io_bytes, values, bulk_bytes = extent
            if n % 256 or encoded * 8 != n * desc['width']:
                raise ValueError(f'{name}: logical/encoded extent mismatch')
            if requested is not None and n != max(256, (requested * 8 // desc['width']) // 256 * 256):
                raise ValueError(f'{name}: extent disagrees with requested encoded footprint')
            if query_bytes != max(8192, encoded // 16) * 8 or io_bytes != 8 or bulk_bytes != 0:
                raise ValueError(f'{name}: query or materialized-array extent mismatch')
            if values != (16 if desc['operation'] == 'get16' else 1):
                raise ValueError(f'{name}: values_per_item does not match operation')
            speed = positive(row['items_per_second'], f'{name}/items_per_second')
            cpu_ns = positive(row['cpu_time'], f'{name}/cpu_time') * UNITS[row['time_unit']]
            positive(row['iterations'], f'{name}/iterations', integer=True)
            if row.get('threads', 1) != 1 or not math.isclose(cpu_ns * speed / 1e9, 256, rel_tol=1e-8):
                raise ValueError(f'{name}: expected 256 single-thread queries per timed iteration')
            samples.append(1e9 / speed)  # Convert repetitions before computing statistics.
            indices.append(positive(row.get('repetition_index', 0), name, integer=True, zero=True))
            declared.add(positive(row.get('repetitions', 1), name, integer=True))
            extents.append(extent)
        if len(set(extents)) != 1:
            raise ValueError(f'{name}: inconsistent extents across repetitions')
        if len(declared) != 1 or len(set(indices)) != len(indices) or set(indices) != set(range(declared.pop())):
            raise ValueError(f'{name}: duplicate or missing repetitions')
        values = extents[0][4]
        cases[name] = {'case': name, **desc, **dict(zip(EXTENTS, extents[0])),
                       'query_count': extents[0][2] // 8, 'queries_per_iteration': 256,
                       'repetitions': len(samples),
                       'ns_per_query_repetitions': [x for _, x in sorted(zip(indices, samples))],
                       'median_ns_per_query': statistics.median(samples),
                       'min_ns_per_query': min(samples), 'max_ns_per_query': max(samples)}
        if desc['operation'] == 'get16':
            cases[name]['median_ns_per_materialized_value'] = statistics.median(samples) / values
    if not cases:
        raise ValueError(f'{path}: no access repetitions')
    return cases, {**identity(path, raw), 'context': context, 'profile': profile,
                   'access_cases': len(cases)}


def comparison(candidate, control):
    if any(candidate[key] != control[key] for key in EXTENTS):
        raise ValueError(f"comparison extents differ: {candidate['case']} versus {control['case']}")
    same = candidate['target'] == control['target'] and candidate['profile'] == control['profile']
    result = {'case': control['case'], 'target': control['target'], 'profile': control['profile'],
              'route': control['route'],
              'same_target_and_profile': same, 'median_ns_per_query': control['median_ns_per_query']}
    if 'median_ns_per_materialized_value' in control:
        result['median_ns_per_materialized_value'] = control['median_ns_per_materialized_value']
    if same:
        result['time_ratio'] = candidate['median_ns_per_query'] / control['median_ns_per_query']
    else:
        result['not_ranked'] = 'different execution targets or build profiles'
    return result


def summarize(cases):
    result = []
    for name, value in sorted(cases.items()):
        if value['provider'] != 'series':
            continue
        row = {**value, 'missing_controls': {}}
        def pair(key, control):
            if control in cases:
                row[key] = comparison(value, cases[control])
            else:
                row['missing_controls'][key] = control
        if value['route'] in STATIC_POINTS:
            pair('bound_point', name.replace('/' + value['point_path'], ''))
            row['static_query_cycle'] = {'query_count': value['query_count'],
                                         'dropped_tail': value['query_count'] % 256,
                                         'bound_wraps_each_index': True}
        else:
            if value['route'] in MATERIALIZERS:
                pair('bound_get16', name.replace('/' + value['route'], ''))
            k, layout, op = value['width'], value['layout'], value['operation']
            suffix = f'k{k}/h0/u64/{op}'
            if value['head'] == 0 and (k <= 7 or (k == 56 and layout == 'local')):
                pair('same_wire_predecessor', f'resident/predecessor/{layout}/{suffix}')
            pair('same_layout_calico', f'resident/calico/{layout}/{suffix}')
            choices = [cases[p] for g in ('local', 'striped')
                       if (p := f'resident/calico/{g}/{suffix}') in cases]
            other = 'striped' if layout == 'local' else 'local'
            alternate = f'resident/calico/{other}/{suffix}'
            if alternate not in cases:
                row['missing_controls']['alternate_layout_calico'] = alternate
            # Do not select a cross-target winner, even though its timing is retained.
            eligible = [c for c in choices if c['target'] == value['target'] and c['profile'] == value['profile']]
            if eligible:
                fastest = min(eligible, key=lambda c: c['median_ns_per_query'])
                row['fastest_calico_layout'] = comparison(value, fastest)
                row['fastest_calico_layout']['layouts_available'] = [c['layout'] for c in eligible]
                row['fastest_calico_layout']['complete_layout_pair'] = len(eligible) == 2
            else:
                row['fastest_calico_layout_unavailable'] = 'no matching-target control layouts'
        result.append(row)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('samples', type=Path)
    parser.add_argument('--profile', choices=PROFILES, required=True,
                        help='build profile from run.py receipt; distinct from execution target labels')
    parser.add_argument('--receipt', type=Path, help='optional run.py validation.json; retain and check profile')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--top', type=int, default=10)
    args = parser.parse_args()
    try:
        cases, source = read(args.samples, args.profile)
        receipt = None
        if args.receipt:
            content = args.receipt.read_bytes()
            receipt = {**identity(args.receipt, content), 'document': json.loads(content)}
            profiles = [p for p in receipt['document']['profiles'] if p['name'] == args.profile]
            if len(profiles) != 1:
                raise ValueError('profile is absent from the supplied validation receipt')
            recorded = profiles[0].get('measurement', {}).get('context')
            if recorded is not None and recorded != source['context']:
                raise ValueError('sample context differs from the selected receipt profile')
        rows = summarize(cases)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))
    result = {
        'format': 1, 'units': 'CPU ns/query; get16 also reports ns/materialized value (query time /16); ratios>1 are slower',
        'scope': 'One profile, matched logical/encoded/query extents and u64 output. '
                 'Immediate same-wire predecessors are primary; Calico is an additional representation control. '
                 'ScanPack controls use fragment_classes. Target labels are execution families; '
                 'the build profile/receipt identifies permitted ISA flags. '
                 'Static point regions compare to bound arithmetic point calls under their registration target label. '
                 'Admitted-region and raw-dense get16 routes pair with ordinary bound get16 and the same primary '
                 'predecessor/Calico controls; admission and runtime placement contracts remain distinct. '
                 'Static regions omit a terminal query-index batch shorter than256; equal extents do not imply '
                 'identical query streams or establish the impact of that omission. '
                 'Driver/checksum and narrow predecessor widening costs remain included. '
                 'Dependent queries include index generation. Requested footprint and recorded allocations do not prove '
                 'cold access or any cache residency; no significance inference or cross-target ranking.',
        'samples': source, 'receipt': receipt, 'summarizer': identity(Path(__file__)),
        'cases': rows, 'controls': [c for c in cases.values() if c['provider'] != 'series'],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(f'{len(cases)} access cases; {len(rows)} SeriesPack; '
          f'{sum(c["provider"] == "predecessor" for c in cases.values())} predecessor controls; '
          f'{sum(bool(r["missing_controls"]) for r in rows)} SeriesPack cases with missing controls')
    for key in ('same_wire_predecessor', 'same_layout_calico', 'bound_point', 'bound_get16'):
        for target in sorted({r['target'] for r in rows}):
            ranked = [r for r in rows if r['target'] == target and 'time_ratio' in r.get(key, {})]
            if ranked:
                print(f'{args.profile}/{target}: {key}')
                for row in sorted(ranked, key=lambda r: r[key]['time_ratio'], reverse=True)[:max(0, args.top)]:
                    materialized = (f" ({row['median_ns_per_materialized_value']:.4f} ns/value)"
                                    if row['operation'] == 'get16' else '')
                    print(f"{row[key]['time_ratio']:7.3f}x {row['median_ns_per_query']:.4f} ns/query"
                          f"{materialized}  {row['case']}")


if __name__ == '__main__':
    main()
