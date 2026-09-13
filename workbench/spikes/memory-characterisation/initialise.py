#!/usr/bin/env python3
"""Initialization prototype: exact-context static lookup, otherwise a quick probe."""
import sys
from pathlib import Path
from run import main

if __name__ == '__main__':
    sys.argv[1:1] = ['--lookup', str(Path(__file__).with_name('lookup.json')), '--profile', 'quick']
    raise SystemExit(main())
