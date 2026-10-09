# Early complete-operation feedback

These are paired iteration results, not qualification of the current design.
Each worker capture preserves its exact sources, compiler flags and executable;
later edits do not change an earlier measurement's meaning. All runs here use
one core of the same Spot c8a.large Zen 5 EPYC guest, Clang 21.1.8,
`x86-64-v3` ISA and `zen5` tune. Bodies, opaque identifiers, offered requests,
snapshots and independent expected outcomes are matched against mapped LMDB.
The 256 MiB total limit is the same; the initial fixture assigns 1 MiB to
Orbital metadata and the remainder to its actual Stage 1 Allocation/Pool.

## First direct admission/body treatment

[Retained repetitions](evidence/zen5-direct-qualification-20261009/feedback/measurements.csv)
and [summary](evidence/zen5-direct-qualification-20261009/feedback/summary.csv)
come from worker `20261009T201008Z-9400a6b9`: 240 trials, five rotated backend
repetitions, six logical cases. Throughput and latency use separate fresh stores.
Scalar latency has 32771 samples; batch64 has 8193 samples, including its
three-request tail. Raw ordered samples remain in the verified worker archive.

Median throughput below is ns per complete logical request:

| Case | Orbital legacy | Orbital direct16 | Mapped LMDB legacy | Mapped LMDB direct16 |
| --- | ---: | ---: | ---: | ---: |
| 1024 rows, 1 Object, point hit, batch1 | 205.56 | 132.22 | 214.47 | 152.52 |
| 32768 rows, 4 Objects, point hit, batch1 | 239.70 | 165.91 | 255.16 | 196.02 |
| Same, point hit, batch64 | 88.43 | 86.40 | 87.45 | 85.53 |
| Same, dependent16, batch64 | 1150.72 | 1230.45 | 1134.40 | 1213.76 |

Direct16 performs full parent/logical/Part admission, derives the real 16-byte
pointer/count state, and changes native operation placement. This is a whole
treatment gain, not isolated qualification cost. Both mapped LMDB and Orbital
receive it. The chain treatment returns a `Record[16]` scratch result before
consuming it; its roughly 7% regression in both systems is useful negative
feedback. Batch64 still trails mapped LMDB. It is serial work under one coherent
snapshot, with no interleaving or latency-hiding claim.

This first executable also eagerly initializes an additional canonical Schema
from the candidate header, alongside the fixture's Schema. It is setup/shared
state, not an eliminated cost. The subsequent source removes that initialization.
The first run has caller/process CPU and component footprints but no PMU replay.

## Scratch-free whole consumption

[All second-run repetitions](evidence/zen5-scratch-free-20261009/feedback/measurements.csv)
come from worker `20261009T201950Z-eaae80c6`, reusing the same instance.
Its 360 primary trials compare legacy, direct16 and `consume16`, which consumes
the native chain directly into caller-owned Outcome using the same Ikea seek
and transfer bodies. The full parent/Part admission remains. The unused
canonical Schema is no longer initialized in this executable.

| 32768 rows, 4 Objects | Orbital legacy | Orbital direct16 | Orbital consume16 | LMDB legacy | LMDB direct16 | LMDB consume16 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dependent16, batch1 | 1395.16 | 1331.87 | 1298.96 | 1376.32 | 1344.39 | 1277.51 |
| dependent16, batch64 | 1216.97 | 1250.17 | 1198.44 | 1195.92 | 1227.70 | 1157.35 |

The scratch-free treatment recovers the regression within this source cut;
Orbital's batch64 chain remains behind the equally improved mapped LMDB arm.
Use within-capture comparisons: linked code and Schema placement differ from
the first run. Codec treatments were temporally blocked in these first two
panels; the next runner rotates codec and backend together per logical-case
repetition. Small overlapping margins need further feedback.

