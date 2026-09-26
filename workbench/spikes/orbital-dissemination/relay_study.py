"""Separate public-link retry timing from the cost of distributing plan hints."""
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
               for name in ("relay_study.py", "experiments.py", "simulator.py", "routing.py", "run_study.py")}
    cases = []
    for timer in (400, 3000):
        for enhancement in ("none", "blocking", "optional"):
            cfg = dict(name=f"public-{timer}-{enhancement}", count=1000,
                       admission="pipelined_candidate", public_consumers=True,
                       enhancement=enhancement, hint_bytes=4096, retry_us=timer)
            cases.append(compact(run(cfg)))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(kind="synthetic_relay_and_retry_sensitivity", sources=sources,
                                          cases=cases), indent=2, sort_keys=True) + "\n")
    print(args.output)
