#!/usr/bin/env python3
"""Small-home packing and ownership sweep; geometry/retention counts, no timings.

64 grids contain 1024 possible identities and sixteen homes per small A class.
Packing tile and allocation owner widths vary independently. Grid-major stores
4 KiB images consecutively inside each tile; only tile pitch is page-rounded.
Virtual placement, larger finite classes, allocation policy and module zones
are absent. Existing small-packing evidence is not modified.
"""

from __future__ import annotations

import argparse
import itertools
import json
from dataclasses import dataclass
from pathlib import Path

CAPS = (16, 32, 48, 64, 96)
WIDTHS = (16, 64, 256, 1024)
GRIDS = 64
SLOTS = 16
LINE = 64
ASC = tuple(range(5))
PREFERRED = (3, 0, 2, 1, 4)
Coordinate = tuple[int, int, int]


def bits(begin: int, end: int, unit: int) -> int:
    first, last = begin // unit, (end - 1) // unit
    return ((1 << (last - first + 1)) - 1) << first


@dataclass(frozen=True)
class Geometry:
    page: int
    packing: int
    form: str
    order: tuple[int, ...] = ASC

    def offsets(self) -> tuple[dict[int, int], int]:
        rows = self.packing // SLOTS
        offsets, cursor = {}, 0
        for cls in self.order:
            if self.form == "class_page_aligned":
                cursor = (cursor + self.page - 1) // self.page * self.page
            offsets[cls] = cursor
            cursor += rows * SLOTS * CAPS[cls]
        return offsets, (cursor + self.page - 1) // self.page * self.page

    def interval(self, coordinate: Coordinate) -> tuple[int, int]:
        grid, cls, slot = coordinate
        rows = self.packing // SLOTS
        tile, row = divmod(grid, rows)
        offsets, pitch = self.offsets()
        if self.form == "grid_major":
            begin = self.page + tile * pitch + row * 4096 + sum(CAPS[:cls]) * SLOTS + slot * CAPS[cls]
        else:
            begin = self.page + tile * pitch + offsets[cls] + (row * SLOTS + slot) * CAPS[cls]
        return begin, begin + CAPS[cls]

    def description(self) -> dict:
        offsets, pitch = self.offsets()
        return dict(page_size=self.page, packing_trampolines=self.packing, form=self.form,
                    class_order=[CAPS[c] for c in self.order], tile_pitch_bytes=pitch,
                    small_payload_aperture_bytes=1024 // self.packing * pitch,
                    class_offsets_bytes=None if self.form == "grid_major" else {str(CAPS[c]): offsets[c] for c in range(5)})


def coordinates(grids: int, cls: int) -> list[Coordinate]:
    return [(grid, cls, slot) for grid in range(grids) for slot in range(SLOTS)]


