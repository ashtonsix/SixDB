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

## First composed native-call screen

[Worker 20261009T221823Z-38f26e9d](evidence/zen5-composed-screen-20261009/)
keeps 192 trials: 96 for current Allocation and 96 for cumulative compact,
Storage16 and growth-core. It uses the same 192-row / 64-Object discriminator,
two repetitions, both clock modes and all older controls. Metadata is released
root tree `b3338ed4` with the separately captured driver overlay; live subsequent
maintenance edits are excluded. Cuts are temporally blocked, so small margins
remain directional. The following medians are ns per complete request:

| Current → Storage16 + growth-core | Scalar hit | Batch64 hit | Scalar update | Batch64 update |
| --- | ---: | ---: | ---: | ---: |
| Composed Inline | 25.31 → 27.26 | 24.15 → 21.78 | 829.32 → 828.39 | 590.26 → 573.42 |
| Composed Directory | 30.67 → 31.06 | 26.11 → 26.41 | 789.95 → 814.59 | 570.05 → 563.05 |
| Matched native LMDB | 132.88 → 132.26 | 58.95 → 62.01 | 1893.20 → 1866.15 | 487.46 → 511.54 |

The shared Shape/native bridge improves the read signal for both systems.
Scalar composed writes are slower than the old Store16 control
(645.17 → 618.21 ns); this is useful negative feedback. The matched LMDB write
body is also slower than its generic control in this panel. The new body
qualifies a private batch once and retains duplicate order; these complete-call
numbers do not isolate the writer kernel, capture or publication.

Initial PMR is 6355 B Inline / 6883 B Directory, including a 227 B shared
Shape. Pool metadata is 30608 → 15032 B. The actual resident shared Schema
is **15296 B**, with 283 B writer-shape heap, 304 B borrowed writer-shape record,
672 B Pool setup/inventory and 896 B Backend record reported separately.
These records have different inclusion scopes and must not be added blindly;
the shared Shape is already in PMR. The total metadata target is not achieved.
Mapped LMDB's same 227 B proof is separate from its map/file footprint.

Batch64 held/released PMR is 9835 / 9715 B Inline and 10883 / 10763 B Directory.
The writes perform 4099 private admissions and updates, 12297 key reads and no
full-row materialization inside the qualified writer. The preserving source
reads still occur outside that writer body. Retained age is again one after
the final batch tail; final release follows validation and is warm.
Held requested extent is absent under growth-core and remains `null`, not zero.

The enclosing driver separately passed 42 targeted native oracle cases,
including both real conversion directions, old selected bodies and four-Series-
Part writes. That is wiring coverage, not a new sanitizer qualification or a
multi-Part performance panel. The prior SAN receipt remains its own source cut.
The two captured ABI summaries were byte-identical: retain one and recover the
other from the same verified archive. Next attribution is caller-owned private
state with scalar-sized scratch, separate from Allocation heat and maintenance.

## Caller-owned private state

[Worker 20261009T222617Z-06d79f04](evidence/zen5-private-state-screen-20261009/)
reuses the preceding guest for another 192 trials. Storage16 plus growth-core
is fixed. The only native source treatment is the released caller-owned private
state patch: fresh stationary Parts, `Result<void>` instead of a large owned
return, and one-entry scratch for scalar updates. Heat and later catalog
maintenance are excluded. All competitors and both clock modes remain selected.
Two source cuts are blocked, with two rotated repetitions each.

| Composed writes, ns per update | Baseline scalar | Private scalar | Baseline batch64 | Private batch64 |
| --- | ---: | ---: | ---: | ---: |
| Inline | 824.01 | 789.12 | 577.87 | 559.23 |
| Directory | 825.78 | 744.34 | 572.91 | 600.73 |

Scalar feedback improves, but Directory batch64 regresses. Native mapped LMDB
batch64 is 505.75 → 503.45 ns; the older mapped control is 486.17 → 473.83 ns.
Copy requests, Pool allocation/reuse counts, live metadata and qualified writer
counters are identical between cuts. This changes call placement and temporary
storage, rather than the retained representation's footprint.

