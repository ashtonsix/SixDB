"""Logical research input and independent exact oracle; no Frame format is implied."""

import argparse
import copy
import json
import random
import sys

U32 = (1 << 32) - 1
U64 = (1 << 64) - 1
FORMAT = "frame-execution-logical-v1"
TRANSPORT = "frame-execution-transport-v2"


def unsigned(value, maximum, name):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError(f"{name} outside unsigned domain")


def label(value, name):
    if (not isinstance(value, str) or not value or
            any(not 32 <= ord(char) <= 126 for char in value)):
        raise ValueError(f"{name} must be a nonempty printable ASCII fixture label")


def validate(document):
    if (document.get("format") != FORMAT or
            document.get("kind") not in ("authored-synthetic", "corpus-derived")):
        raise ValueError("expected logical Frame execution input with an explicit source kind")
    if document["kind"] == "corpus-derived" and (
            not isinstance(document.get("source"), dict) or not document["source"]):
        raise ValueError("corpus-derived inputs must carry their source provenance")
    label(document.get("snapshot"), "snapshot")
    parts = {}
    total = 0
    for part in document["parts"]:
        name = part["part_id"]
        label(name, "part identity")
        if name in parts:
            raise ValueError("duplicate or empty part identity")
        parts[name] = part
        label(part["dictionary_edition"], "dictionary edition")
        unsigned(part["position_limit"], 65536, "position limit")
        dictionary = part["dictionary"]
        for key in dictionary:
            unsigned(key, U64, "dictionary key")
        if len(set(dictionary)) != len(dictionary):
            raise ValueError("duplicate dictionary value")
        keys, occupied = set(dictionary), set()
        for row in part["rows"]:
            unsigned(row["position"], part["position_limit"] - 1, "position")
            if row["position"] in occupied:
                raise ValueError("duplicate physical position")
            occupied.add(row["position"])
            if row["category"] is not None:
                unsigned(row["category"], U64, "category")
                if row["category"] not in keys:
                    raise ValueError("logical category absent from this dictionary")
            if row["x"] is not None:
                unsigned(row["x"], U32, "X")
        total += len(part["rows"])
    # This conservative bound admits exact uint64 SUM for every query/group.
    if total > U64 // U32:
        raise ValueError("input does not admit the exact uint64 sum law")
    queries = set()
    for query in document["queries"]:
        name = query["query_id"]
        label(name, "query identity")
        if name in queries:
            raise ValueError("duplicate or empty query identity")
        queries.add(name)
        if query["snapshot"] != document["snapshot"]:
            raise ValueError("selection belongs to another snapshot")
        unsigned(query["min_x"], U64, "threshold")
        unsigned(query["group_shift"], 63, "group shift")
        if query["category_mode"] not in ("all", "in"):
            raise ValueError("unknown category predicate")
        if type(query["include_null_category"]) is not bool:
            raise ValueError("null-category policy must be explicit")
        for key in query["categories"]:
            unsigned(key, U64, "query category")
        for part_id, selection in query["selection"].items():
            if part_id not in parts:
                raise ValueError("selection names an unknown part")
            if selection is not None:
                if len(set(selection)) != len(selection):
                    raise ValueError("duplicate selected position")
                for position in selection:
                    unsigned(position, parts[part_id]["position_limit"] - 1,
                             "selected position")
        for part_id, coarse in query.get("coarse_selection", {}).items():
            if part_id not in parts:
                raise ValueError("coarse selection names an unknown part")
            grain = coarse["positions_per_block"]
            unsigned(grain, 65536, "coarse grain")
            if not grain:
                raise ValueError("coarse grain must be positive")
            count = (parts[part_id]["position_limit"] + grain - 1) // grain
            per_group = coarse.get("blocks_per_group", max(1, count))
            unsigned(per_group, 65536, "blocks per group")
            if not per_group:
                raise ValueError("blocks per group must be positive")
            blocks = coarse["blocks"]
            if blocks is not None:
                if len(set(blocks)) != len(blocks):
                    raise ValueError("duplicate coarse block")
                for block in blocks:
                    unsigned(block, count - 1, "coarse block")


