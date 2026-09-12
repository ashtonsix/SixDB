#!/usr/bin/env python3
"""Bounded exhaustive layout reference; structural features are not a cost model."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from fractions import Fraction
from functools import lru_cache
import hashlib
import json
import math
from pathlib import Path
import sys


WIDTHS = (1, 7, 3, 5)
NAMES = ("A", "B", "C", "D")
CONTRACT = "byte8-preserve-v1"
MAPS = {
    "R_AC": (0, 2), "R_0": (0, 2, 1, 3), "R_1": (1, 0, 3, 2),
    "W_A": (0,), "W_AC": (0, 2), "W_AB": (0, 1), "W_all": (0, 2, 1, 3),
}


@dataclass(frozen=True)
class Layout:
    unit_bytes: int
    positions: tuple[tuple[int, int], ...]

    @property
    def id(self):
        return f"b{self.unit_bytes}-" + "-".join(
            f"{name}{offset}s{shift}"
            for name, (offset, shift) in zip(NAMES, self.positions)
        )


@lru_cache(maxsize=1)
def layouts():
    result = []
    order = sorted(range(4), key=lambda i: -WIDTHS[i])
    for size in (2, 3):
        occupied = [0] * size
        positions = [(0, 0)] * 4

        def place(depth):
            if depth == 4:
                result.append(Layout(size, tuple(positions)))
                return
            code = order[depth]
            for offset in range(size):
                for shift in range(9 - WIDTHS[code]):
                    mask = ((1 << WIDTHS[code]) - 1) << shift
                    if occupied[offset] & mask:
                        continue
                    occupied[offset] |= mask
                    positions[code] = (offset, shift)
                    place(depth + 1)
                    occupied[offset] ^= mask

        place(0)
    return tuple(sorted(result, key=lambda layout: layout.id))


def spans(offsets):
    result = []
    for offset in sorted(set(offsets)):
        if result and result[-1][0] + result[-1][1] == offset:
            result[-1][1] += 1
        else:
            result.append([offset, 1])
    return result


def features(layout, operation):
    selected = MAPS[operation]
    masks = [0] * layout.unit_bytes
    contributions = [0] * layout.unit_bytes
    for code in selected:
        offset, shift = layout.positions[code]
        masks[offset] |= ((1 << WIDTHS[code]) - 1) << shift
        contributions[offset] += 1
    offsets = [layout.positions[code][0] for code in selected]
    touched = sorted(set(offsets))
    interior = sum(0 < layout.positions[c][1] < 8 - WIDTHS[c] for c in selected)
    result = {
        "ordered_offsets": offsets,
        "selected_masks": masks,
        "selected_bits": sum(WIDTHS[c] for c in selected),
        "selected_byte_count": len(touched),
        "selected_byte_spans": spans(touched),
        "selected_envelope_bytes": max(touched) - min(touched) + 1,
        "nonzero_shift_codes": sum(layout.positions[c][1] != 0 for c in selected),
        "interior_codes": interior,
        "backward_steps": sum(a > b for a, b in zip(offsets, offsets[1:])),
        "offset_travel": sum(abs(a - b) for a, b in zip(offsets, offsets[1:])),
        "maximum_codes_per_selected_byte": max(contributions),
    }
    if operation.startswith("W"):
        old = [i for i, mask in enumerate(masks) if 0 < mask < 255]
        result.update({
            "preserve_masks": [255 ^ mask for mask in masks],
            "preservation_byte_offsets": old,
            "preservation_byte_count": len(old),
            "preservation_bits_in_selected_bytes": sum(8 - masks[i].bit_count() for i in touched),
            "selected_byte_bitmap": sum(1 << i for i in touched),
        })
    return result


def describe(layout):
    occupied = [0] * layout.unit_bytes
    for code, (offset, shift) in enumerate(layout.positions):
        occupied[offset] |= ((1 << WIDTHS[code]) - 1) << shift
    return {
        "candidate_id": layout.id,
        "unit_bytes": layout.unit_bytes,
        "codes": [dict(id=name, width=width, offset=pos[0], shift=pos[1])
                  for name, width, pos in zip(NAMES, WIDTHS, layout.positions)],
        "occupied_masks": occupied,
        "padding_bits": 8 * layout.unit_bytes - sum(WIDTHS),
        "unused_byte_count": occupied.count(0),
        "operations": {op: features(layout, op) for op in MAPS},
    }


def declared_layouts(subset=None):
    known = {layout.id: layout for layout in layouts()}
    ids = sorted(known) if subset is None else subset
    require(isinstance(ids, list) and bool(ids), "subset must be a nonempty JSON list")
    require(all(isinstance(c, str) for c in ids), "subset must contain candidate ID strings")
    require(len(ids) == len(set(ids)), "duplicate candidate in subset")
    require(set(ids) <= known.keys(), "subset contains unknown candidates")
    return [known[c] for c in sorted(ids)]


def catalogue(subset=None):
    candidates = declared_layouts(subset)
    return {
        "schema_version": 1, "contract_id": CONTRACT,
        "universe": "all labelled byte-contained A1/B7/C3/D5 layouts in 2 or 3 bytes",
        "all_candidate_count": len(layouts()), "candidate_count": len(candidates),
        "complete_exhaustive_universe": len(candidates) == len(layouts()),
        "operations": [
            {"operation_id": op, "direction": "read" if op.startswith("R") else "write",
             "code_ids": [NAMES[i] for i in selected],
             "map": list(selected) + [255] * (8 - len(selected))}
            for op, selected in MAPS.items()
        ],
        "feature_status": "logical bit/byte structure; not measured accesses or instruction counts",
        "candidates": [describe(layout) for layout in candidates],
    }


def diversity_vector(layout):
    result = [len(set(b for b, _ in layout.positions))]
    for width, (offset, shift) in zip(WIDTHS, layout.positions):
        result.extend((offset, shift, int(0 < shift < 8 - width)))
    for op in MAPS:
        f = features(layout, op)
        result.extend((f["selected_byte_count"], f["selected_envelope_bytes"],
                       f.get("preservation_byte_count", 0), f["backward_steps"], f["offset_travel"]))
    return tuple(result)


def diverse_subset(three_byte_count=20):
    require(type(three_byte_count) is int and 16 <= three_byte_count <= 24,
            "three-byte-count must be an integer from 16 to 24 for this bounded selector")
    pool = [layout for layout in layouts() if layout.unit_bytes == 3]
    vectors = {layout.id: diversity_vector(layout) for layout in pool}
    columns = list(zip(*vectors.values()))
    ranges = [max(column) - min(column) for column in columns]
    common = math.lcm(*(r for r in ranges if r))
    scales = [common // r if r else 0 for r in ranges]
    # Integer-scaled normalized L1; exact deterministic ties, no measured costs.
    def distance(left, right):
        return sum(abs(x - y) * scale for x, y, scale in zip(vectors[left], vectors[right], scales))
    remaining = set(vectors)
    selected = [min(remaining)]
    remaining.remove(selected[0])
    nearest = {c: distance(c, selected[0]) for c in remaining}
    while len(selected) < three_byte_count:
        choice = min(remaining, key=lambda c: (-nearest[c], c))
        selected.append(choice)
        remaining.remove(choice)
        for candidate in remaining:
            nearest[candidate] = min(nearest[candidate], distance(candidate, choice))
    return sorted([layout.id for layout in layouts() if layout.unit_bytes == 2] + selected)


def tsv(subset=None):
    header = ["id", "unit_bytes"] + [f"{name}_{field}" for name in NAMES for field in ("offset", "shift")]
    lines = ["\t".join(header)]
    for layout in declared_layouts(subset):
        values = [layout.id, layout.unit_bytes] + [value for pair in layout.positions for value in pair]
        lines.append("\t".join(map(str, values)))
    return "\n".join(lines) + "\n"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value, name):
    require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
            f"{name} must be a finite nonnegative JSON number")
    return Fraction(str(value))


def text_field(obj, key):
    value = obj.get(key)
    require(isinstance(value, str) and bool(value.strip()), f"missing/nonempty string required: {key}")
    return value


@dataclass(frozen=True)
class Cost:
    recipe_id: str
    run_ns: Fraction
    prepare_ns: Fraction


def load_costs(document):
    require(isinstance(document, dict), "cost document must be an object")
    require(document.get("schema_version") == 1, "cost schema_version must be 1")
    require(document.get("kind") in ("measured", "synthetic"), "cost kind must be measured or synthetic")
    context = document.get("context", {})
    require(isinstance(context, dict), "context must be an object")
    for field in ("id", "provenance", "statistic"):
        text_field(context, field)
    require(context.get("contract_id") == CONTRACT, f"cost contract_id must be {CONTRACT}")
    known = {layout.id for layout in layouts()}
    rows = document.get("measurements")
    require(isinstance(rows, list) and bool(rows), "measurements must be a nonempty list")
    costs, seen = {}, set()
    for row in rows:
        require(isinstance(row, dict), "measurement rows must be objects")
        candidate, operation = row.get("candidate_id"), row.get("operation_id")
        require(candidate in known, f"unknown candidate: {candidate}")
        require(operation in MAPS, f"unknown operation: {operation}")
        recipe = text_field(row, "recipe_id")
        identity = (candidate, operation, recipe)
        require(identity not in seen, f"duplicate cost (summarize repetitions upstream): {identity}")
        seen.add(identity)
        cost = Cost(recipe, number(row.get("run_ns"), "run_ns"),
                    number(row.get("prepare_ns"), "prepare_ns"))
        costs.setdefault((candidate, operation), []).append(cost)
    return costs


def workload(document):
    require(isinstance(document, dict), "workload document must be an object")
    require(document.get("schema_version") == 1, "workload schema_version must be 1")
    text_field(document, "id")
    raw = document.get("weights")
    require(isinstance(raw, dict) and bool(raw), "weights must be a nonempty object")
    require(set(raw) <= MAPS.keys(), f"unknown workload operations: {set(raw) - MAPS.keys()}")
    weights = {op: number(weight, f"weight {op}") for op, weight in raw.items()}
    total = sum(weights.values())
    require(total > 0, "workload must contain positive weight")
    return {op: weight / total for op, weight in weights.items() if weight > 0}


def selection(candidate, costs, weights, horizon):
    chosen = {}
    for op, weight in weights.items():
        chosen[op] = min(costs[candidate, op],
                         key=lambda c: (horizon * weight * c.run_ns + c.prepare_ns, c.recipe_id))
    preparation = sum(c.prepare_ns for c in chosen.values())
    rate = sum(weights[op] * c.run_ns for op, c in chosen.items())
    return preparation + horizon * rate, preparation, rate, chosen


def plan_report(candidate, costs, weights, horizon):
    total, preparation, rate, chosen = selection(candidate, costs, weights, horizon)
    return {
        "candidate_id": candidate, "total_ns": total,
        "preparation_ns": preparation, "expected_execution_ns": horizon * rate,
        "steady_ns_per_invocation_for_selected_recipes": rate,
        "amortized_preparation_ns_per_invocation": preparation / horizon,
        "total_ns_per_invocation": total / horizon,
        "operations": {
            op: {"recipe_id": c.recipe_id, "expected_invocations": horizon * weights[op],
                 "preparations": 1, "run_ns": c.run_ns, "prepare_ns": c.prepare_ns,
                 "prepare_ns_per_expected_operation": c.prepare_ns / (horizon * weights[op])}
            for op, c in chosen.items()
        },
    }


def heuristic_keys(layout, weights):
    fs = {op: features(layout, op) for op in weights}
    weighted = lambda name: sum(weights[op] * f[name] for op, f in fs.items())
    return {
        "density_first": (layout.unit_bytes,),
        "edge_first": (weighted("interior_codes"), layout.unit_bytes),
        "outer_order_first": (weighted("backward_steps"), weighted("offset_travel"), layout.unit_bytes),
        "coaccess_first": (weighted("selected_byte_count"), layout.unit_bytes),
    }


def relative_regret(value, optimum):
    return (value - optimum) / optimum if optimum else None


def first_migration_win(candidate, costs, weights, resident_rate, migration_ns):
    fastest = {op: min(costs[candidate, op], key=lambda c: (c.run_ns, c.prepare_ns, c.recipe_id))
               for op in weights}
    fastest_rate = sum(weights[op] * c.run_ns for op, c in fastest.items())
    if fastest_rate >= resident_rate:
        return None
    overhead = migration_ns + sum(c.prepare_ns for c in fastest.values())
    # This fastest-execution plan bounds the first strict win from above.
    high = int(overhead // (resident_rate - fastest_rate)) + 1
    low = 0
    while high - low > 1:
        mid = (low + high) // 2
        target = migration_ns + selection(candidate, costs, weights, mid)[0]
        if target < mid * resident_rate:
            high = mid
        else:
            low = mid
    return high


def migration_report(document, cost_document, costs, weights, candidates, horizon):
    require(isinstance(document, dict), "migration document must be an object")
    require(document.get("schema_version") == 1, "migration schema_version must be 1")
    require(document.get("kind") == cost_document["kind"], "migration/cost evidence kinds differ")
    require(document.get("context_id") == cost_document["context"]["id"], "migration context_id differs")
    for field in ("provenance", "scope"):
        text_field(document, field)
    current = document.get("current_candidate")
    require(current in candidates, "current_candidate must belong to declared candidate set")
    resident = document.get("current_recipes", {})
    require(isinstance(resident, dict), "current_recipes must be an object")
    require(set(resident) == set(weights), "current_recipes must name exactly the active operations")
    rate = Fraction(0)
    for op, recipe in resident.items():
        matches = [c for c in costs[current, op] if c.recipe_id == recipe]
        require(len(matches) == 1, f"unknown resident recipe: {op}/{recipe}")
        rate += weights[op] * matches[0].run_ns
    migration = {}
    migration_rows = document.get("migration_costs", [])
    require(isinstance(migration_rows, list), "migration_costs must be a list")
    for row in migration_rows:
        require(isinstance(row, dict), "migration cost rows must be objects")
        candidate = row.get("candidate_id")
        require(candidate in candidates and candidate != current, "migration target outside set or equals current")
        require(candidate not in migration, f"duplicate migration target: {candidate}")
        migration[candidate] = number(row.get("total_ns"), "migration total_ns")
    require(set(migration) == set(candidates) - {current}, "missing migration costs for declared targets")
    rows = []
    for candidate, overhead in migration.items():
        target_total = overhead + selection(candidate, costs, weights, horizon)[0]
        first = first_migration_win(candidate, costs, weights, rate, overhead)
        rows.append({
            "candidate_id": candidate, "migration_ns": overhead,
            "total_ns_at_horizon": target_total,
            "saving_vs_stay_ns": horizon * rate - target_total,
            "first_strictly_better_integer_horizon": first,
            "recipes_at_first_win": None if first is None else {
                op: c.recipe_id for op, c in selection(candidate, costs, weights, first)[3].items()},
        })
    rows.sort(key=lambda row: (row["total_ns_at_horizon"], row["candidate_id"]))
    best = rows[0] if rows and rows[0]["total_ns_at_horizon"] < horizon * rate else None
    return {
        "current_candidate": current, "current_recipes": resident,
        "scope": document["scope"], "provenance": document["provenance"],
        "resident_preparation_is_sunk": True, "stay_total_ns": horizon * rate,
        "decision_at_horizon": "stay" if best is None else best["candidate_id"],
        "targets": rows,
    }


def rank(cost_document, workload_document, horizon, subset=None, migration=None, top=10):
    require(type(horizon) is int and horizon > 0, "invocations must be a positive integer")
    require(type(top) is int and top > 0, "top must be a positive integer")
    costs, weights = load_costs(cost_document), workload(workload_document)
    all_layouts = {layout.id: layout for layout in layouts()}
    candidates = [layout.id for layout in declared_layouts(subset)]
    missing = [(c, op) for c in candidates for op in weights if (c, op) not in costs]
    require(not missing, f"missing {len(missing)} candidate/operation costs; first: {missing[:3]}; use an explicit subset, never implicit filtering")
    scores = {c: selection(c, costs, weights, horizon)[0] for c in candidates}
    ordering = sorted(candidates, key=lambda c: (scores[c], c))
    optimum = scores[ordering[0]]
    keys = {c: heuristic_keys(all_layouts[c], weights) for c in candidates}
    heuristics = {}
    for name in keys[candidates[0]]:
        best_key = min(keys[c][name] for c in candidates)
        tied = [c for c in candidates if keys[c][name] == best_key]
        chosen = tied[0]  # Stable ID tie-break; costs do not choose the heuristic's layout.
        heuristics[name] = {
            "candidate_id": chosen, "structural_key": best_key, "tied_candidate_count": len(tied),
            "total_ns": scores[chosen], "regret_ns": scores[chosen] - optimum,
            "relative_regret": relative_regret(scores[chosen], optimum),
            "regret_ns_range_over_structural_ties": [min(scores[c] for c in tied) - optimum,
                                                     max(scores[c] for c in tied) - optimum],
        }
    result = {
        "schema_version": 1, "kind": cost_document["kind"], "context": cost_document["context"],
        "workload_id": workload_document["id"], "normalized_weights": weights,
        "invocation_horizon": horizon,
        "preparation_scenario": "one fresh independent binding per active operation; retained through horizon",
        "universe": {"all_candidate_count": len(all_layouts), "declared_candidate_count": len(candidates),
                     "complete_exhaustive_universe": len(candidates) == len(all_layouts),
                     "candidate_ids": candidates,
                     "ids_sha256": hashlib.sha256("\n".join(candidates).encode()).hexdigest()},
        "optimal_tie_count": sum(score == optimum for score in scores.values()),
        "ranked_plans": [plan_report(c, costs, weights, horizon) for c in ordering[:top]],
        "heuristics": heuristics,
        "limits": ["Optimum only within declared layouts and supplied recipe alternatives.",
                   "No structural feature is converted into time or used to fill missing costs.",
                   "Linear workload reweighting assumes independent invocations with stable costs; mixed traces need validation.",
                   "Exact numerical ties use supplied decimal summaries, not statistical equivalence.",
                   "Preparation sharing, code-cache interactions and future reuse are not inferred."],
    }
    if migration is not None:
        result["migration"] = migration_report(migration, cost_document, costs, weights, candidates, horizon)
    return result


def read_json(path):
    with Path(path).open() as stream:
        return json.load(stream)


def write_json(value, path):
    def convert(obj):
        if isinstance(obj, Fraction):
            return float(obj)
        raise TypeError(f"unsupported output type: {type(obj).__name__}")
    encoded = json.dumps(value, indent=2, allow_nan=False,
                         default=convert) + "\n"
    write_text(encoded, path)


def write_text(encoded, path):
    if path:
        destination = Path(path)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(encoded)
    else:
        print(encoded, end="")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    enumerate_parser = commands.add_parser("enumerate", help="export exhaustive candidates, maps and logical features")
    enumerate_parser.add_argument("--format", choices=("json", "tsv"), default="json")
    enumerate_parser.add_argument("--subset", help="optional JSON list of candidate IDs")
    enumerate_parser.add_argument("--output")
    selector = commands.add_parser("select", help="all 8 two-byte layouts plus a deterministic structural spread")
    selector.add_argument("--three-byte-count", type=int, default=20)
    selector.add_argument("--output")
    ranking = commands.add_parser("rank", help="rank complete measured/synthetic costs within an explicit universe")
    ranking.add_argument("--costs", required=True)
    ranking.add_argument("--workload", required=True)
    ranking.add_argument("--invocations", type=int, required=True)
    ranking.add_argument("--subset", help="JSON list of candidate IDs; omission requires all 2,816 layouts")
    ranking.add_argument("--migration")
    ranking.add_argument("--top", type=int, default=10)
    ranking.add_argument("--output")
    args = parser.parse_args()
    try:
        if args.command == "enumerate":
            subset = None if args.subset is None else read_json(args.subset)
            if args.format == "tsv":
                write_text(tsv(subset), args.output)
                return
            result = catalogue(subset)
        elif args.command == "select":
            result = diverse_subset(args.three_byte_count)
        else:
            result = rank(read_json(args.costs), read_json(args.workload), args.invocations,
                          None if args.subset is None else read_json(args.subset),
                          None if args.migration is None else read_json(args.migration), args.top)
        write_json(result, args.output)
    except (ValueError, OSError, TypeError, KeyError) as error:
        parser.exit(2, f"error: {error}\n")


if __name__ == "__main__":
    main()