Scalar sampled p99 falls 1350 → 1270 ns Inline and 1260 → 1170 ns Directory.
Batch64 goes 49535.5 → 54500 ns Inline and 54121 → 53036 ns Directory.
There are only 65 group samples per batch64 replay, so nearest-rank p99 is the
maximum; these are obvious-tail feedback signals, not qualified distributions.
The retained negative cells rule out a blanket ABI or throughput win.
The owner's [separate codegen control](../metadata/composed/controls/private-state/README.md)
records smaller AArch64 scalar frames, removed Allocation moves and larger
emitted update text; this capture retains the actual x86-64 executable's
structural summaries. Neither establishes store-to-load forwarding stalls.

## Shared writer-law screen

[Worker 20261009T225603Z-d1b40bd6](evidence/zen5-writer-law-screen-20261009/)
passes 232 oracle trials: 96 per quick cut and 20 per larger/contextual cut.
The baseline is `d000430`; the treatment is Ikea's released writer-law cut1
(`a8c185…` source map). First-three Allocation controls, Storage16 and the
**full growth API** are fixed in both arms. Default caller state remains;
private-state and later bounded maintenance are excluded. A fresh c8a.large
Zen5 Spot guest runs cuts in baseline-then-treatment order, with two rotated
quick repetitions and one contextual repetition. Complete update medians are:

| 192 rows / 64 Objects, ns/update | Baseline scalar | Writer scalar | Baseline batch64 | Writer batch64 |
| --- | ---: | ---: | ---: | ---: |
| Inline | 881.54 | 567.48 | 630.10 | 348.35 |
| Directory | 850.15 | 553.64 | 570.49 | 379.94 |
| Matched native LMDB | 2017.58 | 1590.85 | 506.10 | 252.48 |

Writes improve for both systems. Inline caller CPU per batch64 request falls
630.89 → 348.91 ns; process CPU agrees. Older candidate16 batch64 is
616.84 → 612.82 ns and generic LMDB 503.97 → 496.33 ns. Read controls remain
in the evidence, including Inline scalar 26.30 → 28.18 ns and Directory
batch64 26.80 → 27.84 ns: this is a write-control result, without a read win.

The modest 4096-row / four-Object sample includes actual independent Series4:

| ns/update, baseline → writer | Scalar | Batch64 |
| --- | ---: | ---: |
| Tuple Inline | 1128.25 → 766.64 | 213.53 → 180.63 |
| Tuple Directory | 1138.29 → 787.51 | 215.69 → 187.77 |
| Tuple mapped LMDB | 3127.49 → 2605.38 | 309.58 → 250.71 |
| Series4 Inline | 1572.45 → 1138.52 | 241.51 → 190.78 |
| Series4 Directory | 1688.13 → 1111.48 | 244.91 → 193.67 |

Preparation/admission, copies, capacity, PMR/Pool and reclamation counters match
between source cuts. The quick writes copy 282831 B, admit 4099 private targets,
apply 4099 updates and perform 12297 measured writer key reads. Larger batch64
has 259 private admissions and 45092 writer key reads; Tuple copies 6099968 B
and Series 5967360 B. Source lookup is outside these writer probes, and the
source/clone searches still both exist. This control changes law admission and
native source access, rather than publication or clone volume.

Initial PMR remains 6355 B Inline / 6883 B Directory, Pool metadata 15032 B,
and resident Schema **15296 B**. The Tuple law adds 1408 live bytes in 1432
inline slots; the containing Backend grows 896 → 2368 B including placement
and alignment. Series engages an 8 B proof but reserves the same slots,
including the empty Tuple optional. MappedShape reports 1488 B containing
record, 1416 B slots and 1408 B live law. These views overlap; the original
Schema has not been removed. Quick process peaks are roughly 5.3–5.5 MiB,
without a useful RSS reduction signal. Larger Series held/released Pool
metadata stays 27280 B in both cuts, with reusable backing after reader release.

Both native arms separately pass 17968 composed comparisons, 16 refusals and
ten CLI write smokes each; the [native archive](evidence/zen5-writer-law-screen-20261009/native-artifact.json)
identifies source recipe, logs and binaries. This screen adds no sanitizer claim.
Measured x86-64 composed update stack reservation falls only 3576 → 3544 B;
mapped update stays 1784 B. Large caller state remains a separate discriminator.
Batch64 has 65 samples, so p99 is the maximum; blocked source cuts and few
repetitions keep all margins directional. All twelve table/parity files
regenerate byte-identically. Git keeps the two distinct ABI summaries once;
identical contextual copies and all raw captures remain in the worker archive.

