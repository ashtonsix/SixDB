#!/usr/bin/env python3
"""Compact completed Ninja edges and ELF sections; raw evidence stays in results."""
import argparse
from collections import Counter
import json
from pathlib import Path
import re
import subprocess
import tempfile


def ninja_rows(path):
    if not path.exists():
        return []
    return [line for line in path.read_text().splitlines() if line and not line.startswith('#')]


def build_summary(results):
    before = Counter(ninja_rows(results / 'build-before.ninja_log'))
    completed = []
    for line in ninja_rows(results / 'build-after.ninja_log'):
        if before[line]:
            before[line] -= 1
            continue
        start, end, _, output, _ = line.split('\t')
        completed.append({'output': output, 'seconds': (int(end) - int(start)) / 1000})
    report = {'limits': 'Completed edges from this invocation only. Failed edges have no duration; '
              'overlapping edge durations are not wall time. GNU time RSS is a process maximum, '
              'not aggregate concurrent memory.',
              'build_state': (results / 'build-state.txt').read_text().strip(),
              'completed_edges': sorted(completed, key=lambda row: row['seconds'], reverse=True)}
    for phase in ['configure', 'build']:
        path = results / f'{phase}.resources'
        if path.exists():
            report[phase] = dict(line.split('=', 1) for line in path.read_text().splitlines() if '=' in line)
    (results / 'build-summary.json').write_text(json.dumps(report, indent=2) + '\n')


def sizes(paths, results):
    report = []
    for path in paths:
        sections = subprocess.check_output(['llvm-size-21', '-A', str(path)], text=True)
        (results / f'{path.name}.sections.txt').write_text(sections)
        totals = Counter()
        for line in sections.splitlines():
            match = re.match(r'^([.\w][^\s]*)\s+(\d+)\s+\d+\s*$', line)
            if not match:
                continue
            name, size = match[1], int(match[2])
            if name.startswith(('.debug', '.zdebug')):
                group = 'debug'
            elif name.startswith(('.text', '.init', '.fini', '.plt')) and not name.endswith('_array'):
                group = 'code'
            elif name.startswith('.rodata'):
                group = 'read_only_data'
            elif name.startswith(('.data', '.bss', '.tdata', '.tbss')):
                group = 'writable_data_and_bss'
            elif name.startswith(('.eh_frame', '.gcc_except_table')):
                group = 'unwind'
            else:
                group = 'other'
            totals[group] += size
        row = {'artifact': path.name, 'file_bytes': path.stat().st_size, 'section_bytes': dict(totals)}
        # Strip a disposable copy; preserve the exact measured executable.
        if path.suffix != '.a':
            with tempfile.TemporaryDirectory() as tmp:
                stripped = Path(tmp) / path.name
                subprocess.run(['llvm-strip-21', '--strip-all', '-o', str(stripped), str(path)], check=True)
                row['stripped_file_bytes'] = stripped.stat().st_size
        report.append(row)
    (results / 'code-size.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('results', type=Path)
    parser.add_argument('--sizes', nargs='*', type=Path)
    args = parser.parse_args()
    if args.sizes is None:
        build_summary(args.results)
    else:
        sizes(args.sizes, args.results)
