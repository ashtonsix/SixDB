#!/usr/bin/env python3
"""Cold bucket-layout search. Exact packing facts; no invented cycle estimates.

Emits a structural frontier and TuplePack fragments for a selected candidate.
Measured complete-consumer selection is a separate input, not inferred from
byte counts. The current native map implements only a subset of this grammar.
"""
import argparse
from dataclasses import asdict, dataclass
from fractions import Fraction
import hashlib
import itertools
import json
import math


def align(n, unit):
    return (n + unit - 1) // unit * unit


@dataclass(frozen=True)
class Geometry:
    kb: int
    kc: int
    value_bytes: int
    kind: str
    platform: str
    line_bytes: int = 64
    max_descendants: int = (1 << 32) - 1
    block_bits: int = 24
    role: str = "primary"

    def validate(self):
        if not (0 <= self.kb <= 8 and 0 <= self.kc <= 64 - self.kb):
            raise ValueError("Kb must be 0..8; Kb+Kc must be <=64")
        if self.value_bytes < 0 or self.kind not in ("set", "map", "multimap"):
            raise ValueError("nonnegative value size and set/map/multimap required")
        if self.kind == "set" and self.value_bytes:
            raise ValueError("set has no value bytes")
        if self.platform not in ("neon", "avx2-amd", "avx2-intel", "avx512-amd", "avx512-intel", "sve2"):
            raise ValueError("unknown platform recipe family")
        if self.line_bytes < 16 or self.line_bytes & (self.line_bytes - 1):
            raise ValueError("line bytes must be a power of two >=16")
        if self.max_descendants < 1 or not 0 <= self.block_bits <= 56:
            raise ValueError("declare a positive descendant bound and 0..56 block-ID bits")
        if self.role not in ("primary", "secondary", "single"):
            raise ValueError("role must be primary, secondary or single")


@dataclass(frozen=True)
class Field:
    name: str
    slot: int
    start: int
    bits: int
    logical_bit: int = 0

    def interval(self):
        return self.start // 8, (self.start + self.bits + 7) // 8


@dataclass
class Layout:
    geometry: Geometry
    slots: int
    placement: str
    packing: str
    fingerprints: str
    rounding: int
    fields: list
    fp_groups: list
    tuple_regions: list
    useful: int
    stride: int
    overflow_possible: bool
    physical_fp_bits: int

    def identity(self):
        config = [asdict(self.geometry), self.slots, self.placement, self.packing,
                  self.fingerprints, self.rounding, self.physical_fp_bits]
        return hashlib.sha256(json.dumps(config, sort_keys=True).encode()).hexdigest()[:16]