## Full and bounded maintenance

[Worker 20261009T231018Z-f0210c01](evidence/zen5-maintenance-screen-20261009/)
reuses the writer-law guest and passes 204 oracle trials. Writer law, default
caller state, first-three Allocation controls, Storage16 and full growth stay
fixed. Original `d000430` candidate metadata is compared with the `0acd622`
header-only maintenance delta, then that same implementation with 32 metadata
steps per publication/reader-rotation maintenance invocation. Constructor and
final settlement remain full drain; the final physical poll is 65536 in all
cuts. One rotated quick repetition and one contextual repetition per cut keep
timings directional. Counters distinguish new visits/backlog from original
unavailable diagnostics.

| 64-Object writes, ns/update | Original full | New full | Bounded32 | Bounded32 with final settlement |
| --- | ---: | ---: | ---: | ---: |
| Inline scalar | 564.76 | 576.40 | 557.23 | 557.37 |
| Directory scalar | 577.26 | 583.15 | 557.80 | 557.94 |
| Inline batch64 | 379.19 | 382.07 | 435.64 | 501.65 |
| Directory batch64 | 383.70 | 368.04 | 407.20 | 470.65 |

The full-drain tiny panel shows no large new regression. Bounded32 batch64
is slower even before settlement and retains **4163 versions / 64 debt Objects**
instead of 67 versions / three debts. Inline held PMR is 304699 B with 337527 B
peak, versus new-full 10323 / 17115 B. Pool metadata rises 15032 → 255544 B.
One conservative sweep starts, no sweep completes, zero versions are reclaimed,
and no restart churn occurs. Held counters record 59 entry visits, 1109 version
and cut visits and one head refresh. Source/counter analysis identifies two
causes. The 2336 available steps cannot cover even one step per 4096 required
retirements. Separately, the first sweep keeps its cut-2 publication boundary
and cut-1 reader snapshot until all Objects finish; it therefore preserves
newer versions even after that reader closes. Traversal advances through 58
completed entries, with six still pending, but its retention plan is stale.
Refreshing that plan at completed Object boundaries is a separate proposed
control; it cannot remove the independent service deficit.

Final settlement takes 270553 ns Inline / 260082 ns Directory and clears debt
and pending work, reaching 64 current versions. Reusable PMR remains 140739 B,
Pool metadata 255544 B and idle mapped backing 393216 B. The new full arm
instead settles in about 0.5 microseconds, with 10203 / 11243 B released PMR
and unchanged 15032 B Pool metadata. Neither zero backlog nor low publication
time means the retained capacity disappeared. Settlement follows independent
old/current validation and is warm; the combined figure adds that clock to
publication and excludes validation/fixture work.
The 130536 B released PMR excess is exactly 66024 B extra history capacity plus
64512 B extra descriptor-directory capacity, rather than still-live versions.

The four-Object larger panel has useful negative context: Series4 Inline batch64
is 200.92 ns original → 229.47 ns new-full → 208.55 ns bounded; Tuple is
187.29 → 187.12 → 189.93 ns. These small cuts have three old versions at the
boundary and do not recreate the 64-Object backlog. The scalar bounded signal
is also a different service/load balance. Keep full drain as default; this
bounded budget has not earned selection.

All three cuts separately pass ten native CLI oracles; new cuts settle to zero
debt/pending and one current version per Object. The [native archive](evidence/zen5-maintenance-screen-20261009/native-artifact.json)
keeps their ELF identities, commands and recipes. All 18 table/parity files
regenerate byte-identically. Git keeps the three distinct ABI summaries once,
with identical contextual copies and raw sources/samples in the worker archive.
Allocation advice/direct routes are next, with full maintenance held fixed.

## Allocation advice and direct slots

[Worker 20261009T231831Z-94780ebd](evidence/zen5-allocation-routes-screen-20261009/)
passes 272 oracle trials on the same guest: one quick and one modest larger/
Series4 repetition per cut. The shared writer and new full maintenance are
fixed. Allocation varies from Storage16/full growth to Group advice, then
Direct8 and Direct4 with the same checked Hold API and 8388607 use limit.
Shape export propagates refusal; a common cold accounting adapter observes
each actual slot layout. Source cuts are blocked and retain all competitors.

