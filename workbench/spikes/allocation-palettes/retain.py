#!/usr/bin/env python3
"""Retain a small, reproducible comparison; full outputs remain in build/."""
import argparse
import csv
import hashlib
import json
import subprocess
from pathlib import Path


def rows(path):
    with path.open() as f:
        return list(csv.DictReader(f))


def save(path, records):
    assert records
    with path.open("w") as f:
        w = csv.DictWriter(f, fieldnames=list(records[0])); w.writeheader(); w.writerows(records)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input", type=Path, default=Path("build/allocation-palettes/results"))
    p.add_argument("--output", type=Path, default=Path(__file__).parent / "evidence")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    selected = [r for r in rows(args.input / "traces.csv") if r["page"] in ("16384", "65536")
                and r["count"] == "4096" and r["selection"] == "tight" and r["lifetime"] == "scattered"
                and (r["workload"], r["alignment"]) in (("uniform", "16"), ("tiny", "16"), ("cluster", "64"))]
    save(args.output / "selected.csv", selected)
    sensitivity = [r for r in rows(args.input / "placement.csv") if r["layout"] in ("class-aligned", "grid-packed")]
    save(args.output / "placement.csv", sensitivity)
    growth = [r for r in rows(args.input / "growth.csv") if (r["start"], r["step"], r["alignment"]) == ("256", "16", "64")]
    save(args.output / "growth.csv", growth)
    profiles = {(f, "class-aligned", s, c)
                for f, c in (("current", "first"), ("partition-only", "first"),
                             ("upper-band", "first"), ("current-best", "aligned"),
                             ("upper-global", "aligned"))
                for s in (("strict", "bounded") if f in ("upper-band", "upper-global") else ("strict",))}
    profiles.update((f, "small-packed", "strict", "aligned")
                    for f in ("current", "upper31", "upper32", "balanced31"))
    profiles.update((f, "class-aligned", "unrestricted", "first")
                    for f in ("current", "upper-band"))
    controlled = [r for r in rows(args.input / "recommend.csv")
                  if (r["family"], r["layout"], r["selection"], r["class_choice"]) in profiles]
    save(args.output / "recommend.csv", controlled)
    churn = [r for r in rows(args.input / "churn.csv")
             if r["family"] in ("current", "current-best", "upper-band", "upper-global", "upper31")
             and r["layout"] == "class-aligned"]
    save(args.output / "churn.csv", churn)
    private = [r for r in rows(args.input / "private-growth.csv")
               if r["family"] in ("current", "upper31", "upper32", "upper-band")
               and r["layout"] == "small-packed" and r["start"] == "256"
               and r["alignment"] == "64" and r["population"] == "homogeneous"
               and r["free_mode"] == "none" and r["class_choice"] == "aligned"]
    save(args.output / "private-growth.csv", private)
    (args.output / "counterexamples.json").write_bytes((args.input / "counterexamples.json").read_bytes())
    manifest = json.loads((args.input / "manifest.json").read_text())
    assert manifest["source_sha256"] == hashlib.sha256((Path(__file__).parent / "run.py").read_bytes()).hexdigest()
    controlled_context = json.loads((args.input / "recommend-context.json").read_text())
    assert controlled_context["source_sha256"] == hashlib.sha256((Path(__file__).parent / "recommend.py").read_bytes()).hexdigest()
    manifest["limits"][1] = "current record sizes are native; redesigned metadata is projected; controller containers and malloc overhead excluded"
    qualification = {}
    for name in ("native", "native-sanitize", "production", "production-sanitize",
                 "recommend-native", "recommend-native-sanitize"):
        entries = [json.loads(s) for s in (args.input / f"{name}.jsonl").read_text().splitlines()]
        assert entries and all(r["qualification"] == "pass" for r in entries)
        qualification[name] = {"cases": len(entries), "homes": sum(r["homes"] for r in entries),
                               "host_pages": sorted({r["host_page"] for r in entries}),
                               "details": entries if name.startswith("production") else None}
    source_paths = list(Path(__file__).parent.glob("*.py")) + list(Path(__file__).parent.glob("*.cpp")) + [Path(__file__).parent / "CMakeLists.txt"]
    backend = Path(__file__).parent / "backend"
    source_paths += [backend / n for n in ("finite.cpp", "finite.h", "snapshot.json")]
    source_paths += [Path("orbital/src/allocation") / n for n in ("finite.cpp", "finite.h", "internal.h", "group.cpp")]
    context = dict(manifest, qualification=qualification,
                   experimental_backend_origin=json.loads((backend / "snapshot.json").read_text()),
                   qualification_scope="production checks use current Orbital; configurable backend checks/timings use the frozen spike snapshot",
                   controlled=controlled_context,
                   source_hashes={str(n): hashlib.sha256(n.read_bytes()).hexdigest() for n in source_paths},
                   git_head=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                   compiler=subprocess.check_output(["clang++-21", "--version"], text=True).splitlines()[0],
                   host=subprocess.check_output(["uname", "-a"], text=True).strip(),
                   build="Clang21.1.8 C++23 Release -O3 -g; separate ASan/UBSan -O2 -g",
                   retained={"geometry_trace_rows":len(selected), "placement_rows":len(sensitivity),
                             "growth_rows":len(growth), "recommend_rows":len(controlled),
                             "churn_rows":len(churn), "private_growth_rows":len(private)},
                   raw_hashes={n.name: hashlib.sha256(n.read_bytes()).hexdigest() for n in args.input.iterdir() if n.is_file()})
    (args.output / "context.json").write_text(json.dumps(context, indent=2) + "\n")
    print(f"Retained {sum(context['retained'].values())} selected rows and qualification")


if __name__ == "__main__":
    main()
