# Predictability hints and caller-owned selection

**Recommendation:** offer two compile-time read specializations, named
`lookup_hint::predictable` and `lookup_hint::unpredictable`, keeping predictable
as the default. For unknown or shifting workloads, optional selection of the
whole lookup/consume loop is cheap around larger operator batches. It should
not run inside every point getter. The implementation has graduated;
[the promotion record](GRADUATION.md) records maintained-source qualification.
This page retains the research experiment and its original measurements.

The execution hint never promises presence/absence and is safe to get wrong.
It is separate from writer strategy and is never unchecked membership knowledge. This spike does
not add a setter hint or change mutation/growth behavior.

## Measured tradeoff

The complete [scorecard](evidence/hints-zen5/tables.md) includes actual Boost
1.92 and pinned Abseil controls, the original maintained callback loop, both
static specializations and the selected path. Native Zen 5 AVX-512 W32 map8:

| Path | Hit ns | Random 50/50 ns | Miss ns |
| --- | ---: | ---: | ---: |
| Original | 1.408 | 7.094 | 1.126 |
| Predictable getter | 1.366 | 7.024 | 1.087 |
| Unpredictable getter + predicated fold | 2.090 | 2.157 | 2.396 |
| Selected whole loop | 1.420 | 2.493 | 1.161 |
| Boost | 1.797 | 6.317 | 1.165 |
| Abseil | 1.729 | 6.608 | 1.754 |

Selected W32 sets measure 1.000 / 1.565 / 1.074 ns for hit/random/miss. Both
surfaces retain all-hit below 1.6 ns and exceed the 220M-QPS random target:
map8 delivers about 401M QPS and sets 639M QPS. Across tested predictable hot
patterns and all cold cases, the worst selected-path regression is 3.7%
(W32 set all-hit, +0.036 ns); the predictable specialization's worst is 1.2%.

Cold throughput is preserved. With prefetch32, W40 map8 hit/random/miss changes
from 17.949 / 16.168 / 4.975 ns to 16.779 / 15.037 / 4.875 ns. W50 map8 changes
from 30.624 / 26.949 / 6.760 to 30.777 / 26.827 / 6.477 ns. The latter's existing
cold hit/mixed deficit against Abseil remains: Abseil is 20.864 / 19.492 ns.
This change does not close that separate gap. Static unpredictable remains a
poor choice for those cold maps; the selector measures that cost and stays
mostly predictable.

## Getter and consumer contract

The studied `get<Hint>(key)` returns a copied `{value, found}` result, with
exactly zero value bytes on absence. Predictable uses ordinary lookup, retaining
conditional value consumption and direct addressing. Unpredictable uses the
prior carry/zero-word kernel for supported generic/small-exact set/map8 recipes;
buckets with overflow retain ordinary lookup. Other recipes fall back safely.
Neither specialization changes bucket geometry, encoding, hash or Swiss rules.

The consumer matters. A predictable consumer can branch on `found` as usual.
A branchless aggregate can sum the zero-on-absence value and mask its hit-key
contribution. An arbitrary callback with hit-dependent side effects can
reintroduce the branch. Consequently, the selector chooses a **complete compiled
lookup/consume loop**, not just a getter function pointer. Both compared loops
do identical useful work; exact hit/key/full-value answers are checked.

