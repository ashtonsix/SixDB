#!/usr/bin/env python3
"""Populated-grid private growth: modeled placement/copies, never elapsed time."""
import argparse
import csv
import json
from pathlib import Path

from run import FAMILIES, PALETTES, Group
from recommend import Geometry, Pool, fitting


def destination(group, grid, required, alignment, class_choice="first"):
    """Claim a different Home in the existing identity grid, not an identity."""
    cls = fitting(group.g, required, alignment, class_choice)
    if cls is None:
        return None, "palette-range"
    candidates = []
    for slot in range(group.g.slots):
        if group.occupied[grid][cls] & (1 << slot):
            continue
        lo, hi = group.g.home(grid, cls, slot)
        if lo % alignment:
            continue
        pages = set(range(lo // group.g.page, (hi - 1) // group.g.page + 1))
        candidates.append((len(pages - group.mapped), slot, pages))
    if not candidates:
        return None, "home-pressure"
    _, slot, pages = min(candidates)
    return (grid, cls, slot, pages), None


def insert(pool, group, token, requested, alignment, grid=0):
    """Fixture construction is deliberately confined to the target's grid."""
    if group.identities[grid] == group.g.slots:
        return False
    home, _ = destination(group, grid, requested, alignment, pool.class_choice)
    if home is None:
        return False
    _, cls, slot, pages = home
    group.identities[grid] += 1
    group.occupied[grid][cls] |= 1 << slot
    group.mapped |= pages
    group.live[token] = home
    capacity = group.g.capacities[cls]
    pool.tokens[token] = (group, requested, capacity)
    pool.requested += requested
    pool.capacity += capacity
    pool.peak = max(pool.peak, pool.charge())
    return True


class Grower:
    def __init__(self, pool):
        self.pool = pool
        self.calls = self.moves = self.copied = self.local_moves = 0
        self.local_refusals = self.range_refusals = self.home_refusals = 0
        self.cross_group = self.cross_grid = 0
        self.peak_home_capacity = pool.capacity
        self.max_groups = len(pool.groups)

    def grow(self, token, required, reserved, alignment):
        pool = self.pool
        group, exposed, capacity = pool.tokens[token]
        old_home = group.live[token]
        grid, old_cls, old_slot, old_pages = old_home
        assert required >= exposed and reserved >= required
        self.calls += 1
        needed = max(required, reserved, exposed)
        if needed <= capacity:
            pool.requested += required - exposed
            pool.tokens[token] = (group, required, capacity)
            assert group.live[token] == old_home
            return

        new_home, reason = destination(group, grid, needed, alignment, pool.class_choice)
        if new_home is not None:
            _, cls, slot, pages = new_home
            new_capacity = group.g.capacities[cls]
            # The old binding/Home remains occupied through destination preparation
            # and the modeled copy. A private move never claims another identity.
            identities = list(group.identities)
            group.occupied[grid][cls] |= 1 << slot
            group.mapped |= pages
            assert group.live[token] == old_home
            assert group.occupied[grid][old_cls] & (1 << old_slot)
            assert old_pages <= group.mapped
            self.peak_home_capacity = max(self.peak_home_capacity, pool.capacity + new_capacity)
            pool.peak = max(pool.peak, pool.charge())
            self.copied += exposed
            group.live[token] = new_home
            group.occupied[grid][old_cls] &= ~(1 << old_slot)
            assert group.identities == identities
            pool.requested += required - exposed
            pool.capacity += new_capacity - capacity
            pool.tokens[token] = (group, required, new_capacity)
            self.local_moves += 1
        else:
            self.local_refusals += 1
            self.range_refusals += reason == "palette-range"
            self.home_refusals += reason == "home-pressure"
            replacement = ("replacement", token, self.calls)
            pool.allocate(replacement, needed, alignment)
            successor, _, new_capacity = pool.tokens[replacement]
            successor_home = successor.live[replacement]
            # Actual fallback also allocates before the source is released.
            assert group.live[token] == old_home
            assert group.occupied[grid][old_cls] & (1 << old_slot)
            assert old_pages <= group.mapped
            self.peak_home_capacity = max(self.peak_home_capacity, pool.capacity)
            self.copied += exposed
            self.cross_group += successor is not group
            self.cross_grid += successor is not group or successor_home[0] != grid
            pool.free(token)
            successor.live[token] = successor.live.pop(replacement)
            pool.tokens[token] = pool.tokens.pop(replacement)
            # Reservation is not exposed payload. Copy accounting uses only the
            # previous exposed extent, unlike the isolated growth.py screen.
            pool.requested += required - needed
            pool.tokens[token] = (successor, required, new_capacity)
            self.max_groups = max(self.max_groups, len(pool.groups))
        self.moves += 1
        assert pool.tokens[token][2] >= needed


def setup(family, palette, layout, page, start, alignment, sizes, class_choice="first"):
    pool = Pool(family, layout, page, "unrestricted", sizes, class_choice)
    group = Group(Geometry(palette, layout, page))
    pool.groups.append(group)
    assert insert(pool, group, "target", start, alignment)
    return pool, group


def populate(pool, group, population, alignment):
    # Fixed offered sizes, clipped only for a palette that cannot represent the
    # size. This is a conditioned grid-pressure control, not a population forecast.
    offered = (1025,) if population == "homogeneous" else (24, 193, 257, 513, 769, 1025, 1537, 2049, 2561)
    accepted = []
    for attempt in range(group.g.slots * len(offered)):
        requested = min(offered[attempt % len(offered)], group.g.capacities[-1])
        token = ("neighbor", attempt)
        if insert(pool, group, token, requested, alignment):
            accepted.append((token, requested))
        elif population == "homogeneous":
            break
        if group.identities[0] == group.g.slots:
            break
    return accepted


def trace(family, palette, layout, page, start, alignment, population, free_mode, policy, sizes, class_choice="first"):
    pool, group = setup(family, palette, layout, page, start, alignment, sizes, class_choice)
    neighbors = populate(pool, group, population, alignment)
    freed = neighbors[::2] if free_mode == "half" else []
    for token, _ in freed:
        pool.free(token)
    # Released Homes remain mapped, as they do before actual asynchronous trim.
    initial_charge = pool.charge()
    grower = Grower(pool)
    reservation = start
    cutoff = max(PALETTES[name][-1] for name in FAMILIES[family])
    for required in range(start + 16, cutoff + 1, 16):
        if policy == "exact":
            reservation = required
        elif required > reservation:
            reservation = min(cutoff, max(required, reservation * 2))
        grower.grow("target", required, reservation, alignment)
    before_trim = pool.charge()
    reclaimed = pool.trim()
    result = dict(family=family, initial_palette=palette, layout=layout, page=page,
                  start=start, alignment=alignment, population=population,
                  free_mode=free_mode, policy=policy, class_choice=class_choice, grid_slots=group.g.slots,
                  neighbor_requests=json.dumps([size for _, size in neighbors]),
                  initial_neighbors=len(neighbors), freed_neighbors=len(freed),
                  growth_calls=grower.calls, moves=grower.moves, copied_exposed_bytes=grower.copied,
                  local_moves=grower.local_moves, local_refusals=grower.local_refusals,
                  palette_range_refusals=grower.range_refusals, home_pressure_refusals=grower.home_refusals,
                  cross_group_moves=grower.cross_group, cross_grid_moves=grower.cross_grid,
                  initial_charge=initial_charge, peak_charge=pool.peak,
                  peak_home_capacity=grower.peak_home_capacity, max_groups=grower.max_groups,
                  final_target_capacity=pool.tokens["target"][2], before_trim_charge=before_trim,
                  settled_charge=pool.charge(), settled_backed=pool.backed(), reclaimed=reclaimed)
    for token in list(pool.tokens):
        pool.free(token)
    pool.trim()
    assert pool.charge() == 0
    return result


def verify(sizes):
    # Expected results come from the public production tables and identity/Home
    # laws, rather than running the same placement routine twice.
    for page in (16384, 65536):
        pool, group = setup("current", "B", "class-aligned", page, 257, 16, sizes)
        before = group.live["target"], pool.charge()
        growth = Grower(pool)
        growth.grow("target", 300, 300, 16)
        assert (group.live["target"], pool.charge()) == before
        assert growth.moves == growth.copied == 0

        pool, group = setup("current", "A", "class-aligned", page, 16, 16, sizes)
        growth = Grower(pool)
        growth.grow("target", 32, 32, 16)
        growth.grow("target", 48, 48, 16)
        assert growth.local_moves == 2 and growth.copied == 48
        growth.grow("target", 257, 257, 16)
        assert pool.tokens["target"][0] is group and pool.tokens["target"][2] == 384
        # Fresh allocation would choose B/320. Private growth stays A/384.

        pool, group = setup("current", "A", "class-aligned", page, 16, 64, sizes)
        for i in range(8):
            assert insert(pool, group, ("aligned", i), 32, 64)
        assert not insert(pool, group, "alignment-exhausted", 32, 64)
        growth = Grower(pool)
        growth.grow("target", 17, 17, 64)
        assert growth.home_refusals == growth.cross_grid == 1
        assert growth.cross_group == 0 and growth.copied == 16

        pool, group = setup("split5", "class32", "class-aligned", page, 16, 16, sizes)
        for i in range(7):
            assert insert(pool, group, ("reserved-byte", i), 3072, 16)
        growth = Grower(pool)
        growth.grow("target", 3000, 3000, 16)
        assert growth.home_refusals == growth.cross_grid == 1 and growth.cross_group == 0

        pool, group = setup("fine64", "tiny16", "class-aligned", page, 16, 16, sizes)
        growth = Grower(pool)
        growth.grow("target", 257, 257, 16)
        assert growth.range_refusals == growth.cross_group == 1
        assert pool.tokens["target"][2] == 320 and growth.copied == 16


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", type=Path, required=True, help="native --sizes JSON")
    parser.add_argument("--output", type=Path, default=Path("build/allocation-palettes/results/private-growth.csv"))
    args = parser.parse_args()
    sizes = json.loads(args.sizes.read_text())
    verify(sizes)
    rows = [trace(family, palette, layout, page, start, alignment, population, free_mode, policy, sizes, choice)
            for family, palettes in FAMILIES.items() for palette in palettes
            for page in (16384, 65536)
            for layout in ("class-aligned", "small-packed")
            for start in (16, 256, 1024, 1792) if start <= PALETTES[palette][-1]
            for choice in ("first", "aligned")
            for alignment in (16, 64) for population in ("varied", "homogeneous")
            for free_mode in ("none", "half") for policy in ("exact", "geometric-reservation")]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"{len(rows)} populated-grid private-growth cells; independent corner cases passed; {args.output}")


if __name__ == "__main__":
    main()