def oracle(document):
    """Read logical rows only: dictionaries and encoded codes never enter evaluation."""
    validate(document)
    answers = []
    for query in document["queries"]:
        groups = {}
        for part in document["parts"]:
            if part["part_id"] not in query["selection"]:
                continue
            chosen = query["selection"][part["part_id"]]
            chosen = None if chosen is None else set(chosen)
            coarse = query.get("coarse_selection", {}).get(part["part_id"])
            active_blocks = (None if coarse is None or coarse["blocks"] is None
                             else set(coarse["blocks"]))
            for row in part["rows"]:
                if chosen is not None and row["position"] not in chosen:
                    continue
                if (active_blocks is not None and
                        row["position"] // coarse["positions_per_block"] not in active_blocks):
                    continue
                value, category = row["x"], row["category"]
                if value is None or value < query["min_x"]:
                    continue
                if category is None:
                    if not query["include_null_category"]:
                        continue
                    key = None
                else:
                    if (query["category_mode"] == "in" and
                            category not in query["categories"]):
                        continue
                    key = category >> query["group_shift"]
                count, summed = groups.get(key, (0, 0))
                groups[key] = count + 1, summed + value
        rows = [{"key": key, "count": count, "sum": summed}
                for key, (count, summed) in groups.items()]
        rows.sort(key=lambda row: (row["key"] is not None, row["key"] or 0))
        answers.append({"query_id": query["query_id"], "groups": rows})
    return answers


def transport(document):
    """A private text bridge to the C++ hash witness, not a stored encoding."""
    validate(document)
    lines = [TRANSPORT, json.dumps(document["snapshot"]),
             f'{len(document["parts"])} {len(document["queries"])}']
    for part in document["parts"]:
        dictionary = part["dictionary"]
        codes = {key: code for code, key in enumerate(dictionary)}
        lines.append(f'PART {json.dumps(part["part_id"])} '
                     f'{json.dumps(part["dictionary_edition"])} '
                     f'{part["position_limit"]} {len(dictionary)} {len(part["rows"])}')
        lines.append(" ".join(map(str, dictionary)))
        for row in part["rows"]:
            code = -1 if row["category"] is None else codes[row["category"]]
            lines.append(f'{row["position"]} {code} {int(row["x"] is not None)} '
                         f'{row["x"] or 0}')
    for query in document["queries"]:
        lines.append(f'QUERY {json.dumps(query["query_id"])} {query["min_x"]} '
                     f'{int(query["category_mode"] == "all")} '
                     f'{int(query["include_null_category"])} {query["group_shift"]} '
                     f'{len(query["categories"])}')
        lines.append(" ".join(map(str, query["categories"])))
        for part in document["parts"]:
            selected = query["selection"].get(part["part_id"], [])
            lines.append("-1" if selected is None else
                         " ".join(map(str, [len(selected), *selected])))
            coarse = query.get("coarse_selection", {}).get(part["part_id"],
                         {"positions_per_block": max(1, part["position_limit"]), "blocks": None})
            blocks = coarse["blocks"]
            count = (part["position_limit"] + coarse["positions_per_block"] - 1) // coarse["positions_per_block"]
            lines.append(f'COARSE {coarse["positions_per_block"]} '
                         f'{coarse.get("blocks_per_group", max(1, count))} ' +
                         ("-1" if blocks is None else
                          " ".join(map(str, [len(blocks), *blocks]))))
    return "\n".join(lines) + "\n"


def query(name, selection, *, min_x=0, categories=None, include_null=True, shift=0, coarse=None):
    result = {"query_id": name, "snapshot": "basis-0", "selection": selection,
            "min_x": min_x, "category_mode": "all" if categories is None else "in",
            "categories": [] if categories is None else categories,
            "include_null_category": include_null, "group_shift": shift}
    if coarse is not None:
        result["coarse_selection"] = coarse
    return result


def mask_variants(document, *, positions_per_block, blocks_per_group, query_index=0):
    """Reuse unchanged logical rows; add authored coarse controls to one query."""
    validate(document)
    for value, name in ((positions_per_block, "positions per block"),
                        (blocks_per_group, "blocks per group")):
        unsigned(value, 65536, name)
        if not value:
            raise ValueError(f"{name} must be positive")
    base = document["queries"][query_index]
    result = {**document, "queries": list(document["queries"]),
              "query_variants": {"kind": "authored-coarse-controls", "basis": base["query_id"],
                                 "positions_per_block": positions_per_block,
                                 "blocks_per_group": blocks_per_group}}
    for name in ("all", "none", "clustered", "scattered", "tail"):
        candidate = copy.deepcopy(base)
        candidate["query_id"] += f"/coarse-{name}"
        candidate["coarse_selection"] = {}
        for part in document["parts"]:
            count = (part["position_limit"] + positions_per_block - 1) // positions_per_block
            blocks = {"all": None, "none": [], "clustered": list(range((count + 3) // 4)),
                      "scattered": list(range(0, count, 4)),
                      "tail": [count - 1] if count else []}[name]
            candidate["coarse_selection"][part["part_id"]] = {
                "positions_per_block": positions_per_block,
                "blocks_per_group": blocks_per_group, "blocks": blocks}
        result["queries"].append(candidate)
    validate(result)
    return result


def example():
    def row(position, category, x):
        return {"position": position, "category": category, "x": x}
    return {"format": FORMAT, "kind": "authored-synthetic", "snapshot": "basis-0",
            "parts": [
                {"part_id": "left", "dictionary_edition": "left-d0", "position_limit": 65536,
                 "dictionary": [10, U64, 0, 20],
                 "rows": [row(0, 10, 7), row(2, U64, 3), row(3, None, 5),
                          row(5, 0, None), row(65535, 20, 0)]},
                {"part_id": "right", "dictionary_edition": "right-d0", "position_limit": 10,
                 "dictionary": [U64, 20, 10, 0],
                 "rows": [row(0, U64, 11), row(1, 10, 13), row(3, None, 17),
                          row(5, 0, 19), row(8, 20, None)]}],
            "queries": [
                query("all", {"left": None, "right": None}),
                query("category", {"left": None, "right": None}, min_x=5,
                      categories=[10], include_null=False),
                query("holes", {"left": [1, 2, 65535], "right": [0, 3, 5]},
                      include_null=False),
                query("regroup", {"left": None, "right": None}, shift=63),
                query("above-domain", {"left": None, "right": None}, min_x=U32 + 1),
                query("null-category", {"left": None, "right": None}, categories=[]),
                query("empty", {})]}


def generate(seed=713, parts=3, rows=256, categories=12):
    if parts < 1 or not 0 <= rows <= 65536 or not 1 <= categories <= 65536:
        raise ValueError("invalid synthetic population")
    rng = random.Random(seed)
    pool = [0] if categories == 1 else [0, U64]
    used = set(pool)
    while len(pool) < categories:
        value = rng.getrandbits(64)
        if value not in used:
            pool.append(value)
            used.add(value)
    result = {"format": FORMAT, "kind": "authored-synthetic", "snapshot": "basis-0",
              "generation": {"seed": seed, "parts": parts, "rows_per_part": rows,
                             "categories": categories}, "parts": [], "queries": []}
    for i in range(parts):
        dictionary = pool.copy()
        rng.shuffle(dictionary)
        positions = sorted(rng.sample(range(65536), rows))
        records = []
        for position in positions:
            category = None if rng.randrange(11) == 0 else rng.choice(pool)
            x = None if rng.randrange(9) == 0 else rng.getrandbits(32)
            records.append({"position": position, "category": category, "x": x})
        result["parts"].append({"part_id": f"part-{i}", "dictionary_edition": f"dict-{i}-0",
                                "position_limit": 65536, "dictionary": dictionary,
                                "rows": records})
    all_rows = {part["part_id"]: None for part in result["parts"]}
    scattered = {part["part_id"]: [r["position"] for r in part["rows"][::3]]
                 for part in result["parts"]}
    contiguous = {part["part_id"]: list(range(16384, 32768)) for part in result["parts"]}
    result["queries"] = [query("all", all_rows),
                         query("scattered", scattered, min_x=U32 // 2),
                         query("interval", contiguous),
                         query("few-categories", all_rows, categories=pool[:2], include_null=False),
                         query("regroup", all_rows, shift=63),
                         query("null-category", all_rows, categories=[]),
                         query("above-domain", all_rows, min_x=U32 + 1), query("empty", {})]
    validate(result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    gen = sub.add_parser("generate")
    for name, default in (("seed", 713), ("parts", 3), ("rows", 256), ("categories", 12)):
        gen.add_argument(f"--{name}", type=int, default=default)
    gen.add_argument("--output", required=True)
    for command in ("oracle", "transport"):
        sub.add_parser(command).add_argument("input")
    args = parser.parse_args()
    if args.command == "generate":
        document = generate(args.seed, args.parts, args.rows, args.categories)
        with open(args.output, "w") as output:
            json.dump(document, output, indent=2)
            output.write("\n")
    else:
        with open(args.input) as source:
            document = json.load(source)
        if args.command == "oracle":
            json.dump(oracle(document), sys.stdout, indent=2)
            print()
        else:
            sys.stdout.write(transport(document))


if __name__ == "__main__":
    main()