The getter hint is compile-time: no mode test, counter, timer, allocation or
indirect dispatch is introduced per key. The `hint-overlay.py` prototype changes
only a copied header during the research. The maintained getter and controller
now implement this boundary; use the [usage guide](../../../ikea/docs/hashtable/usage.md#select-lookup-and-consumer-together).

## Cheap switching without a hit-rate oracle

The archived `hint-kernel.inc` demonstrates a caller-owned 64-byte selector. It compares the
cost of the two complete loop specializations on useful, disjoint request
blocks. It never repeats probes, inspects future membership, or samples kernel
PMU counters. Sampling uses serialized TSC reads; all of that work is included
in the outer measured operation.

Blocks contain up to 256 requests. Once per 64 blocks the selector times one
predictable and one unpredictable block, normalizes cost per request, and picks
unpredictable only when predictable costs more than 1.10 times as much. That biases ties
and noisy small wins toward predictable. A full-block stream revisits the
choice at most every 16,384 requests; shorter external batches can retain the
same caller-owned state. Each full one-million-query interval starts with fresh
selector state and measures 124 probe blocks, rather than receiving a warmed
selection for free.

It distinguishes predictable and random streams with the same selectivity.
On alternating W32 map8, original/selected are 1.282/1.326 ns; grouped 50/50 is
1.273/1.288 ns. On random 50/50 it selects unpredictable for 98.4% of requests.
On stable predictable hot streams it selects predictable for 98.4%. Phased
traces alternate hit/random/miss/alternating segments of 131,071, 16,381 or 509
requests, offset by 7,759 requests to avoid alignment with probe windows. All
are measured against the same original and vendor controls; the full tables
show response and switches, including the faster-changing traces.

## Batch boundary

The separate persistent-stream experiment includes the cost of actual calls
and integration for preallocated batches. It is an Ikea-only seam-tax study,
not another vendor scorecard. W32 map8:

| Batch | Original hit | Predictable hit | Selected hit | Original random | Selected random |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 5.896 | 5.791 | 13.574 | 9.843 | 16.136 |
| 8 | 1.724 | 1.677 | 2.505 | 7.634 | 3.626 |
| 32 | 1.512 | 1.470 | 1.691 | 7.472 | 2.845 |
| 64 | 1.467 | 1.421 | 1.543 | 7.336 | 2.745 |
| 256 | 1.463 | 1.412 | 1.461 | 7.225 | 2.583 |
| 1024 | 1.425 | 1.381 | 1.442 | 7.197 | 2.579 |

Use the explicit specialization for point calls and short batches. Automatic
selection meets the stated map budget from batch64 in this experiment and is
near-free at batch256; prefer that larger boundary when available. Do not add
a universal per-get adaptive wrapper on these results. These timings include
batch calls and must not be confused with bare getter latency.

## Verification and scope

The full comparison has 1,440 fresh processes and 6,264 complete, unscaled
same-thread user-space PMU intervals, three seeds and two processes/arm/case.
An independent 648-process pilot reproduced the W32 result. All libraries receive
identical query sequences, checked with sequence fingerprints as well as complete
answers. The seven orderings retain the same query multiset and within-class
order; their sequence fingerprints differ. No query oracle enters selection.

The separate short-batch study has six fresh seed/type processes and 5,040
intervals, each consuming 262,144 useful queries. It checks identical answers
across methods and batch sizes. Its trials/repetitions within each seed process
are not independent fresh processes; no PMU is used there.

Native ASan/UBSan passes both hints over the existing geometry/collision tests,
including wrong hints, zero/max quotients and encodings, first/last buckets,
saturated overflow and erase/refill. The persistent-stream check covers cost
reversal, predictable bias and unequal-sized batches. Another 462 sanitizer
processes exercise request tails 1..4097, both prefetch paths, both value widths
and all seven orderings. Complete logs and captured sources are recoverable.

The comparison uses the earlier memory-study populations/geometries, with no
capacity or encoding retuning: W32 hot b11; W40 hot set b12/map8 b11; cold
W40/W50 set b22/map8 b21. The W50 map remains the earlier 25%-smaller-than-Boost
reference. This is not a new 85%-load/overflow-heavy campaign. It establishes
native Zen5 AVX-512 independent throughput for the measured set/map8 recipes;
it does not establish new AVX2/NEON, serial-latency or all-value-width results.
MVCC/locks/damage publication are not timed. Table-owned memory is unchanged;
profiling state belongs to the caller, outside the owner.

## Reproduce

The [recoverable evidence](evidence/hints-zen5/artifact.json) retains timings,
PMU samples, sequence checks, rejected short-batch cases, native binaries,
assembly, compiler commands, sanitizer outputs and worker/source references.
One incomplete phase-option protocol is excluded explicitly. Missing local
assembly was regenerated with pinned LLVM21 from audited native binaries;
original worker archives retain the emitted native disassembly as well.

Regenerate the scorecard and audits without new timing:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-primary-branchless/evidence/hints-zen5 \
  build/reports/hashtable-primary-hints
```

The archived `native-hint.sh` and `native-hint-stream.sh` reproduce this
research from its captured sources. For maintained headers, use the
[promotion runners](GRADUATION.md), not the archived overlay edits.
