"""Small authored examples and seeded finite workloads; randomness stops here."""

import random

from model import integer


def part(name, shard, key, after=(), mode="W", work=1):
    return {"id": name, "shard": shard, "locks": {key: mode}, "after": list(after), "work": work}


def transaction(name, parts, arrival=0, kind="C"):
    return {"id": name, "kind": kind, "coordinator": parts[0]["shard"],
            "arrival_us": arrival, "parts": parts}


def base(name, txs, **extra):
    return {"version": 1, "name": name,
            "shards": {s: {"period_us": 100, "capacity": 8} for s in ("A", "B")},
            "control_delay_us": 100, "horizon_us": 8000,
            "policy": {"yield": "older", "reserve_after": 3, "backoff": 4},
            "transactions": txs, **extra}


def cycle():
    return base("Opposite-order preparation", [
        transaction("T1", [part("a", "A", "x"), part("b", "B", "y", ["a"])]),
        transaction("T2", [part("b", "B", "y"), part("a", "A", "x", ["b"])]),
    ])


def discovery():
    return base("A → B → A discovery", [transaction("T1", [
        part("read-A", "A", "i", mode="R"),
        part("read-B", "B", "j", ["read-A"], mode="R"),
        part("write-A", "A", "result", ["read-B"]),
    ])])


def reservation_cycle():
    s = base("Reservation blocks an older contender", [
        transaction("T1", [part("b", "B", "y"), part("a", "A", "x", ["b"])]),
        transaction("T2", [part("a", "A", "x"), part("b", "B", "y", ["a"])], arrival=10),
        transaction("T3", [part("a", "A", "x")], arrival=20),
    ])
    s["shards"]["B"]["period_us"] = 300
    s["policy"]["backoff"] = 1
    return s


def readers():
    return base("Shared readers and a writer", [
        transaction(f"R{i}", [part("a", "A", "x", mode="R"), part("b", "B", f"y{i}", ["a"])])
        for i in range(4)
    ] + [transaction("W", [part("write", "A", "x")], arrival=50, kind="L")])


def convoy():
    s = base("Slow participant, local convoy", [transaction("C0", [
        part("a", "A", "hot"), part("b", "B", "cold", ["a"], work=20)
    ])] + [transaction(f"L{i:02}", [part("write", "A", "hot")], arrival=100 + 80 * i, kind="L") for i in range(24)])
    s["shards"]["B"]["period_us"] = 1800
    return s


def local_queue(count=40):
    return base("Local hot-key retry queue", [transaction(f"L{i:03}", [part("p", "A", "hot")], kind="L")
                                             for i in range(count)], horizon_us=20000)


def workload(count=80, hot_percent=80, seed=7, arrival_span_us=2400):
    integer(count, "count", 1, 300)
    integer(hot_percent, "hot_percent", 0, 100)
    integer(seed, "seed", 0, 2**32 - 1)
    integer(arrival_span_us, "arrival_span_us", 0, 100000)
    rng = random.Random(seed)
    txs = []

    def key():
        # Consume identical draws at every hotspot setting, preserving arrivals,
        # transaction shapes and weights in paired contention comparisons.
        hot_draw, cold_key = rng.randrange(100), rng.randrange(24)
        return "hot" if hot_draw < hot_percent else f"k{cold_key}"

    for i in range(count):
        start = rng.choice(("A", "B"))
        other = "B" if start == "A" else "A"
        local = rng.randrange(100) < 35
        parts = [part("p1", start, key(), work=rng.randrange(1, 6))]
        if not local:
            parts.append(part("p2", other, key(), ["p1"], work=rng.randrange(1, 6)))
            if rng.randrange(100) < 30:
                parts.append(part("p3", start, key(), ["p2"]))
        txs.append(transaction(f"T{i:03}", parts, rng.randrange(arrival_span_us + 1), "L" if local else "C"))
    s = base(f"Hotspot {hot_percent}% · {count} transactions · seed {seed}", txs, horizon_us=20000)
    s["generator"] = {"count": count, "hot_percent": hot_percent, "seed": seed, "arrival_span_us": arrival_span_us}
    return s


def presets():
    return {"discovery": discovery(), "cycle": cycle(), "reservation": reservation_cycle(), "readers": readers(),
            "convoy": convoy(), "hotspot": workload(), "spread": workload(hot_percent=0), "local": local_queue()}
