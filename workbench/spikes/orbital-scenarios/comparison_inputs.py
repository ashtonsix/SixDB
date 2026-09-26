"""Shared offered workloads for the arbitration / BRIEF2 comparison.

Ticks and work units are model inputs, not measured database timings. Private
``delay`` is deliberately separate from ``link_delay``: computation holds no
BRIEF2 write promise, while the commitment exchange can hold promises.
"""

from copy import deepcopy
import random


def _transaction(identifier, arrival, group, coordinator, reads, writes, op,
                 **options):
    return {
        "id": identifier,
        "arrival": arrival,
        "group": group,
        "coordinator": coordinator,
        "reads": list(reads),
        "writes": list(writes),
        "op": op,
        **options,
    }


def _case(name, description, initial, collections, transactions, **options):
    scopes = {key: [key] for key in initial}
    scopes.update({scope: list(keys) for scope, keys in collections.items()})
    return {
        "name": name,
        "description": description,
        "initial": dict(initial),
        "scopes": scopes,
        "transactions": sorted(transactions,
                               key=lambda txn: (txn["arrival"], txn["id"])),
        "horizon": 600,
        "capacity": 64,
        "link_delay": 5,
        "arbitration_delay": 10,
        "retry_delay": 3,
        "retry_limit": 4,
        "collect_period": 5,
        **options,
    }


def _rows(width, value=1):
    collections = {
        f"{shard}/rows": [f"{shard}/row/{index:04d}"
                          for index in range(width)]
        for shard in ("eu", "us")
    }
    initial = {key: value for keys in collections.values() for key in keys}
    return initial, collections


def _free_keys(initial, width=8):
    keys = [f"{shard}/free/{index:02d}"
            for shard in ("eu", "us") for index in range(width)]
    initial.update({key: 0 for key in keys})
    return keys


