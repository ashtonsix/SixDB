#!/usr/bin/env python3
"""Offline tables from the completed discovery panel; no new campaign dispatch."""
import argparse
import csv
import json
import statistics
from pathlib import Path


def report(samples,out):
    fields=['panel','rows','objects','ops','execution','width','hints','batch','age','mode','repetition','accounting','wall_ns','ns_per_request','caller_cpu_ns','process_cpu_ns','digest','completed','logical_rows','publications','prefix_copied_bytes','requested_body_bytes','tail_encoded_values','release_ns','held_live_capacity','released_live_capacity','held_versions','released_versions','metadata_peak','pool_reuses','driver_record_bytes','ring_arena_charge','returned_quanta','ring_visits']
    with (out/'measurements.csv').open('w') as f:
        writer=csv.DictWriter(f,fields);writer.writeheader()
        for case,data in samples:
            row={k:case.get(k,'') for k in fields}
            if case['panel']=='ring' and case['execution']=='fused':
                phase=data['interval']; row.update(wall_ns=phase['wall_ns'],caller_cpu_ns=phase['caller_cpu_ns'],process_cpu_ns=phase['process_cpu_ns'],completed=phase['logical_requests'],logical_rows=phase['logical_rows'])
                row['digest']=data['outcome_digest']
            else:
                row.update({k:data.get(k,'') for k in fields if k not in case})
                if case['panel']=='append':
                    row.update(held_live_capacity=data['held']['live_capacity_bytes'],released_live_capacity=data['released']['live_capacity_bytes'],held_versions=data['held']['versions'],released_versions=data['released']['versions'],metadata_peak=data['released']['metadata_peak'],pool_reuses=data['released']['pool_reuses'])
            row['ns_per_request']=float(row['wall_ns'])/case['ops'] if not case.get('accounting') else ''
            writer.writerow(row)
    rows=list(csv.DictReader((out/'measurements.csv').open()));groups={}
    keys=['panel','rows','objects','ops','execution','width','hints','batch','age','mode','accounting']
    for row in rows:groups.setdefault(tuple(row[k] for k in keys),[]).append(row)
    with (out/'summary.csv').open('w') as f:
        writer=csv.DictWriter(f,keys+['observations','ns_per_request_median','ns_per_request_observations']);writer.writeheader()
        for key,values in groups.items():
            times=[float(v['ns_per_request']) for v in values if v['ns_per_request']]
            writer.writerow(dict(zip(keys,key))|dict(observations=len(values),ns_per_request_median=statistics.median(times) if times else '',ns_per_request_observations=json.dumps(times)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report-only', type=Path, required=True, metavar='COLLECTED_RUN')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report(json.loads((args.report_only / 'samples.json').read_text()), args.output)


if __name__ == '__main__':
    main()
