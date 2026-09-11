# Value semantics and execution integration

2026-09-10. Design proposal following the basic block probes. These questions
inform [SeriesPack](../../../ikea/seriespack.md); as Ashton
subsequently clarified, they need not all be settled before starting. The
segment size bound below
is a user commitment; the ownership and interface sketches are proposals.
There is no new implementation or performance evidence here. The
[current composition design](design.md) still owns the underlying authoring model.

The proposed division is: **Ikea implements meanings and local obligations;
Engine chooses the database meaning and visibility unit; Loom supplies owned
resources and schedules work.** Kernel bodies receive the resulting operands
and narrow local capabilities. Merely moving every integration into an outer
wrapper would be insufficient: summary consumers may need live decoded values,
and a dependent address may only become known inside a composition.

## Semantic ownership

“Schema” covers several different decisions. Sharing a description does not
give every consumer authority to define it.

| Description | Engine owns | Ikea supplies and understands |
| --- | --- | --- |
| Collection and record schema | Field identity, logical record shape, constraints, defaults, expression typing, schema evolution and query-language policies | Reusable value domains, field/tuple compositions and operations implementing the selected semantics |
| Value and operation semantics | Which domain and rules a field/expression uses; implicit coercions become explicit operations before binding | Integer signedness/width, floating formats, validity, comparison/equality/hash/conversion/arithmetic contracts and their applicable implementations |
| Actual representation | The composition description associated with each visible segment, migrations, and preferred future choices | Component descriptions, format versions, parameters, dependencies, placement requirements, encoders/decoders and composition presets |
| Segment structure | Identity, routing, membership, field/summary roles, coordinate mapping and visibility | Operations over an admitted segment or record-slice view, with explicit coordinate and dependency contracts |
| Summary maintenance | Which summaries exist, their coverage, permitted deferral and publication with data | Summary representations, contribution/repair kernels, evidence contracts and local maintenance compositions |

Ikea should therefore have a usable semantic vocabulary, without importing
Engine's SQL AST or catalogue. Engine can select a supplied contract or compose
an extension; an implementation only needs the subset of semantics it supports.
An opaque extension does not acquire integer/float rewrite laws by resembling
one physically. Unsupported operations need another bound implementation or
an explicit fallback, not guessed semantics.

Keep three questions separate even within that vocabulary:

- **What values are representable?** For example, signed 32-bit integers,
  IEEE binary32 bit patterns, scaled decimals, byte strings, or nullable values.
- **What does this operation mean?** Numerical versus total ordering, equality,
  overflow and rounding rules, reduction rules, NULL handling, error behavior.
- **How are these values represented here?** Packed words, a dictionary,
  an order-key transform, several planes, or a compressed residual composition.

Operation semantics matter as well as a type tag. A floating format does not
select NaN ordering, whether signed zeros share an equality/hash class, or
which reduction regroupings are legal. Likewise, integer storage does not
select wraparound versus checked arithmetic. Engine chooses the query-facing
rules; Ikea can implement and expose their laws without choosing them globally.

