# Aggregate-maintenance closeout

2026-09-07. This spike is concluded. It mapped candidate mechanisms and ran
two count/sum probes; the design of SixDB's essential descendant summaries
remains open. The implementations, runners, and evidence stay here for reuse
or challenge. No production maintenance policy or module interface is selected.

## What we learned

**Reducing ancestor writes and reducing maintenance CPU are separate results.**
The [partitioned-delta probe](FINDINGS.md) removed 71% of interior-summary
adjustments under uniform writes, yet its location-addressed runs cost about
24 times as much as direct eager updates to resident arrays. Location runs
were leaner than ancestor-addressed runs, but construction and correction had
to earn their cost. The measurements do not attribute that cost to sorting
alone, or establish physical write amplification.

**A much lighter buffer has a useful region.** The
[shared dirty-buffer probe](dirty-buffer/FINDINGS.md) favored amortized append
reservations and batched replay when four writers repeatedly updated hot
locations. With 64 hot keys and 4,096 updates per batch, the conditional Bloom
variant took about 29 ns/update, versus 71 for eager with private shallow
summaries. Removing the filter reduced that to 22. These cycles include
queries, full replay, and reset; every ancestor update is still performed.
This gain does not depend on coalescing or leaving maintenance debt unpaid.

**Dirtiness metadata is conditional value.** Word-local masks, read-before-OR,
and 64-record reservations helped repeated-prefix workloads. Marking and
reset still cost work. Repeated dirty scans could erase the win; flushing on
the first positive read recovered it in the hot read-heavy case. The no-filter
controls were cheaper in the hot cases, and all buffered variants lost to
private-shallow eager on the measured dispersed workloads. Direct non-atomic
eager also won the one-writer filter screen; no-filter one-writer timings were
not collected.

The useful candidates to retain are location-addressed corrections, a simple
bounded append buffer, and optional dirty-prefix indexing. Keep direct eager
and eager with private shallow summaries as baselines. These are candidates
with different measured strengths, not a recommendation to build an adaptive
policy yet.

## What this evidence can support

Both probes use bounded exact integer count/sum, precomputed mutations, and
current-state reads at quiescent batch boundaries. Correctness checks compare
against logical-row oracles; ASan/UBSan checks passed. Timings come from a
Linux ARM VM with guest CPU affinity. Cache residence and physical coherence
traffic were not established, and these runs do not establish a Zen5 winner.

Primary-row work, durability, compression, retained snapshots, overlapping
readers and writers, min/max repair, per-field NULL semantics, and general
numeric contracts are absent. The two probes use different geometries and
workloads; their absolute timings should not be compared with each other.
The [first](evidence/local-arm-20260907/README.md) and
[second](dirty-buffer/evidence/local-arm-20260907/README.md) evidence indices
identify measured source snapshots, raw results, and validation.

## Useful reasons to return

- A candidate summary/container representation makes cold access, patching,
  compression, or durable write cost concrete enough to price buffering.
- A visibility and ownership proposal lets us test safe publication,
  reclamation, and how much cancellation survives retained snapshots.
- A workload supplies enough clean aggregate reads to test whether selective
  dirty tracking repays marking and reset, including concurrent read pressure.
- Extrema repair or multiple fields become the question: the count/sum results
  do not answer those semantics or costs.

The broader [experiment ideas](experiments.md) remain available without a
scheduled continuation. [Sketches, filters, and histograms](../../notebook/secondary-summaries.md)
remain a separate, unexperimented question.

The other outcome is a usable [research workflow](../../notebook/research-experience.md):
one investigation home, independently compiled code, a small default run,
oracle checks, reproducible tables, and source-linked evidence. The next
question can reuse that support and change it where it gets in the way.
