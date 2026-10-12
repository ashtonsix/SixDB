#!/usr/bin/env python3
"""Retain small witnesses against overgeneralizing palette and packing results."""
import argparse
import hashlib
import json
import random
from pathlib import Path

from recommend import Pool, census


def measure(family, layout, page, offered, alignment, sizes):
    pool = Pool(family, layout, page, "strict", sizes, "first")
    for i, size in enumerate(offered):
        pool.allocate(i, size, alignment)
    row = dict(family=family, layout=layout, page=page, alignment=alignment,
               selection="strict", class_choice="first", count=len(offered))
    row.update(census(pool, "initial_"))
    keep = set(random.Random(9).sample(range(len(offered)), len(offered) // 16))
    for i in range(len(offered)):
        if i not in keep:
            pool.free(i)
    pool.trim()
    row.update(census(pool, "settled_"))
    for i in keep:
        pool.free(i)
    pool.trim()
    assert pool.charge() == 0
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sizes", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("build/allocation-palettes/results/counterexamples.json"))
    args = parser.parse_args()
    sizes = json.loads(args.sizes.read_text())
    rng = random.Random(37)
    upper = [rng.randint(1153, 3072) for _ in range(1024)]
    tiny = [(16, 32, 48, 64, 96)[i % 5] for i in range(256)]
    rows = []
    for page in (16384, 65536):
        pair = [measure(f, "class-aligned", page, upper, 64, sizes)
                for f in ("current", "upper-band")]
        assert pair[0]["initial_requested"] == pair[1]["initial_requested"]
        assert pair[0]["initial_backed"] < pair[1]["initial_backed"]
        rows.extend(dict(case="broad-upper", **r) for r in pair)
    pair = [measure("current", layout, 65536, tiny, 16, sizes)
            for layout in ("class-aligned", "small-packed")]
    assert pair[0]["initial_backed"] == 65536 and pair[1]["initial_backed"] == 3 * 65536
    rows.extend(dict(case="mixed-tiny-aperture", **r) for r in pair)
    context = dict(kind=__doc__, rows=rows,
                   inputs={"broad-upper": "1024 randint(1153,3072), Python Random(37)",
                           "mixed-tiny-aperture": "256 repetitions of16,32,48,64,96"},
                   native_record_sizes=sizes,
                   source_hashes={name: hashlib.sha256((Path(__file__).parent / name).read_bytes()).hexdigest()
                                  for name in ("run.py", "recommend.py", "counterexamples.py")})
    args.output.write_text(json.dumps(context, indent=2) + "\n")
    print("6 counterexample cells; request equality and independent page-count witnesses passed")


if __name__ == "__main__":
    main()
