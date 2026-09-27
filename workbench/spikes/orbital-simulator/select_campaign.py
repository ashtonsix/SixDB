"""Retain matched competitors, failure boundaries and search evidence compactly."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def select_traffic(path):
    summary = json.loads((path / "summary.json").read_text())
    ids = set(summary["comparisons"] + summary["controls"])
    for ramp in summary["load"] + summary["health"] + [summary["memory"]]:
        ids.update(r for level in ramp["levels"] for r in level["records"])
    for row in summary["search"]["evaluated"] + summary["search"]["validation"]:
        ids.update(row["records"])
    rows = []
    for identity in sorted(ids):
        record = json.loads((path / (identity + ".json")).read_text())
        observation = record.get("observation")
        row = {k:record[k] for k in ("id", "case", "seed", "status", "exception")}
        if observation:
            row.update(metrics=observation["metrics"], violations=observation["violations"], cohorts={})
            for name, c in observation["cohorts"].items():
                row["cohorts"][name] = {k:c[k] for k in ("offered", "completed", "refused", "unfinished", "completed_in_window")}
                row["cohorts"][name]["oldest_unfinished_ns"] = max(c["unfinished_age_ns"].values(), default=None)
                # The authored transaction generator maps IDs alternately to the
                # two local origins. Preserve collateral harm per origin as well
                # as per application class, without summing overlapping cohorts.
                origin_rows = {}
                for origin in (0, 1):
                    keys = {k for k in c["arrivals_ns"] if (int(k)-1)%2 == origin}
                    latency = [v for k,v in c["latency_ns"].items() if k in keys]
                    origin_rows[str(origin)] = dict(offered=len(keys),
                        completed=sum(k in keys for k in c["completions_ns"]),
                        refused=sum(k in keys for k in c["refusals_ns"]),
                        unfinished=sum(k in keys for k in c["unfinished_age_ns"]),
                        max_latency_ns=max(latency, default=None))
                row["cohorts"][name]["origins"] = origin_rows
        rows.append(row)
    all_records = [json.loads(f.read_text()) for f in path.glob("*.json") if len(f.stem) == 64]
    histories = sum(len(r["case"].get("panel", [None])) for r in all_records)
    return dict(selection="All shape competitors, load/health/memory ramps, semantic controls and search/held-out comparisons; pairwise mixes retained as run identities and verdict totals.",
                summary=summary, simulated_histories=histories, rows=rows,
                omitted_mix_receipts={r["id"]: hashlib.sha256((path / (r["id"] + ".json")).read_bytes()).hexdigest()
                                      for r in all_records if r["id"] not in ids})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("traffic_run", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    value = select_traffic(args.traffic_run)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n")
    print(json.dumps(dict(campaign_records=value["summary"]["runs"],
                         simulated_histories=value["simulated_histories"], retained_rows=len(value["rows"]))))
