#!/usr/bin/env python3
"""Independent scalar answers, cycle-law coverage and interval accounting."""
import argparse
import json
import os
from pathlib import Path
import subprocess
from run import audit_sample

MASK = (1 << 64) - 1


def mix(x):
    x = ((x ^ (x >> 30)) * 0xbf58476d1ce4e5b9) & MASK
    x = ((x ^ (x >> 27)) * 0x94d049bb133111eb) & MASK
    return x ^ (x >> 31)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    allowed = sorted(os.sched_getaffinity(0))
    numa_available = Path('/proc/self/numa_maps').exists()
    checks = []
    for count in sorted({1, min(2, len(allowed))}):
        cpuset = allowed[:count]
        for size in (8192, 20480):
            for seed in (0, 1729):
                for mode, chains, steady in [(m, k, s) for m, k in [('scan', 1)] + [('chase', n) for n in (1, 4, 16, 32, 64)] for s in (False, True)]:
                    command = [str(a.binary.resolve()), '--check', '--cpus', ','.join(map(str, cpuset)),
                        '--bytes', str(size), '--line', '64', '--seed', str(seed),
                        '--mode', mode, '--chains', str(chains)]
                    if steady: command += ['--steady', '--ms', '5']
                    result = subprocess.run(command, text=True, capture_output=True, timeout=30)
                    assert result.returncode == 0, (command, result.stderr)
                    rows = [json.loads(row) for row in result.stdout.splitlines()]
                    mappings = [row for row in rows if row['type'] == 'mapping']
                    samples = [row for row in rows if row['type'] == 'sample']
                    assert len(mappings) == count and len(samples) == 2
                    assert all('AnonHugePages:' in row['smaps'] for row in mappings)
                    assert all(bool(row['numa_maps']) == numa_available for row in mappings)
                    assert len({row['trace_hash_fnv64'] for row in mappings}) == 1
                    expected_sum = sum(mix(seed + i) for i in range(size // 8)) & MASK
                    for row in samples:
                        audit_sample(row)
                        if mode == 'scan':
                            for w in row['workers']:
                                units = w.get('units', row['units_per_worker'])
                                full, tail = divmod(units, size // 8)
                                expected = (expected_sum * full + sum(mix(seed + i) for i in range(tail))) & MASK
                                assert int(w['answer']) == expected
                    checks.append(dict(cpus=count, size=size, seed=seed, mode=mode, chains=chains,
                        steady=steady, samples=len(samples), verified=True))
    cpu = allowed[0]
    rejected = []
    for extra in [('--cpus', f'{cpu},{cpu}'), ('--cpus', '1048576'),
                  ('--bytes', '8193'), ('--chains', '3'), ('--trials', '3'),
                  ('--mode', 'bogus'), ('--line', '63')]:
        command = [str(a.binary.resolve()), '--cpus', str(cpu), '--check', *extra]
        result = subprocess.run(command, text=True, capture_output=True, timeout=10)
        assert result.returncode != 0, command
        rejected.append(list(extra))
    a.output.write_text(json.dumps(dict(checks=checks, rejected=rejected,
        numa_maps_available=numa_available,
        independent_python_scan_oracle=True, cxx_complete_edge_and_wrapping_cycle_checks=True,
        timing_claim=False), indent=2) + '\n')
    print(f'{len(checks)} independent cases and {len(rejected)} invalid commands passed')


if __name__ == '__main__':
    main()
