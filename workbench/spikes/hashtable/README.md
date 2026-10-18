# Hashtable research frontier

This directory retains owning-multimap research. The graduated set/map core lives
in Ikea: [usage](../../../ikea/docs/hashtable/usage.md),
[rationale](../../../ikea/docs/hashtable/rationale.md) and
[maintained comparisons](../../benchmarks/hashtable/README.md).
[Bucket-wise K-way traversal](../hashtable-merge/README.md) is a separate research
frontier. Neither multimap nor K-way is a promoted Ikea API.

## What remains open

- The pool releases a block when its last live reservation goes away. Repeated
  drain/refill can therefore allocate and release blocks in the same capacity
  families on every cycle. Bounded empty-block retention remains unimplemented.
  Fitting scalar appends also retain substantial metadata/addressing cost beyond
  the index lookup.
- Wide multimaps retain a 72-byte index cell after values move out of line.
  Payload addressing, initialized slack and destination construction remain real
  costs. Earlier cache-only, external-only and borrowed-view variants did not
  establish a broadly competitive replacement. Those attempts are archived.
- Cold interleaving has retained wins; staging warm points still costs too much,
  especially on ARM. The panels below compare complete callers, including plain
  prefetch and useful companion work.
- Owning-multimap growth and merge still need to reconcile source ownership,
  retained capacity and payload construction. The consuming-merge panel below
  preserves both its speed and memory costs.

## Retained observations

These historical panels keep all competitors and losses. Their results describe
the recorded source captures and native jobs, not a new timing of the curated
checkout:

| Question | Evidence and limits |
| --- | --- |
| Full cold reads, chain heads and interleaved useful work | [Complete cold panel](evidence/loom-cold-complete-v15.json). Full ordered values and physical drain; actual Abseil and plain-prefetch controls. |
| Explicit cold reader refinements on warm work | [Corrected warm panel](evidence/warmed-readers-v43.json). Zen MM64 direct consumption 7.93 ns versus best measured Abseil 8.16 ns; ARM warmed stages still lose substantially. The invalid earlier capture is recoverable in the archive. |
| Selected observed existing-key writes | [Complete observed-write panel](evidence/observed-write-loom-v44.json). MM8 cold growth beats best measured Abseil on all three CPUs, including companion work. Fitting appends still lose; ARM MM64 growth loses. Fresh-key and public retained-reader composition remain unqualified. |
| Consuming multimap merge | [Capacity/right-reuse panel](evidence/merge-right-v30.json). Shared-domain speed can retain substantially more capacity; speed and a hypothetical compact-output footprint are not one measured implementation. |

Older scalar-append/lifecycle comparisons and rejected attempts are indexed by
[archive member hashes](evidence/archived-comparisons.json). Their findings survive;
the superseded implementations and numbered launchers do not remain live.

## Current executable candidate

`multimap/` contains the retained candidate and its ordinary, guarded, Loom,
growth and merge checks. `reference/iceberg/` supplies the scalar comparator's
earlier control. Both use the [pinned Abseil provider](../../benchmarks/hashtable/abseil.cmake).
Include-path curation and compatibility repairs carry no new timing claim.

```sh
orb -m ubuntu cmake -S . -B build/hashtable-frontier -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=hashtable \
  -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2
orb -m ubuntu cmake --build build/hashtable-frontier \
  --target hashtable_frontier_validate -j2
```

Comparison targets are `hashtable_mm_scalar_{8,64}`,
`hashtable_mm_loom_write_{8,64}` and `hashtable_mm_loom_read_{8,64}`.
The validation target runs six checks: ordinary, observed writes, Ring writes,
growth, merge and Loom reads. The curation capture passed all six under ASan/UBSan
and built all six comparison targets.
After the maintained accumulation extraction, ordinary multimap and Loom read
histories were rechecked against the current core, as recorded in the
[promotion receipt](../../benchmarks/hashtable/evidence/accumulation-promotion.json).
The other four sanitizer results remain scoped to the earlier curation capture.
Scalar eight- and 64-byte controls also passed complete-history smoke runs.
These checks establish correctness/buildability at their recorded source cuts;
timing reproduction uses each panel's captured source and host.

## Recovery

Linux `~/sixdb-archive/hashtable-20261017/` contains the earlier hierarchy,
Iceberg–Swiss and mutable-write studies, their evidence and superseded Loom/grant
probes. The [receipt](archive.json) identifies two archives: the original research
paths and a full runnable project snapshot from that promotion. Both are
historical; the current Ikea core has evolved since then. The
[retired-study recovery section](../../notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
explains which to recover. Original paths and negative results are preserved.
