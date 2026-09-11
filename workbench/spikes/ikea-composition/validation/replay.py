#!/usr/bin/env python3
"""Compatibility entry point; shared replay lives in workbench/tools."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from replay import main

if __name__ == "__main__":
    main()
