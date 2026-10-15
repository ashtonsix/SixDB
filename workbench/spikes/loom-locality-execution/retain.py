#!/usr/bin/env python3
"""Keep comparison cells and compact receipts; recover bulky proof via archive."""
import argparse
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("root", type=Path)
parser.add_argument("output", type=Path)
campaign = parser.add_mutually_exclusive_group()
campaign.add_argument("--followup", action="store_true")
campaign.add_argument("--graduation", action="store_true")
args = parser.parse_args()
fixed = ["worker-result.json", "job.json", "host.json", "hardware.json", "lscpu.json",
         "compiler.txt", "consumer.receipt.json", "binaries.json", "check.txt",
         "policy-cost.csv"]
if args.graduation:
    fixed = [name for name in fixed if name not in ("consumer.receipt.json", "policy-cost.csv")]
    fixed += ["schedule.json", "policy-cost-private.csv", "policy-cost-product.csv"]
elif args.followup:
    fixed += ["consumer-unique.receipt.json", "schedule.json"]
selection = set(fixed)
for pattern in ("loom_window_*.csv", "loom_execution_*.csv", "loom_window_*-codegen.json",
                "loom_window_*-inventory.txt", "residence-*/*.json",
                "loom_graduation_*.csv", "loom_graduation_*-codegen.json"):
    selection.update(str(path.relative_to(args.root)) for path in args.root.glob(pattern))
if args.graduation:
    # Execution receipts include full decoded instruction streams. The archive
    # recipe replays them; Git retains complete timings and compact controls.
    selection -= {f"loom_graduation_{scale}_{kind}_{variant}-codegen.json"
                  for scale in ("hot", "large") for kind in range(3)
                  for variant in ("private", "product")}
for name in sorted(selection):
    if not (args.root / name).is_file():
        parser.error(f"missing selected file: {name}")
tool = Path(__file__).resolve().parents[2] / "tools/artifacts.py"
recipe = "python3 workbench/spikes/loom-locality-execution/replay.py {evidence}"
if args.graduation:
    recipe += " --graduation"
elif args.followup:
    recipe += " --followup"
command = [sys.executable, str(tool), "retain", str(args.root), str(args.output),
           "--regenerate-from", "archive", "--regenerate", recipe]
for name in sorted(selection):
    command += ["--file", name]
subprocess.run(command, check=True)