| 64-Object result | Storage16 | Group advice | Direct8 | Direct4 |
| --- | ---: | ---: | ---: | ---: |
| Fresh/held/released Pool metadata B | 15032 | 10448 | 8400 | 7376 |
| Inline scalar update ns | 547.44 | 603.82 | 548.78 | 543.22 |
| Inline batch64 update ns | 343.91 | 350.42 | 344.79 | 345.04 |
| Directory scalar update ns | 552.10 | 584.30 | 577.42 | 505.08 |
| Directory batch64 update ns | 349.64 | 334.35 | 335.22 | 325.05 |

Requested Pool metadata is roughly halved. PMR, copy/preparation/admission/update
counters, live body capacity and reclamation match between cuts; Inline PMR
remains 6579 B initially, 10323 B held and 10203 B released in batch64.
Schema, law slots and setup still need their existing charges; the whole
metadata target has not been achieved. Caller CPU follows update wall time.

Group advice's scalar negative remains visible. Unchanged mapped LMDB batch64
varies 263.53 / 269.38 / 247.87 / 260.31 ns, so this screen does not establish
a small speed ranking. Direct4's Directory and larger-body signals are useful
directional leads rather than a reason to discard Direct8. Ordinary owner-lane
operation does not price foreign counter contention, saturation or advice
quality under pressure; those belong to the separate qualified runtime controls.

The larger Series4 Inline batch result is 200.90 / 211.11 / 195.29 / 189.56 ns.
Its held/released Pool metadata is 27280 / 22696 / 20648 / 19624 B, with the
same payload/copies and final zero debt/pending in all cuts. Larger Tuple uses
Windows and keeps 3744 B held/released Pool metadata across all four cuts;
the Group saving cannot be generalized to that allocation shape.

All four cuts separately pass ten native CLI smokes, including real Series4,
old readers and mapped LMDB; the [native archive](evidence/zen5-allocation-routes-screen-20261009/native-artifact.json)
keeps commands/ELFs/recipes. All 24 table/parity files regenerate identically.
Git omits the byte-identical Group-advice ABI and identical contextual copies
after archive verification; three distinct exact-executable summaries remain.
Direct routes retain the owner's explicit Clang/Linux enclosing-address
experiment limit, without a portable-language or weak-memory claim.
The narrow point-write comparison follows separately, with Storage16 and new
full maintenance fixed rather than mixing these controls into its baseline.

## Narrow point updates

[Worker 20261009T232342Z-c0b340ec](evidence/zen5-point-write-screen-20261009/)
passes 232 oracle trials with the measured new-full/Storage16 writer source
fixed. The isolated [point command](../metadata/consumer/point-write.md)
preserves clone key/next and writes only value/tag in an ordinary separate TU;
the full writer remains the comparison control. Both cuts keep matched mapped
LMDB specialization, rolling old readers and final settlement. Quick has two
repetitions; the larger Tuple/Series4 context has one. Source cuts are blocked.

| Update ns, full → point | Inline | Directory | Mapped LMDB |
| --- | ---: | ---: | ---: |
| 192 rows / 64 Objects, scalar | 562.00 → 618.82 | 576.17 → 507.98 | 1661.89 → 1751.73 |
| 192 rows / 64 Objects, batch64 | 362.91 → 320.51 | 363.70 → 333.62 | 263.82 → 252.91 |
| 4096 rows / four Objects, scalar | 761.46 → 731.52 | 771.61 → 682.91 | 2713.39 → 2545.99 |
| 4096 rows / four Objects, batch64 | 188.59 → 126.83 | 180.40 → 120.38 | 263.32 → 215.62 |

The small Inline and mapped scalar regressions remain evidence against a
blanket speed claim; both Inline point repetitions are slower than both full
repetitions. Larger Series4 batch Inline improves 198.97 → 147.20 ns and
Directory 186.87 → 141.59 ns. These are useful directional batch/body signals.
Architecture selected the narrow writer while keeping its failure tradeoff:
clone-first admission may return capacity before absent and perform a wasted
copy. Published logical state and refusal outcomes remain independently checked.

