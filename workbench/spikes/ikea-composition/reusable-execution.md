# Reusable execution controls and small bindings

Status: source survey, a bounded TuplePack endpoint decision and remaining
experiment plan, October 23, 2026. This reopens the performance part of the
[interface revision](interface-proposal.md). The current implementation is not a
final interface freeze. Cross-profile checks and complete-caller evidence are
recorded in the [qualification receipts](evidence/interface-cohesion-v2-20261023/README.md);
remaining native losses are explicit. The source decision below is not a general
speed claim.

Ashton's question is broader than restoring a particular TuplePack object size:
which execution mechanisms should Ikea provide so consumers can cheaply move
between blocks, widths and placements while retaining useful compiled work?
The consumers include StreamPack, Engine Directory and Frame. Their operation
grain, retained metadata and admission costs differ.

## Starting evidence

The [TuplePack decisions](../tuple-layout/implementation-decisions.md#representation-and-binding)
already separate physical law, placement and prepared controls. Ordinary and
native read/write bindings borrow shared plans and named views. The native
payload ABI is a separate concern: NEON homogeneous vector aggregates, AVX2
`regcall`, and AVX-512 vectors avoid unnecessary payload materialization. Reusing
that work does not require copying native controls into each block handle.

The [Loom integration census](../orbital-mvcc/metadata/consumer/ikea-census.md)
and [metadata findings](../orbital-mvcc/metadata/loom/README.md) retain the original
16-byte TuplePack bindings and their shared plan/view costs. The earlier filter
candidate copied a code projection into every ordinary reader, increasing it
from two pointers to 24 bytes on the current 64-bit targets. This attempted to
remove a dependent plan/control load in repeated encoded predicates.

The first correction restored the general reader to two pointers and moved the
projection into a separate 24-byte predicate. The
[completed copied-projection comparison](handle-boundaries/PROJECTION-NATIVE.md)
supports a smaller endpoint: public `predicate<Byte, Rows>` copies its code and
retains only the named view. The plan and intermediate reader may expire after
binding, including through the strided/sparse fallback. Packet capacity N no
longer appears in an operation that does not use it.

On the inspected targets both reader and predicate are now 16 bytes; these are
ABI observations, not portable size contracts. `bind_predicate(reader)` and
`bind_predicate(plan, view)` provide the same preparation. The named view's
value, address and backing stay stable through use. Operator, RHS and activity
remain per-command. Reader `filter` is the one-call convenience; repeated callers
can prepare once. Writers and native bindings retain their two-pointer
representation. The unconditional 24-byte reader and plan-borrowing 24-byte
predicate remain source-qualified history.

The [actual call-boundary study](../call-boundaries/README.md#returns-the-actual-boundary)
shows why `sizeof` alone is inadequate. A trivial two-word return can use two
GPRs while `expected` around it already needs a memory result. Nontrivial owning
objects and crowded argument lists have different rules. Inspect producer and
consumer together, including checked construction, storage/reload and live
state. Do not attribute an entire caller frame to its handle.

Place fallible admission at the useful owner/command boundary. After admitting
its plan bank and placements, an owner can retain or pass raw borrowed
operations through repeated execution instead of repeatedly handing checked
factory results across opaque calls. That retains the two-register opportunity
without changing the contract for untrusted input. The complete owner cost still
includes admission and preparation; discarding an error does not establish the
required proof.

Earlier work also supplies important losing controls:

- [Generic inline composition](../tuple-layout/runtime-first.md#what-this-changes-in-the-next-experiment)
  retained a 3,712-byte entry, 240-byte frame and vector spills despite a native
  ABI that avoided payload materialization. Boundary removal alone was insufficient.
- [Mixed-plan selection](../tuple-layout/viability.md#what-the-analyser-must-learn-from-complete-workloads)
  could be about 65% worse than the measured best when isolated costs were
  added. Mixed targets and live plans must be measured as complete workloads.
- [Transient schema construction](../orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
  exchanged resident metadata for substantially more repeated preparation.
  Shared immutable controls are the baseline; rebuilding them is a distinct arm.
- [Executable placement](../executable-placement/README.md) and the current
  Bec/Stream diagnostics show that unchanged hot instruction sequences can
  have different timings. Linked placement, exact duplicate controls and
  counterbalanced trials belong in close comparisons.

The bounded [Frame source survey](evidence/interface-cohesion-v2-20261023/frame/mechanisms.md)
finds useful reuse already implemented: operation-major width catalogues,
per-block pointer/count arguments, and dictionary work retained under an edition.
Its catalogue removed retained bindings without establishing a universal query
speedup. The new comparison should reuse that implementation and its losing
COUNT/SUM and dictionary-cache cases.

## A common separation with module-specific execution

| Part | What can survive a block change | What must be refreshed or admitted |
| --- | --- | --- |
| Physical law | Encoded widths, bit offsets, geometry and byte interpretation | The chosen law must match the bytes; a changed representation may require owner migration |
| Operation projection | Ordered map, groups, packet width and useful row count | A different projection can select new controls over the same encoded bytes without migration |
| Execution control | Selected entry points, shuffle/mask controls, compiled specialization | Different width/map/lane may choose another existing control or need preparation |
| Block binding | A small borrowed reference to shared controls | Address, count, stride, exact read/write grant and named source |
| Content proof | Only facts tied to the same content/edition | Bec population/length, dictionary translation, owner generation and validated structure |
| Owner lifetime | Retained allocation/capture and publication policy | Relocation, replacement and retirement must preserve the referenced generation |

A coherent interface can give these parts consistent roles and operations while
letting each binding carry the facts its consumer needs. A consumer that already
has count and placement in its block descriptor can supply them per command;
an independently retained checked reader needs a surviving source for them.
Compare shared control tables and copied entries explicitly, including the cost
of any extra indirection and retained placement metadata.

An owner-scoped bank of immutable plans is a useful concrete starting point.
For TuplePack, its key includes the complete physical layout, ordered map and
groups, and packet `N`/`Rows`; controls also belong to their compiled target and
carrier. SeriesPack needs width/head/geometry, carrier and the operation family.
Select an existing entry when that exact operation returns, and retain it while
bindings borrow it. A plan lookup is not byte validation: current TuplePack
binding checks unit-byte compatibility, so equal extents, function pointers or
hashes alone cannot prove the source's encoding. The owner supplies that law
identity. If interning is useful, measure lookup, resident memory, equality and
eviction/lifetime costs rather than introducing a global registry by default.
The block comparison uses eight shared plans directly, with no cache machinery.

Read access is another operation fact, separate from placement geometry. The
[promoted NEON sparse-packet study](../neon-tuplepack-masked/README.md) measured
2.6–3.3× sparse-read gains by loading the bounded first-to-last selected window,
including discarded inactive interior bytes. It explicitly documented that
widening; R4's ordinary TuplePack comments promised active-only access.
Neither an allocation extent nor a stride hint proves those interior bytes are
initialized, coherent or isolated from writes. Result oracles and suffix guards
miss this contradiction.

Keep ordinary selected-row commands active-unit-only. Preserve the measured
window implementation behind an explicit reusable `author::read_access::common_window`
grant on native adapters. Its owner admits the initialized/read-isolated hull;
there is no added handle field or runtime mode test. Full-active coalescing and
AVX-512 masked loads need no relaxation. Preserving writes may use the grant for
old-data reads, while their stores and effects remain active-only. The
[physical-envelope check](../../../ikea/test/tuplepack/access.cpp) poisons inactive
interior bytes, unlike the earlier output-only tests. This correction and its
qualification are separate from frozen R4 and the old compact-predicate control.

| Module / current mechanism | Candidate worth testing | Consequential limit |
| --- | --- | --- |
| TuplePack: shared plan and named view for general/native/writer bindings; separate predicate copies one code and borrows only its view | Reuse the compact predicate across useful work; investigate direct validated-law binding for predicate-only owners | Keep checked factory handoff, new view admission and unrelated general read/write costs visible |
| SeriesPack: small typed dense readers, larger expression and erased handles, runtime width catalogue | Share operation-major compiled controls; bind a resource cheaply or supply placement per whole operation | Count, output type, geometry, exact tails and writes remain explicit; copying all function pointers is an alternative to measure |
| StreamPack: two-pointer authored access and exact-layout `rebind` | Explicit compatible-law retarget outside the point loop; reuse G/W/O/Used specialization | Existing rebind retains the original layout identity to hoist format loads. New layout/count/extent/sidecar facts require fresh admission |
| Bec256: a span plus population/length execution proof | A naturally aligned compact execution capsule, retaining a separate full placement view if needed | A proposed pointer/u16 population/u8 length/u8 grant fits two words, but clamping public `storage().size()` would change its contract |
| Hashtable: retained outer capture with a one-pointer borrowed reader | Keep cheap borrowing across same-specialization owners; measure actual hot loops | Seed, geometry/kernel or generation changes may require migration. Retention and isolation remain owner responsibilities |
| Intervals: source-specific geometry and external selection | Reuse admitted source/reference where it removes actual repeated work | Do not add a plan indirection where an operation currently needs none |

Directory currently uses Intervals over direct uint32 endpoint planes and a
typed `KeySource`/`KeyRef` comparator for byte keys. Its sources already carry
small pointer/count or pointer/count/width state, with no SeriesPack width
catalogue. It is a witness for cheap typed context and owner borrowing. Adding
a packing-control indirection there would need an actual new representation.
Likewise Bec's existing `decode` boundaries pass readers by const reference;
shrinking their resident state alone does not change those calls into two-GPR
value transfers.

Frame's current packing dispatcher is a second concrete witness: its 64 width
choices produce about 30 KB of NEON setup code in this candidate. A shared
initialize entry selected by width could reduce selection/setup and text, but
new-buffer admission, input fit and real effect coverage remain per-command.
The dispatcher contains 309 static calls across all choices; that is not the
dynamic call count of one pack operation.

### Physical retention around reusable execution

The merged [single-class allocator and relocatable-handle study](../orbital-hot-paths/SHARED.md)
reinforces the owner boundary. Ordinary allocations expose direct pointers;
Holds keep an address and captured footprint alive, while cross-class growth
can relocate unheld storage. Rebuild address/extent-qualified bindings after an
actual move or extent change. Faster allocation does not supply initialized
body bytes, a selected logical version or wider read permission.

The opt-in relocatable Table is a physical-lifetime prototype. Its Reader belongs
to one `(Table, thread)` epoch domain. Resolve under a surviving Section, then
bind a named view around useful repeated work. The measured batching result
supports amortizing Section admission over many resolves; it does not authorize
retaining raw addresses after protection ends or make a physical slot's address
and extent a coherent layout/version publication. Ikea's operation binding and
the Table's relocation identity therefore serve different facts.

This survey's frozen filter/block comparison does not invoke Orbital allocation:
Ikea has no allocator dependency, and its vectors are constructed before timing.
The new allocator needs the existing live owners' padding, held-growth,
relocation/rebind and capture/escape checks at their current source cut. It does
not require restarting that same-allocator comparison or inserting an allocator
framework into the hot filter loop.

### Writes across retargeted blocks

The current flat journal identifies a named view plus plane and byte span.
Changing that view's placement before resolving its old entries makes their
meaning wrong. Small reusable bindings therefore need an explicit owner policy:
resolve the **whole command journal** while the original mapping is current,
or use a pre-admitted `noexcept` effects adapter that immediately translates to
stable owner/generation coordinates. Processing only newly appended entries is
insufficient: a new command can coalesce into the previous last entry.

Existing SeriesPack `named_effects` restores the original source through
physical copies; StreamPack's metadata adapter shifts plane ordinals. Neither
preserves old backing generations. An owner pointer alone also fails if its
backing changes before resolution. Retained captures, immutable generation
mapping or immediate resolution must establish the lifetime. Damage coverage
does not itself grant isolation or publication authority.

The real SeriesPack write path supplies a second ABI/control caution. Its
[source-qualified coverage audit](evidence/interface-cohesion-v2-20261023/coverage-guard/README.md#remaining-linked-path-findings)
finds seven extra instructions in each inspected AVX2 body/head coverage call,
including saved-register bookkeeping. Physical stores are unchanged and the
enclosing region shrinks; the helper work remains one part of the complete
command, without establishing a timing cause. Two bounded controls
that restore packed private journal lowering fail to remove it. There is no
measured basis for undoing the coherent flat hook or for presenting another
record wrapper as a fix. The shared execution-control comparison must therefore
include one/few writes and whole batches with the real journal, source/plane
identity, preflight and summary maintenance. A smaller reader-only binding does
not qualify that mutable command path.

## Discriminating comparisons

Start with the current TuplePack tradeoff, then the existing Frame/SeriesPack
catalogue and packing witness. Reuse maintained operation bodies and independent
scalar/byte oracles; this does not need a generic benchmark framework.

1. Inspect raw return/pass, checked factory return, stored/reloaded binding and
   a crowded caller on NEON, AVX2 and AVX-512. Record actual size/triviality,
   result convention, dependent loads, stack traffic, calls and code/data size.
   Compare the implemented two-pointer reader and prepared predicate with the
   earlier 24-byte reader-cache candidate using the same bytes and commands.
2. Measure complete tiny commands: attach/bind plus one operation, a retained
   binding with one/few operations, and a series of heterogeneous blocks.
   Sweep reuse enough to expose preparation break-even. Keep width traces
   clustered, bursty and unpredictable, with several live independent bindings
   to expose register pressure and available instruction-level parallelism.
3. On the same Frame planes compare its existing catalogue, ordinary bindings,
   and a shared Ikea control candidate. Preserve block order. Include points,
   exact tails, full native windows, sparse masks, empty selections and packing
   with real effects and preserved neighbours. Charge resident controls, lookup
   work, code/table footprint and cold acquisition separately.
4. Test proof transitions: same shape/new placement, changed width, old/new/old
   dictionary editions, relocated owners, changed content and retained writes.
   A fast path cannot borrow stale content proof or expand a physical grant.
5. Use complete caller disassembly to explain a native result. `llvm-mca` can
   test a compute/port-pressure hypothesis; it cannot establish indirect-target,
   cache, ownership or complete-program cost. Use isolated native Zen AVX2 and
   AVX-512, then NEON, balanced order and identical duplicate arms/layout controls.
   Avoid per-value barriers that manufacture store/reload overhead.

Preparation cost, hot execution cost and retained bytes are separate reported
quantities. Representation changes need a separate comparison from handle
mechanisms. Public names and the amount of shared machinery should follow the
smallest interface that wins these actual compositions while preserving their
contracts; a common mechanism may justify disruptive changes now.

## Decisions supported so far

The [handle-boundary comparison](handle-boundaries/README.md) supports keeping
the general TuplePack reader small and preparing predicates separately.
Raw two-word returns use two GPRs; the checked `expected` factories still return
through memory. Stored/reloaded and crowded callers also improve their emitted
work. Repeated predicates keep their copied code and avoid a plan-to-code
dependency. The compact predicate also removes the fallback's plan borrow.
Neither a universal 16-byte budget nor one larger catch-all binding
follows from these results.

The [R4 native comparison](evidence/interface-cohesion-v2-20261023/native-r4.json)
measures that source cut's public endpoint. It leaves x86 losses in some predicates,
SeriesPack writes and a TuplePack body write. Those are separate from the ABI
improvement. The [point-write inspection](evidence/interface-cohesion-v2-20261023/tuple-point-codegen.json)
finds identical loop/kernel bytes with changed kernel placement; the filter
study also retains load scheduling and branch-position differences. This makes
complete caller context part of the mechanism comparison, even when an isolated
handle handoff improves.

The direct-pointer predicate control did not change any complete hot caller on
the three tested profiles; it changes preparation only. This rejects the simple
forwarding-wrapper explanation. A second control cached `stride == 1` in existing
predicate padding and carried that fact into tight address calculation. It saved
eight instructions per eight selected windows on each profile, but the
[complete native comparison](handle-boundaries/PREDICATE-NATIVE.md) rejects it:
all 27 NEON one-shot block cases lose 25–98%, and per-block preparation for one
query loses 8–12% on NEON and 13–22% on x86. Reused predicates and fresh admission
remain close to baseline. The byte-identical duplicate control stays near parity.
No cached flag was adopted.

The actual NEON one-shot caller adds aggregate copies, including a store that
overlaps the pointer immediately loaded by the callee. A store-forwarding hazard
is a plausible microarchitecture explanation, not an established timing cause.
This negative result strengthens the requirement to measure preparation and
complete commands; a shorter retained-query path is insufficient.

The [block witness](handle-boundaries/README.md#complete-block-commands) compares
retained, per-block prepared, one-shot and fresh placement/binding routes over
eight shared plans. Its warm read comparison leaves plan construction, writes,
content-proof transitions and minimal resident-descriptor design separate.
The completed [copied-projection comparison](handle-boundaries/PROJECTION-NATIVE.md)
shows that a single-code predicate does not need the full plan at execution.
It reuses bounded one-byte gathers and drops irrelevant packet capacity from
its type. All 324 complete block cases improve across the three profiles;
seven x86 predicate-route filter cases still lose over 5%, alongside twelve
shared-decode control losses. Actual linked audit finds less selected work in
those seven paths, not a timing explanation. This supports adopting the simpler
plan-independent endpoint while retaining the measured exceptions.

After-only full core/header checks pass on all three profiles. Independent
expired-plan protection covers all 11 packet shapes, and the compact Rows64
predicate passes a fresh fully instrumented poisoned-interior check. The owning
TuplePack lifetime fixture preserves those consequential boundaries after the
spike. These qualifications do not establish that the earlier f153-to-R4
SeriesPack, TuplePack write or StreamPack losses are closed.

Execution independence also exposes a cold-binding question: a predicate-only
owner should not need generic shuffle preparation merely to select one validated
code. There is no direct binder today. A binder taking a validated layout, code
index and named view could copy the existing law's code without another raw-code
validator or projection type. Transfer binding already permits its authoritative
layouts to expire after admission; it retains the named views instead.
It would check extent compatibility and code index before returning; only the
view would remain borrowed. The present shared-plan block witness excludes this
cold preparation/resident-control saving, so it needs separate complete-command
costing. Fewer plan bytes alone would not establish a workload win.

The strongest next generalization is SeriesPack's shared immutable dense
execution control: Frame already supplies the concrete operation-column witness,
while the ordinary runtime reader currently copies several entry pointers.
An Ikea implementation should admit the layout once, select an existing width
control cheaply, and accept fresh placement/count/grants at the whole-command
boundary. Keep typed callers direct, and measure copied versus shared entries
under the tiny and mixed-block comparisons above before choosing the public
representation. Directory's typed Intervals path needs no extra control layer.

Bec's compact proof capsule and StreamPack's compatible-layout retarget are
separate candidates with observable contract questions. They should not be
hidden behind a common `rebind` that appears to refresh content proof or owner
generations. The current survey establishes their concrete seams and tests;
it does not present unmeasured candidates as graduated interfaces.
