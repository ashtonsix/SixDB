#!/usr/bin/env python3
"""Keep selected comparisons; worker archives retain exact sources and binaries."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from report import summarize

ROOT = Path(__file__).resolve().parents[4]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('selector_job')
    p.add_argument('tail_job')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--evidence', type=Path, required=True)
    args = p.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sources = {}
    for kind, job in [('selector', args.selector_job), ('tail', args.tail_job)]:
        source = ROOT / 'build/workers' / job
        results = source / 'results'
        status = json.loads((results / 'worker-result.json').read_text())
        if status['state'] != 'complete' or status['script_returncode'] != 0:
            raise ValueError(f'{job} is not a complete successful run')
        target = out / kind
        target.mkdir()
        for name in ['artifact.json']:
            shutil.copy2(source / name, target / ('worker-' + name))
        shutil.copy2(results / 'host.json', target / 'host.json')
        manifest = json.loads((results / 'source-manifest.json').read_text())
        job_record = json.loads((results / 'job.json').read_text())
        source_digest = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
        if source_digest != job_record['source']['digest']:
            raise ValueError(f'{job} source manifest differs from its recorded identity')
        sources[kind] = dict(job=job, archive=json.loads((source / 'artifact.json').read_text()),
                             source_digest=source_digest, result=status)
        if kind == 'selector':
            for name in ['samples.csv', 'context.json']:
                shutil.copy2(results / 'comparison' / name, target / name)
            summarize(target)
        else:
            rows = []
            for path in sorted(results.glob('tail-*-*.csv')):
                trial = int(path.name.split('-')[1])
                rows.extend(dict(trial=trial, **row) for row in csv.DictReader(path.open()))
            if len(rows) != 216:
                raise ValueError('expected twelve complete processes per mode, six stages each')
            with (target / 'samples.csv').open('w') as stream:
                writer = csv.DictWriter(stream, list(rows[0])); writer.writeheader(); writer.writerows(rows)
    context = dict(scope='Selector block means and separate synchronous private-tail residency episodes; no integrated policy or multicore claim.',
                   source_runs=sources,
                   analysis_sha256={name: hashlib.sha256(Path(__file__).with_name(name).read_bytes()).hexdigest()
                                    for name in ['retain.py', 'report.py']})
    (out / 'context.json').write_text(json.dumps(context, indent=2) + '\n')
    command = [sys.executable, str(ROOT / 'workbench/tools/artifacts.py'), 'retain', str(out), str(args.evidence)]
    for name in ['context.json', 'selector/comparisons.csv', 'selector/trials.csv',
                 'selector/host.json', 'selector/worker-artifact.json',
                 'tail/samples.csv', 'tail/host.json', 'tail/worker-artifact.json']:
        command += ['--file', name]
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
