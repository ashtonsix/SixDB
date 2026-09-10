# Operation granularity and curated composition

2026-09-10. Independent investigation requested by Ashton, following the
[first consolidation](synthesis-1.md) and the integer probe. This is a design
comparison grounded in existing code and evidence, not a new implementation,
a selected framework, or a performance result. No kernels were rerun here.
Integer code and the wider-body proposal were still under development when read.
LocalPack and ScanPack are now accepted names.

The new direction is to reduce combinations such as `A × B × C × D` to
`A × B + C × D`, without demanding `A + B + C + D` everywhere. Fine-grained
choices may remain curated inside inlined presets; freer continuation
substitution can occur between larger regions. The observed 16–57% continuation
cost in the integer exercise is too steep to accept as a blanket approach.
This direction is to investigate, not a settled interface or overhead budget.

**Provisional conclusion:** define semantic work over its appropriate domain,
then select regions and compatible native handoffs. Do not make every producer
normalise to a fixed public width before the next consumer can use its output.
Preserve introspection inside compiled regions without promising an indirect
call or arbitrary runtime replacement at every exposed node.

## Evidence that changes the boundary question

| Case study | Observed mechanism or result | Consequence for this investigation |
| --- | --- | --- |
| [Integer composition](../ikea-integers/composition/README.md), [authoring](../ikea-integers/composition/authoring.h), [transport](../ikea-integers/composition/transport.h) | A shared read joins body/tail, then filter and sum reuse sixteen decoded u16 values. Three parent placements share consumers. The scalar mask is packed by filter and expanded by sum, including in the inline executor. CPS costs 16–57% across the reported resident comparisons. | Native value reuse is possible; this does not make a boundary after each leaf economical. Try keeping predicate and consumer together before selecting a mask interchange. |
| [Width-56 follow-up](../ikea-integers/wide56/README.md), [wider extension](../ikea-integers/wider-bodies.md) | Eight-value storage packets are independent of native computation. The measured width-56 sum consumes eight u64 lanes on AVX-512, four on AVX2, two on NEON directly. An optional 32-value encoder region improves encoding by 6–9% over the default on the tested AVX-512 targets without changing stored bytes; other wider codecs remain proposals. | An eight-value packet does not oblige a two-lane native implementation to assemble eight values at every call. Conversely, matching wider producers/consumers should not split merely to honour an intermediate get8/get16 API. |
| [Bitset tiling](../ikea-blocks/tiling.h), [native two-stream decode](../ikea-blocks/native_avx512.h), [findings](../ikea-blocks/README.md) | Plain intersection/union use a native grain over multiple physical sizes. The two-stream decoder supplies one 512-position carrier from two independent 256-position compressed streams. The predictor keeps two independent feature records when widening its execution grain. | A native register can hold several semantic groups. Group boundaries and per-stream metadata survive widening. That differs from combining two logical operands of an intersection. |
| [Conjunctive row-filter findings](../row-filter-signatures/FINDINGS.md), [implementation](../row-filter-signatures/model.cpp) | Preparing and fusing same-plane conjunction masks removed avoidable work. Removing an artificial outer block handoff changed the comparison. Progressive byte planes skipped 34.9% of second-plane groups in one case yet lost to eager reading, 0.937 versus 0.684 ns/row. | Boolean-fragment and decision boundaries matter as much as SIMD lane count. Expose progression where useful; do not require progression bookkeeping for every resident comparison. |
| [Aggregate closeout](../aggregate-maintenance/CONCLUSIONS.md), [dirty-buffer implementation](../aggregate-maintenance/dirty-buffer/probe.cpp) | The buffered experiment amortises reservations over 64 records and replays complete batches. Some hot cases benefit; dispersed cases lose. Reads/replay occur at quiescent boundaries and numeric values are bounded exact count/sum. | Reservation, compute, replay and visibility grains differ. Coarser execution can amortise machinery, but publication and numeric laws restrict regrouping. |
| [Calico Xmem staging](../../../../calico/xmem/include/xmem/xmem.h), [QHash integration](../../../../calico/qhash/README.md) | `stage_many` batches range declarations under one handle lock. Its documented semantics retain input order and possible partial declaration on refusal. QHash's standalone claims miss writes to recycled bucket regions; its host adds coverage before flush. | Effect batching has a real boundary and obligations of its own. A coarse mutation region must preserve complete physical coverage, not just concatenate happy-path results. |

