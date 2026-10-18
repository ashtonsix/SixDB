#!/usr/bin/env python3
"""Run complete maintained set/map operations with explicit dimensions."""
import itertools
import json
import os
import subprocess
import sys
from pathlib import Path

root = Path(sys.argv[1])
isas = sys.argv[2:]
seeds = [713, 3191, 76543]
repetitions = int(os.environ.get("HASHTABLE_REPETITIONS", "4"))
assert repetitions > 0 and repetitions % 2 == 0
manifest = dict(compare_capture=os.environ.get("HASHTABLE_COMPARE_CAPTURE")=="1",cpu=os.environ["HASHTABLE_CPU"], isas=isas, seeds=seeds,
                repetitions=repetitions, values=[0, 8, 64], misses=[0, 50],
                occupancies=[50, 85], fresh=[0, 50, 100],
                source_conditioning="incoming_entry_prewalk for accumulation",
                core_order="alternate Ikea/Abseil process order by seed and repetition",
                accumulation_order="alternate AB/BA within each process")
(root / "cases.json").write_text(json.dumps(manifest, indent=2) + "\n")


def invoke(exe, path, **options):
    args = ["taskset", "-c", os.environ["SIXDB_CPU"], str(exe)]
    for name, value in options.items():
        args += ["--" + name.replace("_", "-"), str(value)]
    with path.open("w") as out:
        subprocess.run(args, stdout=out, check=True)


for isa in isas:
    where = root / isa
    for k, v, seed, rep in itertools.product([14, 30], [0, 8, 64], seeds, range(repetitions)):
        exe = where / f"ikea_hashtable_operations_k{k}_b10_v{v}"
        owners = ["ikea", "abseil"]
        if (seed + rep) % 2:
            owners.reverse()
        for owner in owners:
            for miss in [0, 50]:
                invoke(exe, where / f"read-k{k}-v{v}-{owner}-s{seed}-r{rep}-m{miss}.csv",
                       operation="read", owner=owner, seed=seed, rep=rep, miss_percent=miss)
            for load, fresh in itertools.product([50, 85], [0, 50, 100]):
                for strategy in [owner] + (["ikea_split"] if owner == "ikea" else []):
                    invoke(exe, where / f"write-k{k}-v{v}-{strategy}-s{seed}-r{rep}-l{load}-n{fresh}.csv",
                           operation="write", owner=strategy, seed=seed, rep=rep,
                           occupancy_percent=load, new_percent=fresh)
                if v:
                    invoke(exe, where / f"modify-k{k}-v{v}-{owner}-s{seed}-r{rep}-l{load}-n{fresh}.csv",
                           operation="modify", owner=owner, seed=seed, rep=rep,
                           occupancy_percent=load, new_percent=fresh)
            invoke(exe, where / f"grow-k{k}-v{v}-{owner}-s{seed}-r{rep}.csv",
                   operation="grow", owner=owner, seed=seed, rep=rep)
        invoke(exe, where / f"grow-k{k}-v{v}-ikea_descriptor-s{seed}-r{rep}.csv",
               operation="grow", owner="ikea_descriptor", seed=seed, rep=rep)
    # Physical conditioning is recorded; footprint is not a residency claim.
    for v, seed, rep, owner, miss in itertools.product(
            [0, 8, 64], seeds, range(repetitions), ["ikea", "abseil"], [0, 50]):
        invoke(where / f"ikea_hashtable_operations_k30_b18_v{v}",
               where / f"cold-v{v}-{owner}-s{seed}-r{rep}-m{miss}.csv",
               operation="read", owner=owner, seed=seed, rep=rep, miss_percent=miss,
               conditioning_bytes=536870912)
    with (root / "progress.txt").open("a") as out:
        out.write(f"{isa} complete core controls\n")

basic = [(k, b, n) for k, b in [(14, 10), (30, 10), (30, 15)] for n in [2, 8]]
wide = [(k, b, n) for k, b in [(31, 10), (47, 10), (47, 17), (55, 9)] for n in [2, 8]]
for panel, values, cases in [("basic", [0, 8, 64], basic), ("wide", [0, 8], wide)]:
    for v, (k, b, n), overlap, seed in itertools.product(values, cases, [0, 50, 100], seeds):
        invoke(root / "native" / f"ikea_hashtable_accumulation_{panel}_{v}",
               root / "native" / f"accum-{panel}-v{v}-k{k}-b{b}-n{n}-o{overlap}-s{seed}.csv",
               source_k=k, source_b=b, inputs=n, overlap_percent=overlap,
               seed=seed, repetitions=repetitions)
    with (root / "progress.txt").open("a") as out:
        out.write(f"{panel} accumulation complete\n")

if os.environ.get("HASHTABLE_COMPARE_CAPTURE")=="1":
    for panel,cases in [("basic",basic),("wide",wide)]:
        where=root/f"before-{panel}"
        for (k,b,n),overlap,seed in itertools.product(cases,[0,50,100],seeds):
            with (where/f"k{k}-b{b}-n{n}-o{overlap}-s{seed}.csv").open("w") as out:
                subprocess.run(["taskset","-c",os.environ["SIXDB_CPU"],str(where/"bench"),
                    *map(str,(k,b,n,overlap,seed,repetitions,18))],stdout=out,check=True)
