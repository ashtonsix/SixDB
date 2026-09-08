# Experiments to distinguish the choices

Possible studies for the [design question](design.md). The concluded
[executable probe](probe.md) took selected comparisons from this list;
[findings](FINDINGS.md) record what was tested and useful reasons to return.
Untested possibilities remain follow-up ideas, not closeout requirements.
The numbering is not a required sequence.
Keep the first probes small and independent of a production storage layout,
codec, query planner, or transaction protocol. Extend only where a result needs
explanation or another mechanism could change the choice.

## 1. Establish the evidence contract

Build an independent scalar row oracle plus a small Boolean expression model.
Generate exact flags, field tags, shared masks, ordered prefixes, and bins from
the logical rows. Check `L ⊆ true(query) ⊆ U` after every stage, final exact
answers, and every claimed empty block. Validate complete row masks, not only
counts or checksums. Enumeration of small domains should accompany randomized
cases so rare counterexamples do not depend on a lucky seed.

Useful counterexamples for implementations claiming the relevant semantics include:

- `(A AND B) OR C` where `A` fails but `C` is true; a CNF clause covered by one
  plane; a shared atom; an unsupported atom inside an OR; a long expression
  that exhausts the compilation budget and uses a sound fallback.
- NULL/missing transitions, NULL under NOT/IN, distinct rows supplying separate
  conjunction atoms, and same-element versus separate-element set predicates.
- One failed atom among many true atoms; deliberately colliding hash values;
  empty/saturated shared masks; empty and large IN sets; an OR of bin covers.
- Range endpoints inside one bin, endpoints exactly on boundaries, empty/full
  ranges, repeated quantiles, and out-of-training-range values. Check exact
  acceptance as well as rejection, especially strict versus inclusive bounds.
- Row counts around SIMD/tile tails; empty blocks and partial incoming masks;
  physical moves, slot reuse, split/merge, stale metadata, and changed editions.

A small mutation model should hold readers across inserts, updates, deletes,
boundary refresh, and block rebuild. Model dirty bypass and publication orders
explicitly; missing/pending metadata must not manufacture a negative result.
This is a semantic model, not a proof of a concurrent C++ or recovery protocol.

## 2. Separate information quality from physical planes

Begin with two controlled families, using exactly the same data and queries
for every arm:

1. Eight uint32 scalar fields with four tag bits each: keep all 32 bits fixed
   while arranging them as one 32-bit plane, two 16-bit planes, four byte planes,
   field streams, and a bit-sliced control. Vary which fields share a plane.
   Row-survivor sets must agree after consuming equivalent information.
2. One ordered 32-bit field: compare the same value bits as `32`, `16+16`,
   `8+8+8+8`, and `8+8+16`, plus native exact data. Use one logical query across
   every block; do not silently choose a different query from each block's
   quantiles. Preserve boundary refinement and early acceptance.

First count survivors and nonempty execution groups after each stage. Then
price actual scan/probe paths with immediate, bounded-buffer, and bitmap
handoffs. Keep reconstruction of selected rows as well as full-block decoding
controls. A sparse-mask win against an unnecessarily full decode is insufficient.

The following small contrast set should expose several different mechanisms:

| Contrast | Question isolated |
| --- | --- |
| Absent equality; two independent absent equalities; all-but-one atom true | Is rejection governed by failed atoms rather than query length? |
| Same atoms in AND, `(A AND B) OR C`, and a multi-clause CNF | Does whole-fragment evaluation save work, and when does an OR branch dominate? |
| Same values randomly permuted versus clustered | Do identical survivors cause different later-plane and payload group visits? |
| Same selectivity with broad prefixes versus long prefix ties | Does a range refine early, or merely defer most of its work? |
| All rows versus sparse random/contiguous incoming masks | Does signature scanning still beat direct candidate probing? |
| Count/mask output versus same-field and other-field projection | Does output demand consume the data that filtering tried to avoid? |

Other comparisons can vary the information budget: 4/8/16-bit scalar
tags, exact flags, coarse ordered bins, and 8/16/32-bit shared masks. Include
asymmetric allocations to expensive versus cheap residual predicates. A
larger text/set signature is a useful reference: long documents may defeat
every tiny 8/16/32-bit option. Equal-footprint and equal-precision comparisons
answer different questions and should be reported separately.

## 3. Test rollup efficacy before building a hierarchy

Compare no rollup, shared-mask OR, fixed-bit AND/OR bounds, marginal code
presence, joint byte-pattern presence, small pattern lists, and min/max or bin
occupancy where applicable. Positional/transposed bitmaps are a control for
the cost of retaining row correlation. Use 16/64/256 rows and charge each
representation's actual bytes, including variable-size overflow handling.

Include pairs of datasets with identical marginal summaries but different
within-row conjunctions; add independently varied cross-plane correlation.
Vary code diversity, repeated/hot values, tokens per row, query IN-list size,
and clustering. Compare the empty-block ground truth with both row-signature
candidates and rollup candidates. This separates unavoidable true matches,
row encoding collisions, and correlation/saturation introduced by rollup.

