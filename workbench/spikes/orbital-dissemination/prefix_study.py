"""Attribute conditional-prefix waiting to the unaffected producer during repair."""
import argparse
import hashlib
import json
from pathlib import Path

from experiments import run
from run_study import compact


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).parent
    sources = {name: hashlib.sha256((root / name).read_bytes()).hexdigest()
               for name in ("prefix_study.py", "experiments.py", "simulator.py", "routing.py", "run_study.py")}
    rows = []
    for until in (300, 600):
        for mode in ("strict", "pipelined_candidate"):
            cfg = dict(name=f"prefix-{until}-{mode}", count=100, rate=100000,
                       producers=2, admission=mode,
                       faults=[dict(kind="partition", src="p0_p0", dst=w,
                                    at_us=0, until_us=until) for w in ("f0", "s0")])
            result = run(cfg)
            row = compact(result)
            row["first_operation_traces"] = result["traces"]
            rows.append(row)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(kind="synthetic_producer_attribution", sources=sources,
                                          cases=rows), sort_keys=True, indent=2) + "\n")
    print(args.output)
