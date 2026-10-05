#!/usr/bin/env python3
"""Small A home geometry only: no allocation policy, time, RSS or virtual layout.

Each grid retains sixteen identities and sixteen homes in each of five classes.
Four grids constitute one 64-trampoline affinity group. The control-page tile
contains 64 grids; control metadata itself is excluded from payload counters.
All compared layouts resolve identical (grid, class, home-slot) coordinates.
"""

from __future__ import annotations

import argparse
import itertools
import json
from dataclasses import dataclass
from pathlib import Path

PAGE = 4096
LINE = 64
CAPACITIES = (16, 32, 48, 64, 96)
GRIDS = 64
HOMES = 16
AFFINITY = 4
ASCENDING = (0, 1, 2, 3, 4)
SELECTED = (3, 0, 2, 1, 4)  # 64; 16+48; 32+96: four packed pages.
Coordinate = tuple[int, int, int]


def mask(begin: int, end: int, unit: int) -> int:
    first, last = begin // unit, (end - 1) // unit
    return ((1 << (last - first + 1)) - 1) << first


@dataclass(frozen=True)
class Layout:
    name: str
    group_grids: int
    order: tuple[int, ...] = ASCENDING
    align_classes: bool = False

    def offsets(self) -> tuple[dict[int, int], int]:
        offsets, cursor = {}, 0
        for cls in self.order:
            if self.align_classes:
                cursor = (cursor + PAGE - 1) // PAGE * PAGE
            offsets[cls] = cursor
            cursor += self.group_grids * HOMES * CAPACITIES[cls]
        return offsets, (cursor + PAGE - 1) // PAGE * PAGE

    def interval(self, coordinate: Coordinate) -> tuple[int, int]:
        grid, cls, slot = coordinate
        group, row = divmod(grid, self.group_grids)
        offsets, pitch = self.offsets()
        begin = group * pitch + offsets[cls] + (row * HOMES + slot) * CAPACITIES[cls]
        return begin, begin + CAPACITIES[cls]

    def description(self) -> dict:
        offsets, pitch = self.offsets()
        return dict(grids_per_payload_tile=self.group_grids,
                    class_order=[CAPACITIES[cls] for cls in self.order],
                    class_offsets_bytes={str(CAPACITIES[cls]): offset for cls, offset in offsets.items()},
                    tile_pitch_bytes=pitch, small_possible_home_span_bytes=GRIDS // self.group_grids * pitch)


LAYOUTS = (
    Layout("grid_major", 1),
    Layout("affinity_packed_ascending", 4),
    Layout("affinity_packed_selected", 4, SELECTED),
    Layout("affinity_class_page_aligned", 4, align_classes=True),
    Layout("control_page_class_major", 64),
)


def validate(layout: Layout) -> None:
    intervals = sorted(layout.interval((grid, cls, slot)) for grid in range(GRIDS)
                       for cls in range(5) for slot in range(HOMES))
    assert all(begin % 16 == 0 for begin, _ in intervals)
    assert all(left[1] <= right[0] for left, right in zip(intervals, intervals[1:]))
    assert intervals[-1][1] <= layout.description()["small_possible_home_span_bytes"]


def coverage(layout: Layout, current: list[Coordinate], retained: list[Coordinate] | None = None) -> dict:
    retained = retained or []
    assert len(set(current + retained)) == len(current) + len(retained)
    assert all(sum(grid == coordinate[0] for coordinate in current) <= 16 for grid in range(GRIDS))
    page_masks = [mask(*layout.interval(coordinate), PAGE) for coordinate in current + retained]
    line_masks = [mask(*layout.interval(coordinate), LINE) for coordinate in current + retained]
    all_pages = all_lines = current_pages = 0
    page_groups: dict[int, set[int]] = {}
    line_groups: dict[int, set[int]] = {}
    intervals = []
    for index, coordinate in enumerate(current + retained):
        begin, end = layout.interval(coordinate)
        intervals.append((begin, end))
        all_pages |= page_masks[index]
        all_lines |= line_masks[index]
        if index < len(current):
            current_pages |= page_masks[index]
        group = coordinate[0] // AFFINITY
        for unit, target in ((PAGE, page_groups), (LINE, line_groups)):
            for number in range(begin // unit, (end - 1) // unit + 1):
                target.setdefault(number, set()).add(group)
    return dict(current_homes=len(current), retained_homes=len(retained),
                occupied_bytes=sum(end - begin for begin, end in intervals),
                pages=all_pages.bit_count(), current_pages=current_pages.bit_count(),
                retained_only_pages=(all_pages & ~current_pages).bit_count(),
                cache_lines=all_lines.bit_count(),
                address_span_bytes=max(end for _, end in intervals) - min(begin for begin, _ in intervals),
                pages_shared_across_affinity_groups=sum(len(groups) > 1 for groups in page_groups.values()),
                max_affinity_groups_per_page=max(map(len, page_groups.values())),
                lines_shared_across_affinity_groups=sum(len(groups) > 1 for groups in line_groups.values()))


def low_count(layout: Layout) -> dict:
    """All one/two-home patterns across grids 0..4: first affinity + neighbor.

    The independent oracle visits every byte of every candidate home. Pair
    coverage then unions those byte-derived page and line sets.
    """
    coordinates = [(grid, cls, slot) for grid in range(5) for cls in range(5) for slot in range(16)]
    data = []
    for coordinate in coordinates:
        begin, end = layout.interval(coordinate)
        oracle_pages = {byte // PAGE for byte in range(begin, end)}
        oracle_lines = {byte // LINE for byte in range(begin, end)}
        p, line = mask(begin, end, PAGE), mask(begin, end, LINE)
        assert p == sum(1 << page for page in oracle_pages)
        assert line == sum(1 << number for number in oracle_lines)
        data.append((coordinate[0] // AFFINITY, begin, end, p, line, oracle_pages, oracle_lines))
    result = {}
    for count in (1, 2):
        page_sum = line_sum = span_sum = patterns = sharing = page_crossing = 0
        pmin = lmin = smin = 10**12
        pmax = lmax = smax = 0
        for selected in itertools.combinations(data, count):
            p = line = 0
            oracle_p, oracle_l = set(), set()
            for datum in selected:
                p |= datum[3]
                line |= datum[4]
                oracle_p |= datum[5]
                oracle_l |= datum[6]
            np, nl = p.bit_count(), line.bit_count()
            assert np == len(oracle_p) and nl == len(oracle_l)
            span = max(datum[2] for datum in selected) - min(datum[1] for datum in selected)
            patterns += 1
            page_sum += np
            line_sum += nl
            span_sum += span
            pmin, pmax = min(pmin, np), max(pmax, np)
            lmin, lmax = min(lmin, nl), max(lmax, nl)
            smin, smax = min(smin, span), max(smax, span)
            page_crossing += any(len(datum[5]) > 1 for datum in selected)
            if count == 2 and selected[0][0] != selected[1][0] and selected[0][5] & selected[1][5]:
                sharing += 1
        result[str(count)] = dict(patterns=patterns, page_sum=page_sum, cache_line_sum=line_sum,
                                  address_span_sum_bytes=span_sum, page_min=pmin, page_max=pmax,
                                  cache_line_min=lmin, cache_line_max=lmax,
                                  address_span_min_bytes=smin, address_span_max_bytes=smax,
                                  patterns_with_page_crossing_home=page_crossing,
                                  patterns_sharing_page_across_affinity_groups=sharing)
    return result


def cases() -> dict[str, tuple[list[Coordinate], list[Coordinate]]]:
    result = {}
    for cls, capacity in enumerate(CAPACITIES):
        result[f"homogeneous_{capacity}_four_full_grids"] = ([(g, cls, s) for g in range(4) for s in range(16)], [])
        result[f"homogeneous_{capacity}_all_full_grids"] = ([(g, cls, s) for g in range(64) for s in range(16)], [])
        result[f"sparse_{capacity}_64_current"] = ([(g, cls, 0) for g in range(64)], [])
    result["clustered_mixed_64_current"] = ([(g, (0, 1, 3, 4)[g], s) for g in range(4) for s in range(16)], [])
    result["mixed_64_current"] = ([(g, (g * 16 + s) % 5, s) for g in range(4) for s in range(16)], [])
    result["sparse_mixed_64_current"] = ([(g, g % 5, 0) for g in range(64)], [])
    # A shared owner can retain its previous-class home. These snapshots reuse
    # the same identity/home-slot number in different classes; no alias moves.
    result["clustered_growth_64_current_old_16B_retained"] = (
        [(g, 4, s) for g in range(4) for s in range(16)],
        [(g, 0, s) for g in range(4) for s in (0, 8)])
    result["sparse_growth_64_current_old_16B_retained"] = (
        [(g, 4, 0) for g in range(64)], [(g, 0, 0) for g in range(0, 64, 4)])
    return result


def orders() -> dict:
    summaries = []
    for order in itertools.permutations(range(5)):
        layout = Layout("order", 4, order)
        counts = [coverage(layout, [(g, cls, s) for g in range(4) for s in range(16)])["pages"]
                  for cls in range(5)]
        summaries.append((sum(counts), tuple(counts), order))
    minimum = min(score for score, _, _ in summaries)
    best = [(counts, order) for score, counts, order in summaries if score == minimum]
    chosen = next((score, counts) for score, counts, order in summaries if order == SELECTED)
    assert chosen[0] == minimum and chosen[1] == (1, 1, 1, 1, 2)
    return dict(permutations=120, objective="sum of homogeneous four-full-grid page counts, equal weight per class",
                minimum_page_sum=minimum, optimum_orders=len(best),
                selected_order=[CAPACITIES[cls] for cls in SELECTED],
                selected_counts_by_capacity=dict(zip(map(str, CAPACITIES), chosen[1])),
                maximum_page_sum=max(score for score, _, _ in summaries),
                caution="This objective does not select the best order for mixed access or retention histories.")


def run() -> dict:
    comparisons = {}
    for layout in LAYOUTS:
        validate(layout)
        comparisons[layout.name] = dict(geometry=layout.description(), low_count=low_count(layout),
                                        cases={name: coverage(layout, current, retained)
                                               for name, (current, retained) in cases().items()})
    old, selected = comparisons["grid_major"]["cases"], comparisons["affinity_packed_selected"]["cases"]
    one_grid = {str(CAPACITIES[cls]): coverage(LAYOUTS[0], [(0, cls, slot) for slot in range(16)])
                for cls in (0, 4)}
    assert one_grid["16"]["pages"] == one_grid["96"]["pages"] == 1
    assert old["homogeneous_16_four_full_grids"]["pages"] == old["homogeneous_96_four_full_grids"]["pages"] == 4
    assert selected["homogeneous_16_four_full_grids"]["pages"] == 1
    assert selected["homogeneous_96_four_full_grids"]["pages"] == 2
    assert comparisons["affinity_packed_ascending"]["cases"]["homogeneous_64_four_full_grids"]["pages"] == 2
    assert selected["homogeneous_64_four_full_grids"]["pages"] == 1
    global_cases = comparisons["control_page_class_major"]["cases"]
    assert global_cases["homogeneous_64_all_full_grids"]["pages"] == 16
    assert global_cases["homogeneous_16_all_full_grids"]["max_affinity_groups_per_page"] == 4
    assert all(case["lines_shared_across_affinity_groups"] == 0 for value in comparisons.values() for case in value["cases"].values())
    return dict(schema=1, units="payload bytes, covered 4 KiB pages, covered 64 B cache lines; no time/RSS",
                scope="five small A classes only; 64 grids; four grids per affinity group; virtual absent",
                low_count_scope="all one/two occupied homes among 400 coordinates in grids0..4; each is legal under sixteen identities/grid",
                witnesses=dict(original_single_full_grid=one_grid,
                               four_full_grid_selected_pages_by_capacity={str(capacity): selected[f"homogeneous_{capacity}_four_full_grids"]["pages"]
                                                                         for capacity in CAPACITIES},
                               selected_order_adds_no_padding=True,
                               cross_affinity_cache_line_sharing_absent_in_all_cases=True),
                order_search=orders(), comparisons=comparisons,
                limits=["Snapshots compare identical coordinates and absolute capacities; they do not simulate allocation policy.",
                        "Retained homes represent an old class of the same owner, without fixing an MVCC mechanism.",
                        "Whole-control-page placement can share payload pages across affinity owners despite separate cache lines.",
                        "Address span is a geometric measure, not cache/TLB cost or required populated backing.",
                        "Payload page coverage excludes control metadata, partial population and virtual placement."])


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = run()
    encoded = json.dumps(result, separators=(",", ":"), sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(encoded)
        print(f"{len(LAYOUTS)} layouts,120 orders; byte/page oracle and geometry witnesses passed; wrote {args.output}")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
