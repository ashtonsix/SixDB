#!/usr/bin/env python3
"""Controlled palette/partition/selection screen. Logical counts, not timings."""
import argparse
import bisect
import csv
import hashlib
import json
import random
from pathlib import Path

import run

UPPER32 = (16, 32, 48, 64, 96, 128, 192, 256, 320, 384, 448, 512,
           640, 768, 896, 1024, 1152, 1280, 1408, 1536, 1664, 1792,
           1920, 2048, 2176, 2304, 2432, 2560, 2688, 2816, 2944, 3072)
UPPER31 = tuple(c for c in UPPER32 if c != 2944)
BALANCED31 = tuple(sorted(set(UPPER32) - {2176, 2432, 2688, 2944, 1152}
                          | {80, 112, 160, 224}))
run.PALETTES.update({"upper31": UPPER31, "upper32": UPPER32,
                     "balanced31": BALANCED31, "upper15": tuple(range(1280, 3073, 128)),
                     "A8": run.A, "B8": run.B})
run.FAMILIES.update({"upper31": ("upper31",), "upper32": ("upper32",),
                    "balanced31": ("balanced31",), "upper-band": ("A", "B", "upper15"),
                    "current-best": ("A", "B"), "upper-global": ("A", "B", "upper15"),
                    "partition-only": ("A8", "B8")})


class Geometry(run.Geometry):
    def __init__(self, palette, layout, page):
        assert layout in ("class-aligned", "small-packed"), layout
        base = palette[:-1] if palette in ("A8", "B8") else palette
        super().__init__(base, "class-aligned" if layout == "small-packed" else layout, page)
        self.palette, self.layout = palette, layout
        if palette in ("A8", "B8"):
            self.bits, self.slots, self.grids = 5, 8, 32
            self.strides = [8 * c for c in self.capacities]
        if layout == "small-packed":
            # Pack whole small-class runs, not every alternative into every grid.
            # Keep upper-class runs page independent. All run starts are 64B aligned.
            bins = []
            small = [i for i, cap in enumerate(self.capacities) if 256 * cap <= page]
            for i in sorted(small, key=lambda i: -self.capacities[i]):
                for bucket in bins:
                    if sum(256 * self.capacities[j] for j in bucket) + 256 * self.capacities[i] <= page:
                        bucket.append(i)
                        break
                else:
                    bins.append([i])
            off = 0
            for b, bucket in enumerate(bins):
                off = b * page
                for i in bucket:
                    self.offsets[i] = off
                    self.strides[i] = self.slots * self.capacities[i]
                    off += 256 * self.capacities[i]
            for i in range(self.classes):
                if i in small:
                    continue
                if self.capacities[i] > 256:
                    off = run.roundup(off, page)
                self.offsets[i] = off
                self.strides[i] = self.slots * self.capacities[i]
                off += 256 * self.capacities[i]
            self.payload = run.roundup(off, page)
        self.pages = self.payload // page
        self.words = (self.pages + 63) // 64
        grid_bytes = 2 + self.classes * (4 if self.slots == 16 else 2)
        self.control = run.roundup(256 + self.grids * grid_bytes + self.pages, 8) + 24 * self.words


def fitting(g, size, alignment, class_choice):
    cls = bisect.bisect_left(g.capacities, size)
    if class_choice == "aligned":
        # A class whose stride preserves alignment keeps every slot usable.
        while cls < g.classes and g.capacities[cls] % alignment:
            cls += 1
    return cls if cls < g.classes else None