Quick Tuple private copies remain 282831 B with 4099 admissions/updates.
Actual encoded values/bindings fall 12297 → 8198; issued bytes 61485 → 28693
(15 → 7 per command), while issued spans remain 4099. The command is 16 B
rather than 24 B; scratch falls 128 → 64 B, Tuple law 1408 → 1072 B and
stationary law slots 1432 → 1096 B. Backend is 2368 → 2048 B and explicit
caller arrays 3072 → 2560 B. The original Schema stays 15296 B and PMR
remains 6579 B. Source lookup is removed; clone writer key probes remain.

Exact x86-64 code grows: composed update text 3044 → 3540 B and mapped
1996 → 2235 B. Stack reservations fall 3528 → 3048 B and 1784 → 1240 B.
Smaller caller storage is therefore not a universal code-size or scalar win.
Twenty native CLI oracle cases pass, including old/final `next` preservation
and complete settlement; the [native archive](evidence/zen5-point-write-screen-20261009/native-artifact.json)
contains the already verified recipe/logs/ELFs. This adds no sanitizer claim.

All twelve selected table/parity files regenerate byte-identically. Git keeps
all competitors, repetitions and clock modes in the tables, two distinct ABI
summaries, and 44 selected throughput JSON rows because their primitive effect
counters are absent from the older table schema. Identical contextual ABI and
binaries, full raw latency, source cuts and logs remain in the existing worker
archive. The next read-fusion comparison fixes the point writer in both arms.

## Coherent read fusion

[Worker 20261009T233710Z-da0d7e25](evidence/zen5-read-fusion-screen-20261009/)
passes 288 oracle trials, two repetitions in both clock modes, with the measured
point writer fixed in both cuts. The [read-fusion control](../metadata/composed/controls/read-fusion/)
combines actual opaque-ID Inline history selection and the canonical Ikea Tuple
consumer in one ordinary TU. Mapped LMDB uses the same canonical consumer.
All timed Orbital reads use coherent Scope; current-object borrow is excluded.

| Warm read ns/request, baseline → fused | Tiny Inline scalar | Larger Inline scalar | Larger mapped scalar |
| --- | ---: | ---: | ---: |
| Point hit | 26.38 → 21.46 | 98.26 → 88.14 | 162.58 → 153.82 |
| Point miss | 25.28 → 20.61 | 95.61 → 87.35 | 161.51 → 150.80 |
| Dependent16 | 90.77 → 78.56 | 1262.74 → 1148.32 | 1305.22 → 1136.39 |

Tiny is 192 rows / 64 Objects; larger is 32768 rows / four Objects. The larger
dependent scalar still leaves mapped LMDB a small edge, 1148.32 versus 1136.39.
Batch64 Inline is nearly unchanged/slightly worse in the tiny case: hit
22.39 → 22.72 ns and dependent 88.01 → 88.56 ns; that path is not fused.
Larger Inline dependent batch moves 1251.40 → 1216.76 ns, while unchanged
Directory moves 1245.38 → 1222.11 ns; retain that drift for small margins.
Mapped dependent batch improves 1221.39 → 1069.07 ns through the matching
canonical specialization. Improving the comparator remains part of the result.

Copies, capacities and all initial/held/released component accounting match.
Exact measured scalar Inline throughput caller text grows 2537 → 2771 B;
explicit stack reservation falls 312 → 296 B, excluding saved registers.
The fused Scope entry is 922 B with 24 B reservation plus saved registers.
Batch Inline and Directory caller code is unchanged. This measures structural
ABI changes, not forwarding stalls. Thirty-six native CLI oracle checks plus
focused fusion/mapped-Shape checks pass. The compact native receipt and source
recipe identify this cut; native logs remain ignored. Full worker captures
stay in the existing verified bundle, without another archive campaign.

All six tables/parity files regenerate byte-identically. Both exact executable
ABI summaries and all competitors/repetitions/modes remain selected. Cuts are
blocked in time; batch64 has 513 samples, and margins remain directional.
Architecture selected coherent scalar fusion. Larger/cold-start and retained
reader breadth follows before more metadata controls; current-borrow and batch
fusion remain separate.

## Broader bodies and retained readers

