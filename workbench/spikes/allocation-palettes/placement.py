#!/usr/bin/env python3
"""Prepared-first placement sensitivity; counts only, no search-time claim."""
import argparse
import bisect
import csv
import json
import random
from pathlib import Path
from run import FAMILIES, Pool, requests


class PreparedPool(Pool):
    def allocate(self, token, requested, alignment):
        # Current allocator first searches prepared Homes across eligible grids.
        # Keep that principle; omit its ready cache and unavailable/pending state.
        for g in reversed(self.groups):
            cls = bisect.bisect_left(g.g.capacities, requested)
            if cls == g.g.classes:
                continue
            for grid in range(g.g.grids):
                if g.identities[grid] == g.g.slots:
                    continue
                for slot in range(g.g.slots):
                    if g.occupied[grid][cls] & (1 << slot):
                        continue
                    lo, hi = g.g.home(grid, cls, slot)
                    if lo % alignment:
                        continue
                    pages = set(range(lo // self.page, (hi - 1) // self.page + 1))
                    if not pages <= g.mapped:
                        continue
                    g.identities[grid] += 1
                    g.occupied[grid][cls] |= 1 << slot
                    g.live[token] = (grid, cls, slot, pages)
                    cap = g.g.capacities[cls]
                    self.tokens[token] = (g, requested, cap)
                    self.requested += requested
                    self.capacity += cap
                    self.peak = max(self.peak, self.charge())
                    return
        super().allocate(token, requested, alignment)


def run(family, layout, page, workload, alignment, sizes, shift=0):
    count = 1024
    p = PreparedPool(family, layout, page, "reuse-first", sizes)
    offered = [max(16, min(3072, r + shift)) for r in requests(workload, count)]
    for i, n in enumerate(offered):
        p.allocate(i, n, alignment)
    result = dict(family=family, layout=layout, page=page, selection="prepared-first",
                  workload=workload, shift=shift, count=count, alignment=alignment,
                  requested=p.requested, capacity=p.capacity, backed=p.backed(),
                  metadata=p.metadata(), groups=len(p.groups))
    keep = set(random.Random(9).sample(range(count), count // 16))
    for i in range(count):
        if i not in keep:
            p.free(i)
    p.trim()
    result.update(survivors_requested=p.requested, survivors_backed=p.backed())
    for i in keep:
        p.free(i)
    p.trim()
    assert p.charge() == 0
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--sizes", type=Path, required=True)
    p.add_argument("--output", type=Path, default=Path("build/allocation-palettes/results/placement.csv"))
    args = p.parse_args()
    sizes = json.loads(args.sizes.read_text())
    # Independent descriptor-hole witness:32nd class has7, not8 homes/grid.
    for family, expected in (("current", 1), ("split5", 2), ("split6", 1)):
        pool = Pool(family, "class-aligned", 65536, "tight", sizes)
        for i in range(256):
            pool.allocate(i, 3000, 16)
        assert len(pool.groups) == expected, (family, len(pool.groups))
    rows = [run(f, layout, page, w, a, sizes, shift)
            for page in (16384, 65536) for f in FAMILIES
            for layout in ("class-aligned", "class-packed", "grid-packed")
            for w, a, shifts in (("uniform", 16, (0,)), ("tiny", 16, (0,)),
                                 ("log", 64, (0,)), ("cluster", 64, (-64, 0, 64)))
            for shift in shifts]
    with args.output.open("w") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    print(f"{len(rows)} prepared-first sensitivity cells and reserved-byte witness passed")


if __name__ == "__main__":
    main()