class Pool(run.Pool):
    def __init__(self, family, layout, page, selection, sizes, class_choice):
        super().__init__(family, layout, page, selection, sizes)
        self.class_choice = class_choice
        self.mapped_events = self.examined = 0

    def allocate(self, token, requested, alignment):
        names = run.FAMILIES[self.family]
        options = []
        for name in names:
            g = Geometry(name, self.layout, self.page)
            cls = fitting(g, requested, alignment, self.class_choice)
            if cls is not None:
                options.append((g.capacities[cls], names.index(name), name))
        best, _, wanted = min(options)
        if self.family in ("current", "partition-only", "upper-band"):
            wanted = names[1] if alignment >= 64 or requested > 192 else names[0]
            if self.family == "upper-band":
                baseline = next(cap for cap, _, name in options if name == wanted)
                upper = next((cap for cap, _, name in options if name == "upper15"), None)
                if upper is not None and upper < baseline:
                    wanted = "upper15"
            # Price oversizing against the baseline's actual fresh class, not an
            # A class that its established new-Group selector would never choose.
            best = next(cap for cap, _, name in options if name == wanted)
        for prepared in (True, False):
            for g in reversed(self.groups):
                if self.selection == "strict" and g.g.palette != wanted:
                    continue
                cls = fitting(g.g, requested, alignment, self.class_choice)
                if cls is None:
                    continue
                if self.selection == "bounded" and g.g.capacities[cls] * 8 > best * 9:
                    continue
                if self.claim(g, cls, token, requested, alignment, prepared):
                    return
        g = run.Group(Geometry(wanted, self.layout, self.page))
        self.groups.append(g)
        cls = fitting(g.g, requested, alignment, self.class_choice)
        assert cls is not None and self.claim(g, cls, token, requested, alignment, False)

    def claim(self, g, cls, token, requested, alignment, prepared):
        for grid in range(g.g.grids):
            if g.identities[grid] == g.g.slots:
                continue
            candidates = []
            for slot in range(g.g.slots):
                self.examined += 1
                if g.occupied[grid][cls] & (1 << slot):
                    continue
                lo, hi = g.g.home(grid, cls, slot)
                if lo % alignment:
                    continue
                pages = set(range(lo // self.page, (hi - 1) // self.page + 1))
                missing = len(pages - g.mapped)
                if prepared and missing:
                    continue
                candidates.append((missing, slot, pages))
            if candidates:
                missing, slot, pages = min(candidates)
                self.mapped_events += missing
                g.identities[grid] += 1
                g.occupied[grid][cls] |= 1 << slot
                g.mapped |= pages
                g.live[token] = (grid, cls, slot, pages)
                cap = g.g.capacities[cls]
                self.tokens[token] = (g, requested, cap)
                self.requested += requested
                self.capacity += cap
                self.peak = max(self.peak, self.charge())
                return True
        return False


def census(p, stage):
    return {stage + k: v for k, v in dict(requested=p.requested, capacity=p.capacity,
            backed=p.backed(), metadata=p.metadata(), groups=len(p.groups),
            virtual=sum(g.g.payload for g in p.groups)).items()}


def trace(family, layout, page, selection, choice, workload, alignment, sizes, churn=False):
    count = 1024
    p = Pool(family, layout, page, selection, sizes, choice)
    offered = run.requests("cluster" if workload.startswith("cluster") else workload, count)
    shift = {"cluster-low": -64, "cluster": 0, "cluster-high": 64}.get(workload, 0)
    offered = [max(16, min(3072, n + shift)) for n in offered]
    for i, n in enumerate(offered):
        p.allocate(i, n, alignment)
    row = dict(page=page, family=family, layout=layout, selection=selection,
               class_choice=choice, workload=workload, alignment=alignment, churn=int(churn))
    row.update(census(p, "initial_"))
    if churn:
        rng = random.Random(18)
        active = list(range(count))
        for epoch in range(4):
            for i in rng.sample(range(count), count // 2):
                old = active[i]
                new = (epoch + 1) * count + i
                # A successor is acquired while the previous allocation remains live.
                p.allocate(new, offered[(i + (epoch + 1) * 127) % count], alignment)
                p.free(old)
                active[i] = new
            p.trim()
        row.update(census(p, "settled_"))
    else:
        keep = set(random.Random(9).sample(range(count), count // 16))
        for i in range(count):
            if i not in keep:
                p.free(i)
        p.trim()
        row.update(census(p, "settled_"))
    row.update(peak_charge=p.peak, mapped_page_events=p.mapped_events,
               examined_slots=p.examined)
    for i in list(p.tokens):
        p.free(i)
    p.trim()
    assert p.charge() == 0
    return row


def qualify(output):
    cases = []
    for page in (16384, 65536):
        for name in ("A", "B", "A8", "B8", "upper15", "upper31", "upper32", "balanced31"):
            for layout in ("class-aligned", "small-packed"):
                g = Geometry(name, layout, page)
                intervals = []
                for grid in range(g.grids):
                    for cls in range(g.classes):
                        for slot in range(g.slots):
                            coord = (slot << g.bits) | cls
                            if coord == 255:
                                continue
                            assert (coord >> g.bits, coord & ((1 << g.bits) - 1)) == (slot, cls)
                            lo, hi = g.home(grid, cls, slot)
                            assert lo % 16 == 0 and hi <= g.payload
                            intervals.append((lo, hi))
                ordered = sorted(intervals)
                assert all(a[1] <= b[0] for a, b in zip(ordered, ordered[1:]))
                for cls, cap in enumerate(g.capacities):
                    cases.append((f"{name}/{layout}", page, g.payload, g.strides[cls], cls,
                                  cap, g.offsets[cls], g.classes, g.slots, g.bits))
    with (output / "recommend-layouts.tsv").open("w") as f:
        f.write("layout\tpage\tpayload_bytes\tgrid_stride\tcls\tcapacity\toffset\tclasses\tslots\tclass_bits\n")
        for row in cases:
            f.write("\t".join(map(str, row)) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/allocation-palettes/results"))
    parser.add_argument("--churn-only", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    sizes = json.loads(args.sizes.read_text())
    qualify(args.output)
    # The same byte table with a different partition isolates partition effects.
    for family, expected in (("upper31", 1), ("upper32", 2)):
        p = Pool(family, "small-packed", 65536, "strict", sizes, "first")
        for i in range(256):
            p.allocate(i, 3000, 16)
        assert len(p.groups) == expected
    p = Pool("current", "small-packed", 65536, "bounded", sizes, "first")
    for i in range(256):
        p.allocate(i, 64, 64)
    assert len(p.groups) == 1 and p.capacity == 256 * 256
    if args.churn_only:
        rows = [trace(family, layout, page, selection, "aligned", workload,
                      64 if workload in ("log", "cluster") else 16, sizes, churn=True)
                for page in (16384, 65536)
                for layout in ("class-aligned", "small-packed")
                for family in ("current", "current-best", "upper-band", "upper-global", "upper31")
                for selection in ("unrestricted", "strict", "bounded")
                for workload in ("uniform", "tiny", "log", "cluster")]
        # Every candidate received the same IDs, extents and update order.
        for page in (16384, 65536):
            for workload in ("uniform", "tiny", "log", "cluster"):
                for stage in ("initial_", "settled_"):
                    assert len({r[stage + "requested"] for r in rows
                                if r["page"] == page and r["workload"] == workload}) == 1
        with (args.output / "churn.csv").open("w") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
        print(f"{len(rows)} fixed-population successor churn cells; request equality passed")
        return
    rows = []
    for page in (16384, 65536):
        for family in ("current", "partition-only", "upper31", "upper32", "balanced31",
                       "upper-band", "current-best", "upper-global"):
            for layout in ("class-aligned", "small-packed"):
                for selection in ("unrestricted", "strict", "bounded"):
                    for choice in ("first", "aligned"):
                        for workload, align in (("uniform", 16), ("tiny", 16), ("log", 64),
                                                ("cluster-low", 64), ("cluster", 64), ("cluster-high", 64)):
                            rows.append(trace(family, layout, page, selection, choice, workload, align, sizes))
    with (args.output / "recommend.csv").open("w") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0])); w.writeheader(); w.writerows(rows)
    manifest = dict(kind=__doc__, traces=len(rows), palettes=run.PALETTES,
                    source_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    limits=["No ready-cache, pending work, timing or actual large-native-page behavior",
                            "Newest Groups in both prepared-first and missing-page passes",
                            "Bounded reuse allows at most 12.5% capacity over global smallest fit",
                            "Current/upper-band retain A/B fresh routing; current-best/upper-global isolate global best-fit routing",
                            "Strict restricts eligible palette; bounded allows compatible Groups up to its capacity bound",
                            "Small-packed is a layout sensitivity, not a universal recommendation"])
    (args.output / "recommend-context.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{len(rows)} controlled recommendation cells and geometry/descriptor witnesses passed")


if __name__ == "__main__":
    main()