def construct(g, slots, placement, packing, fingerprints, rounding, physical_fp_bits=None):
    g.validate()
    physical_fp_bits = g.kb if physical_fp_bits is None else physical_fp_bits
    if not g.kb <= physical_fp_bits <= 8 or (not g.kb and physical_fp_bits):
        raise ValueError("physical fingerprint width must cover Kb; omit zero-bit fingerprints")
    if slots < 1 or slots > (1 << (g.kb + g.kc)):
        raise ValueError("capacity exceeds reachable distinct keys")
    if rounding < 1:
        raise ValueError("positive stride quantum required")
    fields, fp, regions = [], [], []
    bit = 0

    def field(name, slot, bits, logical_bit=0):
        nonlocal bit
        if bits:
            if packing == "byte":
                bit = align(bit, 8)
            fields.append(Field(name, slot, bit, bits, logical_bit))
            bit += align(bits, 8) if packing == "byte" else bits

    def finish_region(first):
        nonlocal bit
        bit = align(bit, 8)
        if bit != first:
            regions.append((first // 8, (bit - first) // 8))

    def fingerprints_for(first, rows):
        nonlocal bit
        if not g.kb:
            return
        bit = align(bit, 8)
        for i in range(first, first + rows, 8):
            fp.append({"offset": bit // 8, "first_slot": i,
                       "live_slots": min(8, first + rows - i), "physical_lanes": 8,
                       "bytes": physical_fp_bits, "logical_bits": g.kb,
                       "format": f"SeriesPack Local<{physical_fp_bits}>"})
            bit += physical_fp_bits * 8

    overflow = slots < (1 << (g.kb + g.kc))
    field("occupancy", -1, slots.bit_length())
    # A single-bucket material returns grow-required before spilling. Its owner
    # changes topology; a large key domain alone does not require descendants.
    if overflow and g.role != "single":
        # Conservative admitted maximum, not a small counter inferred from N.
        bound = min(g.max_descendants, (1 << (g.kb + g.kc)) - 1)
        field("descendants", -1, bound.bit_length())
        if g.role == "secondary":
            # Validity is supplied by descendant count; rank255 is a real rank.
            field("overflow_ref", -1, g.block_bits + 8)
    finish_region(0)
    if fingerprints == "global" and placement != "key_words":
        fingerprints_for(0, slots)

    value_bits = 8 * g.value_bytes
    if g.kind == "multimap":
        value_bits = max(value_bits, g.block_bits + 8)
    entry = [("suffix", g.kc)]
    if g.kind != "set":
        entry.append(("value_or_ref" if g.kind == "multimap" else "value", value_bits))
    if g.kind == "multimap":
        entry.append(("chain_tag", 1))
    if placement == "key_words":
        if (packing, fingerprints, g.kb, g.kc, physical_fp_bits) != ("byte", "global", 8, 24, 8):
            raise ValueError("word-tile discriminator requires R32 with an 8-bit fingerprint")
        start = bit
        for slot in range(slots):
            field("suffix", slot, 24)
            fp.append({"offset": bit//8, "first_slot": slot, "live_slots": 1,
                       "physical_lanes": 1, "bytes": 1, "logical_bits": 8,
                       "format": "authored SeriesPack byte projection, stride 4"})
            bit += 8
        finish_region(start)
        start = bit
        for slot in range(slots):
            for name, bits in entry[1:]:
                field(name, slot, bits)
        finish_region(start)
    elif placement == "key_planes":
        if packing != "byte" or fingerprints != "global":
            raise ValueError("key planes require byte fragments and global fingerprints")
        # Each suffix byte has the same physical lanes as the SP selector.
        # Padding is initialized, charged storage; inactive lanes are masked.
        lanes = align(slots, 8)
        start = bit
        for shift in range(0, g.kc, 8):
            for slot in range(slots):
                field("suffix", slot, min(8, g.kc-shift), shift)
            bit += (lanes-slots)*8
        finish_region(start)
        start = bit
        for slot in range(slots):
            for name, bits in entry[1:]:
                field(name, slot, bits)
        finish_region(start)
    else:
        group = {"rows": slots, "planes": slots, "groups8": 8, "groups16": 16}[placement]
        for first in range(0, slots, group):
            rows = min(group, slots - first)
            if fingerprints == "grouped":
                fingerprints_for(first, rows)
            start = bit
            if placement == "rows":
                for slot in range(first, first + rows):
                    for name, bits in entry:
                        field(name, slot, bits)
            else:
                for name, bits in entry:
                    for slot in range(first, first + rows):
                        field(name, slot, bits)
                    # Byte-align mini-planes; tight fields still share boundary bytes.
                    bit = align(bit, 8)
            finish_region(start)
    useful = bit // 8
    return Layout(g, slots, placement, packing, fingerprints, rounding, fields,
                  fp, regions, useful, align(useful, rounding), overflow, physical_fp_bits)


def interval_union_size(intervals):
    total, end = 0, -1
    for a, b in sorted(intervals):
        total += max(0, b - max(a, end))
        end = max(end, b)
    return total


def lines(intervals, phase, width):
    return interval_union_size([((a + phase) // width, (b + phase - 1) // width + 1)
                                for a, b in intervals if b > a])


def structural_metrics(layout):
    """Logical minimum windows only; native plan windows can be wider."""
    g = layout.geometry
    header = [f.interval() for f in layout.fields if f.slot == -1]
    fp = [(f["offset"], f["offset"] + f["bytes"]) for f in layout.fp_groups]
    occupancy = [f.interval() for f in layout.fields if f.name == "occupancy"]
    exact_spare_sentinel = g.kc == 0 and layout.physical_fp_bits > g.kb
    if exact_spare_sentinel:
        occupancy = []
    phases = range(0, g.line_bytes, math.gcd(layout.stride, g.line_bytes))
    # Complete phase orbit of a cache-line-aligned array; short arrays are not
    # assumed to sample this orbit uniformly in measured selection.
    point, missing = [], []
    fragments = 0
    for phase in phases:
        missing.append(lines(header + fp, phase, g.line_bytes))
        for slot in range(layout.slots):
            selected = [f for f in layout.fields if f.slot == slot]
            point.append(lines(occupancy + fp + [f.interval() for f in selected], phase, g.line_bytes))
    for slot in range(layout.slots):
        fragments += sum((f.start % 8 + f.bits + 7) // 8 for f in layout.fields if f.slot == slot)
    return {"bucket_bytes": layout.stride, "logical_slots": layout.slots,
            "bytes_per_reserved_slot": str(Fraction(layout.stride, layout.slots)),
            "logical_hit_lines_mean": str(Fraction(sum(point), len(point))),
            "logical_negative_lines_max": max(missing),
            "field_fragments_mean": str(Fraction(fragments, layout.slots)),
            "physical_fp_lanes": sum(f["physical_lanes"] for f in layout.fp_groups),
            "physical_fp_bits": layout.physical_fp_bits,
            "exact_spare_sentinel": exact_spare_sentinel,
            "rank_weights": "uniform reserved ranks; actual dense-prefix occupancy must replace this for measured selection",
            "phase_count": len(phases), "padding_bytes": layout.stride - layout.useful,
            "counts_and_references": [asdict(f) for f in layout.fields if f.slot == -1],
            "ordinary_hash_split_compatible": g.kc == 0 or g.kb == 8,
            "overflow_possible": layout.overflow_possible,
            "stores_descendants": any(f.name == "descendants" for f in layout.fields),
            "stream_link_single_entry_fits": (g.kb + g.kc + 8 * g.value_bytes + 7) // 8 <= 65535,
            "scope": "structural lower bounds; excludes secondary/chain pressure, native overreads, controls and allocator rounding"}


def lower(layout):
    """Field-preserving views;64B is a description limit, not a storage tile.

    Row views may overlap physical boundary bytes in tightly packed storage.
    Their code masks own disjoint bits. Identical relative descriptions are
    shared; the slot and physical base belong to the authored consumer.
    """
    units = []

    def finish(base, fragments):
        if not fragments:
            return
        maps = []
        for key, items in itertools.groupby(sorted(enumerate(fragments),
                key=lambda p: (p[1]["field"], p[1]["slot"], p[1]["logical_bit"])),
                key=lambda p: (p[1]["field"], p[1]["slot"])):
            items = list(items)
            for start in range(0, len(items), 64):
                part = items[start:start + 64]
                maps.append({"field": key[0], "slot": key[1],
                    "packet_bytes": 8 if len(part) <= 8 else 64,
                    "code_indices": [i for i, _ in part],
                    "logical_bits": [f["logical_bit"] for _, f in part]})
        units.append({"offset": base,
                      "bytes": max(f["offset"] for f in fragments) + 1,
                      "fragments": fragments, "projections": maps})

    groups = []
    for first, size in layout.tuple_regions:
        fields = sorted((f for f in layout.fields
                         if first * 8 <= f.start < (first + size) * 8),
                        key=lambda f: f.start)
        if layout.placement == "rows":
            groups.extend(list(fs) for _, fs in itertools.groupby(fields, key=lambda f: f.slot))
        else:
            groups.append(fields)
    for fields in groups:
        base, fragments = None, []
        for f in fields:
            begin, end = f.start, f.start + f.bits
            while begin < end:
                if base is None:
                    base = begin // 8
                # Flush before a whole field that fits its own description.
                # Do not split a value merely because an earlier64B cut did.
                if fragments and (end + 7) // 8 - begin // 8 <= 64 and (end + 7) // 8 - base > 64:
                    finish(base, fragments)
                    base, fragments = None, []
                    continue
                stop = min(end, (base + 64) * 8)
                pieces, at = [], begin
                while at < stop:
                    width = min(stop - at, 8 - at % 8)
                    pieces.append({"field": f.name, "slot": f.slot,
                        "logical_bit": f.logical_bit + at - f.start, "offset": at // 8 - base,
                        "shift": at % 8, "width": width})
                    at += width
                if not pieces or len(fragments) + len(pieces) > 128:
                    finish(base, fragments)
                    base, fragments = None, []
                    continue
                fragments.extend(pieces)
                begin = stop
                if begin < end:
                    finish(base, fragments)
                    base, fragments = None, []
        finish(base, fragments)

    descriptions, ids = [], {}
    for unit in units:
        description = {"bytes": unit["bytes"],
            "codes": [[f["offset"], f["shift"], f["width"]] for f in unit["fragments"]],
            "projections": [{k: v for k, v in p.items() if k != "slot"} for p in unit["projections"]]}
        identity = json.dumps(description, sort_keys=True)
        if identity not in ids:
            ids[identity] = len(descriptions)
            descriptions.append(description)
        unit["description"] = ids[identity]
    return {"fingerprint_groups": layout.fp_groups, "tuple_units": units,
            "tuple_descriptions": descriptions,
            "field_reconstruction": "one zero-extended byte per code; OR each fragment at its logical_bit",
            "execution_status": "cold lowering; native preparation/window verification required before execution"}


def candidates(g, capacities=None):
    domain = 1 << (g.kb + g.kc)
    capacities = capacities or range(1, min(32, domain) + 1)
    seen = set()
    widths = (g.kb, 8) if 0 < g.kb < 8 else (g.kb,)
    for n, placement, packing, fingerprints, width in itertools.product(capacities,
            ("rows", "planes", "groups8", "groups16", "key_planes", "key_words"), ("tight", "byte"), ("global", "grouped"), widths):
        if n > domain or (placement == "key_planes" and (packing != "byte" or fingerprints != "global")):
            continue
        if placement == "key_words" and (packing,fingerprints,g.kb,g.kc,width) != ("byte","global",8,24,8):
            continue
        for rounding in (1, 16, 32, g.line_bytes, g.line_bytes * 3 // 2, 2 * g.line_bytes):
            layout = construct(g, n, placement, packing, fingerprints, rounding, width)
            shape = (n, width, layout.stride, tuple(layout.fields),
                     tuple((p["offset"], p["first_slot"]) for p in layout.fp_groups), tuple(layout.tuple_regions))
            if shape not in seen:
                seen.add(shape)
                yield layout


def frontier(g, capacities=None):
    best = []
    for layout in candidates(g, capacities):
        metrics = structural_metrics(layout)
        score = tuple(Fraction(metrics[k]) for k in ("bytes_per_reserved_slot",
            "logical_hit_lines_mean", "logical_negative_lines_max", "field_fragments_mean"))
        # Different capacities change pow2 bucket count/load/overflow pressure;
        # different fingerprint widths change native comparison and sentinel
        # costs. Neither may be discarded using storage-only domination.
        family=lambda l: l.placement if l.placement in ("key_planes","key_words") else "selective"
        comparable=lambda other: ((other.slots,other.physical_fp_bits,family(other))==
            (layout.slots,layout.physical_fp_bits,family(layout)))
        if any(comparable(other) and all(a <= b for a, b in zip(prior, score)) and prior != score for prior, other, _ in best):
            continue
        best = [(prior, l, m) for prior, l, m in best
                if not (comparable(l) and all(a <= b for a, b in zip(score, prior)) and score != prior)]
        best.append((score, layout, metrics))
    return [{"id": l.identity(), "slots": l.slots, "placement": l.placement,
             "packing": l.packing, "fingerprints": l.fingerprints,
             "physical_fp_bits": l.physical_fp_bits,
             "rounding": l.rounding, "metrics": m} for _, l, m in best]


def checks():
    cases = 0
    for kb, kc, kind, vb, n in itertools.product((0, 1, 7, 8), (0, 1, 7, 9, 55),
            ("set", "map", "multimap"), (0, 1, 8, 17, 80), (1, 8, 14, 17, 32)):
        if (kind == "set" and vb) or n > (1 << (kb + kc)):
            continue
        for placement,width in itertools.product(("rows", "groups8", "planes", "key_planes"), (kb,8) if 0<kb<8 else (kb,)):
            g = Geometry(kb, kc, vb, kind, "neon")
            l = construct(g, n, placement, "byte" if placement=="key_planes" else "tight",
                          "global" if placement=="key_planes" else "grouped", 96,width)
            owned = set()
            for f in l.fields:
                bits = set(range(f.start, f.start + f.bits))
                assert not (owned & bits)
                owned |= bits
            for p in l.fp_groups:
                bits = set(range(p["offset"] * 8, (p["offset"] + p["bytes"]) * 8))
                assert not (owned & bits)
                owned |= bits
            assert max(owned, default=0) < l.stride * 8
            reconstructed = set()
            field_by_key = {}
            for f in l.fields:field_by_key.setdefault((f.name,f.slot),[]).append(f)
            for u in lower(l)["tuple_units"]:
                assert 1 <= u["bytes"] <= 64 and 1 <= len(u["fragments"]) <= 128
                for f in u["fragments"]:
                    assert 0 <= f["offset"] < u["bytes"] and 1 <= f["width"] <= 8 - f["shift"]
                    original = next(part for part in field_by_key[(f["field"], f["slot"])]
                                    if part.logical_bit<=f["logical_bit"]<part.logical_bit+part.bits)
                    physical = (u["offset"] + f["offset"]) * 8 + f["shift"]
                    assert physical == original.start + f["logical_bit"] - original.logical_bit
                    for b in range(physical, physical + f["width"]):
                        assert b not in reconstructed
                        reconstructed.add(b)
                for m in u["projections"]:
                    assert len(m["code_indices"]) <= m["packet_bytes"]
            assert reconstructed == {b for f in l.fields for b in range(f.start, f.start + f.bits)}
            cases += 1
    row = lower(construct(Geometry(8, 47, 8, "map", "neon"),16,"rows","byte","global",1))
    # One shared relative row description; every8B value stays in one view.
    assert len(row["tuple_units"]) == 17 and len(row["tuple_descriptions"]) == 2
    for slot in range(16):
        values = [p for u in row["tuple_units"] for p in u["projections"]
                  if p["field"] == "value" and p["slot"] == slot]
        assert len(values) == 1 and values[0]["packet_bytes"] == 8
        assert len(values[0]["code_indices"]) == 8
    for n,kind,vb in itertools.product((1,8,14,16,32),("set","map","multimap"),(0,8,24)):
        if kind=="set" and vb:continue
        g=Geometry(8,24,vb,kind,"neon")
        l=construct(g,n,"key_words","byte","global",1)
        owned={b for f in l.fields for b in range(f.start,f.start+f.bits)}
        for p in l.fp_groups:
            bits=set(range(p['offset']*8,(p['offset']+1)*8))
            assert not bits&owned
            owned|=bits
        assert len(owned)==l.stride*8-(8-n.bit_length()+7*(kind=='multimap')*n)
        reconstructed={((u['offset']+f['offset'])*8+f['shift']+b)
            for u in lower(l)['tuple_units'] for f in u['fragments'] for b in range(f['width'])}
        assert reconstructed=={b for f in l.fields for b in range(f.start,f.start+f.bits)}
        cases+=1
    return cases


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--check", action="store_true")
    p.add_argument("--kb", type=int, default=8)
    p.add_argument("--kc", type=int, default=47)
    p.add_argument("--value-bytes", type=int, default=8)
    p.add_argument("--kind", default="map")
    p.add_argument("--platform", default="neon")
    p.add_argument("--capacities", type=int, nargs="+")
    p.add_argument("--emit", help="emit full cold lowering for this candidate ID")
    args = p.parse_args()
    if args.check:
        print(json.dumps({"packing_and_lowering_cases": checks()}))
        return
    g = Geometry(args.kb, args.kc, args.value_bytes, args.kind, args.platform)
    g.validate()
    if args.emit:
        for layout in candidates(g, args.capacities):
            if layout.identity() == args.emit:
                print(json.dumps({"id": args.emit, "geometry": asdict(g),
                                  "metrics": structural_metrics(layout), "lowering": lower(layout)}, indent=2))
                return
        p.error("candidate ID not found for these inputs")
    print(json.dumps({"geometry": asdict(g), "selection": "structural frontier, not a performance winner",
                      "frontier": frontier(g, args.capacities)}, indent=2))


if __name__ == "__main__":
    main()
