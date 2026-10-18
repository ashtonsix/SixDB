# Bucket-wise K-way merge research

Single-source mutable accumulation is maintained in
[Ikea](../../../ikea/docs/hashtable/usage.md#mutable-accumulation).
The [maintained benchmark suite](../../benchmarks/hashtable/README.md) owns its
recurring comparisons. Sizing and copying remain caller-owned. This directory
retains the unpromoted K-way alternative; owning-multimap research lives in the
[hashtable frontier](../hashtable/README.md).

This directory retains bucket-wise K-way traversal, its external workspace and
a scalar reference in [merge.h](merge.h). [candidate.h](candidate.h) forwards to
the maintained leaf; it contains no duplicate physical engine. The K-way checker
shares independent ordered-history oracles with the Ikea checker, while physical
codec checks live with Ikea.

## The remaining question

Can visiting each destination bucket across several sources save enough cache
work to beat caller-driven single-source accumulation after paying for spill
partitioning and workspace? The prototype accepts one concrete source type and
an externally sized workspace; destination geometry may differ. It preserves
each key's source order, with the initial destination first. Callback order
across unrelated keys can differ from a sequence of single-source calls.

No further optimisation experiment is active. Consumers use the maintained
single-source operation; there is no promoted K-way contract.

## Current code and checks

`qualify-native.sh` is the optional K-way comparison runner. Its controls are the
maintained accumulation wrapper, K-way, the explicitly named scalar reference
and pinned actual Abseil. Historical `routed_before` results describe an earlier
control, not the current `scalar_reference` implementation.
Preparation is separate, exact union/spill oracles are outside timings, source
order is checked with a non-associative reducer, and incoming storage is prewalked
for each control. `variant-results.py` accepts both current and archived labels.

```sh
orb -m ubuntu cmake -S . -B build/merge-research -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable-merge
orb -m ubuntu cmake --build build/merge-research --target hashtable_merge_check -j2
orb -m ubuntu build/merge-research/workbench/spikes/hashtable-merge/hashtable_merge_check
```

## Evidence and recovery

[Results](RESULTS.md) retain the accumulation study's stopping-point measurements
and known gaps before promotion. They do not qualify the current K-way wrapper
or replace the maintained core's newer scorecard.
Linux `~/sixdb-archive/hashtable-merge-20261017/` contains the earlier research
implementations, runners and full source snapshots ([receipt](archive.json)).
The [job index](evidence/construction-jobs.json) locates measured sources,
binaries, disassembly and raw timings in S3. Replaying an older paired comparison
uses its captured runner; `qualify-native.sh` measures the current four controls.
