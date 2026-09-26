#!/usr/bin/env python3
"""Verify source identity and retain selected comparisons, including failures."""
import argparse
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DEST = HERE / "evidence/20260926"
INPUTS = {
    "comparison": "build/workbench/orbital-dissemination/selected.json",
    "prefix": "build/workbench/orbital-dissemination/prefix.json",
    "relays": "build/workbench/orbital-dissemination/relays.json",
    "adaptation": "build/workbench/orbital-dissemination/adaptation.json",
    "mixed": "build/workbench/orbital-dissemination/mixed.json",
    "enhancement": "build/orbital-dissemination/enhancement-selected.json",
    "edge": "build/workbench/orbital-dissemination/edge.json",
    "proposal": "build/workbench/orbital-dissemination/proposal.json",
    "transport": "build/workbench/orbital-dissemination/transport.json",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(document):
    hashes = document.get("sources", document.get("source_hashes_before", {}))
    if not hashes:
        raise ValueError("missing executable source hashes")
    if "source_hashes_after" in document and document["source_hashes_after"] != hashes:
        raise ValueError("source changed during experiment")
    for name, expected in hashes.items():
        if digest(HERE / name) != expected:
            raise ValueError("evidence source mismatch: " + name)
    if "retention" in document:
        if document["retention"]["script_sha256"] != digest(Path(__file__)):
            raise ValueError("retention script changed")


def compact_resources(row):
    resources = row.pop("resources", {})
    if resources:
        row["resource_summary"] = {
            "elapsed_service_us_by_kind": {
                kind: sum(r["service_us"] for name, r in resources.items() if name.endswith(":"+kind))
                for kind in ("cpu", "tx", "rx", "memory", "disk", "disk_bytes")},
            "peak_resource_bytes": max(r["high_bytes"] for r in resources.values()),
            "outstanding_resource_bytes": sum(r["outstanding_bytes"] for r in resources.values()),
            "largest_mean_waits": sorted((dict(name=n, **r) for n, r in resources.items()),
                key=lambda r: (r["mean_wait_us"], r["name"]), reverse=True)[:3],
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    for name, source in INPUTS.items():
        path = DEST / (name + ".json") if args.verify_only else ROOT / source
        document = json.loads(path.read_text())
        verify(document)
        if not args.verify_only:
            if name in ("edge", "proposal"):
                for row in document["results"]:
                    compact_resources(row)
                if name == "edge":
                    tariffs = document["results"][0]["edge_tariffs"]
                    for row in document["results"]:
                        if row.pop("edge_tariffs") != tariffs:
                            raise ValueError("tariffs vary; cannot hoist common table")
                    document["common_edge_tariffs"] = tariffs
            document["retention"] = dict(
                script_sha256=digest(Path(__file__)), input=source,
                full_input_sha256=digest(path),
                selection="Every selected case retained including failure. Edge/proposal resource tables summarized; identical edge tariffs stored once. Other studies unchanged.")
            DEST.mkdir(parents=True, exist_ok=True)
            (DEST / (name + ".json")).write_text(json.dumps(document, indent=2, sort_keys=True)+"\n")
        print(("verified " if args.verify_only else "retained ") + name)


if __name__ == "__main__":
    main()
