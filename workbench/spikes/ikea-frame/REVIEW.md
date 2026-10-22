# Frame as a substrate for computation

Architectural review, 2026-10-22, by Ikea LEAD with Engine LEAD and three
independent perspectives on Calico, execution and mutation. This expands the
research brief; it does not select an API, physical format or implementation.
No new performance measurements were made.

## Judgement

Frame could be one of SixDB's most consequential components. It determines
whether Engine sees only decoded values, or can exploit the structure of data
throughout a computation. A good Frame can change an operation's work from
rows to distinct values, runs, narrow residuals or a small amount of evidence.
It can also make that advantage survive updates and representation changes.
That is a stronger ambition than supplying a fast ladder of codecs.

The [encoding proposal](ENCODING.md) has good foundations: existing Ikea
packing, mixed block kinds, cheap opt-out, local parameters, logical write
overrides and replica-local representations. Its encoding account is currently
much further developed than its account of the computations these choices make
possible. **Investigate representations and programs for using and maintaining
them together.** Encoding and compression remain the initial emphasis; small
complete consumers should help discover better encodings during that work.

The highest expected returns are preserving structure across operations,
algebra over residuals, efficient masks and sidebands, and changing the physical
form of hot regions. I would spend less early effort on a general kernel
registry, an exhaustive property taxonomy or a learned model that jointly
chooses every field's compression box. Those can consume the project without
establishing useful work savings.

## Frame's place in SixDB

Engine and Ikea agree on the important distinction: Engine chooses database
meaning and complete pipelines; Ikea needs rich reusable operation laws and
implementations to execute those choices well. Frame should understand enough
semantics to exploit an encoding. It should not import Engine's SQL frontend,
catalogue or whole optimiser. Engine's IR is still provisional; there is no
implemented IR to freeze Frame around.

| Concern | Research boundary |
| --- | --- |
| Values and operations | Engine chooses type, NULL, overflow, equality, ordering, hashing and reduction semantics. Ikea exposes applicable implementations and their laws. An unsigned code is not a complete value contract. |
| Structure worth exploiting | Frame describes actual encoding and dependencies, offers operations and conversions, and can return something still structured. Engine can inspect and compose these possibilities. |
| Planning | Engine selects representations and programs against its workload. Frame supplies feasible choices, local analysis and useful costs/facts; an opaque compressor cannot hide all the choices Engine needs. |
| Mutation | Frame performs admitted physical work and can fuse logical before/after contributions. Engine owns membership, dependent indexes/summaries and coordinated visibility. |
| Resources and scheduling | Orbital supplies physical material and coherent local publication; Loom owns readiness, retained execution and scheduling. Frame should expose useful dependencies without embedding another allocator, MVCC system or scheduler. |

These boundaries allow substantial fused Frame operations. They do not require
Engine to reconstruct values solely to maintain a summary outside the kernel.
The existing [composition semantics](../ikea-composition/semantics-and-integration.md)
and [owner integration](../../../ikea/docs/integration.md) already explain that
division.

The logical position domain, occupied-record ranks, field-present ranks,
route-local ranks and byte addresses must remain distinguishable. The
65,536-position bound does not imply 65,536 stored values or initialized slots.
Nor does it settle whether a Frame is its own Object. Engine's starting model
allows Frame material inside a Segment Representation. Independent Frame
Objects are an alternative whose cross-object dependencies and publication
must be priced. Neither the directory nor a column codec settles that choice.

### How much of the representation should Engine understand?

This may be the largest module-contract question. An opaque accessor exposes
too little if Engine cannot discover a better algorithm. Exposing every native
detail would create a different burden. Seek a knee in **description fidelity
and planning effort**, alongside the knee in stored metadata.

The [layout-analyser synthesis](../layout-analyser/design.md) supplies a better
starting point than a list of codec flags: distinguish evidence, reconstruction,
address-discovery, execution-sharing and maintenance dependencies. A category
code, an ordered prefix and a residual against another field can occupy similar
bytes but imply different useful programs. Deep substitutions and operation
expansions should remain inspectable where that enables a better choice;
native leaf instructions can stay opaque.

