# Early complete-operation feedback

These are paired iteration results, not qualification of the current design.
Each worker capture preserves its exact sources, compiler flags and executable;
later edits do not change an earlier measurement's meaning. Runs use
one core of a Spot c8a.large Zen 5 EPYC guest, Clang 21.1.8,
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

## Publication and retained-reader controls

[Worker 20261009T211827Z-20df97c1](evidence/zen5-publication-controls-20261009/feedback/measurements.csv)
passed 200 primary trials and 20 separate hardware replays. Each case has five
rotated repetitions. It performs real full-Part clones, native fixed-key updates,
actual multi-Object publication and periodic old-reader first discovery/release.
The final tail has three requests. Median throughput is ns per logical update:

| Rows / Objects / batch / rotation age | Baseline | Mapped LMDB | Store8 | Store16 | Store32 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 4096 / 1 / 1 / 8 | 2117.60 | 4353.09 | 1508.86 | 1473.71 | 1490.52 |
| 4096 / 4 / 64 / 8 | 294.12 | 284.17 | 242.65 | 243.65 | 241.26 |
| 32768 / 4 / 1 / 64 | 4244.27 | 9200.19 | 3574.77 | 3535.21 | 3506.98 |
| 32768 / 4 / 64 / 64 | 648.10 | 1964.61 | 579.09 | 584.88 | 584.35 |

The large LMDB batch result has the mode discrepancy investigated below;
**do not use its throughput/latency ratio as an architectural gain claim**.
The candidate/baseline comparisons also include unequal generic writer
preparation and maintenance policy: candidate fixed Shape and full drain versus
baseline dynamic Shape and bounded drain. No isolated publication ratio follows.

Large batch median p99 is 61940 ns baseline versus 81521 / 95281 / 87581 ns
for Store8 / 16 / 32: their throughput improvement is not a tail improvement.
Before final release, baseline retains 213 versions / 165047 metadata bytes,
versus seven versions / 8557–13581 bytes in candidates. After full release all
have four versions; candidate reusable capacities remain visible. Final release
median is 88731 ns baseline versus roughly 1 microsecond candidates, with more
baseline maintenance left at this boundary. The Arena still holds 49071360 B
of idle/reusable backing after release in all three candidate arms and baseline.

Configured age is the **periodic rotation interval**, not final held age:
32771 scalar groups leave age three; 8193 batch groups leave age one. The age-N
releases are inside publication samples. The separately timed final release
follows complete old/current validation and is warm. The original witness trace
selects Object0 in the four-Object panels, so it is not diverse old discovery.
This capture also holds the candidate writer basis through its drain; an external
old reader masks that effect. Later code closes basis before install/drain and
independently checks reclamation with no external reader.

## Finite Groups and the 64-Object budget

[Worker 20261009T212633Z-b34c4d41](evidence/zen5-group-controls-20261009/feedback/measurements.csv)
reused that host and passed 200 trials plus 20 hardware replays. Its 192 rows
in 64 Objects have three Tuple rows per Object. Legacy and consume16 are paired,
with the common one/64-slot cache rather than a catalog-sized prepared array.
The source combines committed `83face4` metadata with the owned current caller.

For consume16, scalar medians are 94.35 ns baseline, 156.94 ns mapped LMDB,
and 66.63 / 67.05 / 65.60 ns Store8 / 16 / 32. Batch64 medians are 68.09,
116.81, and 62.06 / 62.69 / 60.69 ns respectively. Scalar clock replay includes
clock overhead; its p99 is not a higher-resolution throughput measurement.

The actual fresh Pool has 30608 metadata bytes and 8192 mapped bytes, charged
38800 B to the Arena. Candidate PMR is 29840 / 30336 / 30744 B versus baseline
Domain 55784 B. **Named catalog plus Pool metadata is about 60 KiB for the
candidates**, before allocator/worker/fixture controls, and far from the 4–8 KiB
total target. This is a useful Allocation control, not a compact-layout success.
The combined incremental build took 9.67 s wall, 9.45 s user CPU, 0.19 s system
CPU, and 391656 KiB largest compiler-process peak, with one job. That is not
simultaneous build memory or a per-backend compilation comparison.

