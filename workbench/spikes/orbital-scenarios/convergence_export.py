#!/usr/bin/env python3
"""Retain all new semantic cases and locality outcomes with replay sources.

The original 1,783-run comparison remains a separately retained experiment.
Intentional negative controls remain labeled. Only locality's repeated input
payloads are omitted from the compact selection; raw files stay in the bundle.
"""

import argparse
from copy import deepcopy
import hashlib
import json
from pathlib import Path
import shutil
import sys


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    names = ("dynamic-sql", "envelope-audit", "composition", "envelope-locality")
    entries = {name: json.loads((args.input / (name + ".json")).read_text()) for name in names}
    sources, inputs = {}, {}
    for name, data in entries.items():
        inputs[name] = digest(args.input / (name + ".json"))
        for source, expected in data.get("source_sha256", data.get("sources", {})).items():
            assert digest(root / source) == expected, f"{name}: source changed: {source}"
            assert source not in sources or sources[source] == expected
            sources[source] = expected
    for name in ("convergence_export.py", "check_envelope_audit.py"):
        sources[name] = digest(root / name)

    out = args.output
    replay = out / "replay"
    code = replay / "workbench/spikes/orbital-scenarios"
    raw = replay / "build/orbital-convergence"
    code.mkdir(parents=True, exist_ok=True)
    raw.mkdir(parents=True, exist_ok=True)
    for source, expected in sources.items():
        shutil.copyfile(root / source, code / source)
        assert digest(code / source) == expected
    for name, data in entries.items():
        source = args.input / (name + ".json")
        shutil.copyfile(source, raw / source.name)
        assert digest(raw / source.name) == inputs[name]
        compact = deepcopy(data)
        if name == "envelope-locality":
            for row in compact["rows"]:
                assert row["serial_check"]
                for cohort in row["cohorts"].values():
                    assert cohort["complete"] == cohort["offered"]
                    assert not cohort["failed"] and not cohort["pending"]
                del row["input"]
        write_json(out / source.name, compact)

    manifest = {
        "source_sha256": sources,
        "raw_sha256": inputs,
        "selection": "All 13 application, 42 audit and 10 composition histories, including expected invalid controls; all 9 locality runs and every cohort. Repeated locality inputs are in replay/build/orbital-convergence.",
        "units": "Authored logical histories and synthetic ticks/service costs, not measured database performance.",
        "export_command": ["python3", "workbench/spikes/orbital-scenarios/convergence_export.py", *sys.argv[1:]],
        "regenerate_from_replay_root": [
            "python3 workbench/spikes/orbital-scenarios/check_dynamic_sql.py",
            "python3 workbench/spikes/orbital-scenarios/check_envelope_audit.py",
            "python3 workbench/spikes/orbital-scenarios/dynamic_sql.py --output build/repeated/dynamic-sql.json",
            "python3 workbench/spikes/orbital-scenarios/envelope_audit.py --output build/repeated/envelope-audit.json",
            "python3 workbench/spikes/orbital-scenarios/composition_probe.py --output build/repeated/composition.json",
            "python3 workbench/spikes/orbital-scenarios/envelope_locality.py --output build/repeated/envelope-locality.json",
            "python3 workbench/spikes/orbital-scenarios/convergence_export.py --input build/orbital-convergence --output build/repeated/export",
        ],
    }
    write_json(out / "study.json", manifest)
    for source, expected in sources.items():
        assert digest(root / source) == expected, f"source changed during export: {source}"
    print(json.dumps({"sources": len(sources), "selected_files": 5,
                      "locality_runs": len(entries["envelope-locality"]["rows"])}))


if __name__ == "__main__":
    main()
