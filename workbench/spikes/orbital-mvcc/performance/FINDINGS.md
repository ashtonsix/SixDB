# Rejected versioned baseline: resident diagnostic, 2026-10-09

These measurements describe the original Stage 2 experiment. That implementation
was withdrawn into this spike because performance had not guided its architecture
throughout implementation. Preserve these observations as the comparison baseline,
not evidence that the architecture or performance target has been accepted.

The thin cursor removes the earlier large native descriptor copies from the
primary read binding. Complete versioned reads are close to the mapped-LMDB
control in this local panel; small scalar reads and two batch-latency cases
still have deficits. Fixed updates favor the actual Orbital adapter here.
This is a bounded diagnostic, not a general LMDB win or an Engine benchmark.

[Selected evidence](evidence/selected-resident-20261009/) keeps every repetition,
including outliers. The [recovery reference](evidence/selected-resident-20261009/artifact.json)
names the verified full bundle. The [runner](run.py) owns the cases and replay.

## What was measured

Linux/OrbStack AArch64 on the Apple host, CPU 2, Clang 21.1.8, Release/O3,
generic tuning, assertions, separate translation units and no LTO. LMDB 0.9.31
uses ordinary locking/COW with flushes disabled. The fixture is touched during
setup; no PMU-derived cache-level attribution is made.

Actual Orbital Objects and checked Part IDs resolve the same 23-byte Tuple rows
stored as opaque LMDB values. Both qualify an 8-byte TupleCursor and run the
same inline native TupleBody directly over backing. The fixture Schema/count
is stable, external control; actual Orbital descriptions must match it.
One snapshot serves each offered group of 1 or 64, including a short final tail.
Native row-key LMDB is contextual; prequalified direct bytes acquire no snapshot.

Three repetitions rotate competitor order. Throughput and latency are separate
fresh-process/store replays. Reads have 32,771 requests; updates 131; conversion 16.
All 288 trials passed independent output/final-row checks, group and mode checks.
The capture completed unchanged, digest
`0d447a62d901a99f28974f2dce8f41472db95ff85f6ad742ce155bdab4b09ed7`,
HEAD `b2c205d` (runtime callback fix `a2c3a2c`). The full bundle includes exact
sources, binaries, commands, library versions, affinity and disassembly.

A subsequent completion audit removed an unused reverse history link, shrinking
Orbital's private Version record from 96 to 88 bytes on this target. Native and
sanitizer integration checks cover that cleanup. The timings, metadata footprints
and exact assembly below remain evidence for the captured version above.

## Read and update observations

Median throughput wall nanoseconds per logical request:

| Work, total rows / Objects | Batch | Orbital retained | Mapped Tuple LMDB |
| --- | ---: | ---: | ---: |
| Point hit, 1024 / 1 |1 |306 |300 |
| Point miss, 1024 / 1 |1 |326 |311 |
| Point hit, 32768 / 4 |1 |354 |373 |
| Point hit, 32768 / 4 |64 |109 |111 |
| Dependent 16, 32768 / 4 |1 |1638 |1670 |
| Dependent 16, 32768 / 4 |64 |1396 |1385 |
| Scan 16, 32768 / 4 |1 |437 |465 |
| Scan 16, 32768 / 4 |64 |186 |189 |
| Fixed update with old reader, 4096 / 4 |1 |5423 |11124 |
| Fixed update with old reader, 4096 / 4 |64 |702 |991 |
| Fixed update with old reader, 32768 / 4 |1 |20677 |51975 |
| Fixed update with old reader, 32768 / 4 |64 |1896 |4875 |

Read ranges overlap in several cases. One large scalar borrowed replay has a
large wall-time outlier; all samples and caller CPU time remain selected.
The table does not establish significance. At 1024 rows scalar point-hit,
borrowed Orbital is 252 ns and prequalified direct bytes 63 ns. At 32768 rows/batch64,
they are 107 ns and 94 ns. The contextual row-key LMDB small point-hit is 176 ns
scalar and 128 ns at batch64; it executes a different physical lookup/body.

