#!/usr/bin/env python3
"""Check the improved-design arithmetic against independent models and retained bytes.

This is a design check, not a native codec or a timing harness.
Run from the repository root on Linux with the pinned workspace: python3 .../design-check.py.
"""
from itertools import accumulate, product
from pathlib import Path
import csv
import random

ROOT = Path(__file__).resolve().parents[3]
G, CAPACITY = 16, 256
KINDS = ("absolute_ends", "sizes", "relative_ends")


def carrier(bound):
    return next(n for n in (1, 2, 4, 8) if bound < 1 << (8 * n))


def geometry_bytes(kind, maximum, limit, count, checkpoints=False, retained_floor=False):
    # Curated capacity, full-capacity directory and G16 padding;
    # the format bound is independent of count. No U, allocator padding or payload.
    assert 0 <= count <= CAPACITY and 0 <= limit <= CAPACITY * maximum
    if maximum == 0 or limit == 0:
        return 32 + 2  # Variable geometry is proven zero; D remains present.
    w, o = carrier(min(maximum, limit)), carrier(limit)
    local = carrier(min(G * min(maximum, limit), limit))
    if retained_floor:
        # encoding.cpp instantiates only u16/u32 coordinates and relative ends.
        # Do not turn its tight-bound cases into evidence for native u8 forms.
        o, local = max(2, o), max(2, local)
    if kind == "absolute_ends":
        return 32 + CAPACITY * o + (G * o if checkpoints else 0) + 2
    return 32 + G * o + ((count + G - 1) // G) * G * (w if kind == "sizes" else local) + 6


class Geometry:
    def __init__(self, kind, widths, origin=0):
        self.kind, self.n, self.origin = kind, len(widths), origin
        ends = [origin + x for x in accumulate(widths)]
        self.extent = ends[-1] if ends else origin
        self.checkpoints = [origin if i == 0 else ends[i - 1] for i in range(0, self.n, G)]
        if kind == "absolute_ends":
            self.values = ends[:]
        else:
            self.values = [0] * (((self.n + G - 1) // G) * G)
            for i, width in enumerate(widths):
                self.values[i] = width if kind == "sizes" else ends[i] - self.checkpoints[i // G]

    def decode(self):
        if self.kind == "absolute_ends":
            return self.values[:self.n]
        if self.kind == "relative_ends":
            return [self.checkpoints[i // G] + self.values[i] for i in range(self.n)]
        return [self.checkpoints[i // G] + sum(self.values[i // G * G:i + 1]) for i in range(self.n)]

    def boundary(self, rank, delta):
        """Change only E[rank]; preserve every later endpoint."""
        if self.kind == "absolute_ends":
            self.values[rank] += delta
        elif self.kind == "sizes":
            self.values[rank] += delta
            if rank + 1 < self.n:
                self.values[rank + 1] -= delta
            if (rank + 1) % G == 0 and rank + 1 < self.n:
                self.checkpoints[(rank + 1) // G] += delta
        else:
            self.values[rank] += delta
            if (rank + 1) % G == 0 and rank + 1 < self.n:
                self.checkpoints[(rank + 1) // G] += delta
                for i in range(rank + 1, min(self.n, rank + 1 + G)):
                    self.values[i] -= delta
        if rank + 1 == self.n:
            self.extent += delta

    def resize(self, rank, delta):
        """Change R[rank]; translate every endpoint from rank onward."""
        if self.kind == "absolute_ends":
            for i in range(rank, self.n):
                self.values[i] += delta
        elif self.kind == "sizes":
            self.values[rank] += delta
        else:
            for i in range(rank, min(self.n, (rank // G + 1) * G)):
                self.values[i] += delta
        if self.kind != "absolute_ends":
            for g in range(rank // G + 1, len(self.checkpoints)):
                self.checkpoints[g] += delta
        self.extent += delta

    def append(self, width):
        assert self.n < CAPACITY
        if self.n % G == 0:
            self.checkpoints.append(self.extent)
            if self.kind != "absolute_ends":
                self.values.extend([0] * G)
        end = self.extent + width
        if self.kind == "absolute_ends":
            self.values.append(end)
        else:
            self.values[self.n] = (width if self.kind == "sizes" or self.n % G == 0
                                   else self.values[self.n - 1] + width)
        self.n += 1
        self.extent = end

    def rewrite(self, choose):
        """One in-place traversal; preserve original carries before each sink."""
        old_global, old_local, new_global, new_local = self.origin, 0, self.origin, 0
        for i in range(self.n):
            if i % G == 0:
                old_local = new_local = 0
            old = self.values[i]
            if self.kind == "absolute_ends":
                width = old - old_global
            elif self.kind == "sizes":
                width = old
            else:
                width = old - old_local
                old_local = old
            chosen = choose(i, old_global, width)
            old_global += width
            if i % G == 0:
                self.checkpoints[i // G] = new_global
            new_global += chosen
            new_local += chosen
            self.values[i] = new_global if self.kind == "absolute_ends" else chosen if self.kind == "sizes" else new_local
        self.extent = new_global


def check_mutations():
    rng, cases = random.Random(713), 0
    for n, maximum, origin in product((0, 1, 15, 16, 17, 31, 32, 33, 64, 128, 255, 256),
                                      (0, 1, 15, 255, 4095, 65535), (0, 1 << 40, 1 << 63)):
        widths = [rng.choice((0, maximum, rng.randrange(maximum + 1))) for _ in range(n)]
        expected = [origin + x for x in accumulate(widths)]
        for kind in KINDS:
            if n < CAPACITY:
                for added in (0, maximum):
                    g = Geometry(kind, widths, origin)
                    g.append(added)
                    assert g.decode() == [origin + x for x in accumulate(widths + [added])]
                    if kind != "absolute_ends":
                        assert all(x == 0 for x in g.values[g.n:])
                    cases += 1
            for rank in range(n):
                for delta in (-1, 1):
                    if 0 <= widths[rank] + delta <= maximum:
                        g = Geometry(kind, widths, origin)
                        g.resize(rank, delta)
                        assert g.decode() == [x + (delta if i >= rank else 0) for i, x in enumerate(expected)]
                        assert g.extent == expected[-1] + delta
                        cases += 1
                    if 0 <= widths[rank] + delta <= maximum and (rank + 1 == n or 0 <= widths[rank + 1] - delta <= maximum):
                        g = Geometry(kind, widths, origin)
                        g.boundary(rank, delta)
                        assert g.decode() == [x + (delta if i == rank else 0) for i, x in enumerate(expected)]
                        assert g.extent == expected[-1] + (delta if rank + 1 == n else 0)
                        cases += 1
            # Independent chosen-size oracle, with retired, live-zero and shrinking slots.
            chosen = [0 if i % 5 == 0 else w // 2 for i, w in enumerate(widths)]
            g = Geometry(kind, widths, origin)
            def sink(i, start, size):
                assert start == origin + sum(widths[:i]) and size == widths[i]
                return chosen[i]
            g.rewrite(sink)
            assert g.decode() == [origin + x for x in accumulate(chosen)]
            assert g.extent == origin + sum(chosen)
            if kind != "absolute_ends":
                assert all(x == 0 for x in g.values[n:])
            cases += 1
    return cases


def check_retained_bytes():
    file = ROOT / "workbench/spikes/boundlist/evidence/ownership/encoding.csv"
    count = 0
    for row in csv.DictReader(file.open()):
        fmt = row["format"]
        kind = {"direct": "absolute_ends", "fused_direct_scan": "absolute_ends", "direct_checkpoints": "absolute_ends",
                "sizes": "sizes", "sizes_full_prefix": "sizes", "relative_ends": "relative_ends"}[fmt]
        maximum, n = int(row["maximum"]), int(row["count"])
        assert int(row["encoded_bytes"]) == geometry_bytes(kind, maximum, n * maximum, n, fmt == "direct_checkpoints", retained_floor=True), row
        count += 1
    return count


def check_headers():
    # Tag arithmetic only; the native literal fixtures test serialization. Legacy bytes use
    # their original derivation rules; a descriptor never silently changes meaning.
    bl3 = {0x60 | k | (imm << 3) | (fixed << 4) for k, imm, fixed in product(range(2), range(2), range(2))}
    bl4 = {0x80 | k | (imm << 3) | (fixed << 4) for k, imm, fixed in product(range(3), range(2), range(2))}
    sp2 = set(range(0x40, 0x60))
    sp3 = {0xc0 | k | (sep << 2) | (full << 3) | (imm << 4) | (fixed << 5)
           for k, sep, full, imm, fixed in product(range(3), range(2), range(2), range(2), range(2))}
    assert not bl3 & bl4 and not sp2 & sp3
    for tag in range(256):
        assert (tag in bl4) == (tag >> 5 == 4 and not tag & 4 and tag & 3 != 3)
        assert (tag in sp3) == (tag >> 6 == 3 and tag & 3 != 3)
    for k in range(3):
        assert (0x80 | k) & 3 == k and (0xc0 | k) & 3 == k


def check_placement():
    cases = 0
    for maximum, limit, n, kind in product((0, 1, 15, 255, 4095, 4096, 65535),
                                           (0, 255, 4095, 65535, 65536),
                                           (0, 1, 15, 16, 17, 31, 32, 128, 255, 256),
                                           ("sizes", "relative_ends")):
        if limit > CAPACITY * maximum:
            continue
        u = carrier(min(maximum, limit))
        e = u if kind == "sizes" else carrier(min(G * maximum, limit))
        assert e >= u
        assigned = ((n + G - 1) // G) * G
        geometry_first = CAPACITY * e + assigned * u if n else 0
        used_first = CAPACITY * u + assigned * e if n else 0
        # The smaller partial-prefix alternative was measured, then rejected:
        # its dynamic geometry base taxed the common read/write path.
        assert used_first <= geometry_first
        if n == CAPACITY:
            assert used_first == geometry_first
        if maximum == 255 and limit == 65535 and n == 17:
            assert (geometry_first, used_first) == (544, 288)
        # Each field start satisfies its natural carrier alignment; no 48/96
        # alignment requirement follows merely from the grouped byte stride.
        group_bytes = G * (e + u)
        assert group_bytes % e == 0 and group_bytes % u == 0
        assert G * e % u == 0
        cases += 1
    assert geometry_bytes("sizes", 0, 0, 256) == 34
    return cases


def main():
    rows, cases = check_retained_bytes(), check_mutations()
    check_headers()
    placement = check_placement()
    print(f"Verified {rows} retained footprint rows; {cases} append/boundary/resize/rewrite model cases; {placement} placement cases; descriptor tag separation.")
    print("Rmax,Limit,Absolute B,Sizes B,Relative B,Size carrier,Relative carrier,Coordinate carrier")
    for maximum, limit in ((255, 65280), (4095, 1048320), (4096, 1048576), (65535, 65535)):
        print(maximum, limit, *(geometry_bytes(k, maximum, limit, CAPACITY) for k in KINDS),
              carrier(min(maximum, limit)), carrier(min(G * min(maximum, limit), limit)), carrier(limit), sep=",")


if __name__ == "__main__":
    main()
