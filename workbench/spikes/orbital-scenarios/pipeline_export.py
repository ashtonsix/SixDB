#!/usr/bin/env python3
"""Retain every pipeline comparison and semantic control with frozen sources."""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    names = ('sweep', 'contrasts', 'semantic', 'release-audit')
    sources, raw_hashes = {}, {}
    code = args.output / 'replay/workbench/spikes/orbital-scenarios'
    raw = args.output / 'replay/build/orbital-pipeline'
    code.mkdir(parents=True, exist_ok=True)
    raw.mkdir(parents=True, exist_ok=True)
    run_count = 0
    for name in names:
        path = args.input / (name + '.json')
        data = json.loads(path.read_text())
        identity = data['source_sha256']
        if isinstance(identity, str):
            identity = {'release_after_position_audit.py': identity,
                        'write_admission.py': data['admission_sha256']}
        for source, expected in identity.items():
            assert sha(root / source) == expected, source
            assert source not in sources or sources[source] == expected
            sources[source] = expected
        raw_hashes[path.name] = sha(path)
        shutil.copyfile(path, raw / path.name)
        compact = deepcopy(data)
        for row in compact.get('rows', []):
            assert row['serial_check']
            assert all(c['complete'] + c['failed'] + c['pending'] == c['offered'] for c in row['cohorts'].values())
            del row['input']
            run_count += 1
        (args.output / path.name).write_text(json.dumps(compact, indent=2, sort_keys=True) + '\n')
    for source in ('pipeline_export.py', 'check_pipeline_simulation.py'):
        sources[source] = sha(root / source)
    for source, expected in sources.items():
        shutil.copyfile(root / source, code / source)
        assert sha(code / source) == expected
    study = {'source_sha256': sources, 'raw_sha256': raw_hashes,
        'run_count': run_count,
        'selection': 'All 132 sweep and 16 contrast outcomes/counters, all 7 semantic-core and 17 independent audit histories, including expected invalid controls. Only repeated timing inputs omitted; complete inputs are in the replay bundle.',
        'units': 'Synthetic service/latency units and logical histories; no measured performance.',
        'commands_from_replay_root': [
            'python3 -m unittest discover -s workbench/spikes/orbital-scenarios -p check*.py',
            'python3 workbench/spikes/orbital-scenarios/pipeline_simulation.py --seeds 0,7,19 --output build/repeated/sweep.json',
            'python3 workbench/spikes/orbital-scenarios/pipeline_contrasts.py --output build/repeated/contrasts.json',
            'python3 workbench/spikes/orbital-scenarios/position_pipeline.py --output build/repeated/semantic.json',
            'python3 workbench/spikes/orbital-scenarios/release_after_position_audit.py --output build/repeated/release-audit.json',
            'python3 workbench/spikes/orbital-scenarios/pipeline_export.py --input build/orbital-pipeline --output build/repeated/export']}
    (args.output / 'study.json').write_text(json.dumps(study, indent=2, sort_keys=True) + '\n')
    assert all(sha(root / source) == expected for source, expected in sources.items())
    print(json.dumps({'runs': run_count, 'sources': len(sources)}))


if __name__ == '__main__':
    main()
