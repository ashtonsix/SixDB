# Reuse selection, October 8, 2026

**Simplify selection. Keep temperature advice for reclamation.** Orbital now
uses the first compatible, confirmed prepared Window that needs no extra
charge. Finite Groups retain their ready-candidate cache and use two fallback
passes: prepared homes, then unprepared homes. Neither ranks temperature or CPU.
The earlier tier queues and extra candidate cache remain rejected.

## Removing hint ranking

A Spot c8a.large (AMD EPYC 9R45), CPU 0, Clang 21.1.8, Release `-O3`, native ISA
and Zen5 tuning compared the baseline, each simplification separately, and both
together. Eight fresh-process trials balanced all four policies across execution
positions. Each case contains eight timed blocks of 65,536 allocation/touch/release
operations; the inventory has 256 slots. Both widths run in separate processes.
The first Spot attempt was reclaimed after three complete trials; its partial
output was recovered. The table uses only the complete retry in another zone.

Values are percentage changes in the **median of eight same-trial ratios** of
median block mean ns/operation. Positive is slower. These are not individual
operation latency percentiles. The combined full-width column writes and reads
every requested payload word; the other columns touch one 64-byte line.

| Caller trace | Window only, 64 B | Finite only, 64 B | Both, 64 B | Both, full payload |
| --- | ---: | ---: | ---: | ---: |
| hot-loop | -4.1% | +0.0% | -4.1% | -1.7% |
| warm-loop | -3.4% | -0.0% | -3.4% | -1.5% |
| batch-hot | -3.1% | +0.1% | -3.1% | -1.2% |
| cold-contamination | -15.5% | -0.3% | -15.4% | -2.8% |
| mixed-hints | -11.3% | -0.0% | -11.0% | -3.2% |
| mixed-sizes | -13.7% | +0.0% | -13.9% | -1.3% |
| window-locality | -9.3% | -0.0% | -9.2% | -0.7% |
| finite-control | +1.9% | -1.2% | -1.6% | -0.2% |
| finite-fallback | +1.0% | -5.6% | -5.8% | -4.4% |

The finite stress trace rotates eight classes through a four-entry cache while
retaining two thirds of seeded allocations. Every measured operation reaches
fallback. Independent untimed diagnostic builds counted 109,266 baseline Group
visits versus 65,792 after removing ranking, for 65,536 operations. The ordinary
finite control had 61,440 cache hits and 4,096 fallbacks under every policy.
Finite-only fallback improved in every trial at both widths. Full-width control
ratios varied roughly ±5%; its small median changes are not a performance claim.

Window locality rotates hot size bursts among 8–64 KiB extents, with mixed release
advice and live anchors. Baseline selection made 148,093 list visits, including
81,259 visits after finding eligible ready storage. First-ready selection made
65,536 visits and no post-ready probes. Full-payload locality was essentially flat
(median −0.7%, observed ratios −0.9% to +0.2% for the combined candidate), with
unchanged mapped bytes. Hint ranking's locality benefit did not pay for its cost
in these traces.

Mixed sizes also expose an inventory effect: every baseline trial issued 55
`mmap` and 55 `mprotect` calls, ending at 17,465,344 mapped bytes. Window-first-ready
issued none in timed blocks and held 8,142,848 bytes. Its gain includes avoiding
new storage, not just cheaper traversal. The homogeneous and locality traces
made no timed VM calls. One-line HOT/WARM/locality trials include large favourable
outliers; retained trial ranges make that variation visible without changing the
stable direction of their medians.

All four candidates passed 11 Orbital and 12 Loom checks. Each trace checked an
independent checksum, retained-live payloads, exact allocation counts and zero
charge after shutdown. Diagnostics used separate instrumented sources/binaries;
no selection counters were added to timed builds. The timed traces exercise
prepared reuse, not unprepared mapping or trimming performance. Module checks
cover those failure/retry and background paths.