[Worker 20261009T234913Z-0fbfd058](evidence/zen5-breadth-screen-20261009/)
passes 304 oracle trials, two repetitions/both modes, on the same frozen
Storage16/new-full/point source with the original unfused protected Shape
read bridge. No later Prepared, fusion, Direct4, origin or heap changes enter it. Reads span 32768 rows / 64 Objects
and 262144 / 256; writes span 192 / 64, 16384 / 64 and actual Series4.
A later source audit corrected the original fusion label: patch application
before nested Git initialization had silently skipped the fusion changes.
The actual worker capture and 2537 B scalar caller establish this point-only
cut; the comparisons below remain valid for it. The intended fused breadth
remains separate work.
The body-only cold start uses capability-tested CLFLUSH+MFENCE after the
verified warm replay, once per process before the timed replay. It flushes
exact admitted current Tuple extents while their Scope/transaction is live.
Metadata, trace/output and execution state remain warm; requests subsequently
rewarm their own bodies. The retained tables report coverage, preparation and
first-batch latency separately. This is a cold start, not a cold request loop.

| 262144 rows / 256 Objects, scalar ns/request | Inline warm → flushed | Directory warm → flushed | Mapped warm → flushed |
| --- | ---: | ---: | ---: |
| Point hit | 163.10 → 317.78 | 193.37 → 316.11 | 225.00 → 405.05 |
| Dependent16 | 1159.07 → 1667.44 | 1195.47 → 1545.90 | 1371.56 → 1727.76 |

Inline's warm point advantage over Directory disappears with flushed bodies;
Directory is ahead in the flushed dependent scalar case. Smaller 32768/64
Inline hit moves 72.64 → 127.04 ns. Flush coverage is 753664 B / 64 ranges
or 6029312 B / 256 ranges. For larger scalar hit, median preparation is
120860 ns Inline, 104755 Directory and 161986 mapped. First latency batches
are 570 → 905 ns, 585 → 770 and 575 → 860 respectively; these first samples
are observations, without a tail claim. Matching warm/cold traces/outcomes
are checked independently of the condition-specific report grouping.

| Full-maintenance update ns/request | Original reader held | Rotate every 64 publications |
| --- | ---: | ---: |
| 192/64 Inline scalar | 1232.92 | 826.80 |
| 192/64 Inline batch64 | 323.61 | 322.97 |
| 16384/64 Inline scalar | 1278.21 | 901.91 |
| 16384/64 Inline batch64 | 457.60 | 470.62 |

Scalar retained-reader age matters even with full drain. Tiny held-original
has 128 versions / 12763 B PMR and 524675 version visits; rotating has
67 / 8771 B and 274444 visits. Both reclaim intermediate versions and end
with zero debt/pending after charged final settlement. Batch amortizes the
same publication work and is much less sensitive. Mapped tiny batch remains
ahead at 261.26 ns held / 268.23 rotating; its scalar held/rotating values are
4465.62 / 1631.48 ns. Larger mapped batch is 1612.47 / 1635.06 ns, with the
previous large-write clock-mode limitation still applicable. Series4 Inline
scalar/batch is 975.09 / 130.93 ns with real old-reader rotation.

Fifty-four native CLI cases pass for the conditioning control over the earlier
point-plus-fusion native build, including exact body range/byte counts, ARM
cache maintenance, old/current rows and settlement. That native composition
is not byte-identical to this unfused worker cut; the worker independently
checks every actual outcome and old/current row. Git keeps
compact native/source receipts, all competitors/repetitions/modes, three
regenerating reports and the exact executable ABI. Source cuts, ELF, full
latency, logs and per-process JSON stay in the existing verified worker bundle.
Body conditioning is an isolated source treatment, not a change to historical
runs. Fresh versus reset/renew LMDB reader handles follow separately; these
results do not yet price that stronger short-reader control.

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
qualification is archived; its source and binary identity are explicit. The
writer law has useful directional support with its inline storage charged.
Maintenance, Allocation-route and narrow-point pairs now retain their negative
controls as well as useful reductions. Read fusion follows with the point writer
fixed, then a modest larger/cold/retained read-write panel. Heap and trim controls
remain separate; broader residency/catalog/retention generality remains open.
The source-only baseline range diagnostic explicitly names uncovered allocator,
map and new snapshot storage; it is not a cold measurement.

## Verified fused breadth and reusable mapped readers

