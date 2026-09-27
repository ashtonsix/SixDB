#!/usr/bin/env python3
"""Run the comparison through the maintained exact-source TLC runner."""
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
raise SystemExit(subprocess.call([sys.executable, str(ROOT / "orbital/spec/suite.py"),
    "--manifest", str(HERE / "cases.json"), "--tier", "all", *sys.argv[1:]]))
