# Ikea Frame, phase 3

Opened 2026-10-24. Phase 3 decides what `ikea::frame` lands as its first
implementation, and how that implementation grows. This page is the
proposal, revised where the prototypes changed it, and since 25 October the
stage side's consolidation plan ([landing plan](#landing-plan)).
[FINDINGS.md](FINDINGS.md) has the results and [CHECKLIST.md](CHECKLIST.md)
what is decided, evidenced and open.

## The brief

Ashton, 2026-10-24, paraphrased:

- Land a first implementation with a few solid encoding stages: **intervals**
  (delta as sizes, or relative ends) → **bound** (choose the outlier predicate,
  extract outliers, subtract the minimum) → **pack** (SeriesPack). Run a set of
  operations directly over them: sum, count, filter, select and similar.
- Keep RLE, separated leading 8/16 bits and other stages in research. Represent
  them, and TuplePack support, as **placeholders** that shape how Frame grows.
- Make the implementation **extensible**, so new research slots in as stages and
  operations are added. Organise it around **a small handful of key ideas**.
- Serve SixDB's full spectrum of needs, including effects and the MVCC
  stakeholders. Make Frame substantial enough for an Engine analysis module to
  make impactful decisions, and for basic database operations to run end to end.
- Do not leave Frame in research indefinitely.

I read "intervals" as an optional delta stage over any value series, using
Intervals' two forms (sizes with checkpoints, or ends relative to checkpoints),
with the identity form for series that do not benefit. Run boundaries are one
consumer of that stage; ordinary columns are another.

The [owner-side contract and fixtures](owner/README.md) cover canonical pipeline
descriptions, partial binding, retained interpretation and publication. The
[control scorecard](controls/SCORECARD.md) keeps measured comparisons and their
remaining qualification limits together.

## How far phase 2 fell short

Phase 2's brief (2026-10-23) asked for a "how we win" hypothesis for every
encoding option and every planned operation, mechanism-informed targets at
each residence tier against strong baselines, a fused/composed classification
to set the seams, and Frame's internal packages.

| Asked for | Delivered | Missing |
| --- | --- | --- |
| Mechanisms and targets for every stage and operation | [MECHANISMS.md](../frame-phase-2/MECHANISMS.md): 13 mechanisms, about 50 target rows across the seven families | Roughly a dozen rows were measured, mostly against native narrow arrays, warm. Polars and DuckDB controls are functional but were never timed ([polars control](../frame-phase-2/instruments/systems/polars/README.md)), so the headline bar (≥ 2× on ≥ 3 operations per family) is untested. |
| Every operation family | SUM, COUNT, predicate masks, points, short reads, multi-consumer scans, MIN/MAX in leaves | Transformation, reordering, search beyond run lookup, grouping, prefix and window scans, hashing and IN: none measured |
| Every encoding stage | Constant, runs, FOR with exceptions, packing, the bounds stage, run geometry through Intervals, one dictionary probe | Floats and ALP, decimals, prediction beyond delta repair, presence and NULL forms, routing, high/low split and heads, analysis and priors beyond bounds, encode throughput against peers |
| Fused/composed seams | [STAGES.md](../frame-phase-2/STAGES.md) classification and [SEAMS.md](../frame-phase-2/SEAMS.md) contracts | No stage contract exists in code. Each composition is hand glue: [pipelines.cpp](../frame-phase-2/runs/pipelines.cpp) switches on block kind over virtual width arrays. Adding a stage means editing every pipeline. |
| Internal packages | [PARTITION.md](../frame-phase-2/PARTITION.md) proposed nine | None was built. The substrate's shared image instead set SeriesPack aside for a new format and a fixed per-block choice, which Ashton rejected on 2026-10-24. |
| Change support (effects, envelopes, in place versus afterimages, group delta buffers) | One bounded witness: [before-mutation envelopes](../frame-phase-2/change/README.md) for packed/FOR points and fixed-basis delta repair | No write path through bounds or exceptions, no slack, no overrides or delta buffers, no compaction or re-encode, no summaries published with data, no Orbital material for a frame part, no conversion. The group delta buffer comparison that Ikea LEAD specified was not run. |

