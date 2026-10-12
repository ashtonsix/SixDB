#!/usr/bin/env python3
"""Finite-palette geometry and deterministic lifetime traces; no timing model."""
import argparse
import bisect
import csv
import hashlib
import json
import random
from pathlib import Path

A = (16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072)
B = (256, 320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072)
B_LARGE = (256, 320, 384, 448, 512, 640, 768, 896, 1088, 1280, 1536, 1792, 2176, 2560, 3072)
PALETTES = {
    # Historical trace families keep B; B-large qualifies the selected defaults.
    "A": A, "B": B, "B-large": B_LARGE,
    "tiny16": tuple(range(16, 241, 16)),
    "medium64": tuple(range(256, 1153, 64)),
    "upper128": tuple(range(1280, 3073, 128)),
    "medium32": (256, 288, 320, 352, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960, 1024),
    "upper-mixed": (1024, 1152, 1280, 1408, 1536, 1664, 1792, 1920, 2048, 2176, 2304, 2432, 2560, 2816, 3072),
    "class32": tuple(sorted((16, 32, 48, 64, 96, 192, 320, 2112) + tuple(range(128, 3073, 128)))),
    "class51": (16, 32, 48) + tuple(range(64, 3073, 64)),
}
FAMILIES = {
    "current": ("A", "B"),
    "fine64": ("tiny16", "medium64", "upper128"),
    "fine32": ("tiny16", "medium32", "upper-mixed"),
    "split5": ("class32",),
    "split6": ("class51",),
}


def roundup(n, unit):
    return (n + unit - 1) // unit * unit


class Geometry:
    def __init__(self, palette, layout, page):
        self.palette, self.layout, self.page = palette, layout, page
        self.capacities = PALETTES[palette]
        self.classes = len(self.capacities)
        self.bits = 4 if self.classes <= 15 else (5 if self.classes <= 32 else 6)
        self.slots = 1 << (8 - self.bits)
        self.grids = 256 // self.slots
        self.offsets, self.strides = [0] * self.classes, [0] * self.classes
        order = list(range(self.classes))
        if layout == "class-aligned" and palette == "A":
            order = [3, 0, 2, 1, 4] + list(range(5, 15))
        off = 0
        for pos, c in enumerate(order):
            if layout == "class-aligned" and not (palette == "A" and pos < 5):
                off = roundup(off, page)
            self.offsets[c] = off
            self.strides[c] = self.slots * self.capacities[c]
            off += (self.slots if layout == "grid-packed" else 256) * self.capacities[c]
        if layout == "grid-packed":
            stride = roundup(off, page)
            self.strides = [stride] * self.classes
            off = self.grids * stride
        self.payload = roundup(off, page)
        self.pages = self.payload // page
        self.words = (self.pages + 63) // 64
        grid_bytes = 2 + self.classes * (4 if self.slots == 16 else 2)
        self.control = roundup(256 + self.grids * grid_bytes + self.pages, 8) + 24 * self.words

    def home(self, grid, cls, slot):
        start = self.offsets[cls] + grid * self.strides[cls] + slot * self.capacities[cls]
        return start, start + self.capacities[cls]


def verify():
    # Independent interval census checks all 3,840 homes and every coordinate.
    for page in (4096, 16384, 65536):
        for name in PALETTES:
            for layout in ("class-aligned", "class-packed", "grid-packed"):
                g = Geometry(name, layout, page)
                intervals = []
                for grid in range(g.grids):
                    for cls in range(g.classes):
                        for slot in range(g.slots):
                            lo, hi = g.home(grid, cls, slot)
                            assert lo % 16 == 0 and hi <= g.payload
                            coordinate = (slot << g.bits) | cls
                            if coordinate == 255:
                                continue
                            assert (coordinate >> g.bits, coordinate & ((1 << g.bits) - 1)) == (slot, cls)
                            intervals.append((lo, hi))
                ordered = sorted(intervals)
                assert all(a[1] <= b[0] for a, b in zip(ordered, ordered[1:]))
    # Known production census, independent published expectations.
    assert Geometry("A", "class-aligned", 4096).payload == 2605056
    assert Geometry("B", "class-aligned", 4096).pages == 1096
    assert Geometry("A", "class-aligned", 16384).control == 1480
    assert Geometry("B", "class-aligned", 65536).pages == 71
    # Example central to the question: finer steps, not a larger cutoff.
    assert B[bisect.bisect_left(B, 2050)] == 2560
    u = PALETTES["upper128"]
    assert u[bisect.bisect_left(u, 2050)] == 2176


