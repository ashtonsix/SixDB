"""Exact partition reuse/reconstruction examples for extension-hosted jobs.

No distributed scheduler, storage backend, timings or general graph API.
"""
from __future__ import annotations

import argparse
from collections import OrderedDict
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path


@dataclass(frozen=True)
class Binding:
    snapshot: int
    code: int
    iterations: int = 1


class Unavailable(Exception):
    pass


class Job:
    """Fixed workload with version-qualified caches and optional checkpoints.

    Four partitions of 32 integers; each map emits one eight-byte modeled value.
    Cache and checkpoints have separately specified finite byte capacities.
    Per-partition computation and final driver result storage are not budgeted
    here; the resource probe studies the missing execution/retention coupling.
    """
    partitions = 4
    rows = 32
    partition_bytes = rows * 8

    def __init__(self, binding=Binding(1, 1), cache_bytes=1024,
                 checkpoint_bytes=0, checkpoint_iteration=None):
        self.binding = binding
        self.cache_bytes = cache_bytes
        self.checkpoint_bytes = checkpoint_bytes
        self.checkpoint_iteration = checkpoint_iteration
        self.cache = OrderedDict()
        self.checkpoints = {}
        self.sources = {1, 2}
        self.codes = {1, 2}
        self.calls = self.source_reads = self.checkpoint_reads = self.checkpoint_writes = 0
        self.peak_cache = 0

    @staticmethod
    def source(snapshot, partition):
        return tuple(snapshot * 1000 + partition * 32 + row for row in range(32))

    def partition(self, partition):
        if not 0 <= partition < self.partitions:
            raise ValueError("partition outside captured coverage")
        b = self.binding
        key = (b.snapshot, b.code, b.iterations, partition)
        if key in self.cache:
            value = self.cache.pop(key)
            self.cache[key] = value
            return value
        checkpoint_key = (b.snapshot, b.code, self.checkpoint_iteration, partition)
        if checkpoint_key in self.checkpoints and self.checkpoint_iteration <= b.iterations:
            value = self.checkpoints[checkpoint_key]
            start = self.checkpoint_iteration
            self.checkpoint_reads += self.partition_bytes
        else:
            if b.snapshot not in self.sources:
                raise Unavailable("captured source edition unavailable")
            value = self.source(b.snapshot, partition)
            self.source_reads += self.partition_bytes
            start = 0
        if start < b.iterations and b.code not in self.codes:
            raise Unavailable("captured executable edition unavailable")
        for iteration in range(start + 1, b.iterations + 1):
            value = tuple((x * (2 * b.code + 1) + b.code) % 1000003 for x in value)
            self.calls += len(value)
            if iteration == self.checkpoint_iteration:
                ck = (b.snapshot, b.code, iteration, partition)
                if ck not in self.checkpoints and (len(self.checkpoints) + 1) * self.partition_bytes <= self.checkpoint_bytes:
                    self.checkpoints[ck] = value
                    self.checkpoint_writes += self.partition_bytes
        if self.partition_bytes <= self.cache_bytes:
            while (len(self.cache) + 1) * self.partition_bytes > self.cache_bytes:
                self.cache.popitem(last=False)
            self.cache[key] = value
            self.peak_cache = max(self.peak_cache, len(self.cache) * self.partition_bytes)
        return value

    def action(self):
        values = [x for partition in range(self.partitions) for x in self.partition(partition)]
        return {"count": len(values), "sum": sum(values), "top3": sorted(values, reverse=True)[:3]}

    def lose_cache(self, partition=None):
        for key in list(self.cache):
            if partition is None or key[-1] == partition:
                del self.cache[key]

    def counters(self):
        return dict(extension_record_calls=self.calls, source_read_bytes=self.source_reads,
                    checkpoint_read_bytes=self.checkpoint_reads,
                    checkpoint_write_bytes=self.checkpoint_writes,
                    peak_cache_bytes=self.peak_cache,
                    checkpoint_bytes=len(self.checkpoints) * self.partition_bytes)


def study():
    results = []
    for name, cache in (("no-cache", 0), ("fitting-cache", 1024), ("thrashing-cache", 512)):
        job = Job(cache_bytes=cache)
        before = job.counters()
        first, second = job.action(), job.action()
        results.append(dict(name=name, before_action=before, first=first, second=second, **job.counters()))
    for name, checkpoint, checkpoint_iteration, iterations in (
            ("lost-partition-lineage", 0, None, 1),
            ("lost-partition-checkpoint", 1024, 1, 1),
            ("lost-iteration-lineage", 0, None, 20),
            ("lost-iteration-checkpoint", 1024, 10, 20),
            ("partial-checkpoint", 512, 10, 20)):
        job = Job(Binding(1, 1, iterations), checkpoint_bytes=checkpoint,
                  checkpoint_iteration=checkpoint_iteration)
        first = job.action()
        before_calls = job.calls
        job.lose_cache(3)
        second = job.action()
        results.append(dict(name=name, first=first, second=second,
                            recovery_record_calls=job.calls - before_calls, **job.counters()))
    for absent in ("source", "code"):
        job = Job()
        first = job.action()
        job.lose_cache(3)
        (job.sources if absent == "source" else job.codes).remove(1)
        try:
            job.action()
            raise AssertionError("missing replay input accepted")
        except Unavailable as error:
            results.append(dict(name="missing-" + absent, first=first,
                                status="unavailable", reason=str(error), **job.counters()))
    job = Job()
    first = job.action()
    job.lose_cache(3)
    # Deliberately invalid control: the recovery branch resolves a live alias.
    old_parts = [job.partition(i) for i in range(3)]
    job.binding = Binding(2, 2)
    torn = old_parts + [job.partition(3)]
    torn_sum = sum(sum(part) for part in torn)
    results.append(dict(name="unsafe-live-alias-recovery", expected_sum=first["sum"],
                        actual_sum=torn_sum, mismatch=torn_sum != first["sum"]))
    job = Job()
    first = job.action()
    job.binding = Binding(2, 2)
    second = job.action()
    results.append(dict(name="new-binding-new-cache-key", first=first, second=second, **job.counters()))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    report = {"model": "exact authored partition computations; logical counters only",
              "source_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "cases": study()}
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(text)
    else:
        print(text, end="")


if __name__ == "__main__":
    main()
