"""Regenerate a compact comparison; all timings are modeled, never wall time."""
import argparse
import hashlib
import json
from pathlib import Path

from experiments import campaign, run
from membership_scenario import run_membership
from read_scenario import campaign as read_campaign


def compact(row):
    kept = {k: v for k, v in row.items() if k not in (
        "resources", "resource_bytes_at_cutoff", "traces", "routing", "by_producer")}
    if "resources" in row:
        resources = row["resources"]
        kept["largest_queue_waits"] = sorted(
            (dict(name=n, **v) for n, v in resources.items()),
            key=lambda r: (r.get("mean_wait_us", 0), r["name"]), reverse=True)[:3]
        if "resource_bytes_at_cutoff" in row:
            kept["total_resource_bytes_at_cutoff"] = sum(row["resource_bytes_at_cutoff"].values())
    if row.get("config", {}).get("faults"):
        kept["by_producer"] = row.get("by_producer", {})
    if row.get("config", {}).get("name", "").startswith("write-"):
        kept["first_trace"] = row.get("traces", {}).get("0")
    if "routing" in row:
        kept["routing"] = row["routing"]
    return kept


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--count", type=int, default=1000)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).parent
    dependencies = ["run_study.py", "experiments.py", "simulator.py", "routing.py",
                    "delivery.py", "membership_scenario.py", "read_scenario.py", "read_probe.py"]
    def hashes():
        return {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in dependencies}
    sources = hashes()
    rows = []
    configurations = campaign(args.count)
    for policy in ("direct", "edmonds", "shortest", "family"):
        configurations.append(dict(name=f"one-crossing-{policy}", kind="messages", count=args.count,
                                   senders=2, receivers=16, receiver_zone="b", size=256,
                                   policy=policy, rate=20000))
    for latency in (8, 20, 40, 80):
        configurations.append(dict(name=f"same-az-sensitivity-{latency}", count=args.count,
                                   admission="pipelined_candidate", same_az_us=latency))
    for i, cfg in enumerate(configurations):
        rows.append(compact(run(cfg)))
        print(f"{i + 1}/{len(configurations)} {cfg['name']}", flush=True)
    rows.extend(compact(r) for r in read_campaign(max(20, args.count // 10)))
    joins = [dict(name="join-healthy"),
             dict(name="join-partition", faults=[dict(kind="partition", src="p", dst="j",
                                                       at_us=500, until_us=1400)]),
             dict(name="join-crash", faults=[dict(kind="crash", node="j", at_us=500, until_us=1400)]),
             dict(name="join-retention", retention_events=16),
             dict(name="join-lost-receipts", retention_events=16,
                  faults=[dict(kind="partition", src="j", dst="p", at_us=0)])]
    for cfg in joins:
        cfg.update(count=80, rate=50000, snapshot_bytes=32768)
        rows.append(compact(run_membership(cfg)))
    if hashes() != sources:
        raise RuntimeError("source changed during study; do not retain mixed-source results")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(
        kind="authored_synthetic_comparison_not_measured_performance", sources=sources,
        selection="All campaign cases, read cases and five join outcomes; no successful-only filtering.",
        records_per_main_case=args.count, cases=rows), indent=2, sort_keys=True) + "\n")
    print(f"{len(rows)} comparisons -> {args.output}")


if __name__ == "__main__":
    main()