Separate the actual historical image and its resolved parameters from advice
and bound code. Engine can retain the bytes but rebind an operation, encode new
data differently, or migrate existing data. Those are distinct choices. The
research need not invent a universal serialized IR: in a small candidate universe,
compare opaque accessors, codec/width facts and richer relations/programs against
a manually authored finite reference. Ask which hidden relationship prevented
a useful algorithm, and which extra description merely added preparation cost.
This makes schema fidelity an experiment rather than maximalism by taste.

## 1. Preserve structure across the execution boundary

Ask **what cheaper problem an encoding turns an operation into**, rather than
only which operations preserve order or permit direct code arithmetic.

A frequency-ranked dictionary loses order in its codes. It can still answer a
range predicate: evaluate the predicate on its dictionary, then scan codes
against the resulting truth table. A permitted scalar transform can transform
dictionary entries once and retain the codes. Equal transformed entries do not
force immediate dictionary deduplication. Grouped or selected reductions can
count codes before translating distinct values. Runs offer the same opportunity
at run rather than row grain.

This is more expressive than intersecting stage capability flags. A composition
can introduce a new algorithm, not merely retain or lose an operation. Even the
output matters: constant, dictionary, run, narrow-lane or selection-backed results
can remain useful to the next operation. A compulsory flat full-width result
would give away much of the benefit at Frame's boundary.

