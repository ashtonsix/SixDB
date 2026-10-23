# Seams, Frame's internal packages and the research split

Proposal, 2026-10-23; Ashton's review is recorded under
[Decisions](#decisions-2026-10-23). The split follows the stage classification in [STAGES.md](STAGES.md) and the
targets in [MECHANISMS.md](MECHANISMS.md).

## Seams

The classification gives three kinds of boundary:

1. **Body families are fused units.** Fitting, emission, decoding and each
   operation family's leaf law for one body kind live together. Engine admits
   families and an objective; Ikea fits parameters inside them.
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

## Research fronts

These are research fronts, not threads to start at once (see
[Decisions](#decisions-2026-10-23)). Ashton asked to start from the encoding stages, so five fronts own stage
families end to end: the codec, every operation family's law over it, and the
targets at every tier. Two fronts own the shared substrate and instruments,
and one owns analysis. Each front lists what it must disambiguate; none
prescribes answers.

| Front | Owns (packages) | Operation families covered | Target rows | Must disambiguate |
| --- | --- | --- | --- | --- |
| **A. Instruments and baselines** | Workbench tools, peers | All, as measurement | All | Sequential and random bandwidth per tier, one core and saturated, on Zen 5 and Graviton 4; whether the guests expose LLC and DRAM counters; native peers for TurboPFor, FastLanes, ALP, DuckDB (C API), CRoaring, vqsort and a native Polars driver; a float and time-series corpus; the residency harness that runs a cell at every tier with the same trace |
| **B. Execution substrate** | `layout`, `exec` | Movement (points, gathers), selection plumbing, every family's dispatch | Points, gathers, every "run and constant" row | Whether per-block fixed cost can reach about 10 cycles; kind-batched dispatch versus per-block dispatch; coarse-mask grain; staging descriptor → body → dictionary with Loom; adopting Ikea's `dense_output`, `bitset256` selections and `visit_point_reads` |
| **C. Integer bodies** | `body` (constant, runs, FOR, exceptions, packing) | Encode, compare, reduce, search, transform, reorder over these bodies | Encoding, comparison, reduction, reorder rows for FOR and runs | Run SUM and COUNT at their mechanistic floor; byte-lane rounding knee per tier; fused patch decode; encoded results for transforms and compaction; narrow-key sort and partition |
| **D. Prediction and checkpoints** | `body` (predicted), checkpoint evidence | Encode, random access, search, prefix and window scans, SUM | Point rows against TurboPFor, monotone search, delta SUM, prefix scans | Checkpoint density and form against TurboPFor and FastLanes random access; linear and piecewise predictors; delta with exceptions; when predict beats map on time-series and key columns |
| **E. Dictionaries and codes** | `dict` | Selection on codes, grouping, hashing, IN, sort on codes | Code rows, grouped aggregates, hash rows | Sorted, frequency or arrival per workload; preparation reuse and its memory; grouping against DuckDB perfect-hash aggregation; cross-part translation |
| **F. Floats and domains** | `domain` | Encode, compare, reduce for floats and decimals | ALP rows, deterministic float SUM | ALP and ALP_RD bytes and speed against the reference; block versus value opt-out; deterministic aggregation cost; offset binary as always-on |
| **G. Presence, splitting and evidence** | `presence`, `evidence` | Selection, rank ↔ offset, MIN/MAX, pruning | Head-plane rows, rank rows, MIN/MAX, pruning | NULL forms; routing against conditioned bases; integer high/low split; head width and cut; statistics precision under writes |
| **H. Analysis and priors** | `analyse` | Encode | Encode rows | Prior quality and bad-guess detection cost; sampling against BtrBlocks; which cheap part statistics rank programs for Engine's analyser |

### Change support inside Ikea

The full MVCC and journal flow is out of scope. What Ikea must supply to it is
in scope, as research questions for the body and substrate fronts:

- how logical write locks project onto the physical envelope a store dirties;
- publishing damage, updates, aggregate contributions and effects;
- modifying bodies in place versus publishing afterimages applied on read or
  at compaction;
- per-group delta buffers, where a little group metadata marks blocks that are
  not pristine and says where to look instead. This is Ashton's leaning, not a
  decision.

Ikea LEAD's constraints for the substrate and bodies:

- Keep three descriptions distinct: logical read and write dependencies, the
  physical read/store envelope a command needs, and the bytes actually issued
  (damage and effects). Ikea's journals record source-qualified plane spans per
  local write group; they are not a pre-acquired lock set. A lock projection
  must exist before the mutation, as a safe potential envelope or an admitted
  repair plan.
- Preserving stores can read neighbours' old bytes, and permission to store
  does not isolate those reads. TuplePack descriptors default to active-unit
  access; wider native windows need initialised, readable, isolated bytes, and
  kind batching must not assume that proof.
- Re-encoding contributes nothing to logical aggregates, and SeriesPack's
  modulo-u64 `sum_change` is not the general exact logical delta.
- Group delta buffers are tested against fitting in-place repair, a base plus
  coalesced afterimages, and native or plain forms, at equal write counts for
  a few hot rows and for one edit in every block. Price the pristine-read tax,
  correction acquisition, write order, evidence invalidation and the eventual
  fold; no fold is the debt control.

Phase 1's lifecycle traces, oracles and repair laws are the evidence base,
reused without the old MVCC and publication machinery.

### Ordering

Fronts A and B gate everyone else's measurements: without calibrated tiers and
leaves and a substrate near their floor, every comparison repeats phase 1's
problem of measuring slow leaves rather than representations. They start first. This sequences measurement, not scope:
every family and stage stays in the spike.

## Decisions (2026-10-23)

Ashton reviewed this proposal and the direction stands.

1. **Threads grow with confidence in seams.** Phase 2 starts with few
   threads and partitions research further only when a seam is well
   established. The fronts above are the candidate partition, not a fan-out
   plan.
2. **Divide work between Claude and Codex by strength.** Ashton's hunch:
   Claude is stronger on creative solutions to well-defined problems, Codex on
   reliability and wide context. The first division:
   - **Claude (this thread):** front B, the execution substrate, with front C's
     integer bodies as its first consumers. The problem is well defined (reach
     the per-block and per-point floors) and needs new designs.
   - **Codex (Ikea LEAD):** front A, instruments and baselines (peers, tier
     calibration, the saturation and residency harness, corpora), plus the
     change-support questions above, which lean on its phase 1 lifecycle
     context.

   This is a proposal to Ikea LEAD; the other fronts open as seams firm up.
   Front C (integer body leaves) opened as its own Claude thread on
   2026-10-23, after the [substrate probe](substrate/README.md) showed the
   leaves, not dispatch, decide run, packed and selection cells. Its scope
   includes predicate emission over run and packed blocks and masked plane
   sums; front B keeps dispatch, selections, staging and residence.
3. **Downloads are approved.** Datasets over 1 GB use only a small slice
   locally, because disk exhaustion recurs; larger slices run only on remote
   workers through the existing tooling.
4. **Mutation:** as in [change support](#change-support-inside-ikea).

### Collaboration

- Messages to Codex go to Ikea LEAD (Codex thread
  `01a1417e-d5ee-7642-afb6-3d2e6f821396`) with `codex queue`, alongside the
  files in `build/frame-collab/`.
- Engine LEAD owns the analyser that will consume the part expression and
  evidence descriptions; front H and the composed rows in STAGES.md are its
  inputs.
- Each front keeps its findings in its own directory under this spike and
  links them from the [README](README.md).