## Short Allocation source-treatment screen

[All four cuts](evidence/zen5-allocation-screen-20261009/) from worker
`20261009T215905Z-f3bfeb71` retain 192 trials: two repetitions per arm, with
192 rows / 64 Objects, batch1/64 reads, real writes and age-eight old readers.
The cumulative treatments are current, first-three compact controls, Storage16,
and removal of growth tracking. Each executable captures its actual patched
sources. Backend order rotates within each cut; the source cuts themselves are
blocked in that order. This is directional feedback, not timing qualification.

| Treatment | Fresh Pool metadata B | Batch64 held/released candidate Pool metadata B | Candidate16 scalar update ns | Candidate16 batch64 update ns |
| --- | ---: | ---: | ---: | ---: |
| Current | 30608 | 91824 | 616.39 | 573.27 |
| First three | 19112 | 57336 | 623.87 | 574.26 |
| Storage16 | 15032 | 45096 | 611.68 | 565.29 |
| Growth removal | 15032 | 45096 | 625.62 | 573.22 |

Actual fresh Allocation metadata is roughly halved. There is no large write
regression signal in this small panel; the narrow timing differences need more
feedback. Growth removal changes Hold bookkeeping without further Group-byte savings
here. Recorded growths are zero: this does not measure grow-operation speed,
and Storage remains 16 B. Candidate PMR remains 30336 B initially; batch64
has 47481 B while held and 46392 B after release. Scalar has 32977 / 31888 B
while held / released and one Group with 30608 → 15032 Pool metadata bytes. Allocation counts, mapped/idle bytes, copied metadata
and catalog state agree across all four cuts. Neither this catalog nor the
baseline reaches the total metadata target.

All competitors and both clock modes remain in the selection. The four ABI
summaries were byte-identical; Git keeps `current/abi.json` once and the existing
worker archive recovers the other copies. Full raw samples, sources and binaries
remain in that archive. This screen uses the old generic writer/catalog controls;
the next small screen composes the shared Shape, inline catalog and native bridge.

## Clock-mode fault discriminator

[Worker 20261009T213742Z-b9d498d3](evidence/zen5-fault-controls-20261009/feedback/measurements.csv)
passed 100 trials comparing original mode-specific latency-buffer reservation
and equal preallocation/page touching before both modes. It uses another Zen 5
Spot guest; both preparations and all backends rotate within this source cut.
Every timed operation still passes the independent oracle. Write-wrapper fault
deltas exclude fixture preparation and validation, but include sample preparation.

| LMDB preparation | Throughput ns/update | Latency replay ns/update | Throughput wrapper minor faults | Latency wrapper minor faults |
| --- | ---: | ---: | ---: | ---: |
| Original mode-specific | 1972.66 | 1119.77 | 508325 | 626 |
| Equal buffer setup | 1988.57 | 1122.16 | 508341 | 626 |

**Equal buffer preparation does not resolve the discrepancy.** It lies inside
the write wrapper, and is not a quantile aggregation artifact. The primary
latency runner also passes a long CSV filename, unlike the local no-export probe;
CLI/allocator-history sensitivity remains a possible discriminator, not a finding.
LMDB's overflow buffers use malloc/free while singleton pages are reused in its
[0.9.31 implementation](https://github.com/LMDB/lmdb/blob/LMDB_0.9.31/libraries/liblmdb/mdb.c).
Local ARM allocator environment probes changed fault counts but did not reproduce
the default mode split; they are diagnostics, not performance evidence.
Keep this limitation on large LMDB writes while comparing Orbital treatments.

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

Actual publication/rolling retained readers, physical and metadata refusal,
large-catalog correctness and finite-Group controls now exist. Full raw caller
qualification is archived; its source and binary identity are explicit. Next
feedback prioritizes quick paired Allocation source treatments and the composed
shared-Shape/inline/native bridge, including Tuple1/Series4 operations. Cold
conditioning and broader residency/catalog/retention generality remain open.
The source-only baseline range diagnostic explicitly names uncovered allocator,
map and new snapshot storage; it is not a cold measurement.
