#!/usr/bin/env python3
"""Finite grid placement/backing model; logical counters, not RSS or timings.

Sixteen identities per grid can each name sixteen homes in each finite class.
Virtual placement is deliberately absent. Metadata consumes sixteen bytes per
grid, densely packed in one directory page per 256 grids. A directory block
reserves every grid aperture, but backs only directory/payload pages in use.

Every policy replays the same absolute-size events. A/B are separate palette
comparisons: tiny requests in B intentionally round up to its minimum class.
Retained raw grants preserve old homes; home pressure blocks a replacement
rather than inventing overflow or changing a shared descriptor's grid.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from dataclasses import dataclass, field
from pathlib import Path

PAGE = 4096
SLOTS = 16
GRIDS_PER_DIRECTORY = PAGE // SLOTS
PALETTES = {
    "A": (16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024, 1536, 2048, 3072),
    "B": (256, 320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048, 2560, 3072),
}


def round_page(value: int) -> int:
    return (value + PAGE - 1) // PAGE * PAGE


def pages(intervals: list[tuple[int, int]]) -> set[int]:
    result: set[int] = set()
    for begin, end in intervals:
        assert begin < end
        result.update(range(begin // PAGE, (end - 1) // PAGE + 1))
    return result


def pages_oracle(intervals: list[tuple[int, int]]) -> set[int]:
    """Independent boundary walk, used for consequential geometry witnesses."""
    result: set[int] = set()
    for begin, end in intervals:
        cursor = begin
        while cursor < end:
            page = cursor - cursor % PAGE
            result.add(page // PAGE)
            cursor = min(end, page + PAGE)
    return result


@dataclass(frozen=True)
class Layout:
    palette: str
    arrangement: str
    offsets: tuple[int, ...] = field(init=False)
    pitch: int = field(init=False)

    def __post_init__(self) -> None:
        offset = 0
        offsets = []
        for index, capacity in enumerate(PALETTES[self.palette]):
            if self.arrangement == "page_ranges" and not (self.palette == "A" and index < 5):
                offset = round_page(offset)
            offsets.append(offset)
            offset += SLOTS * capacity
        object.__setattr__(self, "offsets", tuple(offsets))
        object.__setattr__(self, "pitch", round_page(offset))
        intervals = [self.interval(0, index, slot) for index in range(15) for slot in range(16)]
        ordered = sorted(intervals)
        assert all(left[1] <= right[0] for left, right in zip(ordered, ordered[1:]))
        alignment = 16 if self.palette == "A" else 64
        assert all(begin % alignment == 0 for begin, _ in ordered)
        assert ordered[-1][1] <= self.payload_base(0) + self.pitch
        assert self.interval(1, 0, 0)[0] >= ordered[-1][1]
        assert pages(intervals) == pages_oracle(intervals)

    def payload_base(self, grid: int) -> int:
        block, local = divmod(grid, GRIDS_PER_DIRECTORY)
        return block * (PAGE + GRIDS_PER_DIRECTORY * self.pitch) + PAGE + local * self.pitch

    def directory_page(self, grid: int) -> int:
        return (grid // GRIDS_PER_DIRECTORY) * (1 + GRIDS_PER_DIRECTORY * self.pitch // PAGE)

    def interval(self, grid: int, cls: int, slot: int) -> tuple[int, int]:
        capacity = PALETTES[self.palette][cls]
        begin = self.payload_base(grid) + self.offsets[cls] + slot * capacity
        return begin, begin + capacity

    def cls(self, size: int) -> int:
        return next(index for index, capacity in enumerate(PALETTES[self.palette]) if size <= capacity)


@dataclass
class Home:
    owner: int
    cls: int
    slot: int
    size: int
    writer: int
    current: bool = True
    holds: int = 0


@dataclass
class Owner:
    grid: int
    identity: int
    home: Home | None
    live: bool = True


@dataclass
class Grid:
    number: int
    hint: tuple[int, int] | None = None
    owners: dict[int, int] = field(default_factory=dict)
    homes: dict[tuple[int, int], Home] = field(default_factory=dict)


class Model:
    def __init__(self, layout: Layout, policy: str, compact: bool) -> None:
        self.layout, self.policy, self.compact = layout, policy, compact
        self.grids: list[Grid] = []
        self.owners: dict[int, Owner] = {}
        self.grants: dict[int, tuple[int, Home]] = {}
        self.counts = dict(growth_copies=0, growth_copy_bytes=0, compaction_copies=0,
                           compaction_copy_bytes=0, blocked_replacements=0,
                           payload_page_steps=0, active_page_steps=0,
                           retained_only_page_steps=0, directory_page_steps=0,
                           backing_slack_byte_steps=0, internal_slack_byte_steps=0,
                           released_payload_page_events=0, mapped_payload_page_events=0,
                           peak_payload_pages=0, peak_copy_payload_pages=0,
                           peak_directory_pages=0, peak_retained_homes=0)
        self.last_mapped: set[int] = set()
        self.steps = 0

    def free_slot(self, grid: Grid, cls: int) -> int | None:
        candidates = [slot for slot in range(16) if (cls, slot) not in grid.homes]
        if not candidates:
            return None
        if self.policy == "append_first":
            return candidates[0]
        resident = pages([self.layout.interval(grid.number, h.cls, h.slot) for h in grid.homes.values()])
        return min(candidates, key=lambda slot: (len(pages([self.layout.interval(grid.number, cls, slot)]) - resident), slot))

    def note_copy_peak(self, additional: tuple[int, int] | None = None) -> None:
        intervals = [self.layout.interval(g.number, h.cls, h.slot) for g in self.grids for h in g.homes.values()]
        if additional is not None:
            intervals.append(additional)
        self.counts["peak_copy_payload_pages"] = max(self.counts["peak_copy_payload_pages"], len(pages(intervals)))

    def hint(self, identity: int, lifetime: int, writer: int) -> tuple[int, int]:
        if self.policy == "hint_oracle":
            return lifetime, writer
        # A deliberately misleading but stable classification, not merely a
        # permutation of true labels (which would preserve the same grouping).
        value = (identity * 1103515245 + 12345) & 0x7FFFFFFF
        return (value >> 12) & 1, (value >> 18) & 3

    def choose_grid(self, cls: int, identity: int, lifetime: int, writer: int) -> Grid:
        key = self.hint(identity, lifetime, writer) if self.policy.startswith("hint_") else None
        eligible = [grid for grid in self.grids if len(grid.owners) < 16 and self.free_slot(grid, cls) is not None
                    and (key is None or grid.hint == key or not grid.owners)]
        if self.policy == "append_first":
            # Continue the newest grid; reuse a wholly empty grid before opening
            # a new aperture, but do not search partially occupied older grids.
            newest = self.grids[-1] if self.grids else None
            grid = newest if newest in eligible else next((g for g in eligible if not g.owners), None)
        else:
            def score(grid: Grid) -> tuple[int, int, int]:
                existing = pages([self.layout.interval(grid.number, h.cls, h.slot) for h in grid.homes.values()])
                slot = self.free_slot(grid, cls)
                assert slot is not None
                added = pages([self.layout.interval(grid.number, cls, slot)]) - existing
                return len(added), -len(grid.owners), grid.number
            grid = min(eligible, key=score) if eligible else None
        if grid is None:
            grid = Grid(len(self.grids), key)
            self.grids.append(grid)
        if not grid.owners:
            grid.hint = key
        return grid

    def allocate(self, identity: int, size: int, lifetime: int, writer: int) -> None:
        assert identity not in self.owners
        cls = self.layout.cls(size)
        grid = self.choose_grid(cls, identity, lifetime, writer)
        owner_slot = next(slot for slot in range(16) if slot not in grid.owners)
        slot = self.free_slot(grid, cls)
        assert slot is not None
        home = Home(identity, cls, slot, size, writer)
        grid.homes[cls, slot] = home
        grid.owners[owner_slot] = identity
        self.owners[identity] = Owner(grid.number, owner_slot, home)

    def discard(self, grid: Grid, home: Home) -> None:
        assert not home.current and not home.holds
        del grid.homes[home.cls, home.slot]
        owner = self.owners[home.owner]
        if not owner.live and not any(h.owner == home.owner for h in grid.homes.values()):
            del grid.owners[owner.identity]
            del self.owners[home.owner]

    def resize(self, identity: int, size: int) -> None:
        owner = self.owners[identity]
        old = owner.home
        assert owner.live and old is not None
        grid = self.grids[owner.grid]
        cls = self.layout.cls(size)
        if cls == old.cls and not old.holds:
            old.size = size
            return
        slot = self.free_slot(grid, cls)
        if slot is None:
            self.counts["blocked_replacements"] += 1
            return
        new = Home(identity, cls, slot, size, old.writer)
        grid.homes[cls, slot] = new
        self.note_copy_peak()  # Source and destination coexist while copying.
        owner.home = new
        old.current = False
        self.counts["growth_copies"] += 1
        self.counts["growth_copy_bytes"] += min(old.size, size)
        if not old.holds:
            self.discard(grid, old)

    def free(self, identity: int) -> None:
        owner = self.owners[identity]
        assert owner.live and owner.home is not None
        home = owner.home
        owner.live, owner.home, home.current = False, None, False
        if not home.holds:
            self.discard(self.grids[owner.grid], home)

    def compact_grid(self, grid: Grid) -> None:
        for cls in range(15):
            if self.layout.palette == "A" and cls < 5:
                continue  # The small shared page keeps its home positions.
            movers = sorted((h for h in grid.homes.values() if h.cls == cls and h.current and not h.holds),
                            key=lambda home: home.slot)
            fixed = {h.slot for h in grid.homes.values() if h.cls == cls and (not h.current or h.holds)}
            destinations = [slot for slot in range(16) if slot not in fixed][:len(movers)]
            proposed = [(home, slot) for home, slot in zip(movers, destinations) if home.slot != slot]
            if not proposed:
                continue
            original = pages([self.layout.interval(grid.number, h.cls, h.slot) for h in grid.homes.values()])
            moved_ids = {id(home): slot for home, slot in proposed}
            changed = pages([self.layout.interval(grid.number, h.cls, moved_ids.get(id(h), h.slot))
                             for h in grid.homes.values()])
            if len(changed) >= len(original):
                continue  # Spend copies only when this complete pass releases a page.
            for home, slot in proposed:
                # Destinations are ordered toward the prefix, so earlier moves
                # free any later source slot reused as a destination.
                assert (cls, slot) not in grid.homes
                self.note_copy_peak(self.layout.interval(grid.number, cls, slot))
                del grid.homes[cls, home.slot]
                home.slot = slot
                grid.homes[cls, slot] = home
                self.counts["compaction_copies"] += 1
                self.counts["compaction_copy_bytes"] += home.size

    def check(self) -> None:
        for grid in self.grids:
            assert len(grid.owners) <= 16
            assert len(set(grid.owners.values())) == len(grid.owners)
            for key, home in grid.homes.items():
                assert key == (home.cls, home.slot) and 0 <= home.slot < 16
                assert home.current or home.holds > 0
                assert home.owner in self.owners
                owner = self.owners[home.owner]
                assert owner.grid == grid.number and grid.owners[owner.identity] == home.owner
                if home.current:
                    assert owner.live and owner.home is home
            for identity in grid.owners.values():
                owner = self.owners[identity]
                assert any(home.owner == identity for home in grid.homes.values())
                assert (owner.home is not None) == owner.live
        assert sum(h.holds for g in self.grids for h in g.homes.values()) == len(self.grants)

    def snapshot(self) -> dict[str, int]:
        homes = [(g.number, h) for g in self.grids for h in g.homes.values()]
        mapped = pages([self.layout.interval(g, h.cls, h.slot) for g, h in homes])
        active = pages([self.layout.interval(g, h.cls, h.slot) for g, h in homes if h.current])
        directory = {self.layout.directory_page(g.number) for g in self.grids if g.owners}
        capacity = sum(PALETTES[self.layout.palette][h.cls] for _, h in homes)
        logical = sum(h.size for _, h in homes)
        retained = sum(not h.current for _, h in homes)
        return dict(payload_pages=len(mapped), active_pages=len(active), retained_only_pages=len(mapped - active),
                    directory_pages=len(directory), occupied_home_bytes=capacity, logical_home_bytes=logical,
                    current_homes=sum(h.current for _, h in homes), retained_homes=retained,
                    anchored_identities=sum(len(g.owners) for g in self.grids))

    def account(self) -> None:
        snapshot = self.snapshot()
        mapped = pages([self.layout.interval(g.number, h.cls, h.slot) for g in self.grids for h in g.homes.values()])
        self.counts["released_payload_page_events"] += len(self.last_mapped - mapped)
        self.counts["mapped_payload_page_events"] += len(mapped - self.last_mapped)
        self.last_mapped = mapped
        for source, counter in (("payload_pages", "payload_page_steps"), ("active_pages", "active_page_steps"),
                                ("retained_only_pages", "retained_only_page_steps"),
                                ("directory_pages", "directory_page_steps")):
            self.counts[counter] += snapshot[source]
        self.counts["backing_slack_byte_steps"] += snapshot["payload_pages"] * PAGE - snapshot["occupied_home_bytes"]
        self.counts["internal_slack_byte_steps"] += snapshot["occupied_home_bytes"] - snapshot["logical_home_bytes"]
        for source, counter in (("payload_pages", "peak_payload_pages"), ("directory_pages", "peak_directory_pages"),
                                ("retained_homes", "peak_retained_homes")):
            self.counts[counter] = max(self.counts[counter], snapshot[source])
        self.steps += 1

    def event(self, event: tuple) -> None:
        operation, *arguments = event
        if operation == "alloc":
            self.allocate(*arguments)
        elif operation == "resize":
            self.resize(*arguments)
        elif operation == "free":
            self.free(*arguments)
        elif operation == "hold":
            identity, token = arguments
            owner = self.owners[identity]
            assert owner.live and owner.home is not None and token not in self.grants
            owner.home.holds += 1
            self.grants[token] = owner.grid, owner.home
        elif operation == "release":
            grid_number, home = self.grants.pop(arguments[0])
            home.holds -= 1
            if not home.current and not home.holds:
                self.discard(self.grids[grid_number], home)
        else:
            raise ValueError(operation)
        if self.compact and operation in ("resize", "free", "release"):
            for grid in self.grids:
                self.compact_grid(grid)
        self.check()
        self.account()

    def result(self) -> dict:
        result = self.snapshot() | self.counts
        blocks = (len(self.grids) + GRIDS_PER_DIRECTORY - 1) // GRIDS_PER_DIRECTORY
        result.update(steps=self.steps, grids_opened=len(self.grids),
                      reserved_va_bytes=blocks * (PAGE + GRIDS_PER_DIRECTORY * self.layout.pitch),
                      used_grid_aperture_bytes=blocks * PAGE + len(self.grids) * self.layout.pitch,
                      peak_backed_payload_pages=max(self.counts["peak_payload_pages"],
                                                    self.counts["peak_copy_payload_pages"]))
        lines: dict[int, set[int]] = {}
        writers_by_page: dict[int, set[int]] = {}
        for grid in self.grids:
            for home in grid.homes.values():
                if not home.current:
                    continue
                begin, _ = self.layout.interval(grid.number, home.cls, home.slot)
                end = begin + home.size
                for line in range(begin // 64, (end - 1) // 64 + 1):
                    lines.setdefault(line, set()).add(home.writer)
                for page in pages([(begin, end)]):
                    writers_by_page.setdefault(page, set()).add(home.writer)
        result["final_mixed_writer_lines"] = sum(len(writers) > 1 for writers in lines.values())
        result["final_multi_writer_pages"] = sum(len(writers) > 1 for writers in writers_by_page.values())
        # This dry counter estimates remaining opportunities under this same
        # compactor; it does not assert a global minimum packing arrangement.
        before = result["payload_pages"]
        estimator = copy.deepcopy(self)
        for grid in estimator.grids:
            estimator.compact_grid(grid)
        result["releasable_by_same_compactor_pages"] = before - estimator.snapshot()["payload_pages"]
        return result


def histories() -> dict[str, list[tuple]]:
    traces: dict[str, list[tuple]] = {}
    traces["concentrated"] = [("alloc", i, 64, 1, 0) for i in range(96)]
    traces["concentrated"] += [("free", i) for i in range(64)]
    traces["mixed"] = [("alloc", i, (16, 32, 48, 64, 96)[i % 5], 1, i % 4) for i in range(64)]
    next_id = 64
    for wave in range(8):
        batch = list(range(next_id, next_id + 48))
        traces["mixed"] += [("alloc", i, (16, 48, 256, 384, 768, 1536)[i % 6], 0, wave % 4) for i in batch]
        traces["mixed"] += [("free", i) for i in batch]
        next_id += 48
    traces["mixed"] += [("free", i) for i in range(32)]
    traces["sparse_then_new"] = [("alloc", i, 3072, 1, 0) for i in range(96)]
    traces["sparse_then_new"] += [("free", i) for i in range(96) if i % 16 != 14]
    traces["sparse_then_new"] += [("alloc", i, 3072, 1, 0) for i in range(96, 128)]
    traces["growth"] = [("alloc", i, 16, 1, i % 4) for i in range(64)]
    for size in (32, 64, 128, 384, 1024, 3072):
        traces["growth"] += [("resize", i, size) for i in range(64)]
    traces["growth"] += [("free", i) for i in range(64) if i % 8 != 7]
    traces["churn"] = [("alloc", i, 64, 1, i % 4) for i in range(48)]
    next_id = 48
    for wave in range(6):
        batch = list(range(next_id, next_id + 64))
        traces["churn"] += [("alloc", i, (48, 256, 768, 3072)[(i + wave) % 4], 0, i % 4) for i in batch]
        traces["churn"] += [("free", i) for writer in range(4) for i in batch if i % 4 == writer]
        if wave == 3:
            traces["churn"] += [("resize", i, 1024) for i in range(0, 48, 4)]
        next_id += 64
    traces["churn"] += [("free", i) for i in range(24)]
    traces["churn"] += [("alloc", i, 64, 1, i % 4) for i in range(next_id, next_id + 48)]
    traces["delayed_readers"] = [("alloc", i, 512, 1, i % 4) for i in range(48)]
    traces["delayed_readers"] += [("hold", i, i) for i in range(0, 48, 4)]
    traces["delayed_readers"] += [("resize", i, 1024) for i in range(0, 48, 4)]
    traces["delayed_readers"] += [("free", i) for i in range(0, 24, 4)]
    traces["delayed_readers"] += [("release", i) for i in range(0, 48, 4)]
    return traces


def witnesses() -> dict:
    a = Layout("A", "page_ranges")
    small = [a.interval(0, cls, slot) for cls in range(5) for slot in range(16)]
    assert sum(end - begin for begin, end in small) == PAGE
    assert pages(small) == pages_oracle(small) == {1}
    large = [a.interval(0, 14, slot) for slot in range(16)]
    assert sum(end - begin for begin, end in large) == 49152
    assert pages(large) == pages_oracle(large) and len(pages(large)) == 12
    model = Model(a, "resident_first", False)
    for i in range(16):
        model.event(("alloc", i, 3072, 1, 0))
    for i in range(16):
        if i not in (1, 14):
            model.event(("free", i))
    before = model.snapshot()["payload_pages"]
    assert before == 4
    model.compact_grid(model.grids[0])
    model.check()
    after = model.snapshot()["payload_pages"]
    assert after == 2 and model.counts["compaction_copy_bytes"] == 6144
    blocked = Model(a, "resident_first", False)
    for i in range(16):
        blocked.event(("alloc", i, 3072, 1, 0))
    blocked.event(("hold", 0, 0))
    blocked.event(("resize", 0, 3072))
    assert blocked.counts["blocked_replacements"] == 1 and blocked.snapshot()["current_homes"] == 16
    blocked.event(("release", 0))
    blocked.event(("resize", 0, 3072))
    assert blocked.counts["blocked_replacements"] == 1
    # One retained old home costs backing until its grant ends, even after
    # current placement moves and the allocation identity is retired.
    held = Model(a, "resident_first", False)
    held.event(("alloc", 0, 3072, 1, 0))
    held.event(("hold", 0, 1))
    held.event(("free", 0))
    assert held.snapshot()["active_pages"] == 0 and held.snapshot()["retained_only_pages"] == 1
    held.event(("release", 1))
    assert held.snapshot()["payload_pages"] == 0 and held.snapshot()["anchored_identities"] == 0
    # New identities consolidate in surviving grids without relocating any old
    # identity across a grid boundary. Append-first opens two extra apertures.
    trace = histories()["sparse_then_new"]
    sparse = {}
    for policy in ("append_first", "resident_first"):
        experiment = Model(a, policy, False)
        for event in trace:
            experiment.event(event)
        sparse[policy] = experiment.result()["grids_opened"]
    assert sparse == {"append_first": 8, "resident_first": 6}
    return dict(small_page_bytes=PAGE, full_3072_range_pages=12,
                fragmented_3072_pages=before, compacted_3072_pages=after,
                compaction_copy_bytes=6144, class_full_replace_blocks=True,
                retained_home_releases_after_grant=True, sparse_grid_apertures=sparse)


def run() -> dict:
    checks = witnesses()
    traces = histories()
    results = []
    for palette in PALETTES:
        for arrangement in ("packed", "page_ranges"):
            layout = Layout(palette, arrangement)
            for compact in (False, True):
                for policy in ("append_first", "resident_first", "hint_oracle", "hint_wrong"):
                    for name, trace in traces.items():
                        model = Model(layout, policy, compact)
                        for event in trace:
                            model.event(event)
                        result = model.result()
                        assert result["blocked_replacements"] == 0
                        assert result["mapped_payload_page_events"] - result["released_payload_page_events"] == result["payload_pages"]
                        results.append(dict(palette=palette, arrangement=arrangement, compact=compact,
                                            policy=policy, history=name, **result))
    return dict(schema=1, units="bytes, pages, copies and logical event page-steps; no time/RSS",
                virtual="open; absent from this finite model", page_size=PAGE, witnesses=checks,
                limits=["All occupied reservation capacity is backed; no demand-population within homes.",
                        "Page-step and map/release counters observe event boundaries; copy overlap is a separate peak.",
                        "Copy counters preserve logical payload bytes, not an entire reservation capacity.",
                        "A held raw grant conservatively retains its descriptor identity as well as its home.",
                        "Unborrowed same-class resize mutates in place; logical MVCC obligations are outside this model.",
                        "Compaction is greedy within one class/grid, never across grids or within the A small page.",
                        "Metadata is a dense sixteen-byte trampoline group with no module-zone overhead.",
                        "Writer line/page counts are potential sharing of current payload bytes, not measured contention."],
                histories={name: dict(events=len(trace), sha256=hashlib.sha256(json.dumps(trace).encode()).hexdigest())
                           for name, trace in traces.items()},
                geometry={palette: {arrangement: dict(grid_pitch_bytes=Layout(palette, arrangement).pitch,
                                                       finite_home_bytes=sum(PALETTES[palette]) * 16)
                                    for arrangement in ("packed", "page_ranges")} for palette in PALETTES},
                results=results)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    arguments = parser.parse_args()
    result = run()
    encoded = json.dumps(result, separators=(",", ":"), sort_keys=True) + "\n"
    if arguments.output:
        arguments.output.write_text(encoded)
        print(f"{len(result['results'])} comparisons; witnesses passed; wrote {arguments.output}")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
