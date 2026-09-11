#!/usr/bin/env python3
"""Compatibility entry: retain old Calico defaults and benchmark output paths."""
from pathlib import Path
import importlib.util

path = Path(__file__).resolve().parents[4] / 'workbench/benchmarks/seriespack/run.py'
spec = importlib.util.spec_from_file_location('seriespack_runner', path)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

if __name__ == '__main__':
    raise SystemExit(runner.main(legacy=True))
