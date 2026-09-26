#!/usr/bin/env python3
"""Finite serial-history witnesses; not a concurrency-control implementation.

Each fixture supplies operations and a proposed committed observation/final-state
pair. Enumerate serial executions, respecting any explicit precedence edges.
No timings, failure recovery, intermediate visibility, or protocol are modeled.
"""

import argparse
from copy import deepcopy
from dataclasses import dataclass
import hashlib
from itertools import permutations
import json
from pathlib import Path
from typing import Callable


@dataclass
class Case:
    name: str
    question: str
    initial: dict
    operations: dict[str, Callable]
    observed: dict
    final: dict
    expected: bool
    before: tuple = ()


def maximum(state):
    return max(state["rows"], key=lambda key: (state["rows"][key], key))


def choose(state):
    selected = maximum(state)
    state["chosen"] = selected
    return selected


def insert_larger(state):
    state["rows"]["b"] = 101
    return "ok"


def observe_then_insert(state):
    previous = state["chosen"]
    insert_larger(state)
    return previous


def insert_if_empty(key):
    def operation(state):
        empty = not state["rows"]
        if empty:
            state["rows"][key] = 1
        return empty
    return operation


def assign(values, returning=None):
    def operation(state):
        result = state[returning] if returning else "ok"
        state.update(values)
        return result
    return operation


def add(values, returning=None):
    def operation(state):
        for key, delta in values.items():
            state[key] += delta
        return state[returning] if returning else "ok"
    return operation


def double_all(state):
    for key in state:
        state[key] *= 2
    return "ok"


def update_loser(state):
    state["rows"]["b"] = 2
    return "ok"


def report(state):
    return dict(state)


def total(state):
    return sum(state.values())


