#!/usr/bin/env python3
"""Count selection paths in separate frozen builds; these are untimed logical proofs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
STUDY = Path('workbench/spikes/allocation-reuse-temperature')
ALLOCATION = Path('orbital/src/allocation')
TARGET = 'allocation_reuse_temperature_allocator'
sys.path.insert(0, str(ROOT / 'workbench/tools'))
import capture

HEADER = r'''#pragma once
#include <cstdint>
#include <cstdio>
namespace reuse_diagnostics {
// Ordinary counters: all updates occur in owner-lane selection, never helpers.
struct Counts {
  std::uint64_t cache_hits{}, fallback_entries{}, group_visits[4]{}, selected_pass[4]{};
  std::uint64_t window_visits{}, window_extra_visits{}, compatible_visits{}, compatible_extra_visits{};
};
inline Counts counts;
inline void reset() { counts = {}; }
inline void emit(const char* workload, std::uint64_t allocations, std::uint64_t reuses) {
  const auto& c = counts;
  std::fprintf(stderr, "REUSE_DIAGNOSTICS {\"schema\":1,\"workload\":\"%s\","
      "\"finite_cache_hits\":%llu,\"finite_fallback_entries\":%llu,"
      "\"finite_group_visits_per_pass\":[%llu,%llu,%llu,%llu],"
      "\"finite_selected_per_pass\":[%llu,%llu,%llu,%llu],"
      "\"window_candidate_visits\":%llu,\"window_extra_visits_after_first_ready\":%llu,"
      "\"window_compatible_visits\":%llu,\"window_compatible_extra_visits_after_first_ready\":%llu,"
      "\"allocations\":%llu,\"reuses\":%llu,\"correctness_verified\":true,\"shutdown_charge\":0}\n",
      workload, (unsigned long long)c.cache_hits, (unsigned long long)c.fallback_entries,
      (unsigned long long)c.group_visits[0], (unsigned long long)c.group_visits[1],
      (unsigned long long)c.group_visits[2], (unsigned long long)c.group_visits[3],
      (unsigned long long)c.selected_pass[0], (unsigned long long)c.selected_pass[1],
      (unsigned long long)c.selected_pass[2], (unsigned long long)c.selected_pass[3],
      (unsigned long long)c.window_visits, (unsigned long long)c.window_extra_visits,
      (unsigned long long)c.compatible_visits, (unsigned long long)c.compatible_extra_visits,
      (unsigned long long)allocations, (unsigned long long)reuses);
}
}
'''
DRIVER = '''void run(const Options& options, const char* name, Workload workload) {
  Run state(options, workload);
  for (std::size_t i = 0; i < options.warmup; ++i) {
    const auto first = state.sequence;
    state.block(); state.verify_block(first);
  }
  reuse_diagnostics::reset();
  const auto before = alloc::statistics(*state.worker.memory);
  const auto first = state.sequence;
  state.block(); state.verify_block(first);
  const auto after = alloc::statistics(*state.worker.memory);
  check(after.allocations - before.allocations == options.operations, "diagnostic allocation count differs");
  state.shutdown();
  reuse_diagnostics::emit(name, after.allocations - before.allocations, after.reuses - before.reuses);
}
'''


def replace(text, old, new):
    if text.count(old) != 1:
        raise ValueError('diagnostic source shape changed: ' + old[:100])
    return text.replace(old, new)


def instrument(source):
    group = (source / ALLOCATION / 'group.cpp').read_text()
    start, end = group.index('Result<void> finite_allocate('), group.index('Result<bool> relocate_finite(')
    body = group[start:end]
    body = replace(body, '  if (*cached) {', '  if (*cached) {\n    ++reuse_diagnostics::counts.cache_hits;')
    body = replace(body, '  for (unsigned pass =',
                   '  if (!selected) ++reuse_diagnostics::counts.fallback_entries;\n  for (unsigned pass =')
    body = replace(body, 'group = group->next_available) {',
                   'group = group->next_available) {\n      ++reuse_diagnostics::counts.group_visits[pass];')
    body = replace(body, '        prepared = ready;',
                   '        ++reuse_diagnostics::counts.selected_pass[pass];\n        prepared = ready;')
    group = '#include "reuse_diagnostics.h"\n' + group[:start] + body + group[end:]
    window = (source / ALLOCATION / 'window.cpp').read_text()
    start, end = window.index('Window* select_idle_window('), window.index('Result<void> window_allocate(')
    body = replace(window[start:end], '  Window* selected = nullptr;',
                   '  bool seen_ready = false;\n  Window* selected = nullptr;')
    body = replace(body, '    if (window->reserved < reserved ||',
                   '    ++reuse_diagnostics::counts.window_visits;\n'
                   '    reuse_diagnostics::counts.window_extra_visits += seen_ready;\n'
                   '    if (window->reserved < reserved ||')
    body = replace(body, '    const auto extra =',
                   '    ++reuse_diagnostics::counts.compatible_visits;\n'
                   '    reuse_diagnostics::counts.compatible_extra_visits += seen_ready;\n    const auto extra =')
    body = replace(body, 'prepared - window->charged : 0;',
                   'prepared - window->charged : 0;\n'
                   '    if (!extra && prepared <= window->prepared) seen_ready = true;')
    window = '#include "reuse_diagnostics.h"\n' + window[:start] + body + window[end:]
    driver = (source / STUDY / 'allocator.cpp').read_text()
    start, end = driver.index('void run(const Options& options,'), driver.index('} // namespace\n\nint main(')
    driver = '#include "../../../orbital/src/allocation/reuse_diagnostics.h"\n' + driver[:start] + DRIVER + driver[end:]
    return {ALLOCATION / 'group.cpp': group, ALLOCATION / 'window.cpp': window,
            ALLOCATION / 'reuse_diagnostics.h': HEADER, STUDY / 'allocator.cpp': driver}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(command, log):
    with log.open('w') as stream:
        subprocess.run(list(map(str, command)), stdout=stream, stderr=subprocess.STDOUT, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparison', type=Path, required=True)
    parser.add_argument('--output', type=Path)
    for name in ('workloads', 'touch-bytes', 'operations', 'slots', 'cpu'):
        help_text = {'workloads': 'defaults to comparison cases; all runs all nine distinct cases',
                     'touch-bytes': 'one width: 64/full; defaults to 64 when the comparison has several widths'}
        parser.add_argument('--' + name, type=int if name in ('operations', 'slots', 'cpu') else str,
                            help=help_text.get(name))
    args = parser.parse_args()
    comparison = args.comparison.resolve()
    context = json.loads((comparison / 'context.json').read_text())
    settings = context['settings']
    options = {name: getattr(args, name) if getattr(args, name) is not None else settings.get(name)
               for name in ('workloads', 'touch_bytes', 'operations', 'slots', 'cpu')}
    options['workloads'] = options['workloads'] or 'finite-control,finite-fallback,window-locality'
    options['touch_bytes'] = options['touch_bytes'] or '64'
    if args.touch_bytes is None:
        widths = str(options['touch_bytes']).split(',')
        options['touch_bytes'] = '64' if '64' in widths else widths[0]
    if options['touch_bytes'] == '0':
        options['touch_bytes'] = 'full'
    if options['touch_bytes'] not in ('64', 'full') or min(options['operations'], options['slots']) <= 0 or options['slots'] > 65536:
        parser.error('touch bytes must be 64/full; operations/slots positive; slots <=65536')
    out = (args.output or comparison / ('diagnostics-' + options['touch_bytes'])).resolve()
    out.mkdir(parents=True, exist_ok=False)
    proof = {'schema': 1, 'scope': 'Untimed logical selection-path proof; no timing claim.',
             'command': sys.argv, 'helper_sha256': digest(Path(__file__)),
             'options': options, 'comparison_touch_bytes': settings.get('touch_bytes'),
             'touch_selection': 'explicit single width' if args.touch_bytes is not None else 'derived; prefer 64 among comparison widths',
             'warmup_blocks': settings.get('warmup', 3), 'diagnostic_blocks_per_case': 1, 'policies': {}}
    for policy in context['source_manifests']:
        if policy not in ('baseline', 'window-first-ready', 'finite-two-pass', 'combined'):
            parser.error('diagnostics supports the baseline and simplification policies only')
        source = comparison / ('source-' + policy)
        receipt = source.with_name(source.name + '.capture.json')
        metadata = capture.verify(source)
        binary = comparison / ('build-' + policy) / STUDY / TARGET
        frozen = {'receipt': str(receipt), 'receipt_sha256': digest(receipt),
                  'source_digest': metadata['source_digest'], 'binary_sha256': digest(binary)}
        draft = out / ('draft-' + policy)
        replacements = []
        for path, text in instrument(source).items():
            input_path = draft / path
            input_path.parent.mkdir(parents=True, exist_ok=True)
            input_path.write_text(text)
            replacements.append(str(path) + '=' + str(input_path))
        diagnostic = out / ('source-' + policy)
        captured = capture.capture(source, diagnostic, replacements=replacements)
        build = out / ('build-' + policy)
        cache = dict(line.split('=', 1) for line in (comparison / ('build-' + policy) / 'CMakeCache.txt').read_text().splitlines()
                     if '=' in line and not line.startswith(('//', '#')))
        inherited = ['-D' + key.split(':')[0] + '=' + value for key, value in cache.items()
                     if key.split(':')[0] in ('CMAKE_CXX_COMPILER', 'CMAKE_BUILD_TYPE', 'CMAKE_CXX_FLAGS',
                                             'CMAKE_CXX_FLAGS_RELEASE', 'SIXDB_MARCH', 'SIXDB_TUNE')]
        run(['cmake', '-S', diagnostic, '-B', build, '-G', 'Ninja',
             '-DSIXDB_SPIKES=allocation-reuse-temperature', *inherited], out / (policy + '-configure.log'))
        run(['cmake', '--build', build, '--target', TARGET, '-j', os.environ.get('SIXDB_BUILD_JOBS', '4')],
            out / (policy + '-build.log'))
        diagnostic_binary = build / STUDY / TARGET
        command = [str(diagnostic_binary), '--rounds', '1', '--warmup', str(settings.get('warmup', 3))]
        for name, value in options.items():
            command += ['--' + name.replace('_', '-'), str(value)]
        result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, check=True)
        (out / (policy + '-run.log')).write_text(result.stderr)
        rows = [json.loads(line.removeprefix('REUSE_DIAGNOSTICS ')) for line in result.stderr.splitlines()
                if line.startswith('REUSE_DIAGNOSTICS ')]
        if not rows or any(row['schema'] != 1 or row['allocations'] != options['operations'] or
                           not row['correctness_verified'] or row['shutdown_charge'] != 0 for row in rows):
            raise ValueError('missing or invalid diagnostic proof for ' + policy)
        if 'all' not in options['workloads'].split(',') and [row['workload'] for row in rows] != options['workloads'].split(','):
            raise ValueError('diagnostic cases differ from requested workloads: ' + policy)
        capture.verify(source)
        capture.verify(diagnostic)
        if frozen['receipt_sha256'] != digest(receipt) or frozen['binary_sha256'] != digest(binary):
            raise ValueError('frozen comparison changed: ' + policy)
        proof['policies'][policy] = {'frozen': frozen, 'diagnostic_receipt': str(diagnostic.with_name(diagnostic.name + '.capture.json')),
                                    'diagnostic_source_digest': captured['source_digest'],
                                    'diagnostic_binary_sha256': digest(diagnostic_binary),
                                    'finite_pass_order': ['prepared', 'unprepared'] if policy in ('finite-two-pass', 'combined') else
                                        ['prepared matching hints', 'prepared other hints', 'unprepared matching hints', 'unprepared other hints'],
                                    'cases': rows}
        print('Verified untimed paths', policy, flush=True)
    (out / 'paths.json').write_text(json.dumps(proof, indent=2) + '\n')
    print(out / 'paths.json')


if __name__ == '__main__':
    main()