[Complete comparisons](evidence/zen5-fused-feedback-20261010/renew-reader/measurements.csv)
and [source audit](evidence/zen5-fused-feedback-20261010/source-recipe.json)
come from `20261010T002323Z-2438d29a`: 144 fresh-reader trials, 144 renew-reader
trials and 304 breadth trials. Each exact capture retains the original measured
coherent fusion, point writer, Storage16 and full maintenance; source postimages
and the 2771-byte scalar caller establish that composition. These replace the
intended fused coverage missing from the earlier point-only captures. The
superseded reader control remains recoverable through its
[thin archive reference](evidence/zen5-fused-feedback-20261010/superseded-reader-artifact.json);
its complete duplicate export leaves Git. The earlier breadth comparison remains
explicitly point-only. The new job uses another Spot guest, so compare treatments
within this capture.

Fresh versus renew uses the same stationary LMDB owner and 8-byte lease. Both
end each group snapshot inside the complete interval. Renew retains an inactive
handle between groups; setup and final abort have separate clocks. Independent
publication/old-view/move/refusal checks pass in each exact cut. All 160 mapped
read trials have the expected begin/renew/reset/abort counts, no active lease
between groups, and no retained handle after final cleanup. Store/ordinary
Read/short lease are 88/16/8 bytes; private transaction allocation is unavailable.

Median mapped throughput, ns per complete request:

| Case | Fresh | Renew |
| --- | ---: | ---: |
| 192/64 hit, scalar | 121.62 | 73.06 |
| 192/64 hit, batch64 | 56.66 | 59.72 |
| 192/64 dependent16, scalar | 178.77 | 154.12 |
| 32768/4 hit, scalar | 151.38 | 128.36 |
| 32768/4 dependent16, scalar | 1149.27 | 1129.58 |
| 32768/4 dependent16, batch64 | 1052.18 | 1077.43 |

Scalar setup amortization helps; batch results include regressions. Unchanged
Inline tiny scalar hit is 21.66/21.52 ns and dependent16 78.15/78.23. Larger
unchanged Inline scalar dependent16 shifts 1137.32/1092.36, while its batch
shifts 1212.46/1248.32. This blocked two-repetition pair is directional evidence,
not attribution of every small timing change to the reader policy.

The breadth arm uses renew and the same once-per-trial body cold start. All
warm/cold pairs preserve oracle and logical/body counters. Exactly 64/256 body
ranges cover 753664/6029312 bytes. Metadata, trace, execution and translations
remain warm; the timed replay naturally reheats bodies. Larger scalar medians:

| 262144 rows / 256 Objects | Inline warm / cold | Directory warm / cold | Mapped warm / cold |
| --- | ---: | ---: | ---: |
| hit | 111.47 / 289.22 | 166.74 / 317.80 | 177.82 / 383.59 |
| dependent16 | 995.02 / 1421.07 | 1165.53 / 1510.82 | 1103.21 / 1556.05 |

Fusion keeps an advantage in these cold-start medians. The earlier point-only
breadth record cannot support a claim about fused cold behavior. Warm batching
still matters: the renewed 32768/4 hit costs Inline 86.77 versus mapped 75.42 ns,
and dependent16 batch64 Inline 1248.32 versus mapped 1077.43.

Holding the original reader still makes scalar maintenance expensive. Tiny
Inline update is 1215.85 ns with age0 versus 812.60 with age64 rotation; batch64
is 298.73/296.87 versus mapped 265.17/264.45. Larger 16384/64 Inline batch is
455.93/459.20 versus mapped 1572.77/1605.50; the mapped large-write fault/clock
limitation discussed above still applies. Series4 batch is 133.54 ns. Every
write settles to zero pending maintenance/debt after the retained reader ends.
All nine table/parity reports and three ABI reports regenerate byte-identically;
breadth shares the byte-identical renew ABI, retained once. Full source, binaries,
ordered samples and diagnostics remain in the existing worker archive.

## Selected Prepared, Allocation and fusion composition

[Complete paired evidence](evidence/zen5-selected-composition-screen-20261010/selected/measurements.csv)
from `20261010T002940Z-a0983b07` compares the actual measured fused source with
runtime commit `768e033` / tree `e509697`, including caller-owned Prepared and
Direct4 Allocation. Both use the same reset/renew mapped reader, point writer
and coherent fusion. All 344 trials pass. Every native source/header/CMake/driver
postimage in both actual captures matches its expected source manifest; the
previous mislabeled selected run is excluded. This is a combined source change,
not attribution of timing to one component.