What did land is real and carries forward: SeriesPack's kernels improved
inside Ikea with the format unchanged, beating `plane256` and native at most
narrow widths ([performance](../frame-phase-2/seriespack/performance.md));
the bounds stage chooses lo and width within 0.5% of the oracle on real
columns ([bounds](../frame-phase-2/bounds/README.md)); run geometry composed
through Intervals, bounds and SeriesPack saves 31–43% of its bytes
([runs](../frame-phase-2/runs/README.md)); Intervals' search became the shared
run lookup ([intervals-search](../intervals-search/README.md)); and the
substrate measured residence, staging, concurrency, group grain and placement
([substrate](../frame-phase-2/substrate/README.md)).

Composability was lost twice for the same reason: each front optimised a
measured cell with whatever glue was quickest, and nothing in code forced a
stage boundary. Phase 3's first job is to make that boundary a contract that
the fast path also uses.

## Starting point

What carries in, what failed, and what phase 3 must establish:

- **Operations compose through linearity and fuse per layout.** SeriesPack's
  iteration found that SUM over a join is the shifted SUM of each field, that
  range kernels fuse traversal with the leaf read, and that an outlined
  expression read cost 7× ([performance §3](../frame-phase-2/seriespack/performance.md#findings)).
  Phase 3 reuses this: stage laws are linear fixups around SeriesPack's own
  range kernels, and nothing outlines the inner loop.
- **Bounds compensation is block-grain arithmetic.** SUM = fields + lo × (rows
  − E) + Σ exceptions; COUNT ≥ t runs the field count against t − lo, then
  fixes each exception; a point reads the field first and searches compact
  exceptions only when the field is 0 ([SEAMS: bounds stage](../frame-phase-2/SEAMS.md),
  [patched points](../frame-phase-2/seriespack/patch/README.md)). No law needs
  to enter SeriesPack's loop, which is what makes generic composition cheap.
- **Driver shape is a cost.** Per-window filter calls cost 3–7×
  ([performance §2](../frame-phase-2/seriespack/performance.md#findings)), and
  per-block virtual calls in the runs pipelines are a likely overhead that was
  never isolated. The prototype measures it.
- **Delta reads are the known gap.** Sizes packed by SeriesPack cost points
  4–15× and SUM 1.4–2.3× against u8 ends, half of it SeriesPack's short-range
  decode ([runs §3](../frame-phase-2/runs/README.md#findings)). The delta
  stage's laws need a fused checkpoint-plus-prefix read, not Intervals' native
  arrays behind a scalar projection.
- **Group grain needs contiguous placement.** 16-block groups cost scans
  1.0–1.7× warm and 1.6–5× flushed when each group is its own allocation, but
  fold 900× cheaper than a whole image ([fold grain](../frame-phase-2/substrate/README.md#fold-and-reclaim-grain)).
  Groups inside one part allocation, with part-wide descriptor columns, are the
  untested fix.
- **Effects are three descriptions.** Logical destinations, issued bytes and
  maintenance dependencies stay separate; a lock projection must exist before
  the first payload access ([integration](../../../ikea/docs/integration.md),
  [envelopes](../frame-phase-2/change/README.md)). Bec256's empty and full sets
  share zero-byte bodies, so damage must carry interpretation
  ([views findings](../orbital-views/FINDINGS.md#composition-evidence)).
- **Representations of one Object must be logically identical and convertible,
  with deterministic writes** (Ashton, 2026-10-22). Orbital's `adopt` replaces
  a version's physical alternative without changing logical time
  ([Orbital](../../../orbital/README.md#embedded-objects)), which is what a
  group re-encode is.
- **Phase 1's geometry and write rules stand as candidates**: 2^16-row parts,
  4,096-row groups, 256-row blocks, in-place writes when a value fits, slack,
  forced compaction, streaming re-encode as the same operation
  ([ENCODING.md](../frame-phase-1/ENCODING.md#geometry), [WRITES.md](../frame-phase-1/WRITES.md)).

## Proposed architecture: five ideas

### 1. Stage: one transform, one owner

A frame is a tree of stages whose leaves are existing Ikea containers.
Each stage maps a parent series to one or more child series and owns
everything about that mapping:

- **fit**: choose per-block parameters from the values, Engine's admitted set,
  its objective weights and a prior;
- **emit**: produce the child series and the stage's side data (exceptions,
  checkpoints);
- **decode**: reconstruct parent values for any row range from the children;
- **laws**: optional rewrites of an operation on the parent into operations on
  the children plus block-grain arithmetic (idea 2);
- **change**: classify a logical write as absorbed, redirected or refused
  (idea 4);
- **accounts**: which parameters are interpretation (needed to decode), which
  are evidence (droppable) and which are preparation (per query), following
  [STAGES.md §3](../frame-phase-2/STAGES.md#consequences-for-the-seams).

Leaves are SeriesPack (one series), TuplePack (several fields sharing records)
and Bec (presence). Frame invents no body format; a new body is a new or
improved Ikea module.

| Stage | Shape | Status in the first landing |
| --- | --- | --- |
| `intervals` | 1 → 1, same rows; forms none, sizes (Gn checkpoints), relative ends | Lands |
| `bound` | 1 → 1 plus exceptions; lo and width per block | Lands (from [stage.h](../frame-phase-2/bounds/stage.h)) |
| `pack` | leaf; SeriesPack preset and width per block; width 0 is a constant block with no body | Lands |
| `runs` | 1 → 2, fewer rows: boundaries (themselves `intervals → bound → pack`) and values | Placeholder |
| `split` | 1 → 2, same rows: high head and low suffix; filters read the head first | Placeholder |
| `map` | 1 → 1 with a part-scoped dictionary | Placeholder |
| `domain` | 1 → 1: offset binary, ALP with round-trip exceptions | Placeholder (offset binary may land with signed deltas) |
| `presence`, `route` | 1 → n with a row map (rank, category) | Placeholder |
| TuplePack leaf | several columns → one record leaf | Placeholder |

The placeholders matter because their shapes differ: `runs` changes the row
count, `split` and `route` have several children, `map` holds part-scoped
state, TuplePack is shared by columns. If the stage contract admits all of
them, later research slots in without changing it. The prototype checks that
by building each placeholder's fit, emit and decode.

### 2. Law: every cell works, some cells are fast

Operations form a small vocabulary with a reference definition over decoded
values: `get`, `read`, `select` (gather the rows of a selection), `filter`
(predicate to selection), `count`, `sum`, `min` and `max`, extended over time.
Each (stage, operation) cell has a **decode fallback** that is always correct.
A **law** is an optional faster rewrite for one cell: `bound`'s SUM is the
child's SUM plus lo × (rows − E) plus exceptions; `intervals`' monotone COUNT
is a checkpoint search.

- A new stage works with every operation as soon as it can decode.
- A new operation works over every stage as soon as it has a reference
  definition.
- A law is added, measured and kept or removed without touching other cells.

Execution has two levels. Laws compose at block grain through runtime
dispatch (one call per part and stage), so no stage tree is instantiated as a
template. They resolve down to runs of blocks that share a leaf
specialisation, call SeriesPack's range kernels once per run, and apply
block-grain fixups from descriptor columns. Leaf kernels are compiled once in
Ikea. Critical paths get a **plan** built at bind time from the same tree:
points first, then the delta prefix and head-first filters. That is Ashton's
"only critical paths hand-fused". Prototype 1 found SUM at 0.71–1.38× phase
2's hand glue, COUNT at 1.03–1.88×, and points faster than the glue with a
point plan on Graviton 4 ([findings §1](FINDINGS.md#1-runtime-composed-laws-against-phase-2s-hand-glue)).

### 3. Block: part, group and block, with headless descriptors

- **Part**: 2^16 rows of one column (or one TuplePack field group), the Object
  whose representations Orbital holds. Its stage tree and admitted choices are
  stated once in a headless schema using Ikea's description envelope
  ([interfaces](../../../ikea/docs/interfaces.md#keep-data-headless-and-frame-shared-descriptions)).
- **Group**: 16 blocks, the unit of placement, slack, compaction and
  copy-on-write. Bodies of equal width sit together inside a group, so one
  SeriesPack call covers each width run. In the first owner recipe, a group's
  headless **image** occupies one Orbital Part. Checkpoints use their own row
  domain and image stream; the retained material maps sections to Parts, and
  can co-pack disjoint sections. Readers borrow the arrays their Scope selects
  ([findings §6](FINDINGS.md#6-end-to-end-through-orbital)).
- **Block**: 256 rows, the unit of fitting. Every per-block parameter lives in
  a descriptor column owned by its stage (bound's lo and exception offset,
  pack's width, intervals' checkpoints). Descriptor columns are ordinary
  arrays first; they are series, so a later preset can frame them too.

Coarse selections (one bit per block) are tested before any descriptor or body
address is formed, as [SEAMS: selections](../frame-phase-2/SEAMS.md) requires.
A selection states demand, not read authority: a delta row needs its
checkpoint and earlier residuals, and a packed write reads its neighbours.
Row-level protection is therefore an owner-supplied grant, enforced before
address formation and kept separate from query selections
([checklist A5, B14](CHECKLIST.md)).

### 4. Change: classify, declare, apply, publish

A logical write batch moves down the tree like an operation. Each stage
**absorbs** it (the value fits its parameters), **redirects** it to side data
(an exception slot, a group delta buffer) or **refuses** it (the group must
re-encode). Classification needs only interpretation, so the caller can
**declare** the physical envelope (possible reads, possible stores, effect
slots) before touching payload, as the envelope witness showed. **Apply** uses
SeriesPack's checked setters and Ikea's journal. **Publish** returns three
separate outputs: issued-byte effects, the exact logical contribution to
summaries (SUM and COUNT deltas; MIN and MAX with a precision level), and
evidence invalidations.

A group re-encode (forced compaction, sealing a streaming tail) is a
conversion: same logical version, new physical alternative, Orbital `adopt`.
Fitting is deterministic in (values, admitted set, prior), so replay of
admitted input reproduces a logically identical group.

Writes have a fallback like reads: a stage without a write law refuses, and
the owner re-encodes the group, so every stage supports writes before it has
a write law.

Where write-time outliers live was the main open choice. Prototype 4 compared
residual-domain patches in bound's exception areas with per-group slack
(Ashton's original model), a group delta buffer marking non-pristine blocks
(Ashton's leaning) and immediate group re-encode. Slack costs a seventh of the
buffer's bytes and less read tax at the same outlier write cost; the buffer
only wins hot-row rewrites. **Proposed for the first landing: slack, with
group re-encode when it runs out**; the buffer stays in research
([findings §4](FINDINGS.md#4-writes-exception-slack-delta-buffer-or-re-encode)).

### 5. Description: what Engine sees

Engine never switches on a stage's private kinds. It sees, per part:

- the stage tree, the choices Engine admitted and the weights it set (bytes,
  scan, point, write); presets are named trees plus weights;
- each stage's **capabilities**: which laws exist and which are lost (delta
  loses direct comparison unless monotone);
- **statistics** produced as by-products of fitting: min, max, exception
  counts, run counts, sample distincts, bytes under the chosen and rejected
  windows;
- **cost tables** per stage and operation, per ISA and residence tier,
  calibrated on the workers.

That is enough for an analysis module to choose between real alternatives that
phase 2 already showed reverse with workload: delta or not, the exception
penalty and cap (points against bytes), Local or Bulk (operation mix and ISA),
checkpoint spacing G16 or G64, and later RLE and heads.

## Serving SixDB

| Stakeholder | Needs from Frame | Where the architecture supplies it |
| --- | --- | --- |
| Engine planning | Laws, capabilities, costs, statistics, presets | Ideas 2 and 5 |
| Engine summaries and indexes | Exact logical contributions, extrema with precision, invalidation | Idea 4 publish |
| Engine locks and access control | Lock projection onto physical envelopes before stores; row protections | Idea 4 declare; owner grants separate from query demand |
| Orbital Objects | Part as Object; groups as Parts or page ranges; material Control; conversion | Idea 3; logical updates publish, pure equivalent conversion adopts |
| Orbital Journal | Deterministic writes and fits for replay from admitted input | Idea 4 |
| Loom | Staged dependent loads (descriptor → body → exceptions), bulk windows, leases | Point laws expose a prepare/finish split like Intervals' `prepare_lower_bound` |
| Port | Few bytes; group-local damage | Idea 3 geometry; idea 4 effects |
| Shore | Transcode to and from external formats | Placeholder: `read` into Arrow-shaped buffers |

## What lands first

| Area | First landing |
| --- | --- |
| Module | `ikea::frame` (`include/ikea/frame/`), following Ikea's lifecycle: `layout` (the stage tree), `view::attach` over a part's planes, `validate`, `bind_reader`, `bind_writer`, then commands |
| Stages | `intervals` (none, sizes, relative ends; G16 and G64) with its SUM law, fused checkpoint-plus-prefix point read and framed checkpoints; `bound`; `pack` (SeriesPack Local and Bulk presets; width 0 for constants) |
| Reads | `get`, `read`, `select`, `filter`, `count`, `sum`, `min`, `max`, with laws where measured and decode elsewhere |
| Writes | `set`, `replace`, `append` to a plain tail, `seal` and `compact` as group re-encode; declare, journal, contribution |
| Description | Schema encode/decode, capabilities, fit statistics, cost tables, two or three presets |
| Placeholders | `runs`, `split`, `map`, `domain`, `presence`, `route`, TuplePack leaf: each with its shape, children and parameters stated, and a reference decode in the spike |
| Integration | An Orbital material for a frame part and an end-to-end check through ObjectStore and Loom: bulk load, `SUM(b) WHERE a ≥ t`, points, updates with summaries published, a group re-encode adopted |

## Interface sketch

For reaction, not settled. It follows Ikea's lifecycle
([interfaces](../../../ikea/docs/interfaces.md#follow-one-lifecycle)) so Frame
adds nouns only for its own ideas: stage, law, group, census. The prototype's
`frame.h` is the contract underneath it.

```cpp
namespace fr = ikea::frame;
// Describe: a headless law for a column's parts, stored once in the schema.
auto law = fr::layout::make({fr::intervals{.form = fr::sizes, .checkpoints = 16},
                             fr::bound{.penalty = 2, .cap = 64, .slack = 32},
                             fr::pack{.preset = ikea::seriespack::preset::compact}});
// Build: fit and emit one part into group images (one Orbital Part each).
auto images = fr::encode(*law, values, prior);
// Place: attach to the Parts a Scope selected, borrowing the arrays.
auto part = fr::const_view::attach(*law, rows, parts);
// Bind: laws compose at block grain; plans for critical paths (points).
auto reader = fr::bind_reader(*part);
reader->sum(blocks);                  reader->sum(selection);
reader->count<ge>(blocks, t);         reader->filter<ge>(blocks, t, selection);
reader->get(row);                     reader->read(first, out);
reader->select(selection, out);
// Change: classify from interpretation, declare, apply or re-encode.
auto writer = fr::bind_writer(*draft);
auto plan = writer->classify(row, value);  // absorb, redirect or refuse
plan.envelope();                           // possible reads, stores, records
writer->apply(plan, effects, contribution);
// Describe: what Engine's analysis reads.
fr::census(*part);
```

Coarse `blocks` and fine `selection` are Ikea's existing selection types;
`effects` is `ikea::journal`; `contribution` is the exact SUM and COUNT
change plus statistics precision.

## Landing plan

Ashton asked on 24 October to finish the investigations in flight and move
towards consolidation. This is the stage side's account; Ikea LEAD owns the
description, binding and owner side (CHECKLIST's division of work) and the
module lands in reviewed batches through Ikea LEAD, as SeriesPack did.

**What the stage side lands, from the prototype**
([findings](FINDINGS.md)):

- The three stages with their measured laws: `pack` over SeriesPack's
  compiled-once runtime operations (no width TUs in Frame); `bound` with
  per-group exception areas and slack, the one-pass COUNT descriptor law
  selected per ISA, selected SUM and `summarise_selected` (SUM, COUNT, MIN,
  MAX in one pass); `intervals` in both forms with framed checkpoints, the
  relative-ends SUM law and the monotone COUNT law.
- The decode fallback for every other stage × operation cell, and the write
  fallback (refuse, then group re-encode).
- Execution as measured: block-grain laws composed at runtime, fused per
  part (never a retained column-wide mask), and one bind-time plan, the
  pipelined point plan, with its admission limits.
- Fit choosing among trees per column, including the intervals form. Where
  no tree repays (URLHash), Engine keeps a native plain or narrow column:
  `pack` alone still trails typed native on wide scans and dispersed pairs,
  so it is not that choice.

**Changes from the proposal above.** These are deliberate and Ashton should
know them:

- Delta lands without a SUM law for the sizes form. The masked-sum law lost
  to decoding and stays off. SeriesPack already supplies a position-weighted
  primitive; Frame's composed, exact-wide law is not integrated. Relative
  ends carries delta's fast SUM and points, at a byte cost on some columns,
  so fit picks the form per column.
- Points land 2.6–6.4× behind front A's narrow gather. Smaller entries did
  not help; entries co-located with bodies is the next representation
  experiment, and the plan is reversible (part C of the checklist).
- Loom staging for points is not used: a plain software pipeline in the
  plan beat a 32-slot Ring on both machines.
- RLE, H8/H16, shared TuplePack record/field, presence-rank and split have
  canonical description shapes and explicit unsupported-image refusal through
  Ikea LEAD's descriptions. Map, n-way routing and domain stages remain
  unproven extension cases; no canonical image or executable support is claimed.

**Witnesses on existing programs, with no new experiment matrix.**
Cross-column selected movement (B7) is closed by an exact fixture in
[check.cpp](proto/check.cpp). The Engine choice judged by regret (B11) is the
one witness left: one trace with a held-out workload reversal and a real
native candidate. B10 (scans across groups) stays
reversible after landing. A MAX law that skips blocks from their bounds
(B6) is built and oracle-checked but not timed, and adds no public MAX
binding yet.

**Shape of the first Ikea batch** (Ikea LEAD's side,
`build/frame-collab/phase3/CODEX-CONSOLIDATION-REPLY.md`): the public
description, image validation and immutable checked binding for non-null
u64; the stages behind it as an internal execution layer, not the
prototype's virtual `stage`/`column` driver; placeholders described and
refused, never silently executed; group private-afterimage preparation as
the change route; Orbital material and history as a maintained integration
example and conformance test, so Ikea does not depend on Orbital. Extraction
reuses one implementation rather than copying prototype algorithms.

## Prototypes

| # | Question | Result so far ([FINDINGS.md](FINDINGS.md)) |
| --- | --- | --- |
| 1 | Do block-grain laws composed at runtime match hand glue? | SUM yes; COUNT pays 3–8 ns per block of bookkeeping; points need a plan. Delta's relative-ends form makes SUM 1.1–8× and points 1.4–3.4× faster than the sizes form, at a byte cost on four of eleven columns |
| 2 | Does the stage contract admit every placeholder shape? | Yes for `runs`, `split` and a shared TuplePack record, after seven contract changes (own geometry per series, row-range children, combined child selections, deposit-and-seal leaves, admitted widths, priors from descriptors, capabilities before change) |
| 3 | What does a new stage or operation cost to add? | 60–150 lines in one file each; SeriesPack's compiled-once operations removed Frame's width-specialised kernels at timing parity |
| 4 | Where should write-time outliers live? | Exception slack; every store declared before payload, contributions exact, replay byte-identical |
| 5 | Can fit statistics rank presets? | Ranking right in 18–21 of 24 cells, but on an easy test; absolute costs off by a median 34–83% without residence tiers |
| 6 | Group images as Orbital Parts | Works end to end: older Scopes keep their versions, a write publishes one Part and reuses fifteen, in about 1 µs of publication |
| 7 | Seams against front A's qualified controls | Fusing per part beats a retained mask; clustered predicates beat the native pair 1.9–8.5×, dispersed ones lose up to 3.4×; descriptor COUNT loses 1.5× on two columns on Zen 5; points stay 2.6–6.4× behind the narrow gather |

## Coordination

The bounds stage is owned by the "Improve bounds selection for outliers"
thread, `runs/` by "Frame research phase 2", SeriesPack kernels by "Speed up
SeriesPack operations", and Ikea review by Codex Ikea LEAD. This spike reads
their files and proposes changes through their owners; its code lives here.