The selector comparison kept the old API and hint layout fixed. Promotion also
removed unused allocation preference arguments and release CPU advice. Actual
Lane placement and hardware discovery remain independent. On the pinned LP64
build, `ReleaseHint` shrank from 24 to 16 bytes, Group from 20,872 to 16,768 bytes,
and Window from 344 to 328 bytes. Concrete metadata charging follows `sizeof`.
All consumers were rebuilt for the changed by-value release ABI. The promoted
cut passed native and ASan/UBSan module checks, three composed packed access
modes, growth/inventory checks and both payload widths at 17 and 256 slots.

[Selected evidence](evidence/zen5-ablation/README.md) retains every matching
block, policy and trial, including controls and VM counters. Its report recipe
regenerates paired trial ratios, observed ranges and byte snapshots.

A four-trial [API/layout replay](evidence/zen5-cleanup/README.md) reused the same
Spot instance and compared the exact archived combined binary with the cleaned
cut. Removing the unused advice/plumbing improved one-line Window traces by
about 15–16% further; finite control improved 6.0% and fallback 4.8%.
Full-payload Window costs were flat to 2.2% better; finite control/fallback were
6.0%/4.0% better. These are a second paired comparison against the unranked
candidate, not percentages against the original ranked baseline. The cleanup
bundle includes API, calling convention, caller and metadata changes; it does
not isolate any one of them. All 23 native module checks and every checksum,
retained payload, allocation count and zero-charge shutdown passed again.
The reused worker was closed with no idle window after collection.

## Earlier queue experiments

The first two campaigns tested separate temperature lists, bounded HOT/WARM
retention with oldest-first demotion and newest/FIFO acquisition, then a recent
candidate cache. They used the same machine type/compiler settings, four fresh
trials and 12 blocks of 131,072 operations, with ordinary 64-byte touches.
Windows normally held 6,304 body bytes plus 64-byte padding, rounded to 8 KiB.
HOT/WARM limits were 256 KiB/4 MiB experimental inputs, not calibrated budgets.
These values use pooled median block means against those campaigns' own baseline.

| Caller trace | Separate tiers | Bounded, newest-first | Bounded, FIFO acquire | Recent candidate cache |
| --- | ---: | ---: | ---: | ---: |
| HOT loop | +4.0% | +7.0% | +7.4% | +1.2% |
| WARM loop | +5.1% | +5.6% | +7.0% | +1.2% |
| HOT batch | +3.8% | +27.9% | +27.8% | +1.2% |
| Buried HOT / COLD returns | −6.3% | −5.9% | −5.7% | +3.9% |
| Mixed hints | −2.7% | +19.0% | +19.1% | +2.3% |
| Mixed sizes | +1.7% | +23.5% | +35.6% | +2.2% |

Separate lists helped the targeted buried-HOT case, but taxed ordinary reuse.
Capacity demotion made batches and mixed sizes worse. A fitting 4 MiB WARM return
could displace about 512 idle 8 KiB Windows on one release: an operation-count
observation, not a measured p99. The additional candidate cache did not help.
Those variants passed the then-current 11 Orbital and nine Loom checks.

`probe.cpp` isolates metadata and full-line memory traffic, with independent
list/accounting, order, oversized-admission and advice checks; ASan/UBSan passed.
It supports the mechanism explanation, not the actual-allocator timings.
[Cascade](evidence/zen5-cascade/artifact.json) and
[candidate-cache](evidence/zen5-cached/artifact.json) archives recover full runs.
The old `delayed-reuse` duplicates HOT batch at this inventory size and contributes
no independent delay evidence; it is omitted from the new default cases.

## What remains separate

COLD storage stays reusable until explicit asynchronous trim. This study does
not establish automatic aging, machine-wide cache budgets, cache residence,
NUMA placement or complete database performance. A future hard idle-memory bound
should be measured as an outer maintenance policy, including its release bursts.

Finite Groups share pages. A last release hint does not describe every free home;
retain geometry, page readiness and the complete fallback as authority. Temperature
still guides cold-first trim; timestamps prevent stale advice from replacing newer
fields. `trim_cold(keep_bytes)` keeps a Pool-wide idle-payload floor, not a cap,
and may reclaim warmer inventory after COLD. Readiness, advice and reclamation
remain distinct responsibilities.
