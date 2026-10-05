# Retaining released pages, October 5, 2026

Job `20261005T104640Z-cce878b1`: fresh Spot c8a.large, AMD EPYC 9R45,
Clang 21.1.8, 4 KiB pages, benchmark pinned to CPU 0. This tests
[Ashton's release-hint follow-up](../../../../notebook/orbital/ASHTON-SCRATCH.md).

The [pool](../../recycling.cpp) holds anonymous mapped pages with intrusive free
lists. A lease can be copied for backend use. Only its final release makes a
page available. Release records a caller-supplied temperature, last-access time
and CPU. It performs no heap allocation or VM operation while the pool service
remains alive. Cold trim runs off the pool latch on a helper thread; service
shutdown joins that thread and releases remaining mappings.

## Retained-page comparison

Every iteration acquires one already mapped page, writes 64 bytes and releases
it. A page is prepared and touched before timing. Six trial means per cell,
10,000 iterations each: three forward and three reverse case orders.

| Request / release hint | Acquire/write/release | Release component | VM calls in timed loop |
| --- | ---: | ---: | ---: |
| HOT / HOT | 113.0 ns | 72.8 ns | 0 |
| WARM / WARM | 112.9 ns | 72.1 ns | 0 |
| COLD / COLD, retained | 112.2 ns | 71.7 ns | 0 |
| HOT / COLD, fallback | 113.0 ns | 72.4 ns | 0 |

These labels select bookkeeping buckets. **All four cases use physically warm
storage.** This is not a measurement of L2/LLC/DRAM latency or a calibrated aging
policy. Per-release clock reads and the phase timer are included. Small timing
differences among buckets have no demonstrated significance.

The HOT/COLD case falls back on every iteration. A cold retained page remains
useful when desired inventory is empty. A new mapping is therefore unnecessary
for this path. Initial page mapping, source preparation, page-content replacement,
permission changes, trimming and shutdown are outside this comparison.

The earlier [view abort cycle](../zen5/README.md) paid for immediate unmapping.
This pool's cycle omits object/view bookkeeping and supplies weaker byte-state
semantics. It establishes a cheap reuse mechanism; it does not establish a
113 ns complete object operation. At measurement the prototypes were separate.
The live spike now composes them, including protection restoration and content
preparation that this raw-page comparison does not time.

## Consequential histories

The probe holds a backend lease after the CPU lease closes and verifies that
another acquisition cannot receive its address. Completion then makes that
address available, with the original hint, CPU and old timestamp preserved.

It requests cold trim with HOT/WARM inventory empty and checks that cold storage
remains available. Returning a WARM page permits background trimming. The probe
pauses that worker before its actual `munmap`, then completes foreground
allocation and release. This checks that the reclamation operation holds neither
the pool latch nor the caller. It also checks final held-page counts.

These cases pass locally and on Zen5; local ASan/UBSan passes. There is no
contention benchmark or bounded-wait proof. The foreground path still uses a
shared metadata latch. The pool also returns uninitialised prior contents:
the receiving view must prepare its new bytes and permissions before use.

## Selection and recovery

All four cells in all six trials are retained as `forward-*.csv` and
`reverse-*.csv`. [Checks](checks.jsonl) and [host context](host.json) are retained
too. At collection, the three native recycling source files matched the captured
manifest. The live pool has since added window sizes, protection restoration
and fallback retention by size; the archive preserves the measured version.
The report script was added after measurement; the archive contains the executed
runner and complete sources/binaries. [Provenance](provenance.json) covers the
retained raw files; [artifact.json](artifact.json) recovers the full worker bundle.

Generate `comparisons.csv` from these inputs:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/orbital-views/evidence/recycling build/reports/orbital-view-recycling
```

The [summary script](../../recycle-summary.py) checks that all timed acquisitions
reuse existing pages, no mapping/unmapping occurs, and only the intended case
falls back. Future `recycle.sh` runs produce the summary inside the worker bundle.