There are important alternatives to an elaborate lazy array system: one native
fused consumer, a retained structured result, or reusable scratch may each win.
Current [Vortex execution](https://docs.vortex.dev/developer-guide/internals/execution)
provides a precedent for encodings rewriting or executing enclosing operations;
[DuckDB's vectors](https://duckdb.org/docs/2.0/internals/vector) preserve constant
and dictionary structure while supplying a generic fallback. These establish
the opportunity, not a reason to copy either implementation architecture.

The research question is where structure remains useful long enough to repay
its tracking and binding. Compare predicate → transform → reduction, and
predicate → projection → reorder, with identical consumed results. Charge
dictionary classification and query setup when they cannot be reused.

Include a consumer across the module boundary: a Frame code/selection stream
feeding Engine grouping/hash work, or selected prefixes/lengths feeding a
StreamPack payload projection. This tests native reuse, retained structure and
coordinate identity after the result leaves Frame. It does not require building
a whole query engine or moving string codecs and joins into Frame.

Semantic conditions are part of this opportunity. Evaluating a function on every
dictionary entry is unsafe if it can raise on unused entries: a dictionary
`[0,1]` with only code 1 selected must not make `1/x` fail at the unreferenced
zero. Totality, referenced codes, validity and selected population can determine
whether lifting is legal. Cheap native bodies need not carry rich semantic
objects; binding must know the relevant law.

## 2. Explore algebra that changes which encodings are attractive

Prediction offers more reduction algorithms than telescoping a sum of deltas.
For previous-value coding, write the reconstruction as

```
x[i] = a + i*b + sum(s[j], j=1..i),  i=0..n-1.
```

Then

```
sum(x[i]) = n*a + b*n*(n-1)/2 + sum((n-j)*s[j], j=1..n-1).
```

A SUM can be a weighted narrow residual reduction instead of a prefix decode.
For selected rows with bits `m[i]`, the anchor weight is `sum(m[i])`, the slope
weight is `sum(i*m[i])`, and residual `s[j]` has weight
`sum(m[i], i=j..n-1)`: the selected suffix population. Residual exceptions receive
the same weights. For logical overrides, reduce the base on selected rows
**excluding** overridden positions, then add the selected override values. This
avoids reconstructing replaced base values solely to subtract them during a read.
This removes output demand, not predecessor dependencies: a residual at an
excluded or overridden position can still contribute to later selected base
values and keeps its corresponding suffix weight.

Another candidate follows directly from Local residual bit planes. Before
exception/override handling, with `M` in the tile's original-position coordinates:

```
selected_sum = base*popcount(M) + sum(2^b * popcount(plane[b] & M), b=0..K-1).
```

It may avoid transpose, lane reconstruction and widening altogether at low K.
This is especially worth testing against the now-improved NEON Local decoder,
as well as GFNI/AVX paths. It is not automatically faster than decode-and-sum.

These are algebraic hypotheses, not measured kernels. They hold for a declared
nonwrapping integer reconstruction, or for the corresponding modular answer.
An exact wide sum of individually wrapped reconstructed values requires wrap
corrections. Floating rounding and signed-domain rules need separate treatment.
For ordinary linear prediction `x[i]=a+i*b+r[i]`, the residual sum has a simpler
unweighted form; forcing every predictor into one chained/checkpoint model would
miss that advantage too.

The broader question is whether Frame can expose a small set of useful
decompositions—counts, weighted residuals, histograms, moments and corrections—
which Engine can compose without demanding full values. This need not become a
symbolic algebra framework before any kernel earns its place.

## 3. Make masks and sidebands central to the performance question

Calico's [aggregate implementation](../../../../calico/frame/include/frame/aggregate.h)
uses its dense SIMD SUM only without a mask; masked SUM walks selected rows
scalarly. Masked or patched MIN/MAX also use scalar merging. Its celebrated
dense SUM result therefore does not establish strong filtered, sparse or mutated
execution. Its [fused decoder](../../../../calico/frame/include/frame/fused.h)
does supply a valuable technique: separate clean runs and keep patch-path live
state from spilling the wide clean reader.

This is a more valuable frontier than another clean decode tournament. Equal
selection or override density can mean long clean runs, entire excluded blocks,
or one awkward row in every native packet. Compare direct bit-plane masks,
dense native masked arithmetic, sparse gathers, clean runs and decode-once
consumers. Exclude overridden base rows from predicates and merge exact override
verdicts; do not charge every clean row for the heaviest sideband path.

Keep record absence, NULL, operation selection and policy exclusion distinct.
An output mask is not automatically a physical read grant. Current
[SeriesPack and TuplePack filters](../../../ikea/docs/filters.md) deliberately
have different inactive-row access contracts. A predicted or routed value may
also depend on other rows or fields. Frame's protection and scheduling envelope
must include those dependencies even when only one output row is demanded.

The ambitious question is whether Frame can retain its clean-path advantage
under realistic filtering and mutation, rather than letting the first mask
collapse encoded execution into a scalar fallback.

Selections themselves can retain useful structure: all, range, runs, bitmap or
ordinals. Compare these where they change the next consumer's work; do not choose
one universal exchange format before pricing conversion and original-coordinate
identity. A range already known from a bound search need not first become a
fully populated bitmap merely to drive a range-aware consumer.

## 4. Separate compression, addressing and query evidence

Calico's symbol elegantly combined three jobs: selecting compression parameters,
locating residuals and answering a query prepass. The jobs need not share the
same optimum. Its recovered [MDL-codebook findings](https://github.com/ashtonsix/calico/blob/475a1bcd1c8cee781bcb607c2a87b4ea42ea8c6a/frame/spikes/mdl-codebook/FINDINGS.md)
show a useful counterexample. In those in-sample 1024-row, 14-field comparisons,
a joint symbol saved roughly 0.05–0.10 bits/row of symbol overhead, but its
constrained residual boxes cost roughly 0.6–6 bits/row versus naturally sized
per-field models. Co-clustering fields did not exploit predictive redundancy.
That result is not a general rejection of shared models or a current SixDB
measurement.

The reusable mechanism is a cheap control stream: classify a small table once
per query, then scan profile IDs or a metadata plane before touching bodies.
It can coexist with independently fitted local bases/widths and optional logical
summaries. A profile might name a recurring operation shape without prescribing
one compression box for every field.

Distinguish three lifetimes of information:

| Information | Example | What should pay for it? |
| --- | --- | --- |
| Required interpretation | Kind, base, geometry, dictionary edition | Every use of those encoded bytes |
| Optional persistent evidence | Conservative bounds, exact counts, summaries | Queries it accelerates and writes that maintain it |
| Derived execution preparation | Dictionary truth table, decoder controls, retained prefix cursor | Binding/query reuse; it need not become durable block metadata |

The metadata knee is consequently operation dependent. The
[Bec256 directory study](../bec256-composition/metadata-findings.md)
halved one metadata form from 1024 to 512 bytes with modest overhead in several
Zen scans, yet alternating empty/full blocks took 0.303 versus 0.109 microseconds.
Once evidence removes body work, metadata *is* the operation. Conversely, larger
hot evidence can earn its cost by avoiding expensive cold-body work; a rule that
summary bytes must be smaller than avoided body bytes is not a cost model.

An imprecise bound is also not necessarily useless. Deleting an extremum can
invalidate an *exact MIN* while leaving the old lower bound safe for pruning;
widening bounds to include a new value remains conservative. Investigate levels
of useful evidence rather than treating every dirty statistic as wholly unusable.
Re-encoding can repay lost precision and restore cheap computation even when it
does not save bytes.

## 5. Put cross-column alternatives beside category routing

A one-column prototype is a useful instrument; it should not become Frame's
architectural limit. Category routing is one way to exploit correlation. Other
realizations can preserve original coordinates and trade route-rank translation
for different dependencies. A conditioned base still needs its category unless
the caller already supplies it:

- A category selects a base, dictionary or affine predictor without moving rows.
- Several columns share a code stream but retain their own dictionaries.
- One column is derived from another with a small residual/exception stream.
- Several columns share a permutation when clustered access repays translation.

Also compare TuplePack value-field groups against separate SeriesPack columns
for point reads, selected multi-field projections and co-updates. TuplePack
should not become only Frame's metadata codec by accident. The
[tuple-layout study](../tuple-layout/viability.md) already shows that isolated
operation costs can choose poor complete mixed read/write plans.

The [FastLanes file-format paper](https://vldb.org/pvldb/vol18/p4629-afroozeh.pdf)
provides a shared-code/different-dictionary example and a partial-decompression
boundary. Those ideas do not require adopting its packing geometry. Calico's
predictive transform, by contrast, paid on a limited telemetry case and incurred
large matrix work elsewhere. Cheap shared codes or conditioned bases deserve to
compete with a complex predictor.

The dependency must remain visible to planning. A narrow dependent column can
lose an isolated projection because decoding first requires other field blocks.
Changing a category can require physical repair to unchanged logical values.
For example, categories `[A,B]`, values `[10,20]`, and routes `A:[10], B:[20]`
cannot decode against a newly changed category `[B,B]`: the first value becomes
20 and the second can overrun its route. The old routing basis and the current
query value are different facts until dependent material converts.

Likewise, part-wide dictionaries need stable interpretation across group-local
rebuilds. Stable value IDs with a separate order view, an immutable base with an
extension, and sorted recoding are serious alternatives within the decided
part-wide scope. Calico's [sorted dictionary](../../../../calico/engine/include/engine/dict.h)
explicitly refused unseen writes. Its
[dictionary fingerprints](../../../../calico/engine/include/engine/dict_fingerprint.h)
also show useful reusable work per distinct value, with an important warning:
stale sidecars can split an exact group. IDs, translations and derived hashes
must share the dictionary's interpretation lifetime.

## 6. Make writable plain regions a deliberate state

The present proposal uses plain blocks as compression fallback and streaming
tails. Give them a second job: a profitable writable state during churn.
Compare the following lifecycle rather than assuming encoded form is always the
state to restore:

```
sealed encoded region → sparse logical overrides → writable plain region
          ↑                                          |
          +──────── reseal when future work repays it ┘
```

This is a candidate, not a mandatory state machine. It could operate on blocks,
predicted sub-blocks or groups. Selective folding could retain unchanged encoded
bodies verbatim, fold only dirty blocks, or thaw a repeatedly changing region.
Private batched edits could append/coalesce before publishing a sorted sideband,
instead of moving a bitmap-ranked array on each scalar insertion.

Forced group re-encoding on exhausted slack risks repeatedly recreating an
encoding the next writes immediately undermine. Byte pressure is one signal;
override read tax, lost pruning precision, conversion cost and retained-reader
debt are others. Engine can choose which debt is worth repaying; Frame supplies
legal transitions and useful operation facts. There need not be a hidden policy
engine in every container.

The [StreamPack findings](../streampack/RECOMMENDATION.md) already show why this
matters: isolated mutation winners did not predict complete mixed-workload
winners, and the reader phase changed cleaning economics. The question here is
whether representation changes can make writes cheap while preserving useful
read gains over a whole phase-changing trace.

Logical mutation can also be a shared computational seam. When before/after
logical values are available, fuse requested contribution or invalidation work
there, across all encodings. A dictionary-code delta is not its logical-value
delta; a physical re-encode has zero logical delta. The existing
[summary composition](../../../ikea/docs/integration.md#consume-effects-and-publish)
provides foundations without requiring a mandatory effects packet per row.

## 7. Exploit representation freedom without conflating the grains

Calico [excluded a native 512-bit layout](../../../../calico/frame/SPEC.md)
despite reported x86 advantages because its stored bytes would vary by ISA.
SixDB does not inherit that restriction. Begin with current SeriesPack's
architecture-oriented layouts; then compare a compact portable representation,
a locally chosen representation and an auxiliary hot execution form. Charge
conversion, simultaneous ownership, warmup, subsequent reuse and retirement.
Replica-local bytes are valuable only when the complete lifecycle wins.

Likewise, the logical 256/4096/65536 geometry does not settle native grain,
mutation grain, physical Part grain or retained-owner grain. A 4096-position
group is 512 bytes at one bit/value or 32 KiB at 64 bits/value, before metadata.
One page-oriented rule can give away compression through padding at one end
and through old-reader co-lifetime at the other.

Current [Orbital material](../../../orbital/docs/material.md)
can retain certified unchanged source bytes alongside fresh bytes. Several Parts
sharing one physical owner still share its retention cost; naming slices does
not make interior pages independently reclaimable. Compare independent hot
groups with compact shared stable material, including metadata and allocator
capacity, rather than equating store damage with copied or resident bytes.

Execution can adapt more cheaply than storage. Guarded versus unconditional
filtering, sparse versus dense traversal, retained cursors and grain changes
need not re-encode data. The [layout-analyser findings](../layout-analyser/findings.md)
show a useful candidate menu does not ensure its selector chooses well. Priors
should propose diverse mechanisms and fit local parameters, with chronological
holdouts and a cheap plain control, not fill an analysis budget with redundant
variants of the same hypothesis.

## Research programme to discriminate the architectures

These are connected questions to explore, not sequential promotion gates or a
cut to a narrow vertical slice. The witnesses should accompany encoding work,
before representation choices inadvertently answer them.

| Question | Competing experiments | What would change our judgement? |
| --- | --- | --- |
| How much schema fidelity buys better algorithms? | Opaque accessors, codec/width metadata and exposed relations/expansions against a manually authored finite reference; keep one image while changing its operation program. | Richer descriptions unlock materially better choices after preparation; preserve those relations without standardizing every internal detail. |
| Does structure survive long enough to matter? | Flattening, fused native decode/consume, retained dictionary/run/constant results, reused scratch; predicate → transform → reduction/projection/reorder. | Work scales with distinct values/runs and complete consumers win after setup; otherwise the simpler fused boundary is preferable. |
| Can reductions change the winning encoding? | Bit-plane masked FOR SUM; weighted predicted SUM; fused prefix-decode/reduce; ordinary FOR and plain controls. Sweep K, mask topology, exceptions and overrides. | Narrow reductions repay weights/corrections and make prediction or bit planes attractive beyond compression alone. |
| What information deserves storage? | Direct descriptors, packed metadata with retained native frames, optional synopsis planes and query-specialised profiles. Include constant/empty/sparse blocks and drift. | Identify knees for complete operations and shared preparation, rather than a universal descriptor-byte minimum. |
| Which correlation mechanism earns its dependency? | Category plus two related columns: routed streams, conditioned bases, shared codes and independent encodings. Include isolated projection and category edits. | Saved work/bytes survive address discovery, dependent projection and maintenance. |
| Can writes change form instead of losing the read advantage? | Forced group re-encode, selective folding, private append/coalesce, thaw/reseal; repeated versus scattered writes and phase changes. | Whole-trace throughput, tail pause and retained memory improve, including cleaning rather than stopping before reserves fill. |
| Which physical ownership earns selective replacement? | Block/group/part and mixed backing, with identical logical geometry and old readers. | Reduced copying survives capture metadata, peak backing, page occupancy and eventual reclamation. |
| How much locality and preparation should Engine retain? | Direct versus actual Loom staging at real descriptor/geometry/body/dictionary dependencies; retained versus fresh bindings. | Cold overlap repays tracking while ready/hot paths retain their direct throughput. |
| How much specialization is useful? | Small fused islands plus shared native consumers versus per-expression fusion and stage interpretation; heterogeneous blocks and operations. | Complete runtime wins coexist with bounded code size, build resources and cheap preparation. |

Preserve Ashton's bar: at least 2× a strong system baseline for at least three
examined operations per family, with encoding/decoding/compression first and at
least Calico parity. Also compare tuned plain/current-Ikea consumers, fused
decode-and-consume and reused-scratch materialization. A codec kernel against a
full SQL query cannot attribute the gain to this seam; a full query comparison
separately asks whether Engine retains the gain. Consume actual results and
match NULL, arithmetic, selection and materialization semantics.

Measure mixed operation chains, construction/analysis, query preparation, writes
through cleaning, conversion and retention. Keep hot/cold conditioning,
dependent latency and independent throughput distinct. Memory includes bodies,
descriptors, dictionaries, slack, shared plans, scratch and retained generations.
Inspect complete assembly and compiler expansion, using the
[existing composition findings](../filter-composition/README.md): selective
sharing can help, while a CPS hop per cheap transform or one specialization for
every axis can lose.

## Arithmetic and ownership cautions for the prototypes

These qualify experiments; they are not the centre of the research programme.

- The proposed `K+8` checkpoint bound excludes arbitrary full-width exception
  contributions. With anchor zero, small ordinary deltas and one patched delta
  `2^40`, a later true checkpoint cannot fit nine bits merely because packed K
  is one. Predictor-specific state and correction handling need deriving.
- The current SeriesPack expression nodes supply bit-window joins and child
  substitution, not general FOR/prediction semantics. Writable semantic nodes
  also need reverse projection, admission and store coverage. Boundlist already
  accepts wide ordered endpoint sources; general prediction need not become a
  new Boundlist search law or require a rename first.
- In-place eligibility depends on kind, aliasing and coordinates. Changing one
  row in a constant/run block is different from replacing an independent packed
  slot; compacted presence and routed dependencies add further conditions.
- `sum_change` is modulo-u64 in its unsigned expression domain. Dictionary,
  null, split and float laws need logical contribution handling. Preserve cheap
  affine fixed-population cases where biases cancel; do not discard valid
  shortcuts or pretend the packed delta is universal.
- A reversible float bit map does not select NaN/zero comparison or hashing.
  ALP round-trip equality does not authorize a differently rounded reduction.
  If native reduction order is fixed within semantic groups, physical routing
  and conversion must preserve those groups or an equivalent answer law.
- Current Orbital implements coherent local Scope/Prepared publication and
  exact-version conversion/adopt. Durable epoch admission, generic Drafts and
  distributed lock projection remain proposals. The
  [write notes](WRITES.md) combine current mechanisms and wider design; they
  must not be used as evidence those future services are implemented. Ikea's
  damage hooks run per local write group, not as transaction-wide preflight.

## Calico evidence worth reusing, and its limits

The review read actual Frame, Engine and kmath sources, not only their indices.
Especially useful are aggregate/patch/fused bodies, dictionary construction and
fingerprints, conservative verdicts and fine summaries, and the unfinished
persisted-mutation backlog. The [survey](SURVEY.md) remains the capability map.

Calico's retired MDL-codebook and prepass studies can be recovered at commit
`475a1bcd1c8cee781bcb607c2a87b4ea42ea8c6a`, under
`frame/spikes/{mdl-codebook,residual-theses,packed-composed,prepass-real}`.
Their fitter comparisons and negative cases are useful hypotheses. The old
prepass's reported cold speedup timed an uncompressed int32 sweep using masks
prepared outside the timer; it is not a complete compressed-query control, and
allocation size alone did not establish DRAM service for each access.

Calico's [backlog](../../../../calico/frame/BACKLOG.md) also records that persisted
PFOR sibling mutation and atomic representation-changing writes were unfinished.
Its fast packed point writes do not establish a solved mutable encoded container.
SixDB's freedom to learn locally and change representations removes constraints,
but does not remove that full-lifecycle problem. Solving it while retaining
structure through execution is where Frame can materially raise SixDB's ceiling.