The signature and aggregate timings are local ARM Linux VM observations, not
three-target hardware conclusions. The integer table reports separate target
runs linked in its own README. Xmem/QHash supply prior mechanisms and known
limitations, not adopted SixDB integration contracts.

The integer reader comparison also supplies an execution choice over unchanged
bytes: different reader lowerings favour different measured hot/cold regimes.
See [measurements](../ikea-integers/measurements.md#two-readers-over-the-continuous-wire).
Wire legality, parent eligibility and contextual execution cost remain separate.
This investigation does not infer a cache classifier from those results.

## Several quantities are being called width or grain

Keep these distinguishable without necessarily giving every quantity a runtime
field or a public type:

| Quantity | Example / meaning |
| --- | --- |
| Value domain and precision | Unsigned 12-bit values, reconstructed signed values, bit positions, or approximate evidence. Changing native lane width does not change this meaning. |
| Requested logical coverage | Values or positions in `[begin,end)`, possibly with an active selection. The outer request can be much larger than one native call. |
| Indivisible semantic/dependency group | A format's independently decodable block, a predictor restart group, or a consumer that genuinely needs 32 values together. Not every operation can be split arbitrarily. |
| Physical packet and placement | Eight packed values, separate head plane, byte stride, alignment and accessible extent. |
| Native compute/transport grain | Sixteen u16 lanes in YMM0, two Q registers, or eight u64 lanes in a ZMM. Equal byte size is not equal value meaning or ABI compatibility. |
| Decision/observation grain | A 64-row rollup can reject work whose exact comparisons run sixteen or four rows at a time. |
| Scheduling/publication grain | A leased buffer, an append reservation, or a transaction's completed effects. These do not follow automatically from vector width. |

The distinction is not just dimensional bookkeeping. The bitset predictor
must not reinterpret two 256-position feature records as one 512-position
statistic. An aggregate may allow two partial sums to combine; a sorting or
prefix operation may have cross-group dependencies. A get16 endpoint can be
implemented from smaller native groups without imposing that shape internally.

Keep an operation's semantic contract stable, including any indivisible groups.
Represent implementation grain and partial support separately. Where the
operation laws permit it, implementations realise the same work over different
extents. Public result normalisation belongs at the actual caller boundary;
mandatory u64 arrays, one-bit masks or a fixed count are not implied inside it.

## Where to spend specialisation

For one concrete discussion, let A denote packing/reader alternatives, B an
ordered reconstruction preset, C a predicate fragment and D its consumer.
Candidate cut:

```text
curated A × B                  compatible native edge       curated C × D
read + reconstruct     -------------------------------->   predicate + consume
      inspectable expansion                         inspectable expansion
```

Examples inside B might include base restoration, delta reconstruction or a
sign transform. These are ordered compositions with applicability conditions,
not freely commuting switches. Delta reconstruction may need a carry and
preceding inactive values; that can change a sensible cut. C × D may preserve a
native predicate for a sum or projection instead of producing a scalar mask
then reconstructing one. A manual fusion spanning both regions remains an
eligible exceptional implementation of the same requested work.

A rough code-count model is `F × (A×B + C×D) + bridges + selected overrides`,
where F is the number of retained compatible transport families. This is an
accounting hypothesis, not a measured complexity or size bound. Carrier roles,
grains and mask forms can make F itself large. Explicit instantiation/link
boundaries must actually prevent an A/B choice from instantiating every C/D
combination; a source template that captures the downstream concrete type can
silently recreate the product. Shared header bodies alone do not prove reuse
of machine code or cheap incremental builds.

Curated presets need not hide structure from Engine. A preset can expose its
expansion and name supported substitutions. A fine edit can select another
already compiled preset, request selective compilation, or remain unsupported
for that execution route. These are distinct from replacing a region by
another existing continuation implementation. An inspectable node is not a
promise of an independently compiled stage.

Merely renaming a small leaf sequence a region does not amortise its handoff.
The region must remove a costly cut, perform useful work per invocation, or
exploit a better carrier. Processing more native groups per call may help, but
must not force their results into a large temporary just to reach the consumer.
The measured ratios do not isolate a fixed dispatch cost from lost inlining,
mask conversions or register allocation, so they cannot select a universal
minimum batch size.

## Three execution sketches to compare

Names below are pseudocode roles, not a proposed library. All retain explicit
native bodies and a separate cold description of supported implementations.

### A. Curated regions with typed native handoffs

Preparation chooses a small compatible family for a read/reconstruction region
and predicate/consumer region. Constants, owners and source mappings are bound
once. The authored expansion remains ordinary calls to supported operations.

```cpp
// Inside a selected native preset; these are explicit target-native bodies.
auto values = reconstruct_native(read_native(source, position), carry);
auto selected = compare_native(values, bound_predicate);
consume_native(values, selected, accumulator);

// When cutting between reconstruction and the predicate/consumer preset:
// ... musttail next(cursor, bound_parameters, position, count, native_values);
```

The bottom line describes a compatible family, not a C++ variadic calling
convention. Native operands must occupy a real agreed function signature.
Position/count can be implicit or hoisted where the particular family fixes
them. Lifetime owners stay in the outer driver; no shared ownership operation
is required on each hop. A selected bridge handles a genuine mismatch.

**Strength:** matching-grain edges can be direct, and high-value combinations
stay inline without compiling every end-to-end plan. The current integer
exercise already supplies a small fixed-family example, although its cut before
and after scalar-mask production is questionable.

**Attack:** carrier families and bridges may proliferate. A 32-value consumer
following two 16-value productions needs someone to retain the first result.
An opaque second producer may clobber its registers. Tail calling alone does
not solve this: preserving values through that producer changes its transport
family, or forces storage. Curated produce32/consume32 regions might be cheaper
than a universal regrouping mechanism.

### B. A consumer-driven chunk/cursor loop

A prepared consumer requests useful work from a source cursor. The cursor owns
progress and storage references, not necessarily the values. Use a visitation
form to make an immediate native lifetime visible:

```cpp
// Conceptual contract; not an erased std::function callback per small chunk.
source.visit_next(requested_capacity, [&](Coordinates where,
                                          NativeValues values, Extent n) {
    consumer.update(where, values, n);
});
```

This could inline inside a selected preset, or use one prepared region boundary.
It must not conceal a virtual per-element iterator or an opaque wide aggregate
return. The requested capacity need not equal the source's natural grain.
The API needs distinct outcomes for end, a supported partial result, and a
dependency/resource boundary; zero active values are not end of source.

**Strength:** a consumer's needs and leftover work are visible. Zipping sources
with different natural grains and draining a final short range read naturally.
An outer loop can choose common coordinates and leave representation details
to the selected readers.

**Attack:** pleasant cursor syntax can conceal the same normalisation and
indirect-call costs as a fine CPS chain. Asking for 32 while a visitor owns only
16 does not grant a lifetime for the first 16 across the next visit. A callback
that parks or retains references breaks an immediate borrow. The ABI and
ownership choices still need the concrete treatment in A; cursor syntax is
not a substitute for them. Let this sketch prove an ergonomic advantage in a
two-source or partial-consumption case before introducing cursor infrastructure.

### C. Regions exchange explicit stored chunks

A region fills an owned/leased chunk; consumers borrow slices and keep progress
until done. If the data already exists in a suitable physical form, a chunk may
be a view rather than a newly decoded buffer. Decoded values that were only in
registers must be stored explicitly to obtain this lifetime.

**Strength:** mismatched grains, delayed fanout and suspension have straightforward
ownership. Expensive variable-width work or already materialised results may
justify the boundary. It also gives a useful explanatory benchmark control.

**Attack:** making this the obligatory interchange reintroduces the very stores
and loads the native probes avoided. Chunk sizes, allocation and backpressure
become work. It is a supported operating region or explicit fallback to price,
not a way to declare every composition cheap.

A and B may share machinery. C is not automatically a failure: crossing a real
lifetime boundary can require storage. The question is whether a simple local
edit is forced into C unnecessarily, and whether supporting it in A costs an
unreasonable number of compiled variants.

## Work the grain mismatches through

Use an ordered sequence of 37 values to expose a final partial group; this is
a thought experiment, not a new measured workload. A source producing up to
16 gives extents `16,16,5`. An active mask selects within each extent without
changing those coordinates unless an explicit compaction operation does so.

| Case | Direct possibilities | Obligation and cost to expose |
| --- | --- | --- |
| 16 → 16 | Hand the native carrier straight to a compatible consumer preset. | No detour through get8, u64 output or a canonical mask. A matching count still needs matching value meaning, lane order and native ABI. |
| 16 → 8 | An inline adapter feeds the low/high groups, or a consumer preset accepts a pair of eight-value updates. The 37-value trace consumes `8,8,8,8,5`. | Slicing metadata may be free, register extraction may not be. Two opaque calls can force retention of the other half. State whether the consumer processes the halves in order and when progress advances. |
| 16 → 32, decomposable consumer | Call an incremental update twice and finish the logical group, if its algebra permits this. | A completed 32-value result need not require a 32-value register object. Preserve required accumulation order and finalisation semantics. |
| 16 → 32, indivisible consumer | Choose a produce32 region, retain two native fragments in a compatible family, or explicitly gather them into stored state. | Do not call a true32 kernel with16 and pretend count alone repairs it. The final five need a legal partial implementation, a residual route, or an explicit unsupported case. Padding values is not universally neutral. |
| One producer, two consumers | Run both inside an inline region; or preserve the value through compatible immediate continuations. | Consumption by one user does not release the value. A deferred user requires owned storage or legal recomputation. Re-decoding pure input is an explicit cost choice; side effects must not be replayed. |

For all rows, distinguish:

- the logical work remaining in the source;
- the values actually produced and valid in this invocation;
- the physical bytes admitted/readable for the chosen implementation;
- the active selection among valid positions;
- downstream capacity and the amount actually consumed.

Some are constants of a prepared implementation, so this is not a demand for
a five-counter hot context. A native carrier with five valid lanes does not
prove that eight values' backing bytes are readable. A zero selection may
allow a filter/consumer to skip work while a delta decoder still advances its
dependency state. A final partial must not read another segment just to fill
a register. Segment, restart and visibility boundaries constrain gathering.

Progress must be exact: when a producer has delivered16 and a consumer has
used8, identify the owner of the remaining8 and the source position used on
resumption. Early completion has to discharge agreed effects and resource
obligations even if no more values are needed.

Requesting32 source positions also differs from requesting32 selected results.
A sparse filter may consume many more positions to supply the latter. Compaction
changes density and needs a coordinate mapping; it must not silently change
what count, cursor advancement or a mask means at the next edge.

### Multiple sources: union/intersection is not merely two callbacks

The existing [plain bitset adapter](../ikea-blocks/tiling.h) explicitly requires
matching ordered bit coordinates. Make that law survive any cursor or native
port. Logical coordinate alignment, allocation alignment and native-grain
compatibility are different facts.

For A producing256 bit positions and B producing512, operate on matching
subranges while retaining B's unconsumed half, select A's two-block native
producer, or select a region containing both reads and the bitwise operation.
Neither source's next storage block is necessarily adjacent to its previous
one. Each carries its own accessible extents and interpretation metadata.

A joint reader can introduce its own `left layouts × right layouts` compilation
product. That may be a worthwhile curated family, but is not evidence that all
source combinations have been factored away. The alternative of separate
readers needs a carrier/lifetime for the first result across the second read.
Compare these costs explicitly rather than making either arrangement universal.

The Bec two-stream decoder's two populations describe two independently
decoded256-position groups. To produce512 positions of `A ∩ B`, one possible
region combines `A0,A1` and `B0,B1` in the same logical order. Treating an
`A0,B0` decode pair as one512-position sequence and advancing both sources by512
would confuse native packing with logical coverage. This is a proposed stress
case for reuse of the existing native decoder, not a claim that compressed
set algebra was implemented by the first probe.

Masks must refer to the same coordinates before bitwise combination. Compacted
candidate lists need an explicit row mapping; equal counts do not establish
alignment. Sorted integer-set union is a key-merge operation with different
progress rules, not positional bitmap OR. End of a physical source, a known
empty logical range and an unavailable buffer also need different meanings.

## Two less trivial tests of region boundaries

### Boolean fragments and evidence grains

The row-filter implementation prepares one same-plane conjunction test rather
than dispatching once per atom. Joint64 rollups express evidence over64 rows,
while exact/plane comparisons execute in smaller native groups. These are
useful examples of `A×B + C×D`: compile selected Boolean fragments with the
plane/value operations they exploit, then expose a meaningful refinement or
consumer boundary.

A coarse rejection can skip a covered range. A positive rollup does not produce
64 certainly-true row bits. A candidate mask from hashed tags still requires
exact verification. For `(A OR C) AND (B OR D)`, splitting on the wrong plane
boundary can prevent early rejection; for `(A AND B) OR C`, projecting away the
other plane can make every local necessary condition tautological. These are
semantic dependencies, not just dispatch costs.

Compare eager evaluation of a prepared fragment with progressive evaluation
that exposes unresolved obligations. Within either fragment, keep native
predicates native when the next operation accepts them. Export a conventional
bitmap where a caller or retained observation requires one. Metadata bypass
must remain an available plan. A planner observation every64 or256 positions
does not imply an opaque call after each four- or sixteen-lane comparison.

### Aggregate accumulation and effect batching

The [aggregate design](../aggregate-maintenance/design.md#separate-the-aggregate-semantics)
already separates additive count/sum, exact extrema and conservative bounds.
Changing compute grain can regroup arithmetic: bounded exact integer addition
may permit partial reductions where ordinary floating-point accumulation under
a specified order does not. Removing a minimum's last witness is not a negative
min delta. No generic `associative` tag should be inferred from an operation
being called sum or reduce.

The dirty-buffer experiment shows that reservation64, single-record append,
ancestor replay and an epoch's quiescent read can coexist. A query need not call
the buffer once per record just because the append body processes one record.
Conversely, delaying maintenance to a larger batch must retain the visibility
semantics promised by the container call. The existing quiescent experiment
does not establish correctness for overlapping readers and writers.

Treat row-signature repair, aggregate invalidation/deltas and MVCC byte spans
as separate effect consumers at the horizon. They may be batched without being
collapsed into one generic dirty flag. The selected driver must know which
duties are complete and which remain caller obligations at its return boundary.

## Block obligations: tags describe, witnesses discharge

Locality is one block obligation among others. Reflection can name properties
and applicable operations; it cannot make a value-dependent condition true.
The declaration belongs to a contract, while any supporting fact has an owner,
scope and lifetime.

| Kind of fact | Example | What it can justify |
| --- | --- | --- |
| Representation/property declaration | Little endian; LocalPack tail; sorted unique under comparator C; prefix ordered under a stated transform. | Candidate selection and identification of obligations. A sorted claim must come from an invariant-preserving producer or admission, not merely a user-constructible tag. |
| Stable admitted fact | Retained snapshot, readable extent, actual alignment/phase, supported ISA, valid summary edition. | Repeated trusted execution while that fact remains valid. |
| Value-dependent precondition | New first value is above the current maximum; values fit the chosen width; sufficient output capacity for this update. | A checked entry or an explicit witness under the current state/ownership. It is not generally established once by the collection schema. |
| Dynamic effect/result | New maximum/count, changed byte ranges, a dirty summary, exact versus possible filter evidence. | Updating invariants and forwarding the specific obligations that consumers require. |

For append to a sorted unique container, the relevant law is: either input is
empty, or the input is itself strictly ordered and its first value exceeds the
container's current maximum (with an empty-container case). Two individually
sorted chunks also need an ordering relation at their boundary. For a trusted
sorted input batch, comparing its first value with the old maximum can suffice
for that boundary; an unvalidated vector does not gain sortedness from its lane
count. After a successful append, the new maximum governs the next batch.

Illustrative division, not an API selection:

```text
entry/driver: establish ownership and current-state facts
              discharge sorted-input / first-above-max / capacity obligations
native preset: encode or append admitted values, report its physical effects
driver:       maintain or invalidate affected logical summaries
              complete publication obligations, or return explicit pending duties
```

A witness concerning the current maximum becomes stale if another writer can
change that state. Hold the needed exclusive ownership/version condition or
revalidate at the appropriate entry boundary. This need not become a check in
each inner arithmetic operation. Input refinement from an upstream producer can
also discharge an obligation when its scope and semantics match.

Physical effects cover all writes, including packet neighbours affected by
packing, metadata, relocation and recycled storage. A logical append count or
returned encoded length is insufficient. Byte spans can be conservatively
coalesced only within admitted ownership and with declared precision; complete
coverage and exact diffs are different guarantees. Xmem's batch staging prior
also warns that batching must not silently change failure/publication semantics.
This remains interface exploration, not an instruction to implement MVCC now.

## Concrete stress cases to choose from

These are competing ways to learn, not a mandatory test matrix or roadmap.
Each should include the actual authoring edits as well as execution evidence.

1. **Two curated regions versus the current leaf cuts.** Keep the existing
   integer read/less-than/sum semantics. Compare the current read/filter/sum
   chain, a read/reconstruction region followed by an inline predicate/sum
   region with native predicate state, and full inline execution. Add one
   alternative reader and one alternative consumer to expose object reuse and
   registration costs. Report rebuilt TUs and text size alongside timings;
   distinguish eliminating a cut from changing mask representation. This is
   the smallest direct test of the proposed factoring direction.
2. **A width/grain mismatch that cannot be waved away.** Use a wider integer
   body with explicit native2/4/8 routes as available, plus a sixteen-value
   producer/consumer pair for the logical `16→8` and `16→32` cases. Run a
   37-value request and a boundary where gathering further is forbidden.
   Compare incremental32 consumption with a genuinely32-wide operation.
   Show retained values and all conversions; the matching case must have no
   forced split/reassembly or materialised intermediate. Do not force the
   same native grain across ISA targets merely to simplify the harness.
3. **Two aligned bitset sources with unequal physical grains.** Reuse plain
   bitset kernels and, where selected, the existing two-stream decoder. Vary
   physical placement independently on A/B while preserving logical coverage.
   Compare a joint read/bitwise preset with cursor-style alignment. Add one
   consumer of the decoded A values so fanout and retention are real. This
   extends a read/compute seam without reopening compressed encode–operate–
   encode algebra as a prerequisite.
4. **A conjunctive fragment with a useful coarse observation.** Place two atoms
   in one signature plane and an expensive residual elsewhere; use a64-row
   rollup with smaller native comparisons. Compare eager and progressive
   presets and a direct-data bypass. Include empty and sparse prefilters and
   a Boolean arrangement whose other-plane dependency prevents rejection.
   Trace evidence coverage and residual obligations at an observation boundary;
   do not infer speed from skipped plane visits alone.
5. **Sorted append as a paper stress case first.** Append two chunks whose
   individual order is valid but whose boundary violates the current maximum.
   Then use a legal append that changes a partial packed packet and its summary
   metadata. Show who discharges the state-dependent precondition and who owns
   physical coverage versus logical maintenance. A suspended/invalidated
   witness should not survive by virtue of its type. This does not require a
   new aggregate, row-signature or MVCC implementation campaign.

For executable comparisons, keep mathematical work and observable obligations
constant unless the changed obligation is the question. Attribute native masks,
gathering, retained values, dispatch, redundant reads and final materialisation
separately where possible. Retain a plain memory-chunk control when it clarifies
the price of long-lived state. No pass/fail timing budget is inferred here.

## What to carry back into the design

- Keep the recordable authoring direction, but stop equating its leaves with
  continuation boundaries. Curated inline presets and their substitution points
  should be first-class subjects of the next comparison.
- Match domain, coordinates, grain and native carrier at a selected edge.
  Normalise outward; introduce an adapter only for a real mismatch or caller
  requirement. An exact semantic group still constrains legal splitting.
- Prefer a few explicit cuts that preserve valuable local optimisation over
  either compulsory full fusion or compulsory dispatch after every operation.
  Demonstrate reduced compilation products; source-level reuse is insufficient.
- Keep progress/extent/lifetime legible. Iterator-like syntax is worth testing
  for uneven sources, but it does not solve native transport or retained state.
- Extend placement obligations to invariant and effect obligations, distinguishing
  declarations from current-state witnesses. Their details remain provisional.

I would compare the two curated-region cut first for execution cost, and the
unequal-grain/two-source examples for author ergonomics. A successful short
integer sum alone cannot choose the general interface. The existing fine-stage
and bounded-carrier sketches remain useful controls and minority options;
neither becomes the default for all kernels on the strength of these probes.