class Group:
    def __init__(self, geometry):
        self.g = geometry
        self.identities = [0] * geometry.grids
        self.occupied = [[0] * geometry.classes for _ in range(geometry.grids)]
        if geometry.classes == (1 << geometry.bits):
            for grid in self.occupied:
                grid[-1] = 1 << (geometry.slots - 1)  # 0xff stays unbound.
        self.mapped = set()
        self.live = {}

    def allocate(self, token, requested, alignment):
        cls = bisect.bisect_left(self.g.capacities, requested)
        if cls == self.g.classes:
            return None
        for grid in range(self.g.grids):
            if self.identities[grid] == self.g.slots:
                continue
            candidates = []
            for slot in range(self.g.slots):
                if self.occupied[grid][cls] & (1 << slot):
                    continue
                lo, hi = self.g.home(grid, cls, slot)
                if lo % alignment:
                    continue
                pages = set(range(lo // self.g.page, (hi - 1) // self.g.page + 1))
                candidates.append((len(pages - self.mapped), slot, pages))
            if candidates:
                _, slot, pages = min(candidates)
                self.identities[grid] += 1
                self.occupied[grid][cls] |= 1 << slot
                self.mapped |= pages
                self.live[token] = (grid, cls, slot, pages)
                return self.g.capacities[cls]
        return None

    def free(self, token):
        grid, cls, slot, _ = self.live.pop(token)
        self.identities[grid] -= 1
        self.occupied[grid][cls] &= ~(1 << slot)

    def trim(self):
        live = set().union(*(record[3] for record in self.live.values())) if self.live else set()
        # Independent interval oracle, rather than trusting saved page memberships.
        oracle = set()
        for grid, cls, slot, _ in self.live.values():
            lo, hi = self.g.home(grid, cls, slot)
            cursor = lo
            while cursor < hi:
                oracle.add(cursor // self.g.page)
                cursor = min(hi, roundup(cursor + 1, self.g.page))
        assert oracle == live and live <= self.mapped
        reclaimed = len(self.mapped - live) * self.g.page
        self.mapped = live
        return reclaimed


class Pool:
    def __init__(self, family, layout, page, selection, sizes):
        self.family, self.layout, self.page, self.selection = family, layout, page, selection
        self.sizes = sizes
        self.groups, self.tokens = [], {}
        self.requested = self.capacity = self.copied = self.moves = 0
        self.peak = 0

    def allocate(self, token, requested, alignment):
        names = FAMILIES[self.family]
        if self.family == "current":
            wanted = "B" if alignment >= 64 or requested > 192 else "A"
        else:
            wanted = min((n for n in names if PALETTES[n][-1] >= requested),
                         key=lambda n: (PALETTES[n][bisect.bisect_left(PALETTES[n], requested)], names.index(n)))
        existing = self.groups if self.selection == "reuse-first" else [g for g in self.groups if g.g.palette == wanted]
        for g in existing:
            c = g.allocate(token, requested, alignment)
            if c is not None:
                break
        else:
            g = Group(Geometry(wanted, self.layout, self.page))
            self.groups.append(g)
            c = g.allocate(token, requested, alignment)
            assert c is not None
        self.tokens[token] = (g, requested, c)
        self.requested += requested
        self.capacity += c
        self.peak = max(self.peak, self.charge())

    def free(self, token):
        g, req, cap = self.tokens.pop(token)
        g.free(token)
        self.requested -= req
        self.capacity -= cap

    def charge(self):
        return self.backed() + self.metadata()

    def backed(self):
        return sum(len(g.mapped) * self.page for g in self.groups)

    def metadata(self):
        return sum(g.g.control + self.sizes["Group"] + self.sizes["FiniteGroup"] +
                   16 * max(0, g.g.classes - 15) + 8 * g.g.words for g in self.groups)

    def trim(self):
        reclaimed = sum(g.trim() for g in self.groups)
        self.groups = [g for g in self.groups if g.live]
        return reclaimed


def requests(workload, count):
    rng = random.Random(20261012)
    if workload == "uniform":
        return [rng.randint(16, 3072) for _ in range(count)]
    if workload == "boundary":
        return [rng.choice((193, 257, 513, 769, 1025, 1537, 1793, 2049, 2561)) for _ in range(count)]
    if workload == "cluster":
        return [rng.randint(2017, 2112) for _ in range(count)]
    if workload == "tiny":
        return [rng.choice((24, 40, 72, 88, 112, 144, 176, 208)) for _ in range(count)]
    if workload == "log":
        return [min(3072, int(16 * (192 ** rng.random()))) for _ in range(count)]
    raise ValueError(workload)


def trace(family, layout, page, selection, workload, count, alignment, lifetime, sizes):
    p = Pool(family, layout, page, selection, sizes)
    req = requests(workload, count)
    for i, size in enumerate(req):
        p.allocate(i, size, alignment)
    initial = {"requested": p.requested, "capacity": p.capacity, "backed": p.backed(), "metadata": p.metadata(), "groups": len(p.groups)}
    # Survivors are workload IDs, never selected by a candidate's page placement.
    keep = set(random.Random(9).sample(range(count), max(1, count // 16))) if lifetime == "scattered" else set(range(max(1, count // 16)))
    for i in range(count):
        if i not in keep:
            p.free(i)
    reclaimed = p.trim()
    survivor_req, survivor_backed = p.requested, p.backed()
    # Old uses survive while separate successor allocations grow by ~25%.
    for i in sorted(keep):
        successor = min(3072, req[i] + max(16, req[i] // 4))
        p.allocate(count + i, successor, alignment)
        p.copied += req[i]
        p.moves += 1
    for i in sorted(keep):
        p.free(i)
    p.trim()
    final_charge = p.charge()
    for i in list(p.tokens):
        p.free(i)
    p.trim()
    assert p.charge() == 0
    return dict(family=family, layout=layout, page=page, selection=selection,
                workload=workload, count=count, alignment=alignment, lifetime=lifetime,
                **initial, slack=initial["capacity"]-initial["requested"],
                survivors_requested=survivor_req, survivors_backed=survivor_backed,
                reclaimed=reclaimed, peak_charge=p.peak, final_charge=final_charge,
                successor_copied=p.copied, successor_allocations=p.moves)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/allocation-palettes/results"))
    parser.add_argument("--sizes", type=Path, required=True, help="native --sizes JSON")
    args = parser.parse_args()
    verify()
    args.output.mkdir(parents=True, exist_ok=True)
    sizes = json.loads(args.sizes.read_text())
    with (args.output / "layouts.tsv").open("w") as f:
        f.write("layout\tpage\tpayload_bytes\tgrid_stride\tcls\tcapacity\toffset\tclasses\tslots\tclass_bits\n")
        for page in (4096, 16384, 65536):
            for name in PALETTES:
                for layout in ("class-aligned", "class-packed", "grid-packed"):
                    g = Geometry(name, layout, page)
                    for cls, c in enumerate(g.capacities):
                        f.write(f"{name}/{layout}\t{page}\t{g.payload}\t{g.strides[cls]}\t{cls}\t{c}\t{g.offsets[cls]}\t{g.classes}\t{g.slots}\t{g.bits}\n")
    rows = []
    for page in (4096, 16384, 65536):
        for family in FAMILIES:
            for layout in ("class-aligned", "class-packed", "grid-packed"):
                for selection in ("tight", "reuse-first"):
                    for workload in ("uniform", "boundary", "cluster", "tiny", "log"):
                        for count in (32, 256, 4096):
                            for alignment in (16, 64):
                                for lifetime in ("scattered", "clustered"):
                                    rows.append(trace(family, layout, page, selection, workload, count, alignment, lifetime, sizes))
    with (args.output / "traces.csv").open("w") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    manifest = {"kind": "deterministic geometry/lifetime model; no modeled timings or RSS",
                "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                "palettes": PALETTES, "native_record_sizes": sizes, "traces": len(rows),
                "limits": ["256 identities; 16/32/64 grids of16/8/4 identities; one-byte 15x16/32x8/51x4 home coordinates;0xff reserved; no concurrent/pending work",
                           "exact requested metadata, excluding containers and malloc overhead",
                           "split-bit metadata projects extra capacity/offset tables and byte occupancy masks; not measured record layouts",
                           "trim immediately discards idle modeled granules/empty Groups",
                           "reuse-first is group-order search, without production ready-cache shortcuts",
                           "successor growth is separate allocation while predecessor stays live, not held grow"]}
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"{len(rows)} modeled traces; exhaustive geometry checks passed; outputs: {args.output}")


if __name__ == "__main__":
    main()