def _point_stream(rng, prefix, keys, count, *, start=0, spacing=1,
                  group="regional", read_only=False, per_tick=1):
    transactions = []
    for index in range(count):
        key = rng.choice(keys)
        transactions.append(_transaction(
            f"{prefix}-{index:04d}",
            start + spacing * (index // per_tick) + rng.randrange(3),
            group, key.split("/", 1)[0], [key],
            [] if read_only else [key],
            "report" if read_only else "increment"))
    return transactions


def _reports(rng, prefix, scopes, count=96, *, start=20, spacing=3,
             allow_old=False):
    return [_transaction(
        f"{prefix}-{index:04d}", start + spacing * index + rng.randrange(3),
        "report", "eu", scopes, [], "report", allow_old=allow_old)
        for index in range(count)]


def cases(seed=7, width=64):
    """Return reproducible, independent plain-dict workload fixtures.

    ``width`` is rows per shard in broad collections. The number of offered
    requests stays bounded as width grows; scan/certification coverage grows.
    Missing rows are explicit ``None`` slots in collection coverage. Aggregate
    operations ignore these absent values, while reports preserve absence.
    """
    if not isinstance(width, int) or width < 4:
        raise ValueError("width must be an integer of at least 4")
    result = []

    # Each case has its own RNG, so adding one does not reshuffle the others.
    def rng_for(name):
        return random.Random(f"{seed}/{name}")

    name = "ordinary_disjoint"
    rng = rng_for(name)
    initial, collections = _rows(width)
    free = _free_keys(initial)
    transactions = _point_stream(rng, "ordinary-write", free, 128,
                                 per_tick=2)
    transactions += _point_stream(rng, "ordinary-lookup", free, 64,
                                  group="report", read_only=True)
    transactions += _reports(rng, "ordinary-scan", list(collections), 32,
                             start=5, spacing=4)
    result.append(_case(name,
        "Ordinary local lookups and updates, plus reports over disjoint static "
        "rows. Reports always contain ones; local operations use the same "
        "atomic fast path in both models.",
        initial, collections, transactions, horizon=400))

    name = "ordinary_hot_local"
    rng = rng_for(name)
    initial = {"eu/hot": 0}
    free = _free_keys(initial)
    transactions = _point_stream(rng, "hot-increment", ["eu/hot"], 200,
                                 per_tick=4, group="local_hot")
    transactions += _point_stream(rng, "hot-unrelated", free, 64)
    result.append(_case(name,
        "Two hundred ordinary local increments of one key; final hot value "
        "must be 200 if all complete. Queueing is capacity cost, not a reason "
        "to model local reads and writes as independently interleaved C1 work.",
        initial, {}, transactions, horizon=300, capacity=16))

    name = "disjoint_wan"
    rng = rng_for(name)
    initial = {f"{shard}/wan/{index:02d}": 0
               for shard in ("eu", "us") for index in range(32)}
    free = _free_keys(initial)
    transactions = [_transaction(
        f"disjoint-wan-{index:03d}", 3 * index, "wan", "eu",
        [f"eu/wan/{index:02d}", f"us/wan/{index:02d}"],
        [f"eu/wan/{index:02d}", f"us/wan/{index:02d}"],
        "increment", delay=60) for index in range(32)]
    transactions += _point_stream(rng, "disjoint-local", free, 128)
    transactions += _point_stream(rng, "disjoint-lookup", free, 48,
                                  group="report", read_only=True)
    result.append(_case(name,
        "Independent cross-shard increments spend 60 ticks computing, with "
        "unrelated local work throughout. Every WAN pair should end at (1,1); "
        "computation delay is additional to actual control-message latency.",
        initial, {}, transactions, horizon=400))

    name = "narrow_conflicting_wan"
    rng = rng_for(name)
    initial = {"eu/hot": 0, "us/hot": 0}
    free = _free_keys(initial)
    transactions = [_transaction(
        f"conflicting-wan-{index:03d}", 8 * index, "wan", "eu",
        ["eu/hot", "us/hot"], ["eu/hot", "us/hot"], "increment", delay=60)
        for index in range(24)]
    transactions += _point_stream(rng, "conflicting-local", ["eu/hot"], 96,
                                  group="local_hot", spacing=2)
    transactions += _point_stream(rng, "conflicting-unrelated", free, 128)
    result.append(_case(name,
        "Long cross-shard RMWs conflict on two actual outputs and with EU "
        "point updates; a separate regional cohort touches neither output. "
        "Report failures as well as successful latency for both hot cohorts.",
        initial, {}, transactions))

    # A distinct marker keeps the positive case free of an accidental output
    # read dependency. Arrival slots exist in the scope even while absent.
    for backedge in (False, True):
        name = "max_backedge" if backedge else "broad_max_arrivals"
        rng = rng_for("max-arrival-pair")
        initial, collections = _rows(width)
        all_rows = [key for keys in collections.values() for key in keys]
        for index, key in enumerate(all_rows):
            initial[key] = index + 1
        slots_per_shard = min(16, max(1, width // 4))
        arrivals = [key for keys in collections.values()
                    for key in keys[-slots_per_shard:]]
        for key in arrivals:
            initial[key] = None
        initial["eu/winner"] = None
        free = _free_keys(initial)
        transactions = [_transaction(
            f"max-select-{index:02d}", 150 * index, "bulk", "eu",
            list(collections), ["eu/winner"], "max", delay=100)
            for index in range(3)]
        transactions += [_transaction(
            f"max-arrival-{index:03d}", 20 + 7 * index, "regional",
            key.split("/", 1)[0], ["eu/winner"] if backedge else [],
            [key], "blind", value=2 * width + index + 1)
            for index, key in enumerate(arrivals)]
        transactions += _point_stream(rng, "max-unrelated", free, 128,
                                      spacing=2)
        present_rows = [key for key in all_rows if key not in arrivals]
        transactions += _point_stream(rng, "max-lookup", present_rows, 64,
                                      group="report", read_only=True,
                                      spacing=3)
        description = (
            "The max/marker program overlaps larger arrivals that also read "
            "the marker. An arrival reading the old marker and the old maximum "
            "cannot both commit; verify observations, not just final values."
            if backedge else
            "The max/marker program overlaps blind larger arrivals. The old "
            "maximum may still be marked at its original snapshot; no other "
            "transaction reads the marker or writes the selected output.")
        result.append(_case(name, description, initial, collections,
                            transactions, horizon=650))

    for mutate in (False, True):
        name = "bulk_update_point_updates" if mutate else "bulk_update_reports"
        rng = rng_for("bulk-update-pair")
        initial, collections = _rows(width)
        all_rows = [key for keys in collections.values() for key in keys]
        free = _free_keys(initial)
        transactions = [_transaction(
            f"bulk-increment-{index:02d}", 160 * index, "bulk", "eu",
            list(collections), all_rows, "increment", delay=80)
            for index in range(3)]
        transactions += _reports(rng, "bulk-report", list(collections), 96)
        transactions += _point_stream(rng, "bulk-unrelated", free, 96,
                                      spacing=3)
        transactions += _point_stream(
            rng, "bulk-point-probe", all_rows, 64, start=20, spacing=5,
            group="source_update" if mutate else "point_report",
            read_only=not mutate)
        description = (
            "The same bulk/update trace, but point probes now mutate their "
            "source rows. Newer output versions force promotion, and actual "
            "source changes can reject the captured bulk computation."
            if mutate else
            "Bulk read-dependent increments overlap broad and point reports. "
            "Reports can raise output read bounds without invalidating "
            "unchanged source values. All rows remain equal after each "
            "committed bulk update; a complete report cannot see half of one.")
        result.append(_case(name, description, initial, collections,
                            transactions, horizon=750))

    name = "blind_bulk_readers"
    rng = rng_for(name)
    initial, collections = _rows(width)
    all_rows = [key for keys in collections.values() for key in keys]
    free = _free_keys(initial)
    transactions = [_transaction(
        "blind-bulk", 0, "bulk", "eu", [], all_rows, "blind",
        value=9, delay=90)]
    transactions += _reports(rng, "blind-report", list(collections), 128,
                             spacing=2)
    transactions += _point_stream(rng, "blind-unrelated", free, 96,
                                  spacing=3)
    result.append(_case(name,
        "One atomic blind replacement has no input renewal, but retains its "
        "entire real output footprint. Reports must contain all ones or all "
        "nines. Unrelated regional keys remain outside its promises.",
        initial, collections, transactions))

    # These two fixtures differ only in name/description and report allow_old.
    # Unlike the compute-heavy cases, delay=0 here makes the network exchange
    # itself responsible for the life of a write promise.
    name = "wan_reports_fresh"
    rng = rng_for(name)
    initial, collections = _rows(width)
    free = _free_keys(initial)
    transactions = [_transaction(
        f"report-wan-{index:03d}", 4 * index, "wan", "eu", [],
        [collections["eu/rows"][index % width],
         collections["us/rows"][index % width]],
        "blind", value=index + 2) for index in range(64)]
    transactions += _reports(rng, "wan-scan", list(collections), 128,
                             start=10, spacing=2)
    transactions += _point_stream(rng, "wan-unrelated", free, 96,
                                  spacing=3)
    fresh = _case(name,
        "Fresh broad reports overlap a stream of narrow cross-shard write "
        "promises. Promise lifetime comes from 12-tick links, not a private "
        "compute delay. Failures under continuously overlapping promises "
        "are a service limitation, not zero-latency successes.",
        initial, collections, transactions, link_delay=12, horizon=700)
    result.append(fresh)
    historical = deepcopy(fresh)
    historical["name"] = "wan_reports_allow_old"
    historical["description"] = (
        "Exactly the fresh-report arrival trace, with historical reports "
        "explicitly allowed. Every successful report still needs one common "
        "snapshot; report its age and do not label this the same freshness "
        "service as the fresh variant.")
    for transaction in historical["transactions"]:
        if transaction["group"] == "report":
            transaction["allow_old"] = True
    result.append(historical)

    name = "partial_claim_bridge"
    rng = rng_for(name)
    initial = {f"{shard}/{role}/{index}": 0
               for shard, role in (("eu", "x"), ("us", "remote"),
                                   ("us", "y")) for index in range(3)}
    free = _free_keys(initial)
    transactions = []
    for index in range(3):
        start = 100 * index
        transactions.append(_transaction(
            f"bridge-holder-{index}", start, "wan", "eu", [],
            [f"eu/x/{index}", f"us/remote/{index}"], "blind", value=1))
        transactions.append(_transaction(
            f"bridge-follower-{index}", start + 4, "bridge", "us", [],
            [f"us/y/{index}", f"eu/x/{index}"], "blind", value=2))
        transactions += [_transaction(
            f"bridge-y-reader-{index}-{offset:02d}", start + 5 + offset,
            "report", "us", [f"us/y/{index}"], [], "report")
            for offset in range(40)]
    transactions += _point_stream(rng, "bridge-unrelated", free, 128,
                                  spacing=2)
    result.append(_case(name,
        "An EU coordinator first promises EU/x. A US coordinator can then "
        "promise US/y before its EU/x request is rejected. Observe temporary "
        "y obstruction until abort arrives, even if the follower later "
        "retries successfully. This targets, rather than assumes, that "
        "particular control-message interleaving.",
        initial, {}, transactions, link_delay=12, horizon=500))

    name = "max_unchanged_loser_updates"
    rng = rng_for(name)
    initial, collections = _rows(width)
    initial["us/row/0000"] = 1_000_000
    initial["eu/winner"] = None
    free = _free_keys(initial)
    losers = [key for keys in collections.values() for key in keys
              if key != "us/row/0000"]
    transactions = [_transaction(
        f"stable-max-{index:02d}", 140 * index, "bulk", "eu",
        list(collections), ["eu/winner"], "max", delay=80)
        for index in range(3)]
    transactions += _point_stream(rng, "loser-update", losers, 96,
                                  start=20, spacing=3, group="source_update")
    transactions += _point_stream(rng, "marker-reader", ["eu/winner"], 64,
                                  start=25, spacing=4, group="report",
                                  read_only=True)
    transactions += _point_stream(rng, "loser-unrelated", free, 96,
                                  spacing=3)
    result.append(_case(name,
        "Point updates never change the maximum, but marker readers can "
        "force promotion. Conservative collection renewal may reject even "
        "though the selected answer remains identical. This deliberately "
        "measures the accepted cost of coarse dependency evidence.",
        initial, collections, transactions, horizon=650))

    name = "wan_compute_reports"
    compute = deepcopy(fresh)
    compute["name"] = name
    compute["description"] = (
        "The fresh-report workload with 80 ticks of private WAN computation "
        "and ordinary 5-tick links. Broad old protections may live through "
        "the computation; BRIEF2 promises begin only afterwards. Compare "
        "the compute interval separately from the commitment interval.")
    compute["link_delay"] = 5
    compute["horizon"] = 800
    for transaction in compute["transactions"]:
        if transaction["group"] == "wan":
            transaction["delay"] = 80
    result.append(compute)

    # Matched ordinary distributed work: the same initial universe, requests,
    # coordinators and arrival trace, with only the target-key mapping changed.
    for hot in (False, True):
        name = "ordinary_distributed_hot" if hot else "ordinary_distributed_cold"
        rng = rng_for("ordinary-distributed-pair")
        initial = {f"{owner}/distributed/{index:03d}": 0
                   for owner in ("eu", "us") for index in range(128)}
        free = _free_keys(initial)
        transactions = []
        for index in range(128):
            target = 0 if hot else index
            keys = [f"{owner}/distributed/{target:03d}"
                    for owner in ("eu", "us")]
            transactions.append(_transaction(
                f"ordinary-distributed-{index:03d}",
                2 * index + rng.randrange(3), "distributed",
                "eu" if index % 2 == 0 else "us", keys, keys,
                "increment", delay=0))
        transactions += _point_stream(
            rng, "distributed-unrelated", free, 128, spacing=2)
        description = (
            "The exact ordinary distributed cold trace, with all 128 RMWs "
            "mapped to one shared EU/US pair. Only key overlap changes: "
            "there is no private WAN delay or extra local hot-key traffic. "
            "Both hot values equal the number of committed distributed RMWs."
            if hot else
            "Ordinary two-key distributed RMWs with zero private delay and "
            "distinct EU/US pairs. Every completed pair becomes (1,1). "
            "Offered load and control-message latency match the hot variant.")
        result.append(_case(name, description, initial, {}, transactions,
                            horizon=1200))

    # Keep the report and traffic identical, moving only the writers across
    # the report's coverage boundary. Its long tail is private computation.
    for inside in (False, True):
        name = "broad_read_writers_inside" if inside else "broad_read_writers_outside"
        rng = rng_for("broad-read-fanout-pair")
        initial, collections = _rows(width)
        report_scopes = list(collections)
        covered = [collections[f"{owner}/rows"][index]
                   for index in range(width) for owner in ("eu", "us")]
        for owner in ("eu", "us"):
            keys = [f"{owner}/outside/{index:04d}" for index in range(width)]
            collections[f"{owner}/outside"] = keys
            initial.update({key: 1 for key in keys})
        outside = [collections[f"{owner}/outside"][index]
                   for index in range(width) for owner in ("eu", "us")]
        free = _free_keys(initial)
        targets = covered if inside else outside
        transactions = [_transaction(
            "fanout-report", 0, "report", "eu", report_scopes, [],
            "report", delay=100)]
        for index in range(128):
            key = targets[index % len(targets)]
            transactions.append(_transaction(
                f"fanout-writer-{index:03d}", 30 + index + rng.randrange(3),
                "regional", key.split("/", 1)[0], [key], [key], "increment"))
        transactions += _point_stream(
            rng, "fanout-unrelated", free, 64, start=20, spacing=2,
            group="unrelated")
        description = (
            "The matched long read-only report with independent point RMWs "
            "moved inside its collection. The report has no output promise "
            "or promotion, so any large arbitration component comes from "
            "retained read protection, not a broad true write set."
            if inside else
            "One broad cross-shard report spends 100 ticks computing after "
            "capture. Independent point RMWs affect an equally sized outside "
            "collection. The inside variant changes only writer targets; "
            "the query, row coverage, cost and arrival trace stay identical.")
        result.append(_case(name, description, initial, collections,
                            transactions, horizon=600))

    name = "rare_wan_saturated_local"
    rng = rng_for(name)
    initial = {f"{owner}/rare/{index}": 0
               for owner in ("eu", "us") for index in range(2)}
    free = _free_keys(initial)
    regional = {owner: [key for key in free if key.startswith(owner + "/")]
                for owner in ("eu", "us")}
    transactions = []
    for index in range(1000):
        for owner in ("eu", "us"):
            key = rng.choice(regional[owner])
            transactions.append(_transaction(
                f"rare-local-{owner}-{index:04d}", (4 * index) // 17,
                "regional", owner, [key], [key], "increment"))
    transactions += [
        _transaction("rare-wan-control", 0, "wan", "eu", [],
                     ["eu/rare/0", "us/rare/0"], "blind", value=1),
        _transaction("rare-wan-compute", 10, "wan", "us", [],
                     ["eu/rare/1", "us/rare/1"], "blind", value=1,
                     delay=200),
    ]
    result.append(_case(name,
        "Two disjoint WAN writes amid 2,000 local increments. Each shard "
        "receives 17 local requests per four ticks: at three work units per "
        "increment and capacity16, offered local load is about 79.7%. "
        "100-tick one-way links give a 200-tick control RTT; one WAN write "
        "also computes privately for 200 ticks. The first can hold promises "
        "during the busy local interval. WAN keys and local keys are disjoint, "
        "and the horizon leaves time after offered traffic ends.",
        initial, {}, transactions, capacity=16, link_delay=100, horizon=1400))

    return result
