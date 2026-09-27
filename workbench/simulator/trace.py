"""Independent, streaming trace tools. No import of the runtime or Orbital fold."""
from __future__ import annotations

import argparse
import collections
import json
from pathlib import Path


def records(path):
    with Path(path).open() as source:
        for line in source:
            yield json.loads(line)


def summary(path):
    """Conservation and causal-shape checks need only bounded live state."""
    counts = collections.Counter()
    live = {}
    peak = collections.Counter()
    held = collections.Counter()
    last_id = 0
    errors = []
    for row in records(path):
        identity, kind = row["id"], row["kind"]
        if identity != last_id + 1:
            errors.append(f"noncontiguous record {identity} after {last_id}")
        if any(parent >= identity or parent < 0 for parent in [row["cause"], *row["parents"]]):
            errors.append(f"invalid causal predecessor at {identity}")
        last_id = identity
        counts[kind] += 1
        if kind == "buffer.allocate":
            key = row["operation"]
            if key in live:
                errors.append(f"duplicate buffer {key}")
            live[key] = row["host"], row["size"]
            held[row["host"]] += row["size"]
            peak[row["host"]] = max(peak[row["host"]], held[row["host"]])
        elif kind == "buffer.retire":
            key = row["operation"]
            old = live.pop(key, None)
            if old != (row["host"], row["size"]):
                errors.append(f"unmatched buffer retirement {key}")
            if old:
                held[old[0]] -= old[1]
    return {"records": last_id, "counts": dict(counts), "buffer_peak": dict(peak),
            "unretired_buffers": len(live), "violations": errors}


def slice_history(path, roots):
    """Two streaming passes: index causal edges, then emit the selected history.

    Deliberately a diagnostic for a selected trace, not the campaign hot path.
    The compact index costs O(records); payloads are never retained in it.
    """
    parents = {r["id"]: [r["cause"], *r["parents"]] for r in records(path)}
    wanted, pending = set(), list(roots)
    while pending:
        identity = pending.pop()
        if not identity or identity in wanted:
            continue
        if identity not in parents:
            raise ValueError(f"missing causal record {identity}")
        wanted.add(identity)
        pending.extend(parents[identity])
    return (r for r in records(path) if r["id"] in wanted)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--slice", nargs="+", type=int, dest="roots")
    args = parser.parse_args()
    if args.roots:
        for row in slice_history(args.trace, args.roots):
            print(json.dumps(row, separators=(",", ":")))
    else:
        result = summary(args.trace)
        print(json.dumps(result, indent=2))
        raise SystemExit(bool(result["violations"]))
