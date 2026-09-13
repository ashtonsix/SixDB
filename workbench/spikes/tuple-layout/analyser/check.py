#!/usr/bin/env python3
"""Compatibility entry point; implementation lives with the layout-analyser spike."""
from pathlib import Path
import runpy
import sys

if __name__ == "__main__":
    target = Path(__file__).resolve().parents[2] / "layout-analyser/tuplepack-reference"
    sys.path.insert(0, str(target))
    runpy.run_path(str(target / "check.py"), run_name="__main__")