A rollup can lose before any timing: if nearly all blocks pass, stop refining
its hierarchy design for that workload. For survivors, test one chosen nested
arrangement against flat levels, including blocks where all levels pass.
Report total summary bytes and construction/repair work. Do not label a cached
query-specific result mask as a free query-independent block summary.

## 4. Price residual work and query-wide execution

Add a single expensive text predicate and mixed scalar/text expressions.
Reuse the [accident descriptions](../../datasets/accidents/README.md) and
[ua-parser inputs](../../datasets/uap-core/README.md) where they answer the
question; record query selection and preserve absent, rare, and common terms
separately. These are convenient inputs, not a representative SixDB query mix.
Use a small synthetic text/set generator to control lengths, repeated values,
gram occupancy, and exact match frequency independently.

Compare exact reusable term bits, token/n-gram summaries, length/prefix gates,
and direct exact evaluation. Include query preparation and rotating unseen
terms. Reusing [regexp necessary conditions](../regexp-lowering/design.md)
is an integration question for this probe; no FSST representation is assumed.
Only claim decode savings when a measured codec path actually avoids decoding.

Strong controls should include:

- Tuned direct AoS and direct SoA predicates, with and without sparse candidates.
- Exact narrow/dictionary-encoded column scans where the data admits them;
  maintain just the queried subset as well as the full schema when comparing
  auxiliary copies. Charge each control's actual stored information.
- Progressive reads of existing value planes, avoiding a duplicate signature
  where possible; selected-row and full-block reconstruction.
- Exact materialized predicate bitmaps for reused predicates, dictionary-level
  text evaluation for repeated values, and an inverted/posting path when the
  workload warrants one. Their preparation/maintenance costs are separate axes.
- A fixed factored Boolean plan, a bounded adaptive plan, and direct-data bypass.
  A best-arm hindsight curve can show headroom but is not a deployable selector.

Measure one plane-local CNF fragment end to end against separate atom stages,
including masks, residual obligations, and decoded-state reuse. Compare
algebraic SIMD kernels with an 8-bit decision table and charge plan construction
over one query, repeated queries, and parameter rotation. Extend to shared
multi-query scans or runtime join filters only if this simpler contract pays.

## 5. Does the benefit survive maintenance and drift?

Replay identical insert/replace/delete histories with queries interleaved.
Separate updates to represented fields from unrelated fields. Include hot-row
and uniform writes, growing sets/text, key moves, repeated last-witness deletion,
and moving value/query distributions. Keep data, query, hash, and permutation
seeds independent; changing a term's frequency must not accidentally change
string length or physical placement too.

Compare fixed bins, frozen trained bins, and an explicitly versioned refresh
policy. Contrast dedicated slot replacement with shared-mask recomputation,
counted contributions, and conservative stale supersets plus rebuild. For
rollups, compare eager repair, dirty descent, and immutable editions. Start
with single-thread histories; contention, retained snapshots, and durability
need later models/measurements rather than implied guarantees.

Record initial build cost, steady update overhead, refresh cost/frequency,
temporary peak bytes, retained editions, final maintenance debt, and extra
query work from stale evidence. A useful accounting inequality is:

```
sum over queries (direct query cost - filtered query cost)
    > initial build + extra mutation maintenance + refresh/reclamation
```

All terms concern the same logical workload and final maintenance state.
This model can identify amortization candidates; only a complete mixed-history
timing run measures their sustained payoff. Allow a feature to be absent or
retired when its query reuse no longer repays its maintenance.

## Measurement and useful stopping points

Keep preparation/build, query execution, and mutation/refresh measurements
separate, then combine them in specified workload histories. Report total
rows, true matches, false candidates among nonmatches, per-stage unresolved
rows, nonempty groups, rejected blocks, residual calls/decoded bytes, and
output demand. Count signature, rollup, base, dictionary, scratch, padding,
and retained-version bytes separately.

Candidate counts and address-derived cache-line footprints explain mechanisms;
they are not hardware misses or bandwidth. Name actual hardware/ISA/compiler,
working sets, access order, and warmup. Inspect generated scan kernels and
use PMU evidence where available. Linux/Zen5 is a primary target; local ARM
results apply to that machine, and emulation is for correctness only.

Use [independent study targets](../README.md#building-a-study) and the
[shared runner/retention tools](../../tools/README.md) when code is introduced.
Validate before timing, pin CPUs, and run repetitions sequentially. Keep
per-query/seed variation and paired comparisons; do not choose a universal
threshold from a pooled median or call repetition spread operation p99.
Reserve fresh cases/seeds for any proposed adaptive policy.

The first useful stopping point is a correctness-backed map of information
quality and groups avoided, with explicit losing cases. The next is measured
crossovers for a small set of complete queries. A later maintenance study
can support a physical design choice. None requires selecting a production
planner ABI, global plane width, rollup fanout, or storage layout in advance.
