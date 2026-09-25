#!/usr/bin/env python3
"""Regenerate the small comparison behind FINDINGS.md, not a general benchmark."""

import argparse
import json
from pathlib import Path

from run import compare, model_identity
from model import LEGACY_POLICIES
from scenarios import presets


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for name, scenario in presets().items():
        if name not in ("discovery", "cycle", "reservation", "readers", "convoy", "hotspot", "spread"):
            continue
        for row in compare(scenario, policies=LEGACY_POLICIES)["comparisons"]:
            rows.append({"scenario": name, **row})
    evidence = {"description": "Synthetic finite workloads; no measured performance or liveness proof.",
                "model_files_sha256": model_identity(), "max_steps": 2000, "comparisons": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n")
    print(f"Wrote {len(rows)} policy/scenario comparisons to {args.output}")


if __name__ == "__main__":
    main()