def order_search(page: int, packing: int) -> tuple[tuple[int, ...], dict]:
    scores = []
    for order in itertools.permutations(range(5)):
        geometry = Geometry(page, packing, "class_major", order)
        counts = []
        for cls in range(5):
            page_mask = 0
            for coordinate in coordinates(packing // 16, cls):
                page_mask |= bits(*geometry.interval(coordinate), page)
            counts.append(page_mask.bit_count())
        scores.append((sum(counts), order, counts))
    minimum = min(score for score, _, _ in scores)
    best = [row for row in scores if row[0] == minimum]
    chosen = next((row for row in best if row[1] == PREFERRED), best[0])
    return chosen[1], dict(permutations=120, objective="equal-weight homogeneous full-packing-tile page-count sum",
                          minimum_page_sum=minimum, maximum_page_sum=max(row[0] for row in scores),
                          optimum_orders=len(best), selected_order=[CAPS[c] for c in chosen[1]],
                          selected_counts_by_capacity=dict(zip(map(str, CAPS), chosen[2])))


def validate(geometry: Geometry) -> None:
    ordered = sorted(geometry.interval((g, c, s)) for g in range(GRIDS) for c in range(5) for s in range(SLOTS))
    assert all(begin % 16 == 0 for begin, _ in ordered)
    assert all(a[1] <= b[0] for a, b in zip(ordered, ordered[1:]))
    assert sum(end - begin for begin, end in ordered) == 262144
    assert ordered[-1][1] <= geometry.page + geometry.description()["small_payload_aperture_bytes"]
    # Independent byte walk checks all 5120 possible homes, not only selected
    # snapshots. This also catches straddling homes in larger packing tiles.
    for begin, end in ordered:
        by_page = {byte // geometry.page for byte in range(begin, end)}
        by_line = {byte // LINE for byte in range(begin, end)}
        assert bits(begin, end, geometry.page) == sum(1 << page for page in by_page)
        assert bits(begin, end, LINE) == sum(1 << line for line in by_line)


def snapshots() -> dict[str, tuple[list[Coordinate], list[Coordinate], list[Coordinate]]]:
    """Current homes, explicitly pinned old homes, all retired candidates.

    Home slot equals identity slot in these absolute, layout-independent cases.
    A current and previous-class home can therefore belong to one identity.
    """
    result = {}
    for cls in (0, 3, 4):
        result[f"full_{CAPS[cls]}_1024_current"] = coordinates(64, cls), [], []
        result[f"dense_{CAPS[cls]}_64_current"] = coordinates(4, cls), [], []
        result[f"sparse_{CAPS[cls]}_64_current"] = [(g, cls, 0) for g in range(64)], [], []
    result["mixed_dense_64_current"] = ([(g, (g * 16 + s) % 5, s) for g in range(4) for s in range(16)], [], [])
    result["mixed_sparse_64_current"] = ([(g, g % 5, 0) for g in range(64)], [], [])
    selected = [(g, s) for g in range(3) for s in range(16)] + [(3, s) for s in range(12)] + [(g, 0) for g in (16, 32, 48, 63)]
    result["skew_few_grids_64_current"] = ([(g, (g * 16 + s) % 5, s) for g, s in selected], [], [])
    old_dense = coordinates(4, 0)
    result["delayed_dense_64_current"] = coordinates(4, 4), [(g, 0, s) for g in range(4) for s in (0, 8)], old_dense
    old_sparse = [(g, 0, 0) for g in range(64)]
    result["delayed_sparse_64_current"] = [(g, 4, 0) for g in range(64)], old_sparse[::4], old_sparse
    result["one_pin_1023_other_retired"] = [], [(0, 0, 0)], coordinates(64, 0)
    result["all_freed"] = [], [], coordinates(64, 0)
    return result


def ownership(geometry: Geometry, occupied: list[Coordinate], identities: set[tuple[int, int]], width: int) -> dict:
    rows = width // 16
    page_owners: dict[int, set[int]] = {}
    line_owners: dict[int, set[int]] = {}
    control_owners: dict[int, set[int]] = {}
    for coordinate in occupied:
        begin, end = geometry.interval(coordinate)
        owner = coordinate[0] // rows
        for unit, target in ((geometry.page, page_owners), (LINE, line_owners)):
            for index in range(begin // unit, (end - 1) // unit + 1):
                target.setdefault(index, set()).add(owner)
    for grid, slot in identities:
        control_owners.setdefault((grid * 16 + slot) // LINE, set()).add(grid // rows)
    assert all(len(owners) == 1 for owners in line_owners.values())
    return dict(payload_pages_shared_by_owners=sum(len(owners) > 1 for owners in page_owners.values()),
                max_owners_per_payload_page=max(map(len, page_owners.values()), default=0),
                control_lines_shared_by_owners=sum(len(owners) > 1 for owners in control_owners.values()),
                max_owners_per_control_line=max(map(len, control_owners.values()), default=0))


def measure(geometry: Geometry, current: list[Coordinate], pins: list[Coordinate], retired: list[Coordinate], scope: str) -> dict:
    assert len(set(current + pins)) == len(current) + len(pins)
    assert set(pins) <= set(retired)
    assert all(sum(g == coordinate[0] for coordinate in current) <= 16 for g in range(64))
    unit = 1 if scope == "grid" else geometry.packing // 16
    if scope == "home":
        kept = list(pins)
    else:
        # Hypothetical whole-unit destruction: a pin retains retired storage in
        # its unit. This is an optional retention policy, not required by the
        # coordinate geometry. Active replacements belong to a distinct cohort.
        pinned_units = {coordinate[0] // unit for coordinate in pins}
        kept = [coordinate for coordinate in retired if coordinate[0] // unit in pinned_units]
    occupied = current + kept
    intervals = [geometry.interval(coordinate) for coordinate in occupied]
    page_mask = line_mask = current_mask = 0
    for coordinate, (begin, end) in zip(occupied, intervals):
        page_mask |= bits(begin, end, geometry.page)
        line_mask |= bits(begin, end, LINE)
    for coordinate in current:
        current_mask |= bits(*geometry.interval(coordinate), geometry.page)
    npage = page_mask.bit_count()
    occupied_bytes = sum(end - begin for begin, end in intervals)
    current_bytes = sum(CAPS[c] for _, c, _ in current)
    pinned_bytes = sum(CAPS[c] for _, c, _ in pins)
    needed_bytes = current_bytes + pinned_bytes
    identities = {(g, s) for g, _, s in current + pins}
    return dict(current_homes=len(current), pinned_old_homes=len(pins), kept_old_homes=len(kept),
                extra_retained_homes=len(kept) - len(pins), occupied_bytes=occupied_bytes,
                current_bytes=current_bytes, pinned_old_bytes=pinned_bytes, needed_bytes=needed_bytes,
                extra_retained_bytes=sum(CAPS[c] for _, c, _ in kept) - pinned_bytes,
                payload_pages=npage, old_only_pages=(page_mask & ~current_mask).bit_count(),
                payload_cache_lines=line_mask.bit_count(),
                payload_address_span_bytes=max((end for _, end in intervals), default=0) - min((begin for begin, _ in intervals), default=0),
                payload_backing_amplification=round(npage * geometry.page / needed_bytes, 6) if needed_bytes else None,
                current_density=round(current_bytes / (npage * geometry.page), 6) if npage else None,
                dense_control_backing_pages=1, payload_and_control_backing_bytes=(npage + 1) * geometry.page,
                occupied_control_lines=len({(g * 16 + s) // LINE for g, s in identities}),
                ownership={str(width): ownership(geometry, occupied, identities, width) for width in WIDTHS})


def factor_results(results: list[dict]) -> tuple[dict, list[dict]]:
    """Keep named coverage metrics; intern only duplicated ownership profiles."""
    facts, profiles, indices = {}, [], {}
    common = ("current_homes", "pinned_old_homes", "current_bytes", "pinned_old_bytes", "needed_bytes", "occupied_control_lines")
    for result in results:
        for name, scopes in result["cases"].items():
            for measured in scopes.values():
                snapshot = {field: measured.pop(field) for field in common}
                if name in facts:
                    assert facts[name] == snapshot
                facts[name] = snapshot
                profile = measured.pop("ownership")
                key = json.dumps(profile, sort_keys=True, separators=(",", ":"))
                if key not in indices:
                    indices[key] = len(profiles)
                    profiles.append(profile)
                measured["ownership_profile"] = indices[key]
                del measured["dense_control_backing_pages"]
                del measured["payload_and_control_backing_bytes"]
    return facts, profiles


def run() -> dict:
    results, searches = [], []
    cases = snapshots()
    for page in (4096, 16384):
        for packing in WIDTHS:
            order, search = order_search(page, packing)
            searches.append(dict(page_size=page, packing_trampolines=packing, **search))
            geometries = (Geometry(page, packing, "grid_major"), Geometry(page, packing, "class_major"),
                          Geometry(page, packing, "class_major", order), Geometry(page, packing, "class_page_aligned"))
            for label, geometry in zip(("grid_major", "class_major_ascending", "class_major_reordered", "class_page_aligned"), geometries):
                validate(geometry)
                measured = {}
                for name, (current, pins, retired) in cases.items():
                    scopes = ("home", "grid", "packing_group") if pins else ("home",)
                    measured[name] = {scope: measure(geometry, current, pins, retired, scope) for scope in scopes}
                results.append(dict(label=label, geometry=geometry.description(), cases=measured))
    witness = next(row for row in results if row["label"] == "class_major_reordered" and row["geometry"]["page_size"] == 16384 and row["geometry"]["packing_trampolines"] == 1024)
    full16 = witness["cases"]["full_16_1024_current"]["home"]
    assert full16["occupied_bytes"] == 1024 * 16 == 16384 and full16["payload_pages"] == 1
    assert full16["ownership"]["16"]["max_owners_per_payload_page"] == 64
    assert full16["ownership"]["16"]["max_owners_per_control_line"] == 4
    assert full16["ownership"]["64"]["max_owners_per_control_line"] == 1
    pin = witness["cases"]["one_pin_1023_other_retired"]
    assert [pin[scope]["kept_old_homes"] for scope in ("home", "grid", "packing_group")] == [1, 16, 1024]
    assert [pin[scope]["payload_pages"] for scope in ("home", "grid", "packing_group")] == [1, 1, 1]
    witness4k = next(row for row in results if row["label"] == "class_major_reordered" and row["geometry"]["page_size"] == 4096 and row["geometry"]["packing_trampolines"] == 1024)
    pin4k = witness4k["cases"]["one_pin_1023_other_retired"]
    assert [pin4k[scope]["payload_pages"] for scope in ("home", "grid", "packing_group")] == [1, 1, 4]
    concise = dict(full1024_by16_bytes=16384, full1024_by16_on16KiB_pages=1,
                   full1024_by16_class_major_pages_on16KiB_by_packing={str(width): next(row for row in results if row["label"] == "class_major_reordered" and row["geometry"]["page_size"] == 16384 and row["geometry"]["packing_trampolines"] == width)["cases"]["full_16_1024_current"]["home"]["payload_pages"] for width in WIDTHS},
                   max_payload_page_owners_for_full16KiB_page={width: profile["max_owners_per_payload_page"] for width, profile in full16["ownership"].items()},
                   max_control_line_owners_for_full16KiB_page={width: profile["max_owners_per_control_line"] for width, profile in full16["ownership"].items()},
                   one_pin_home_grid_group_retained_homes=[1, 16, 1024],
                   one_pin_home_grid_group_backed_pages_on16KiB_class_major=[1, 1, 1],
                   one_pin_home_grid_group_backed_pages_on4KiB_class_major=[1, 1, 4],
                   mixed_dense64_on16KiB_grid_major_packing64_pages=1,
                   mixed_dense64_on16KiB_class_major_packing1024_pages=5,
                   all5120_possible_homes_byte_checked_per_geometry=True)
    baseline = next(row for row in results if row["label"] == "grid_major" and row["geometry"]["page_size"] == 16384 and row["geometry"]["packing_trampolines"] == 64)
    assert baseline["cases"]["mixed_dense_64_current"]["home"]["payload_pages"] == 1
    assert witness["cases"]["mixed_dense_64_current"]["home"]["payload_pages"] == 5
    facts, profiles = factor_results(results)
    return dict(schema=2, units="logical storage/page/cache-line coverage; no time or RSS", grid_count=64,
                capacities=list(CAPS), packing_and_owner_widths=list(WIDTHS), order_search=searches, results=results,
                snapshot_facts=facts, ownership_profiles=profiles, witnesses=concise,
                metadata=dict(dense_trampoline_bytes=1024, persistent_backing_pages=1,
                              allocator_and_module_metadata="excluded", total_backing_bytes="(payload_pages +1) *page_size"),
                amplification_denominator="current bytes plus explicitly pinned old bytes; coarse extra retention is excluded",
                traces={"growth_dense": ["dense_16_64_current", "delayed_dense_64_current", "dense_96_64_current"],
                        "growth_sparse": ["sparse_16_64_current", "delayed_sparse_64_current", "sparse_96_64_current"],
                        "retirement": ["full_16_1024_current", "one_pin_1023_other_retired", "all_freed"]},
                limits=["Five small A classes only; larger finite classes and virtual placement remain open.",
                        "One persistent dense control page backs1024B of trampolines; allocator/module metadata excluded.",
                        "Ownership labels identify allocation coordinators; actual payload readers/writers can run elsewhere.",
                        "Coarse retirement retains old-cohort homes only; current replacements are a separate cohort.",
                        "Extra retained homes do not automatically retain their already-dead descriptor identities.",
                        "Payload-only grants may permit a different control-cell lifetime; no lifetime protocol is selected.",
                        "Pins cover exact homes; wider native read/write reach can retain additional homes or pages.",
                        "Class reorder minimizes a homogeneous page-count sum, not mixed histories or access latency.",
                        "Equal coordinates/capacities are compared without relocation, allocator search or populated-page timing."])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    # Put the small reviewable witness section first; unindented JSON keeps the
    # complete named sweep compact while permitting ordinary line-based reads.
    encoded = json.dumps({"witnesses": result["witnesses"], **result}, indent=0) + "\n"
    if args.output:
        args.output.write_text(encoded)
        print(f"32 geometries,8 order searches,5120 byte-checked homes/geometry; witnesses passed; wrote {args.output}")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
