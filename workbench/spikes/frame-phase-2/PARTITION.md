# Seams, Frame's internal packages and the research split

Proposal for Ashton's review, 2026-10-23. Nothing here is fanned out yet.
The split follows the stage classification in [STAGES.md](STAGES.md) and the
targets in [MECHANISMS.md](MECHANISMS.md).

## Seams

The classification gives three kinds of boundary:

1. **Body families are fused units.** Fitting, emission, decoding and each
   operation family's leaf law for one body kind live together. Engine sees
   only the family.
2. **Composed choices form the part's expression.** Row order, presence,
   routing, domain, map or predict, and the body family. Engine's analyser
   searches this space; Frame describes each choice's laws and costs.
3. **Evidence is its own account.** Statistics, heads, checkpoints and
   dictionary preparation can be built, dropped and maintained without
   re-encoding bodies, so they are priced like indexes.

Execution crosses all three: one query step runs as one fused operation over
whatever the expression and evidence contain. The execution substrate (how
blocks are dispatched, masked and staged) is therefore a seam of its own,
shared by every body family.

## Proposed internal packages

These are research boundaries for organising phase 2 code, not a public API.
Each reuses Ikea's maintained modules rather than reimplementing them.

| Package | Owns | Builds on |
| --- | --- | --- |
| `layout` | Part, group and block geometry; descriptor planes as columns; headless part schema; block-kind census | TuplePack (descriptors), Intervals (body offsets) |
| `exec` | Batched dispatch over descriptor planes; coarse and fine selections; staging of dependent loads; the operation-family drivers | Bec256 and `bitset256` selections, Loom rings and streams |
| `body` | One fused codec per body family: constant, runs, FOR with exceptions, predicted with checkpoints, coded. Fitting, emission, decoding and per-operation leaf laws | SeriesPack (`dense_output`, `visit_point_reads`), Intervals |
| `domain` | Logical types to the unsigned working domain: offset binary, float map, ALP and decimal conversion; exact arithmetic laws (wide sums, deterministic float aggregation) | kmath-style superaccumulators (Calico reference) |
| `dict` | Part dictionaries (sorted, frequency, arrival), editions, cross-part translation, prepared truth tables and weights | SeriesPack, Hashtable |
| `presence` | Validity forms, NULL compaction and rank, category routing, conditioned bases | Bec256, Intervals |
| `evidence` | Block statistics with precision levels, heads and fingerprints, checkpoint density, zone tables | SeriesPack heads |
| `change` | In-place writes, overrides and slack, compaction and re-encode, plain streaming tail, effects, damage and summary contributions | SeriesPack and TuplePack effects, Orbital owners |
| `analyse` | Priors, sampling, bad-guess detection for block fitting; part and table statistics for Engine's analyser | `body`, `layout` |

The public SeriesPack and TuplePack query surface would sit above `exec`,
once the families show what it needs.

## Research threads

Ashton asked to start from the encoding stages, so five threads own stage
families end to end: the codec, every operation family's law over it, and the
targets at every tier. Two threads own the shared substrate and instruments,
and one owns analysis. Each thread's brief lists what it must disambiguate;
none prescribes answers.

| Thread | Owns (packages) | Operation families covered | Target rows | Must disambiguate |
| --- | --- | --- | --- | --- |
| **A. Instruments and baselines** | Workbench tools, peers | All, as measurement | All | Sequential and random bandwidth per tier, one core and saturated, on Zen 5 and Graviton 4; whether the guests expose LLC and DRAM counters; native peers for TurboPFor, FastLanes, ALP, DuckDB (C API), CRoaring, vqsort and a native Polars driver; a float and time-series corpus; the residency harness that runs a cell at every tier with the same trace |
| **B. Execution substrate** | `layout`, `exec` | Movement (points, gathers), selection plumbing, every family's dispatch | Points, gathers, every "run and constant" row | Whether per-block fixed cost can reach about 10 cycles; kind-batched dispatch versus per-block dispatch; coarse-mask grain; staging descriptor → body → dictionary with Loom; adopting Ikea's `dense_output`, `bitset256` selections and `visit_point_reads` |
| **C. Integer bodies** | `body` (constant, runs, FOR, exceptions, packing) | Encode, compare, reduce, search, transform, reorder over these bodies | Encoding, comparison, reduction, reorder rows for FOR and runs | Run SUM and COUNT at their mechanistic floor; byte-lane rounding knee per tier; fused patch decode; encoded results for transforms and compaction; narrow-key sort and partition |
| **D. Prediction and checkpoints** | `body` (predicted), checkpoint evidence | Encode, random access, search, prefix and window scans, SUM | Point rows against TurboPFor, monotone search, delta SUM, prefix scans | Checkpoint density and form against TurboPFor and FastLanes random access; linear and piecewise predictors; delta with exceptions; when predict beats map on time-series and key columns |
| **E. Dictionaries and codes** | `dict` | Selection on codes, grouping, hashing, IN, sort on codes | Code rows, grouped aggregates, hash rows | Sorted, frequency or arrival per workload; preparation reuse and its memory; grouping against DuckDB perfect-hash aggregation; cross-part translation |
| **F. Floats and domains** | `domain` | Encode, compare, reduce for floats and decimals | ALP rows, deterministic float SUM | ALP and ALP_RD bytes and speed against the reference; block versus value opt-out; deterministic aggregation cost; offset binary as always-on |
| **G. Presence, splitting and evidence** | `presence`, `evidence` | Selection, rank ↔ offset, MIN/MAX, pruning | Head-plane rows, rank rows, MIN/MAX, pruning | NULL forms; routing against conditioned bases; integer high/low split; head width and cut; statistics precision under writes |
| **H. Analysis and priors** | `analyse` | Encode | Encode rows | Prior quality and bad-guess detection cost; sampling against BtrBlocks; which cheap part statistics rank programs for Engine's analyser |

Mutation stays with the phase 1 lifecycle owner (Codex, Ikea LEAD) for now:
`change` has substantial executable work already. Threads C to G each charge
their body's write and repair laws through that owner rather than starting a
second lifecycle study.

### Ordering

Threads A and B gate the others' measurements: without calibrated tiers and a
substrate near its floor, every comparison repeats phase 1's problem of
measuring overhead. They start first. Threads C to H can start their codecs,
laws and correctness checks at the same time, then time against the substrate
once its first cut lands. This sequences measurement, not scope: every family
and stage stays in the spike.

### Collaboration

- Codex (Ikea LEAD) owns lifecycle and has the phase 1 scan and selection
  harnesses. Thread B should consult it on the substrate before building, and
  Codex's unfinished predicate(A) → payload(B) panel is the natural first
  workload for B's measurements.
- Engine LEAD owns the analyser that will consume the expression and evidence
  descriptions; thread H and the composed-choice rows in STAGES.md are its
  inputs.
- Each thread keeps its findings in its own directory under this spike and
  links them from the [README](README.md).

## Decisions for Ashton

1. Whether this split by stage family (plus substrate, instruments and
   analysis) is the partition to fan out, or whether some threads should merge
   or split.
2. Approval to download and build the missing native peers on the workers
   (TurboPFor, FastLanes, ALP, DuckDB's C library, CRoaring, vqsort, a Rust
   Polars driver), and a float and time-series corpus.
3. Whether mutation stays with Codex's lifecycle work or gets a phase 2 thread.
