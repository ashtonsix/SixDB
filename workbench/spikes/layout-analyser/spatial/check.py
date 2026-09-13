#!/usr/bin/env python3
"""Check physical layout, real dependent traversal and reported accounting."""
import argparse
import csv
import io
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary')
    p.add_argument('--cpu', type=int, required=True)
    args = p.parse_args()
    base = [args.binary, '--cpu', str(args.cpu)]
    subprocess.run(base + ['--check'], check=True)
    for line in [64, 128]:
        extra = [] if line == 64 else ['--compute', '3', '--ordered', '--prefetch-extension']
        run = subprocess.run(base + ['--rows', '128', '--reps', '1', '--ms', '.05', '--line', str(line)] + extra,
                             check=True, capture_output=True, text=True)
        rows = list(csv.DictReader(io.StringIO(run.stdout)))
        assert len(rows) == 27
        assert len({r['order_fingerprint'] for r in rows}) == 1
        digests = {}
        for r in rows:
            assert int(r['compute_rounds']) == (0 if line == 64 else 3)
            assert r['pattern'] == ('random' if line == 64 else 'ordered')
            assert int(r['prefetch_extension']) == (line == 128)
            n = int(r['operations'])
            passes = int(r['passes'])
            denominator = int(r['extension_denominator'])
            extension = 0 if not denominator else n // denominator
            assert n == 128 * passes and int(r['extension_operations']) == extension
            assert int(r['useful_bytes']) == 64 * n + 32 * extension
            assert abs(float(r['ns_per_operation']) - int(r['elapsed_ns']) / n) < 1e-6
            digest = int(r['logical_checksum'])
            assert digests.setdefault(denominator, digest) == digest
            assert int(r['checksum']) == digest * passes % 2**64
            if r['layout'] == 'dense96' and denominator == 0:
                assert int(r['model_lines']) / n == (1.5 if line == 64 else 1.25)
            if r['layout'] == 'dense96' and denominator == 1:
                assert int(r['model_lines']) / n == (2 if line == 64 else 1.5)
            if r['layout'] == 'padded128' and denominator == 1:
                assert int(r['model_lines']) / n == (2 if line == 64 else 1)
        assert 'numa ' in run.stderr and 'KernelPageSize:' in run.stderr
    for bad in [['--rows', '33'], ['--phase', '1'], ['--line', '96'], ['--compute', '65'], ['--reps', '0']]:
        run = subprocess.run(base + bad, capture_output=True)
        assert run.returncode != 0
    print('CSV accounting, native 64/128B geometry, placement receipts and invalid-input checks passed.')


if __name__ == '__main__':
    main()
