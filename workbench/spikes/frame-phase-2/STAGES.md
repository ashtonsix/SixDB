# Which encoding stages are fused, and which are composed

Proposal, 2026-10-23. It classifies every stage in Ashton's pipeline by who
should make the choice and how expensive the choice is to undo. The
classification sets the seams in [PARTITION.md](PARTITION.md). Where phase 1
has no evidence, the row says so; those rows are research questions, not
decisions.

## The test

A stage choice is **fused** when Ikea can make it from the block's own values
(plus priors), every alternative offers consumers the same operation laws, and
the alternatives do not trade costs the workload cares about. Engine never
needs to see it; the encoder fits it and the decoder fuses it.

A stage choice is **composed** when the best option depends on something the
block's values cannot tell Ikea:

- the workload: access pattern, query mix, write rate, reuse horizon;
- context beyond the block: other columns, other parts, row order;
- policy: memory budget, retention, latency targets.

Composed choices are Engine's. They usually change which operation laws exist
(sorted codes allow range predicates; prediction removes direct comparison),
so a planner has to know about them.

Between the two is a **weighted** class: the choice depends only on the data,
but it trades bytes against scan, point, fitting, write or repair cost. Same
laws are not enough to hide such a choice (Ikea LEAD's review). Engine supplies
the admitted candidate set and an objective (a few weights: bytes, scan cost,
point cost, write cost); Ikea fits parameters inside them. Engine need not
choose each block's base or width, and it must not switch on a child's private
kinds either. Ashton's "presets" are named candidate sets and weight vectors,
not fixed stage lists.

Choice ownership is separate from execution fusion, and from what Engine must
be able to see. A family that Engine sees must still expose the properties on
which legal operations and update isolation depend. The useful separation from
phase 1 stays: logical laws, physical image and parameters, proved facts,
prepared operation, empirical costs.

Stickiness is the second axis. Some choices could be changed without touching
the body (derived evidence such as statistics and duplicate heads; checkpoints,
if the layout keeps them physically separate, which phase 1 did not establish:
its checkpoint mode changed body size and fitting). Others need a block, group,
part or whole-table re-encode. Sticky composed choices are
the consequential ones for Engine's analyser; cheap composed choices behave
like indexes Engine can build and drop.

## Classification

| # | Stage choice | Class | Decided by | Undo cost | Phase 1 evidence | Still to disambiguate |
| --- | --- | --- | --- | --- | --- | --- |
| — | Row order and clustering | Composed | Table workload, all columns | Table | Largest byte lever: 179 bits/row original, 294 shuffled, 238 reclustered ([row order](../frame-phase-1/encoding/README.md#row-order-in-real-bodies)) | How fast inserts erode it; whether Frame needs order-repair operations |
| 0a | Block fitting: kind, base, width, exceptions | Fused | Block values + priors | Block | Real bodies sit 0.3–10 bits/value above the oracle model, mostly run geometry and hash literals | Cost of priors and bad-guess detection; never measured against peer encoders |
| 0b | Part/table analysis that informs Engine (distincts, runs, order sensitivity, candidate bytes) | Composed input | Engine's analyser consumes it | None (derived) | Census exists but was never tested as a planner ([inventory](../frame-phase-1/INVENTORY.md#have-we-found-the-metadata-knee)) | Which cheap statistics rank programs correctly |
| 1 | Constant block | Fused | Data | Block | Dominant: 41% of ClickBench blocks, closed forms for every aggregate | Execution only: descriptor-column checks reach 0.2–0.6 ns per block |
| 1 | Runs vs packed | Weighted | Data; writes break runs | Block | COUNT closed forms win big (UserID 42 vs 460 µs); SUM over a median 24 runs per block cannot beat native u8 (see below) | The run-count crossover per operation, with leaves at their floor |
| 2 | Offset binary for signed | Fused | Always (one XOR) | None | Free under FOR; only the sum law needs a bias term | Nothing; Ashton's original "only where order survives" can become "always" |
| 2 | Decimal-as-float → integer; ALP (e,f) with round-trip check | Fused | Data (lossless check) | Block | None. Jena used pre-scaled integers | Everything: bytes, decode cost, exception rate, block vs value-level opt-out |
| 2 | Float map (lossless monotone) | Fused | Fallback when ALP fails | Block | None | Same |
| — | Float aggregation semantics (deterministic order, superaccumulator) | Composed (logical law) | Query semantics | None | None | Cost of exact/deterministic SUM per representation |
| 3 | Category routing, fused with null elimination | Composed | Cross-column dependency; writes to the category column | Part | Byte model only | Read gains vs the write coupling; conditioned bases as the alternative |
| 3 | Presence form: dense with fill vs compacted with rank index | Composed | Null density and point vs scan mix | Part | Predictor fill is free in bytes, 3.6–7.6× slower to encode; compaction pays only in NULL-heavy packed blocks (model) | Sparse validity form is unbuilt |
| 3 | Fill value inside a dense block | Fused | Data | Block | Zero fill sometimes reads faster after updates | Fold into the fitting objective |
| 3 | High/low split for compression (ALP_RD, jittered timestamps, random suffixes) | Fused | Data (per-bit entropy) | Block | None for integers | Which integer columns pay |
| 3 | High part kept as a separate head or fingerprint plane for filtering | Composed (evidence) | Filter workload | None (derivable) | Logical H16 3.4–4.8× faster on Jena filters, loses on RegionID; costs 2 bytes/row | Head width (8/16), cut point, and maintenance under writes |
| 4/5 | Map vs predict | Composed | Operation laws differ (equality, order, grouping vs search, closed-form SUM) | Block, but tied to the part dictionary | Delta almost never wins on ClickBench; wins on Jena | Needs time-series and key columns to decide fairly |
| 4 | Dictionary order: sorted, frequency, arrival | Composed | Range vs equality workload, cache budget | Part | Arrival 43.9 vs sorted 50.9 bits on URLHash, 1.3–3× slower scans; frequency COUNT up to 1.45× slower | Per-workload selection rule |
| 4 | Dictionary scope beyond one part; translation | Composed | Cross-part grouping and merging | Part | Decided part-wide; cross-part untested | Translation cost vs stable fingerprints |
| 4 | Dictionary preparation (truth tables, weights) | Composed (cache) | Reuse horizon, memory budget | None (derived) | Full cache can reach 2× the column's bytes; makes repeated equality much faster | When a retained preparation pays |
| 5 | Predictor kind: none, previous, linear | Weighted (inside Engine's admitted set) | Data, plus point and repair cost | Block | Previous helps Jena; linear untested | Linear and LeCo-style pieces |
| 5 | Category predictor | Composed | Cross-column | Part | None | Same questions as routing |
| 6 | Checkpoint density (every 16, 64, none) | Composed (evidence) | Point vs bytes | None if rebuilt from the body | Jena 147 / 139 / 133 / 128 bits/row; point 13–17 ns with, 27–32 ns without | Density as a droppable index; vs TurboPFor random access |
| 6 | Checkpoint form (sizes vs relative ends; exceptions in or out) | Weighted | Data, plus point cost | Block | Analysed, not measured | Exception handling cost |
| 7 | Frame of reference base, width, exception budget | Weighted (Ikea fits inside the admitted objective) | Data + priors; reserve trades against later writes | Block | Exact; tile geometry makes 256-row planes bit-tight | Priors vs exact fitting cost |
| 7 | Block-level vs part-level base when heads exist | Weighted | Data, but head meaning depends on it | Block | Analysed only | Folded into the head question |
| 8 | Encode-time exceptions (patches) | Fused | Data | Block | Exceptions are cheap insurance on real columns | Fused patch decode (Calico was 1.9–4.7× slower unfused) |
| 8 | Write-time overrides, slack and compaction threshold | Composed (policy) | Write rate, reader ages | Group | Owner lifecycle works; update-dominated cells lose to native | Slack sizing, read tax by override density |
| 9 | Packing: Local vs Striped, tile height | Fused | ISA and consumer grain | Block | Layout matters per ISA (NEON transpose study) | Grain under AVX-512 |
| 9 | Width rounding to byte lanes | Weighted | Bytes vs decode cost | Block | Byte-lane run widths 0.45–0.61× SUM time for 0–24% more bytes | Where the knee sits per tier |
| — | Native or plain representation for point-heavy or write-heavy regions | Composed | Workload | Group | Native beats every compressed point reader; native wins update-dominated histories | When to switch, and at what grain |
| — | Group layer and slack for cold parts | Composed | Lifecycle | Part | Not measured | Whether cold parts drop groups |

## What phase 1 settles, and what it does not

Phase 1 is enough to place the integer body stages (constant, runs, FOR,
exceptions, packing, predictor kind) on the fused side, and to place dictionary
order, checkpoint density, heads and presence form on the composed side. In
each composed case it found a real workload-dependent reversal, not just a
small difference.

It says nothing about floats, ALP, integer splits, routing, linear or category
predictors, analysis and priors, or encode throughput. Those placements are
argued from the laws above and need evidence.

Phase 1's speed comparisons between stages are weak evidence for a different
reason than first assumed. The [substrate probe](substrate/README.md) re-ran
phase 1's images on Zen 5 and Graviton 4 with kind-batched dispatch. Batching
saved 0–30% on run- and packed-heavy columns and made some cells slower, so
dispatch was not the main cost. The leaves were: on L2-resident OS, phase 1
spends about 15 ns per block against 2.5 ns for a native u8 array on AVX-512.
Part of that is leaf code (unpacking short planes, per-run loops), and part is
the representation itself: OS has a median of 24 runs per block, and no run
leaf can sum 24 runs much faster than AVX-512 sums 256 bytes.

So the stage classification above rests on laws and byte results, which
survive this. Speed comparisons between stages need leaves near their floor,
priced per operation: runs beat packed for COUNT and bounds (UserID COUNT is
31× the native array) and lose for SUM at OS's run counts.

## Consequences for the seams

1. **Body codecs are one fused unit per body family.** Fitting, emission,
   decoding and every operation's leaf law for a body kind belong together.
   Engine admits families and an objective and sees the properties legal
   operations depend on; Ikea fits the parameters.
2. **Composed choices form a small expression per part.** Roughly: row order
   (table), presence ∘ routing ∘ domain ∘ (map | predict) ∘ body family, plus
   a set of evidence planes. That expression is what Engine's analyser
   searches over. It is a planning description; execution still runs one fused
   Ikea operation per query step.
3. **Three accounts, not one.** (a) Indispensable interpretation: routing
   basis, dictionary edition, predictor anchors, and a split's high plane,
   which is payload. (b) Optional evidence: statistics, duplicate heads, and
   checkpoints if they are made separable; costed like indexes (bytes, build,
   maintenance under writes). (c) Query preparation, such as a predicate truth
   table. Their invalidation and lifetime laws differ. Evidence semantics must
   be available before suffix acquisition, and inlier bounds are never
   all-value pruning bounds ([residual bounds](../../notebook/pfor-bounds.md)).
   (Ikea LEAD's review.)
4. **Execution fusion is orthogonal to choice ownership.** Encode-side
   (residuals → outlier classification → pack) and decode-side (unpack → base
   → patch → consume) chains fuse whatever chose their parameters. Phase 1's
   inventory reached the same conclusion
   ([which things stay together](../frame-phase-1/INVENTORY.md#which-things-should-stay-together)).
