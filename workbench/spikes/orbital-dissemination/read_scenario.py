"""Network-backed coherent read partitioning and ring response experiments.

The application's fixed cut and finite chunk coverage are supplied facts.
Worker scan speed and result size are synthetic. Query-serving replicas may
divide a query; this does not divide mandated fold or all-match verification.
"""
import argparse
import json
from pathlib import Path

from experiments import finish, network, schedule
from read_probe import Coverage
from simulator import Outcomes, Resource, Sim


def run_reads(config):
    sim = Sim(config.get("seed", 1))
    shards, replicas = config.get("shards", 4), config.get("replicas", 4)
    pieces = config.get("pieces", 4)
    if min(shards, replicas, pieces) < 1:
        raise ValueError("positive shards, replicas and pieces required")
    names = {"client": "a", "dispatcher": "a", "reducer": "b"}
    workers = {}
    for shard in range(shards):
        workers[shard] = []
        for replica in range(replicas):
            name = f"w{0 if config.get('shared_workers') else shard}_{replica}"
            names[name] = "b" if replica % 2 else "a"
            workers[shard].append(name)
    net = network(sim, names, config)
    out = Outcomes(sim)
    scan = {name: Resource(sim, name + ":scan", config.get("scan_slots", 1),
                          capacity=config.get("scan_queue_bytes", 1 << 25))
            for name in set(sum(workers.values(), []))}
    ready_delay = config.get("ready_delay_us", {})
    count, rate = config.get("count", 100), config.get("rate", 1000)
    total_bytes = config.get("bytes_per_shard", 1_000_000)
    result_bytes = config.get("result_bytes_per_shard", 4096)
    selection = config.get("selection", "round_robin")
    used_bytes = 0
    planned_bytes = {w: 0 for w in scan}

    def offer(key):
        born = sim.now
        out.offer(key)
        coverage = Coverage(f"cut-{key}", "read-plan",
                            {f"{s}/{p}" for s in range(shards) for p in range(pieces)})
        accepted = set()

        def dispatch():
            def got_piece(shard, piece):
                chunk = f"{shard}/{piece}"
                if not coverage.accept(f"cut-{key}", "read-plan", chunk, chunk):
                    sim.count["duplicate_read_results"] += 1
                    return
                accepted.add((shard, piece))
                if coverage.complete:
                    def reduced():
                        # Ring: request enters dispatcher; a different host replies.
                        net.send("reducer", "client", config.get("final_result_bytes", 128),
                                 lambda: out.finish(key), "control")
                    net.compute("reducer", config.get("reduce_us", 2),
                                result_bytes * shards, reduced)

            def send_piece(shard, piece, worker, hedge=False):
                nbytes = total_bytes // pieces + int(piece < total_bytes % pieces)
                rbytes = result_bytes // pieces + int(piece < result_bytes % pieces)
                planned_bytes[worker] += nbytes
                def received():
                    start = max(sim.now, born + ready_delay.get(worker, 0))
                    def execute():
                        nonlocal used_bytes
                        if hedge and (shard, piece) in accepted:
                            sim.count["hedges_cancelled_before_scan"] += 1
                            planned_bytes[worker] -= nbytes
                            return
                        def scanned():
                            planned_bytes[worker] -= nbytes
                            if not net.alive(worker):
                                sim.count["read_lost_on_crash"] += 1
                                return
                            net.send(worker, "reducer", rbytes,
                                     lambda: got_piece(shard, piece), "bulk")
                        duration = nbytes / config.get("scan_bytes_us", 1000) * net.nodes[worker].cpu_slow
                        if scan[worker].submit(duration, nbytes, scanned, "bulk"):
                            used_bytes += nbytes
                            sim.count["read_scan_attempts"] += 1
                        else:
                            planned_bytes[worker] -= nbytes
                        # Once submitted, cancellation doesn't magically refund
                        # work or active buffers. Actual backend cancellation is
                        # intentionally absent from this baseline.
                    sim.at(start, execute)
                net.send("dispatcher", worker, 64, received, "control")

            for shard in range(shards):
                for piece in range(pieces):
                    choices = workers[shard]
                    if selection == "queue_ready":
                        # Oracle current queue/cut state is a lower-bound policy,
                        # not a demonstrated distributed observation mechanism.
                        worker = min(choices, key=lambda w: (
                            max(sim.now, born + ready_delay.get(w, 0)) +
                            planned_bytes[w] / config.get("scan_bytes_us", 1000), w))
                    else:
                        worker = choices[(piece + key) % replicas]
                    send_piece(shard, piece, worker)
                    if "hedge_us" in config and replicas > 1:
                        backup = choices[(choices.index(worker) + 1) % replicas]
                        def hedge(shard=shard, piece=piece, backup=backup):
                            if (shard, piece) not in accepted:
                                sim.count["read_hedges"] += 1
                                send_piece(shard, piece, backup, True)
                        sim.later(config["hedge_us"], hedge)
        net.send("client", "dispatcher", 96, dispatch, "control")

    cutoff = schedule(sim, count, rate, offer, config.get("burst", 1))
    drain = cutoff + config.get("drain_us", 10000)
    sim.run(cutoff)
    snapshot = {n: r.used for n, r in sim.resources.items()}
    sim.run(drain)
    result = out.summary(cutoff, drain)
    result.update(scan_bytes=used_bytes, logical_scan_bytes=count * total_bytes * shards,
                  selection_observation="instantaneous oracle" if selection == "queue_ready" else "static",
                  endpoint="client receives reduced coherent read result")
    return finish(config, sim, net, result, snapshot)


def campaign(count=100):
    result = []
    for shared in (False, True):
        for pieces in (1, 4, 16):
            for hedge in (False, True):
                cfg = dict(name=f"reads-{shared}-{pieces}-{hedge}", count=count, rate=1000,
                           shared_workers=shared, pieces=pieces)
                if hedge:
                    cfg["hedge_us"] = 200
                result.append(run_reads(cfg))
    for selection in ("round_robin", "queue_ready"):
        cfg = dict(name=f"readiness-{selection}", count=count, pieces=4, selection=selection,
                   ready_delay_us={f"w{s}_3": 600 for s in range(4)})
        result.append(run_reads(cfg))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--count", default=100, type=int)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(campaign(args.count), indent=2) + "\n")
    print(args.output)


if __name__ == "__main__":
    main()