Validity is a composable semantic input, not a sentinel reserved in every codec.
Whether missing differs from NULL belongs to the selected record semantics.
Inactive rows, absent records, NULL values, and values not yet inspected are
different states. The [signature design](../row-filter-signatures/design.md#predicate-evidence-and-boolean-composition)
already explains why bounds on SQL TRUE cannot simply be complemented for NOT.
Equality normalization must also agree with hashing and relevant summaries.

### An order transform is an operation with laws

For a two's-complement signed `w`-bit integer, the unsigned order key is:

```cpp
// Pseudocode: U_w is exactly w bits; all bit operations are unsigned.
U_w order_key(I_w x) { return bit_cast<U_w>(x) ^ (U_w(1) << (w - 1)); }
```

It maps the minimum signed value to zero and the maximum to `2^w - 1`,
preserving signed order under unsigned comparison. This is neither occupied
record rank nor zigzag coding. If keys are compared as byte strings, their
serialization must also preserve unsigned order; native little-endian bytes
do not supply that property.

The useful authoring knowledge is the relationship:

```cpp
unsigned_less(order_key(x), order_key(y)) == signed_less(x, y)
inverse_order_key(order_key(x)) == x
```

Those laws let binding move a predicate into the stored domain, transform a
query constant once, or choose a fused decode/compare implementation. They do
not permit summing order keys as though they were signed field values.
Zigzag can be useful for small signed residuals but does not preserve signed
order. A transform's compression utility and its legal predicate rewrites are
separate facts.

```cpp
// Illustrative composition, not a chosen format or descriptor syntax.
field.meaning = nullable(signed_integer(32));
segment_a.actual = nullable_values(order_key_view(packed_unsigned(...)), ...);
segment_b.actual = nullable_values(signed_values(...), ...);

// Engine requests the same field predicate on either actual representation.
select(field, signed_less_than(-3), incoming_rows);
// A may compare encoded order keys; B may compare signed native values.
// Both preserve validity, original coordinates and the predicate's meaning.
```

A floating order encoding needs its own declared relation. Handling negative
bit patterns alone does not settle NaNs, payload preservation or signed zero.
A reversible bit-pattern encoding, an equality-normalized key and an SQL sort
key may be three different operations. Canonicalizing zeros or NaNs is not a
lossless physical rewrite when the field promises preservation of their bits.
Keep a residual comparison where an encoding supplies only a necessary test.

This argues against either an untyped byte-only Ikea or a compulsory boxed
`Value` at every kernel boundary. Domain and operation descriptions belong in
analysis/binding; a selected body can take plain native integers/vectors.
Ikea's serializable component/semantic descriptions give Engine building blocks
for its composition schema. Versioned descriptions and dependency identities
must be recoverable; runtime function pointers and bound resource handles are
not the persisted schema. The serialization envelope remains open.

## Segment and record-slice

Adopt **at most `2^16` segment-local positions**. This bounds local coordinates,
not the logical key interval or the number of encoded/allocated bytes. Keep
position values separate from counts and half-open endpoints: position 65,535
fits in 16 bits, but a population or end of 65,536 does not. Smaller segments
need not allocate full-capacity masks or arrays.

The [trie-remapping coordinate analysis](../trie-remapping/design.md#keep-the-coordinates-distinct)
distinguishes logical keys, local positions, occupied ranks and addresses.
The [measurements](../trie-remapping/FINDINGS.md) did not select the bound; this
is Ashton's subsequent design decision. It does not select rank-aligned columns,
one physical plane per field, fixed logical prefix width, or a multiplicity
representation.

Proposed naming: **segment** for the Engine-owned node, **record-slice** for a
view selecting records from that node. A slice need not be contiguous or own
storage. If Engine later chooses one name for the node, the view/owner
distinction still matters more than the spelling.

An Ikea attachment needs the local facts used by its operation: coordinate
domain and mapping edition, selected positions or ranks, admitted field/summary
operands, and compatible owners. The Engine handle can supply these without
Ikea knowing the routing tree or mandating a segment header. A mapping may be
the identity; using a common domain does not require an indirection per row.
Row and column layouts can both expose this view. A variable-width value may
occupy many bytes or reference separately owned storage.

Logical schema revision, coordinate mapping edition, actual representation
revision and visible data version must remain distinguishable. Changing one
does not necessarily change the others. A retained rank mask cannot silently
survive an insertion that shifts ranks. A plan may change while reading the
same snapshot; migration of one segment does not require migrating its siblings.

Standalone structures use the same blocks and relevant value contracts without
a collection schema, record visibility or the segment bound. A million-entry
heap or sketch must not acquire a segment wrapper just to use packed integers.

## Mutation: local work and publication

The mutation seam needs **physical coverage, logical consequences and a
visibility obligation**. One generic dirty flag cannot replace these.

| Information | Source | Consumer and constraint |
| --- | --- | --- |
| Physical writes | Selected writer plus placement mapping | MVCC/durability adapter: owner-relative spans, including metadata, relocation links and actual wide stores. Conservative coverage is permitted only within admitted writable ownership; it is not an exact byte-difference list. |
| Logical change | The semantic operation where old/new values, presence and coordinates are available | Selected summary/index maintenance: contributions, replacement facts or invalidation. Re-encoding identical values has physical effects without a logical aggregate delta. |
| Validity of derived state | Maintenance composition plus its Engine contract | Readers: exact state, valid conservative evidence, corrections covering the visible version, or an explicit bypass. “Repair queued” alone provides no read guarantee. |
| Publication dependencies | Engine's transaction composition | Coordinated visibility of data, membership/mapping/descriptor changes and required effects; local kernel completion cannot establish this. |

The raw writer should receive a writable view and sufficient capacity. A fixed
patch's coverage can be derived once by its adapter; a variable encoder may
return lengths or append bounded span records. The body need not call MVCC for
every store. Footprints still belong to the selected implementation: a wide
store that writes unchanged neighboring bytes needs ownership of those bytes.
An estimate of final encoded length cannot stand in for that footprint.

Effect capacity and output resources should be acquired before entering a
bounded region. If a codec can fail after writing, its local contract must
identify valid partial state and coverage. Easier regions can promise no
remaining capacity failures once admitted. Both need safe discard/rollback;
neither may lose effect records when a journal fills after bytes were changed.
General variable-output work can stop between bounded pieces to acquire more.

Write coverage does not by itself provide an MVCC concurrency contract. Engine
may also need expected versions or read-dependency witnesses for a read/modify/
write, including transform inputs outside the selected rows. Binding supplies
the required dependency facts; publication validates them under Engine's chosen
protocol. This does not require instrumenting every native load.

### Reuse semantic values where they already exist

```cpp
// One recordable mutation fragment; local operations retain native lowerings.
auto replace_values(Ops& ops, AttachedField field, Rows rows, NewValues input) {
    auto before = ops.read(field, rows);       // only if selected consumers need it
    auto after = ops.convert(input, field.value_contract);
    auto change = ops.replace(rows, before, after);
    ops.maintain(field.local_summary_recipe, change);
    ops.write(field, rows, after);
    return change;                            // symbolic reuse, not mandatory storage
}
```

`maintain` expands into the selected contribution or invalidation work. It is
not an arbitrary observer list invoked per value. The recorder must retain
dependencies and effect ordering; the executor need not carry a runtime effect
token through every scalar operation. A native region can consume old/new
vectors immediately for both encoding and summary work, avoiding a second
decode or a compulsory beforeimage array. Only needed facts cross its boundary.
If a delayed consumer needs those values, retaining them has a real cost.

The composition owns which consumers exist and their sequencing. An opaque
fused body must declare the same semantic/effect obligations; describing a
write does not mechanically prove arbitrary native code reports all its stores.
Local coverage and semantic tests remain necessary for such extensions.

Count and exact integer contribution kernels can be simple. Deleting the last
minimum witness may require repair or downgrade to a conservative bound.
Floating sum cannot inherit arbitrary subtract/add regrouping from an integer
delta interface. The [aggregate semantics](../aggregate-maintenance/design.md#separate-the-aggregate-semantics)
own those distinctions. Engine can select eager maintenance, compatible pending
corrections or invalidation/descent without changing the underlying writer.
Ikea computes local effects; Engine routes them to affected ancestors and other
database dependencies. A remap/split can change summary coverage without
changing field values, so it also needs an explicit structural change.

### A sealed local change, not an implicit commit

Illustrative lifecycle for a version-private mutation workspace:

```cpp
attempt = engine.begin_mutation(snapshot, selected_mutation);
resources = await_ready(loom.acquire(attempt.resource_request));
local = ikea.bind_mutation(admitted_operands, attempt.local_contract, resources);
wait_until_complete(local);                         // run bounded regions; resume any pending work
sealed = local.seal();                              // local effects complete; transfer ownership
attempt.attach(move(sealed));                       // may join other segments/participants
engine.publish(move(attempt));                     // asynchronous outcome is Engine-owned
```

This is a dependency sketch, not six mandatory allocations or virtual calls.
The waits denote lifecycle dependencies, not blocking a worker or choosing C++
coroutines. Sealing is only available after successful local completion.
In this sketch, writes and effects target attempt-owned state; a shared eager
summary update would need its own admitted visibility/abort contract.
An Engine-bound container call can perform the local lifecycle on the caller's
behalf. The caller supplies a transaction/snapshot scope and handles an outcome
or pending operation; it should not have to remember separate summary and MVCC
calls. A standalone caller can instead use a direct, nontransactional binding.

Sealing means no further local writes, complete physical coverage, and either
completed derived state or the agreed correction/invalidation records. It does
not mean conflict checks passed or bytes became durable/visible. Engine and
its Loom/Orbital adapters own those steps and the multi-participant protocol.
Before visibility, every required pruning level must be sound for the reader's
version, or readers must see a compatible bypass. An atomic pointer to leaf
data alone is insufficient if ancestors still certify old bounds.

The outer publication protocol must also establish when staged bytes are final
for hashing/shipping, and how repair obligations survive recovery. Scheduling
an ephemeral repair callback cannot replace publishing the invalidation or
correction state that makes reads safe.

Three physical approaches remain credible:

| Approach | Benefit | Obligation/cost |
| --- | --- | --- |
| Private replacement extents or overlays | Cancellation before submission can discard work; old readers retain old state | Copy/allocation cost, publication of descriptors and delayed reclamation |
| Exact patches under an admitted MVCC write scope | Retains small-update locality | Beforeimages/version isolation, abort behavior, complete coverage and claim lifetime must already be provided |
| Deferred logical updates | May amortize summary/data maintenance | Reads, bounds, replay and retention must incorporate pending updates under the chosen semantics |

Private workspace is the clearest first integration sketch, not a requirement
to copy every segment on every update. Do not present an in-place writer as an
equivalent substitute until its rollback and visibility guarantees are supplied.

Cancellation belongs to the lifecycle. Before submission, discard the private
attempt and its unpublished effects. After submission, cancellation requests
must resolve through Engine's transaction outcome; they cannot assume rollback
or report “cancelled, no effect” while publication may succeed. Retries must
not replay contributions twice. Local sealing/attachment should consume an
owned result once; transaction identity and replay deduplication remain Engine
responsibilities. Partial publication is only available under an explicitly
different operation contract.

## Loom: resources, progress and retained state

Keep three distinct execution situations, rather than making every CPS stage
an asynchronous task:

| Situation | State and lifetime | Scheduling choice |
| --- | --- | --- |
| Immediate inline/CPS/fused continuation | Compatible native values; binding retains owners outside the stage | Continue immediately, without scheduler entry or mandatory materialization |
| DRAM latency hiding | A small owned ring slot holds only state crossing the chosen stop | Loom rotates useful work using access geometry and hardware-prefetch modeling; a cache prefetch does not deliver a readiness event |
| Buffer pressure, disk/network wait, longer suspension | Stable operation state owns tickets, leases and resume data | Completion-driven wait outside the short-lived ring; admitted limits bound retained resources |

An access description should state identity/incarnation, range or address,
required readable/writable extent, alignment and access shape. A codec can
expose dependent address discovery as an operation. The driver binds these to
Loom requests; the kernel does not choose a pool, ring width or eviction policy.
Native code can still contain tuned prefetch instructions or internal software
pipelining; it must make its externally relevant access/suspension obligations
clear so a surrounding driver does not duplicate the work.

Acquire means a lease with lifetime and access rights, not just a pointer.
Residency lifetime and snapshot/version lifetime are separate. A pinned version
need not be resident, and resident bytes need not still denote the requested
version. For dependent variable-width accesses, acquiring a bounded next closure
is often more realistic than acquiring every possible dependency up front.
Output growth and retained input leases need a budget/backpressure rule so an
operation cannot hold all pool capacity while waiting for its next buffer.

### Choose stops where they are cheap and useful

```cpp
// Region A: admitted metadata resolves addresses; no suspension inside the body.
addresses = resolve_inputs(cursor, selection);

// Chosen boundary: often retains compact addresses/coordinates, not decoded values.
decision = access_driver.prepare(addresses, access_shape, held_owners);
if (decision.requires_wait_or_rotation())
    return park_at(consume_inputs, retain(cursor, selection, addresses, held_owners));

// Region B: admitted bytes; native intermediates can feed several consumers.
decode_transform_filter_consume(addresses, selection, output);
```

The direct path invokes the same bodies without writing a resume frame just
to satisfy a uniform asynchronous interface. A short DRAM rotation stores the
state actually needed across that rotation. A longer wait may transfer it to
stable storage or restart from compact state after reacquisition. Neither
alternative is free; recomputation is legal only if it preserves observations
and does not repeat mutations/effects.

Sometimes the next address depends on a decoded value needed again afterward.
Then there is a real choice: retain native state, retain a compact sufficient
value, recompute from a pinned source, or choose a larger nonsuspending region.
There is no wrapper that both suspends arbitrarily and preserves registers for
free. Binding should reject a substitute lacking a required safe stop, or
select an explicit adapter; final-value equivalence alone is insufficient.

At a legal suspension point, operation state must account for logical progress
and coordinates, residual masks/evidence, cursor and plan revision, live values
or reconstruction inputs, owner/lease handles, and any private mutation/effect
frontier. These are obligations, not mandatory fields in one universal context.
Retain exactly what that operation needs. Resuming a newer plan requires a
compatible state/progress boundary or an explicit conversion; a mapping change
cannot silently retarget old position masks.

Nontrivial owners should live in the outer invocation or stable operation
state. A tail-call stage borrows that state. The
[Clang musttail rules](https://clang.llvm.org/docs/AttributeReference.html#musttail)
end locals' lifetimes before the call and require trivially destructible locals
and compatible signatures/conventions. An owning lease local or a pointer to
a departing stack local cannot become a convenient native continuation payload.
Exact adapters still need validation with the pinned compiler and targets.

BytePack's [aligned cursor table](../../../../calico/workbench/prototypes/bytepack/chain.h)
remains useful for immediate straight-through pipelines and early completion.
It does not retain a suspended C++ stack. A pending operation must retain its
plan/table owner as well as the saved cursor; early exit must still reach the
appropriate completion/cleanup path. Resource ownership sits outside leaf
stages, so it is not skipped by the table's terminal shortcut.

Cancellation checks can occur between bounded regions. A long indivisible body
cannot claim a short cancellation latency; expose smaller regions when that
latency is required. Pending completion and cancellation need one serialized
or otherwise race-safe lifecycle with generation checks. Cancellation detaches
the consumer, but resources used by outstanding I/O cannot be reclaimed until
the backend releases them. A late completion must never resume a reused slot.
If pointers are dropped over a long wait, resume must reacquire the intended
version and re-establish the needed binding facts, rather than trusting stale
addresses.

## Authoring ergonomics and alternatives

The preferred combination is **ordinary local bodies, effect-aware compositions
and selected execution drivers**. One body can serve direct standalone use,
an Engine-bound operation and a resumable operation. The wrappers differ in
their external lifecycle obligations; the computational kernel remains the same.

| Author | What they need to specify | What is supplied elsewhere |
| --- | --- | --- |
| Basic codec/native kernel | Values, target, grain, extents/aliasing, writes, failure behavior | Collection schema, ancestor maintenance, transaction publication, Loom queue policy |
| Semantic transform | Domain mapping, inverse/precision and applicable operation laws | Collection policy, actual field attachment, placement and scheduling |
| Composition author | Data/effect dependencies and required observations; extra address-discovery or bounded phases where necessary | Reusable lifecycle adapters, resource mechanics and publication coordinator |
| Engine/Loom integration author | Admission, effect destinations, safe stops, ownership and outcome handling | Local implementations and their inspectable contracts |

This should yield short practical authoring recipes: add a transform by stating
its mapping and laws once; add a writer with an adapter that accounts for its
footprint; add summary maintenance as a consumer of an existing semantic change;
make an operation resumable by selecting cuts and their live state. None should
require editing every participating kernel or duplicating its algorithm in a
second asynchronous implementation.

The outer call's completion contract is also explicit. An admitted immediate
call returns directly; an API permitting pending work can hand ownership back
to its driver. A suspendable implementation cannot silently replace a function
that promised an immediate result. On the ready path, neither case should need
schema analysis, a queue operation or creation of a heap continuation.

Two alternatives help expose what this proposal must avoid:

- **One implicit runtime context in every kernel:** convenient access to all
  services, but hides effects, suspension, reentrancy and specialization costs.
  Narrow attached capabilities are useful; a universal service locator is not
  the proposed authoring interface.
- **Every operation returns a materialized value/effect/request packet:** easy
  to interpret and pause anywhere, but can force stores and multiply dispatch.
  Retained packets fit actual asynchronous boundaries. They should not define
  immediate native composition.

Declarations should be reusable adapter knowledge, not a second hand-authored
copy of an entire composition. Reusable region/driver families can contain the
hard lifetime and publication logic. Optional hooks should disappear when no
consumer is bound; shared compilation should avoid a specialization product
of every codec, summary policy, publication mode and wait mechanism. The best
boundaries and their costs remain unmeasured for this integration.

## Counterexamples and decisions still open

These are pressure tests for the proposal, not a required campaign or roadmap.

| Pressure case | What it would distinguish |
| --- | --- |
| The same nullable signed field uses an order-key composition in one segment and direct values in another | Shared semantic operation, constant translation, exact versus candidate filtering, and deep replacement without whole-collection migration |
| A physical re-encode preserves all records but rewrites metadata and wide suffix bytes | Complete MVCC coverage without inventing logical summary deltas; placement remains part of write legality |
| A selected update changes NULL to a value, removes an extremum, then stops before sealing | Correct contribution/validity rules, beforeimage reuse and discard of private effects |
| Leaf publication races a reader using an ancestor signature | Publication must protect every pruning level or expose a compatible bypass; deferred repair alone fails |
| A dependent varlen read waits for another buffer while retaining a decoded value | Bounded pool admission, retain/recompute tradeoffs, and no hidden requirement to materialize all native handoffs |
| Cancellation races I/O completion or transaction submission | Owned live state, stale-completion rejection, resource release and an honest transaction outcome |
| A nested replacement changes access grain, write footprint or safe stops | Enclosing resource/effect/observation obligations are re-established, not inferred from equal final values |
| A standalone structure exceeds 65,536 entries | Relevant semantics remain reusable without Engine record machinery or segment-sized coordinates |

The most consequential open choices are the initial numeric/NULL policies;
the concrete segment attachment description; the private-overlay versus exact
patch mutation surface; the reader-visible protocol for deferred summaries;
and the first native region's retained state and Loom access request. A small
vertical design example crossing these seams would be more informative than
another isolated codec. It should compare direct execution with only the
wrappers that example needs, charging beforeimage work, effects, retained bytes,
dispatch and cancellation granularity as well as the inner kernel.

## Prior art used here

The [aggregate closeout](../aggregate-maintenance/CONCLUSIONS.md) establishes
local buffering tradeoffs, not concurrent publication. The
[dirty-buffer note](../aggregate-maintenance/shared-dirty-buffer.md#read-pressure-flush-and-publication)
and [signature design](../row-filter-signatures/design.md#mutation-drift-and-physical-design)
identify compatible generations and reader-visible correction/bypass obligations.

Calico's [xmem staging](../../../../calico/xmem/include/xmem/xmem.h) demonstrates
owner-relative span staging and batching; `stage_many` can leave a partial staged
set on refusal. It is not a ready-made atomic Ikea mutation interface. Calico's
[AMAC](../../../../calico/loom/spec/AMAC.md) and
[residency admission](../../../../calico/loom/spec/IO.md) distinguish short ring
state from completion-driven waits, residency from semantic pins, and
cancellation from backend completion. Their mechanisms and limitations inform
this proposal; their fixed capacities, ownership split and tuning are not SixDB
decisions or performance measurements.

Arrow's [C data interface](https://arrow.apache.org/docs/format/CDataInterface.html)
separates type/child descriptions from buffer-bearing arrays and explicit
release ownership. That separation is useful prior art for attachment; its
columnar interchange layout is not proposed as Ikea's native value ABI or
persistent composition schema.