Median of the per-replay whole-batch p99 is 137,419 ns for owned dependent16/batch64
against 130,876 ns mapped, and 25,501 ns for owned scan16/batch64 against 23,500 ns.
These remain visible deficits. Batch64 p99 is not request p99. Update/batch64
has only three offered groups per replay; its p99 is that replay's maximum.

Writes include real cloning, codec update, freeze, publication and bounded
drain/poll, with the old reader held. They also include unequal preparation:
Orbital binds the source twice and builds a dynamic Shape per changed Object;
LMDB binds once and reuses prepared Shape. No isolated publication ratio follows.
For 32768 rows, scalar updates copy 24,682,496 bytes across 131 Parts; batch64
copies 2,072,576 bytes across 11 Parts in both arms.

## Retention, release and conversion

For 32768 rows and 131 scalar updates, held Orbital versions grow 4→135, metadata
3844→105989 bytes, and Arena charge 754976→25480440 bytes. Releasing the old
reader and draining returns versions to 4 and metadata to 3844. Arena charge
stays 25480440 while Pool backing is cached. At batch64 there are 15 held versions
and 2831160 Arena bytes, then 4 versions after release. Release/drain has its own
clock; shutdown independently requires zero charge. LMDB file-used bytes are
a high-water footprint and do not shrink on reader release.

Conversion alternates formats across four 1024-row Objects, starting once from
rows and once from columns. Those histories average 15.2 us and 14.3 us per actual
conversion respectively. Each includes 8 calls in each direction; these are
starting-format histories, not direction-isolated means. Each performs 65536
decoded and 65536 encoded values, requests 372736 body bytes across 40 Parts,
and has 4256-byte bounded codec scratch. Existing old bindings retain their
format and values. LMDB has no invented conversion counterpart.

## ABI and qualification

The measured executable has an 8-byte cursor/16-byte result, 16-byte shared
TupleControl and 24-byte ephemeral TupleBody. Generic ReadData for conversion
and update is 56 bytes/64-byte result. See [exact symbol evidence](evidence/selected-resident-20261009/abi.json).

The actual common `read_operation<TupleBody>` is 848 code bytes with a 432-byte
frame. Point and dependent get work has no successful helper/admission call
per probe. Scan is outlined. The frame includes shared scan16 scratch even
when point/chain takes another branch; it is common to all Tuple arms.
Caller frames are 496 bytes for `run_orbital_reads`, 640 for `run_lmdb_reads` and
288 for direct control. The current Orbital cursor wrapper has 112 bytes.
These frame reservations include ordinary saved registers and scratch; they
are not proof of spills or hardware store-to-load forwarding stalls. No model
cycles are treated as latency.

The captured baseline checks pass 10546 comparisons; native row controls 532;
direct mapped Tuple controls 2096. The same compiled source passes 101 separate ASan/UBSan
always-use-after-return CLI processes, including 8 Objects, both clocks,
batch1/64 tails, retained updates/conversions, invalid options and refusal of an
existing database with unchanged bytes. Full sanitizer receipts and executables
remain in the archive; Git keeps [a small synopsis](evidence/selected-resident-20261009/sanitizer-summary.json).

The remaining leads are scalar admission/reader cost, the shared dispatch frame,
adapter preparation and retained-history capacity. Membership changes, insertion
and splits, durability, Engine navigation and multiple workers remain unpriced.
This cut introduces no policy or design for them.

## Selection and recovery

Git keeps authored code, this interpretation, all matched repetitions/counters,
compact summaries, native/SAN qualification and selected exact ABI facts.
Full logs, per-process JSON, source snapshots, binaries and complete assembly
live in the verified 22.5 MB bundle. Generated databases are disposable fixtures;
the runner deletes only its own newly created databases after successful checks.
Active incremental build workspaces stay available.

Report extraction was corrected after measurement to retain nested C++ target
names and a small sanitizer synopsis. The bundle records that analysis source
and its hash separately; compiled sources, binaries and measurements are unchanged.
Offline ABI architecture is read from the captured ELF, never the analysis host.

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/orbital-mvcc/performance/evidence/selected-resident-20261009 \
  build/reports/versioned-performance/NEW-report
```

The recipe recovers the archive and regenerates the selected reports without
executing recovered code or rerunning measurements.