Tiny Inline components change PMR 6579→6563 B, catalog Core 704→688 B and
Pool metadata 15032→7376 B. Arena charge falls 23224→15568 B, including that
Pool metadata reduction; body capacity stays 6144 B. Schema 15296 B, Backend
2048 B, caller arrays 2560 B and writer law 1072 B remain. These components
remain distinct; the allocator saving is not a whole metadata-budget result.

Median complete-request throughput, ns; base→selected:

| Case | Inline | Mapped control |
| --- | ---: | ---: |
| 192/64 hit, scalar | 20.78→20.59 | 73.61→72.45 |
| 192/64 update, scalar, age8 | 501.18→507.19 | 1538.29→1547.57 |
| 192/64 update, batch64, age8 | 296.13→292.05 | 212.80→218.44 |
| 192/64 update, batch64, age0 | 292.91→290.19 | 259.38→262.74 |
| 4096/4 Tuple update, scalar | 650.70→694.99 | 2382.31→2374.63 |
| 4096/4 Tuple update, batch64 | 111.96→113.55 | 185.66→196.73 |
| 32768/4 dependent16, scalar | 1140.72→1102.55 | 1154.48→1105.91 |
| 32768/4 dependent16, batch64 | 1216.44→1217.12 | 1104.16→1051.08 |

Series4 batch is 131.56→133.85 ns. The memory reduction has no blanket speed
win: scalar writes include regressions, tiny batched writes still trail mapped,
and the unchanged mapped scalar-dependent arm shifts alongside Inline. This
short blocked two-repetition pair does not establish a small treatment benefit.
All logical/copy/body-capacity/version counters match; every raw Orbital write
ends with one version per Object and zero contexts/retirement records. This
driver predates detailed maintenance visits, debt and pending fields; unavailable
fields are not zero. That instrumentation remains with the separate maintenance
and breadth controls. Native target qualification has three consequence checks
and 26 CLI cases. Six table/parity reports and both ABI reports regenerate
byte-identically; full captures remain archived.

## Compiled Object catalog boundary

[Matched extraction evidence](evidence/zen5-object-module-screen-20261010/module/measurements.csv)
from `20261010T003336Z-352a5321` holds selected `768e033`, Direct4, Prepared,
Schema and mapped renew constant, applying only the revised catalog extraction
patch `fbc3e315`. Actual source postimages include its corrected final-disposal
callback boundary. All 184 trials and exact worker lease checks pass. Root's
four native consequence suites and opaque-header check qualify the extraction;
this comparison makes no sanitizer claim.

All available logical, copy, body-capacity, version, metadata allocation,
Schema and caller counters match exactly. Tiny Inline PMR/Pool stay 6563/7376 B.
Four Inline read callers keep identical code sizes 3030/2771/2840/2653 B and
explicit scalar/batch stack reservations 296/1336 B. Extraction moves cold
publication/history to ordinary compiled boundaries while preserving these
inlined read callers. Ordinary PMR callback devirtualization is not assumed.

Median complete-request throughput, ns; base→module:

| Case | Inline | Mapped control |
| --- | ---: | ---: |
| 192/64 hit, scalar | 20.62→20.84 | 75.53→74.13 |
| 32768/4 dependent16, scalar | 1076.45→1109.68 | 1104.65→1156.13 |
| 192/64 update, scalar, age8 | 497.62→483.99 | 1540.09→1559.02 |
| 192/64 update, scalar, held original | 1285.69→1205.23 | 4709.92→4790.77 |
| 192/64 update, batch64, age8 | 289.92→293.26 | 215.19→216.99 |
| 192/64 update, batch64, held original | 286.44→286.38 | 261.74→257.98 |
| 4096/4 Tuple update, batch64 | 112.02→114.32 | 193.38→189.97 |

Series4 batch is 133.81→128.45 ns. The short blocked pair has both favorable
and unfavorable changes, alongside movement in unchanged mapped controls.
It supports preserving the read ABI and component costs at this compilation
boundary, without proving that extraction is cost-free or universally faster.
Every raw Orbital write finishes with one version per Object and zero contexts
and retirement records. Six table/parity reports and both ABI reports regenerate
byte-identically; the source, diagnostics and binaries stay in the existing
worker archive. This predates the later physical extraction and trim changes.