def cases():
    base = {"rows": {"a": 100}, "chosen": None}
    result = {"rows": {"a": 100, "b": 101}, "chosen": "a"}
    yield Case("max_with_independent_insert",
               "A changed maximum need not prevent an overlapping select-and-act.",
               base, {"R": choose, "W": insert_larger},
               {"R": "a", "W": "ok"}, result, True)
    yield Case("max_with_dependency_cycle",
               "An extra read closes the cycle despite disjoint write locations.",
               base, {"R": choose, "W": observe_then_insert},
               {"R": "a", "W": None}, result, False)
    yield Case("old_max_after_completed_insert",
               "A serial witness must also respect promised real-time precedence.",
               base, {"R": choose, "W": insert_larger},
               {"R": "a", "W": "ok"}, result, False, (("W", "R"),))
    yield Case("empty_predicate_two_inserts",
               "No initial keys does not mean no read dependency.",
               {"rows": {}}, {"T": insert_if_empty("a"), "U": insert_if_empty("b")},
               {"T": True, "U": True}, {"rows": {"a": 1, "b": 1}}, False)
    yield Case("stale_root_loses_disjoint_write",
               "A private whole-root image can overwrite an unrelated live update.",
               {"a": 10, "b": 20}, {"B": add({"a": 1}), "P": assign({"b": 23})},
               {"B": "ok", "P": "ok"}, {"a": 11, "b": 20}, False)
    yield Case("staged_read_modify_write_loses_increment",
               "Applying a stale bulk result is not applying the ordered program.",
               {"a": 10, "b": 20}, {"B": double_all, "P": add({"b": 3})},
               {"B": "ok", "P": "ok"}, {"a": 20, "b": 40}, False)
    yield Case("ordered_bulk_program_after_increment",
               "A bulk program evaluated at its serial position includes earlier writes.",
               {"a": 10, "b": 20}, {"B": double_all, "P": add({"b": 3})},
               {"B": "ok", "P": "ok"}, {"a": 20, "b": 46}, True)
    yield Case("blind_bulk_assignment",
               "An overlapping blind overwrite can be valid at the later serial position.",
               {"a": 10, "b": 20},
               {"B": assign({"a": 100, "b": 100}), "P": assign({"b": 23})},
               {"B": "ok", "P": "ok"}, {"a": 100, "b": 100}, True)
    yield Case("bulk_assignment_returning_old_value",
               "Returning an old value removes the blind operation's reorder freedom.",
               {"a": 10, "b": 20},
               {"B": assign({"a": 100, "b": 100}, "b"), "P": assign({"b": 23})},
               {"B": 20, "P": "ok"}, {"a": 100, "b": 100}, False)
    yield Case("chunked_publication_partial_reader",
               "A reader between chunks distinguishes chunked and atomic bulk updates.",
               {"a": 0, "b": 0}, {"B": assign({"a": 1, "b": 1}), "R": total},
               {"B": "ok", "R": 1}, {"a": 1, "b": 1}, False)
    yield Case("commuting_bulk_deltas",
               "Exact deltas with completion-only results permit either order.",
               {"a": 10, "b": 20}, {"B": add({"a": 1, "b": 1}), "P": add({"b": 3})},
               {"B": "ok", "P": "ok"}, {"a": 11, "b": 24}, True)
    yield Case("deltas_returning_intermediate_values",
               "Commuting final state does not ensure compatible returned values.",
               {"a": 10, "b": 20},
               {"B": add({"a": 1, "b": 1}, "b"), "P": add({"b": 3}, "b")},
               {"B": 21, "P": 23}, {"a": 11, "b": 24}, False)
    yield Case("maximum_unchanged_by_loser_update",
               "A scanned version can change while the query result remains admissible.",
               {"rows": {"a": 100, "b": 1}, "chosen": None},
               {"R": choose, "W": update_loser}, {"R": "a", "W": "ok"},
               {"rows": {"a": 100, "b": 2}, "chosen": "a"}, True)
    yield Case("incoherent_cross_shard_cut",
               "Combining two locally committed versions can tear a transfer.",
               {"x": 10, "y": 10}, {"T": add({"x": -1, "y": 1}), "R": total},
               {"T": "ok", "R": 21}, {"x": 9, "y": 11}, False)
    yield Case("independent_point_read_modify_writes",
               "Coarse table validation can reject point transactions that admit every order.",
               {str(index): 0 for index in range(4)},
               {f"T{index}": add({str(index): 1}) for index in range(4)},
               {f"T{index}": "ok" for index in range(4)},
               {str(index): 1 for index in range(4)}, True)
    for count in (2, 4):
        initial = {str(index): 0 for index in range(count)}
        operations = {"R": report}
        operations.update({f"W{index}": assign({str(index): 1}) for index in range(count)})
        observed = {"R": dict(initial)}
        observed.update({f"W{index}": "ok" for index in range(count)})
        yield Case(f"broad_report_{count}_independent_writers",
                   "All reader-to-writer dependencies point forward; writers remain independent.",
                   initial, operations, observed, dict.fromkeys(initial, 1), True)


def inspect(case):
    checked = 0
    witnesses = []
    for order in permutations(case.operations):
        if any(order.index(first) > order.index(second) for first, second in case.before):
            continue
        checked += 1
        state = deepcopy(case.initial)
        observations = {name: case.operations[name](state) for name in order}
        if state == case.final and observations == case.observed:
            witnesses.append(order)
    assert bool(witnesses) == case.expected, case.name
    return {"name": case.name, "question": case.question,
            "initial": case.initial, "observed": case.observed, "final": case.final,
            "required_precedence": case.before, "serial_orders_checked": checked,
            "admissible": bool(witnesses), "witness_count": len(witnesses),
            "first_witness": witnesses[0] if witnesses else None}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    result = {"model": "finite serial-history enumeration",
              "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "limits": "No concurrency protocol, costs, failure model or epoch-fixpoint proof. "
                        "Only declared operations, final state, outputs and precedence are checked.",
              "cases": [inspect(case) for case in cases()]}
    encoded = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
        print(f"Checked {len(result['cases'])} cases; wrote {args.output}")
    else:
        print(encoded, end="")


if __name__ == "__main__":
    main()