Separate second-run hardware replays observed cycles, retired instructions,
DTLB misses and branch misses at full group coverage; the requested generic
LLC event failed with ENOENT. These raw diagnostics are archived, not used as
request-only counters: their original boundary also included post-clock
O(requests) logical accounting. The next source puts PMU boundaries in the
common trial loop, excluding that accounting and direct-control preparation.
Pool counters cover physical Allocation work, not snapshot metadata allocation
churn. The legacy process peak is `getrusage.ru_maxrss`. A local Linux reproducer
showed it can inherit the launcher high-water through exec; the catalog panel
reported the identical 61340 KiB for every backend. Those values do not establish
benchmark runtime peak. New receipts use the measured process's own
`/proc/self/status` VmHWM/VmRSS and retain the inherited value separately.
Process memory still includes the fixture, traces, oracle and validation, and
is separate from backend-owned component accounting.

## Actual catalog controls

[All third-run repetitions](evidence/zen5-catalog-controls-20261009/feedback/measurements.csv)
come from worker `20261009T203047Z-ad9e29ad`: 500 trials, five repeated logical
cases, both legacy and scratch-free consumers, with codec/backend order rotated
together. Store8/16/32 use actual descriptor/history catalogs, caller-resident
Scope and the same Stage 1 Allocation/Pool backing as the baseline. Body-owner
control records are charged to their PMR resource. Full Schema/fixture state
remains resident; this is a bulky control, not the compact-law treatment.

Median throughput, scratch-free `consume16`, ns per complete request:

| Case | Baseline | Mapped LMDB | Store8 | Store16 | Store32 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1024 rows, 1 Object, hit, batch1 | 130.71 | 154.30 | 105.21 | 105.77 | 103.09 |
| 32768 rows, 4 Objects, hit, batch1 | 164.74 | 193.37 | 133.28 | 134.69 | 130.60 |
| Same, hit, batch64 | 85.50 | 84.50 | 82.77 | 84.50 | 81.27 |
| Same, dependent16, batch1 | 1261.24 | 1260.82 | 1204.45 | 1229.07 | 1177.91 |
| Same, dependent16, batch64 | 1164.05 | 1150.42 | 1134.82 | 1170.11 | 1115.73 |

The 32-byte node leads these medians despite higher initial PMR metadata:
4212 B versus 2348 B for Store8 and 3324 B for Store16 in the four-Object case.
Baseline Domain metadata is 3812 B. The candidate PMR owns body-owner shared
controls; baseline coverage differs, so these totals are not equivalent
ownership inventories. Actual Pool metadata is 1280 B and Arena charge 754944 B
for these arms, with four allocations; mapped LMDB has its distinct file/map
accounting. None is the 64-Object budget fixture.

The median dependent16 batch64 latency p99 is **82280 ns for Store32 versus
80771 ns for mapped LMDB**. The median throughput lead is not a tail lead.
A separate scalar four-Object hardware replay observed baseline 1808 retired
instructions/request versus Store32 1268, and cycles 738 versus 591. IPC falls
from 2.45 to 2.14: less executed work supports this gain. This diagnostic replay
is not a repeated PMU distribution. All requested generic LLC events failed
with ENOENT; cycles, instructions, DTLB and branch misses had full group coverage.
This capture's counter boundary excludes post-clock logical accounting.

The third source used the original eight-slot fixture cache. Later sources use
a common request-bounded direct-mapped cache with one slot for scalar callers
and 64 for batch callers; collisions rebind actual opaque IDs. Later directory,
shape and physical-owner candidates also differ. The archived executable and
source map determine the measured code; these numbers do not qualify those edits.

## Retention and next discriminators

Git keeps the authored experiment, all selected competitors/repetitions,
CPU/component accounting, exact executable ABI summaries, correctness synopsis,
host context and verified archive references. Full raw latency, per-process
JSON, PMU diagnostics, source captures, executables, compile commands and
disassembly stay in the existing worker S3 bundles. The third selection omits
the 457250-byte repeated diagnostic JSON; its archive report recipe regenerates
a compact counter table with every requested event, error and coverage. Future
captures select that table directly. No duplicate bundle upload
is needed. Disposable validated databases are regenerated and removed.

Next feedback exercises actual publication, old-reader first discovery,
bounded retained windows and release/maintenance CPU using these real catalogs.
The 192-row/64-Object profile exposes finite Groups and their fresh Pool cost. Cold ranges, retained-cut discovery, bounded churn/release, larger
Object/independent-Part fixtures and capacity remain owed. The source-only
baseline diagnostic explicitly reports uncovered allocator/map headers and new
snapshot storage; it cannot claim every metadata byte was conditioned.
