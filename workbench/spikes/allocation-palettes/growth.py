#!/usr/bin/env python3
"""Private best-fit growth screen: class transitions/copies, not elapsed time."""
import argparse
import bisect
import csv
from pathlib import Path
from run import FAMILIES, PALETTES


def choose(family, size, alignment):
    names = FAMILIES[family]
    if family == "current":
        name = "B" if alignment >= 64 or size > 192 else "A"
    else:
        name = min((n for n in names if PALETTES[n][-1] >= size),
                   key=lambda n: PALETTES[n][bisect.bisect_left(PALETTES[n], size)])
    return name, PALETTES[name][bisect.bisect_left(PALETTES[name], size)]


def screen(family, start, step, alignment, policy):
    extent = start
    band, cap = choose(family, extent, alignment)
    moves = copies = crossings = calls = 0
    for required in range(start + step, 3073, step):
        if required <= extent:
            continue
        next_extent = required if policy == "exact" else min(3072, max(required, extent * 2))
        calls += 1
        if next_extent > cap:
            next_band, next_cap = choose(family, next_extent, alignment)
            copies += extent
            moves += 1
            crossings += next_band != band
            band, cap = next_band, next_cap
        extent = next_extent
    return dict(family=family, start=start, step=step, alignment=alignment, policy=policy,
                growth_calls=calls, class_moves=moves, cross_palette_moves=crossings,
                copied_exposed_bytes=copies, final_extent=extent, final_capacity=cap)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, default=Path("build/allocation-palettes/results/growth.csv"))
    args = p.parse_args()
    assert choose("current", 2050, 64)[1] == 2560
    assert choose("fine64", 2050, 64)[1] == 2176
    rows = [screen(f, start, step, align, policy)
            for f in FAMILIES for start in (16, 256, 1024, 1792)
            for step in (16, 64, 256) for align in (16, 64) for policy in ("exact", "geometric")]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w") as out:
        writer = csv.DictWriter(out, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    print(f"{len(rows)} private growth screens: {args.output}")


if __name__ == "__main__":
    main()
