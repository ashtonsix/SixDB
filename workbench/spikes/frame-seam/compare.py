#!/usr/bin/env python3
"""Paired local timing of saved Frame binaries, one round per process.

  compare.py run OUT.jsonl ROUNDS COLS NAME=DIR [NAME=DIR ...]
  compare.py table OUT.jsonl [BASE]

Each DIR holds frame_landing_measure and frame_seam_measure built from one
source state. Rounds rotate the arm order and pin one CPU. Only the Ikea
arm of the landing harness is kept (its spike/native arms are unchanged).
"""
import json, os, statistics, subprocess, sys
from collections import defaultdict


def run(out, rounds, cols, arms):
    cpu = os.environ.get("SEAM_CPU", "3")
    with open(out, "a") as f:
        for r in range(rounds):
            order = arms[r % len(arms):] + arms[:r % len(arms)]
            for name, d in order:
                for exe, args in (("frame_landing_measure", [cols, "1"]), ("frame_seam_measure", [cols, name, "1"])):
                    p = subprocess.run(["taskset", "-c", cpu, os.path.join(d, exe), *args],
                                       check=True, capture_output=True, text=True)
                    for line in p.stdout.splitlines():
                        rec = json.loads(line)
                        if exe == "frame_landing_measure":
                            if rec["arm"] != "ikea":
                                continue
                            rec["arm"] = name
                        rec["round"] = r
                        f.write(json.dumps(rec) + "\n")
            print(f"round {r + 1}/{rounds}", file=sys.stderr, flush=True)


def table(out, base=None):
    cells = defaultdict(list)
    arms = []
    for line in open(out):
        rec = json.loads(line)
        cells[(rec["column"], rec["op"], rec["conditioning"], rec["arm"])].append(rec["ns"])
        if rec["arm"] not in arms:
            arms.append(rec["arm"])
    base = base or arms[0]
    keys = sorted({k[:3] for k in cells})
    print("| Column | Operation | Conditioning | " + " | ".join(f"{a} ns" for a in arms) +
          " | " + " | ".join(f"{a}/{base}" for a in arms if a != base) + " |")
    print("| --- | --- | --- | " + " | ".join("---:" for _ in arms) + " | " +
          " | ".join("---:" for a in arms if a != base) + " |")
    for k in keys:
        med = {a: statistics.median(cells[k + (a,)]) for a in arms if cells.get(k + (a,))}
        if base not in med:
            continue
        print(f"| {k[0]} | {k[1]} | {k[2]} | " + " | ".join(f"{med[a]:.0f}" if a in med else "" for a in arms) +
              " | " + " | ".join(f"{med[a] / med[base]:.2f}" if a in med else "" for a in arms if a != base) + " |")


if __name__ == "__main__":
    if sys.argv[1] == "run":
        run(sys.argv[2], int(sys.argv[3]), sys.argv[4], [tuple(a.split("=", 1)) for a in sys.argv[5:]])
    else:
        table(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
