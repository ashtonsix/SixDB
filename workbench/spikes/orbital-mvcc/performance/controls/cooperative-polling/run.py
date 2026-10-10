#!/usr/bin/env python3
"""One bounded local panel. No remote runner, retries, or policy selection."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--binary', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--small', action='store_true')
args = parser.parse_args()
args.binary = args.binary.resolve()
args.output.mkdir(parents=True, exist_ok=False)
common = ['--rows', '32768' if args.small else '8388608',
          '--objects', '1' if args.small else '256',
          '--ops', '2051' if args.small else '524291', '--group', '1024']
arms = [('unloaded', 'ring', 128), ('empty', 'ring', 128),
        ('loaded', 'ring', 128), ('deferred', 'ring', 128),
        ('loaded', 'ring', 32), ('loaded', 'manual', 128)]
manifest = {'binary': str(args.binary), 'binary_sha256': hashlib.sha256(args.binary.read_bytes()).hexdigest(),
            'small': args.small, 'runs': []}
for mode in ('throughput', 'delay'):
    for treatment, execution, quantum in arms:
        if mode == 'delay' and treatment in ('unloaded', 'empty'):
            continue
        name = f'{mode}-{treatment}-{execution}-q{quantum}'
        command = [str(args.binary), *common, '--treatment', treatment,
                   '--execution', execution, '--quantum', str(quantum), '--mode', mode,
                   '--samples', str((args.output / (name + '.csv')).resolve())]
        result = subprocess.run(command, text=True, capture_output=True)
        (args.output / (name + '.json')).write_text(result.stdout)
        (args.output / (name + '.stderr')).write_text(result.stderr)
        manifest['runs'].append({'name': name, 'command': command, 'exit_code': result.returncode})
        (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
        if result.returncode:
            raise SystemExit(f'{name} failed: {result.stderr}')
        row = json.loads(result.stdout)
        print(json.dumps({key: row[key] for key in ('execution', 'treatment', 'mode', 'quantum',
              'all_work_complete_ns', 'read_complete_ns', 'completion_delay_p50_ns', 'completion_delay_max_ns')}), flush=True)
