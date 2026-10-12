#!/usr/bin/env python3
"""Compile retained, context-qualified observations; never infer from CPU names."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'workbench/spikes/memory-characterisation/lookup.json'
OUTPUT = ROOT / 'loom/src/hardware_table.inc'
raw = SOURCE.read_bytes()
table = json.loads(raw)
assert table['format'] == 1
entries = []
source_digest = hashlib.sha256(raw).hexdigest()
widths = {1, 2, 4, 8, 12, 16, 24, 32, 48, 64}
def width_qualification(mlp):
    candidates = mlp.get('candidate_k', [])
    knees = mlp.get('individual_10pct_k', [])
    if not isinstance(candidates, list) or not isinstance(knees, list):
        return 'weak'
    if len(knees) < 2:
        return 'incomplete'
    if any(type(k) is not int or k not in widths for k in knees + candidates):
        return 'weak'
    aggregate_censored = mlp.get('right_censored', False)
    if type(aggregate_censored) is not bool:
        return 'weak'
    # Only an actual tested upper-limit knee establishes right censoring.
    # lookup.py's aggregate flag means ALL seeds; native reuse rejects ANY.
    if max(widths) in knees:
        return 'censored'
    if aggregate_censored:
        return 'weak'  # Inconsistent summary, not evidence at the sweep limit.
    if mlp.get('status') == 'insufficient-independent-runs':
        return 'incomplete'
    if mlp.get('status') != 'validated' or not candidates:
        return 'mixed'
    return 'qualified'


def qualified_width(mlp):
    return min(mlp['candidate_k']) if width_qualification(mlp) == 'qualified' else 0

def qualified_forward(observation):
    # lookup.py admits these only after four separated-control observations
    # agree; disagreement across matched contexts is explicitly unresolved.
    return {'observed-benefit': 'ForwardObservation::observed_benefit',
            'no-clear-benefit': 'ForwardObservation::no_observed_benefit'}.get(
                observation, 'ForwardObservation::unknown')

def qualified_prefetch(entry):
    # Protocol 2 fences before conditional stimuli and after cache maintenance.
    # Earlier hot controls could fetch the target on a mispredicted path; their
    # apparent separation cannot qualify either training or adjacent facts.
    protocol = entry.get('prefetch_control_protocol', 0)
    if type(protocol) is not int or protocol != 2:
        return 0, ('ForwardObservation::unknown', 'ForwardObservation::unknown')
    training = entry.get('consistent_training_suffix', {}).get('1')
    if type(training) is not int or not 2 <= training <= 64:
        training = 0
    adjacent = entry.get('adjacent_forward_observation', {})
    return training, tuple(qualified_forward(adjacent.get(f'parity{i}')) for i in range(2))

# Fixed independent expectations for the retained per-seed censoring regression.
assert qualified_width({'status': 'validated', 'candidate_k': [24, 64],
                        'individual_10pct_k': [64, 24], 'right_censored': False}) == 0
assert qualified_width({'status': 'validated', 'candidate_k': [24, 64],
                        'individual_10pct_k': [32, 24], 'right_censored': False}) == 24
# Rejected summaries without a tested upper-limit knee are not censored.
assert width_qualification({'status': 'validated', 'candidate_k': [24]}) == 'incomplete'
assert width_qualification({'status': 'validated', 'candidate_k': [24],
                           'individual_10pct_k': [24]}) == 'incomplete'
assert width_qualification({'status': 'validated', 'candidate_k': [24],
                           'individual_10pct_k': [24, 32], 'right_censored': True}) == 'weak'
assert width_qualification({'status': 'validated', 'candidate_k': [24],
                           'individual_10pct_k': [64.0, 24]}) == 'weak'
assert width_qualification({'status': 'validated', 'candidate_k': [True, 24],
                           'individual_10pct_k': [16, 24]}) == 'weak'
assert width_qualification({'status': 'unresolved-repeatability', 'candidate_k': [24],
                           'individual_10pct_k': [16, 24]}) == 'mixed'
assert width_qualification({'status': 'validated', 'candidate_k': [],
                           'individual_10pct_k': [16, 24]}) == 'mixed'
assert width_qualification({'status': 'validated', 'candidate_k': [24, 64],
                           'individual_10pct_k': [64, 24], 'right_censored': False}) == 'censored'
assert qualified_forward('observed-benefit') == 'ForwardObservation::observed_benefit'
assert qualified_forward('no-clear-benefit') == 'ForwardObservation::no_observed_benefit'
assert qualified_forward('unresolved') == 'ForwardObservation::unknown'
assert qualified_forward(None) == 'ForwardObservation::unknown'
# Frozen independent inputs: convincing old summaries still remain unknown,
# while protocol-2 summaries retain independently resolved parity observations.
frozen_prefetch = {'consistent_training_suffix': {'1': 3},
                   'adjacent_forward_observation': {'parity0': 'observed-benefit',
                                                    'parity1': 'no-clear-benefit'}}
unknown_prefetch = (0, ('ForwardObservation::unknown', 'ForwardObservation::unknown'))
assert qualified_prefetch(frozen_prefetch) == unknown_prefetch
assert qualified_prefetch({**frozen_prefetch, 'prefetch_control_protocol': 1}) == unknown_prefetch
assert qualified_prefetch({**frozen_prefetch, 'prefetch_control_protocol': 2}) == (
    3, ('ForwardObservation::observed_benefit', 'ForwardObservation::no_observed_benefit'))
for malformed_protocol in (None, '2', 2.0, True, {}, []):
    assert qualified_prefetch({**frozen_prefetch, 'prefetch_control_protocol': malformed_protocol}) == unknown_prefetch
assert qualified_prefetch({'prefetch_control_protocol': 2,
                          'consistent_training_suffix': {'1': 1},
                          'adjacent_forward_observation': {'parity0': 'unresolved'}}) == unknown_prefetch
for key, entry in sorted(table['entries'].items()):
    if entry.get('status') != 'validated' or entry.get('probe_format') != 1:
        continue
    context = entry['lookup_context']
    # Prove that the retained index actually identifies its full context.
    assert hashlib.sha256(json.dumps(context, sort_keys=True).encode()).hexdigest() == key
    mlp = entry.get('mlp', {})
    width = qualified_width(mlp)
    training, forward_observations = qualified_prefetch(entry)
    line, page = entry['line_bytes_reported'], context['base_page']
    assert type(line) is int and type(page) is int and 16 <= line <= page
    assert line & (line - 1) == 0 and page & (page - 1) == 0
    signature = json.dumps(context, sort_keys=True, separators=(',', ':'))
    forward = ', '.join(forward_observations)
    captures = entry.get('evidence_source_digests', [])
    assert 2 <= len(captures) <= 4 and all(isinstance(v, str) and len(v) == 64 and
        all(c in '0123456789abcdef' for c in v) for v in captures)
    mask = sum(1 << i for i, value in enumerate(sorted(widths)) if value in mlp.get('candidate_k', []))
    capture_values = ', '.join('"' + value + '"' for value in captures)
    protocol = entry.get('prefetch_control_protocol', 0)
    protocol = protocol if type(protocol) is int and protocol == 2 else 0
    width_reason = width_qualification(mlp)
    metadata = f'{{"{source_digest}", 1, {protocol}, 268435456, {mask}, {{{capture_values}}}, {len(captures)}, HardwareQualification::{width_reason}}}'
    entries.append(f'  {{R"loom_context({signature})loom_context", {{{line}, {page}}}, {width}, {training}, 1, true, {{{forward}}}, {metadata}}}')
OUTPUT.write_text('// Generated by loom/tools/generate_hardware_table.py.\n'
                  '// Source: workbench/spikes/memory-characterisation/lookup.json\n'
                  f'// Source SHA256: {hashlib.sha256(raw).hexdigest()}\n'
                  '// Isolated measured candidates; no exact hardware resource counts.\n'
                  '// Prefetch facts require corrected conditional-control protocol 2.\n'
                  f'constexpr std::array<HardwareTableEntry, {len(entries)}> retained_table{{{{\n'
                  + ',\n'.join(entries) + '\n}};\n')
print(f'{len(entries)} retained contexts -> {OUTPUT}')
