#!/usr/bin/env python3
"""Summarize a selected metric, preserving machines, cases and repetitions."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import statistics
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('inputs', nargs='+', help='NAME=rows.jsonl')
    parser.add_argument('--case', default='.*', help='case-name regular expression')
    parser.add_argument('--metric', default='ns_per_item', help='ns_per_item, elapsed_ns, items or counter name')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    stream = args.output.open('w', newline='') if args.output else sys.stdout
    writer = csv.writer(stream)
    writer.writerow(['input', 'case', 'metric', 'median', 'min', 'max', 'repetitions',
                     'item_unit', 'items_min', 'items_max', 'input_sha256'])
    for argument in args.inputs:
        name, separator, filename = argument.partition('=')
        if not separator:
            parser.error('inputs must be NAME=rows.jsonl')
        source = Path(filename).read_bytes()
        groups = {}
        for line in source.splitlines():
            row = json.loads(line)
            if not row['verified']:
                raise ValueError('unverified input row: ' + row['case'])
            if re.search(args.case, row['case']):
                groups.setdefault(row['case'], []).append(row)
        for case, rows in sorted(groups.items()):
            if len(set(row['repeat'] for row in rows)) != len(rows):
                raise ValueError('duplicate case/repeat: ' + case)
            if len(set(row['unit'] for row in rows)) != 1:
                raise ValueError('mixed item units: ' + case)
            values = [row['elapsed_ns'] / row['items'] if args.metric == 'ns_per_item'
                      else row[args.metric] if args.metric in ['elapsed_ns', 'items']
                      else row['counters'][args.metric] for row in rows]
            writer.writerow([name, case, args.metric, statistics.median(values), min(values), max(values),
                             len(rows), rows[0]['unit'], min(row['items'] for row in rows),
                             max(row['items'] for row in rows), hashlib.sha256(source).hexdigest()])
    if args.output:
        stream.close()


if __name__ == '__main__':
    main()
