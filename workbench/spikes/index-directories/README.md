# Index directories

The current instruction is to finish the composition review, performance budget
and full algorithm on paper before more implementation. Existing fixtures and
unfinished `engine/` sources do not establish a useful mutable directory or a
complete Engine performance result. This review changes the implementation
strategy, not the supplied index model.

- [Budget](BUDGET.md): matched peers, operation ceilings and limits.
- [Pseudocode](PSEUDOCODE.md): reads, batches, balancing, extras, schema and failure.
- [Design](DESIGN.md): byte/ownership ledgers, implementation choices and prior art.
- [Findings](FINDINGS.md): measurements, source identities and recovery.

## Spike plan and present decision

The proposed objective is complete Engine at least twice as fast as the faster
valid Calico/LMDB comparator. Initially allocate at most one quarter of that
peer's time to all directory, binding and coupled publication work. The sum
across a request matters; five layers do not receive five budgets. These are
aggressive planning allocations, not measured achievements or inherited APIs.

The actual composition reviewed for this plan is:

- Ikea's [owner integration](../../../ikea/docs/integration.md), actual
  [effects](../../../ikea/include/ikea/effects.h), Hashtable owner/clone/native
  interfaces and current Boundlist/StreamPack portfolio. Coverage precedes
  issued stores; it supplies neither isolation nor beforeimages.
- Orbital's [material/source contract](../../../orbital/docs/material.md),
  [capture implementation](../../../orbital/include/orbital/instantiate/representation.h)
  and [prepare/install](../../../orbital/include/orbital/instantiate/object.h).
  Capture retains physical sources without retaining ancestor Representations.
  One existing Core publishes finished changes at one cut.
- Loom's [ordinary/retained operation rules](../../../loom/docs/usage.md) and
  actual allocation caller. Ready work is ordinary native work. A per-Part Hold,
  Task, Ring visit or scheduler entry is not required for each lookup.
- Calico's [actual trie design](../../../../calico/design/TRIE.md),
  [QHash results](../../../../calico/qhash/REPORT.md) and prepared runtime caller;
  the [prior-art survey](DESIGN.md#prior-art-carried-into-the-experiment) relates
  each source to a specific implementation question.

The paper work now has four concrete comparisons, rather than a growing matrix:

1. Trace one locator replacement from original Scope through final cleanup.
   Price the actual 1 KiB/three-Part F32/C16 leaf against 2 KiB/five-Part
   F128/C32. Both preserve stable child names, so a payload-only change causes
   no ancestor publication. F128 can save a routing level but retains more
   sources per edit. Keep the measured raw-child graph as a read control and
   count its path captures/child retentions in write comparisons.
2. Check logical seek, prefix lookup, continuation and selected material against
   the same peer-derived envelope. Distinguish a qualified direct byte recipe
   from a recipe requiring authoritative runtime geometry/corrections. The
   extra material/schema selections in the latter cannot be priced as the former.
3. Normalize final membership batches, not scalar intermediate trees. Prove
   occupancy, duplicate-boundary behavior, cross-parent repairs and name lifetime.
   F32 needs repair at most 375 ns to support the conservative 75 ns amortized
   structural allocation; F128's 1.5 us goal gives about 71.4 ns. These include
   their actual repair work and charged publication/retirement, not just copying.
4. Use fixed original-suffix ranks for the first extra-list recipe. This removes
   holder enumeration and rank-repair work from that candidate, at the price of
   rebuilding one bounded local StreamPack image. Compare its whole byte cost;
   sixteen holders alone does not bound the payload size.

No performance fit is established for the complete mutable composition. The
existing stable-name probe lost, and graph read timings cannot certify a different
stable-name implementation. Dense scans also have very little complete-request
headroom in the one-record-per-Segment stress case. Keep these as unresolved
costs; do not fill them with sums of unrelated kernel timings.

Once the paper path has no unsupported calls or unpriced work, the first future
implementation experiment should be the smallest complete read/update/balance
witness with actual Ikea/Loom/Orbital calls and native peers. Take measurements
from that first witness, including failure and cleanup. If it misses, revise the
algorithm before adding features. No broad tuning campaign is needed now.

The eventual canonical driver must separate record population, Segment population
and material bytes: multiple ordered rows per independently allocated Segment,
duplicates crossing Segment boundaries, projected/full reads, actual ranges,
one-field updates and atomic multi-Segment batches. Include sparse/empty/tail
cases and a one-row stress control. Its row and summary work belongs to the
driver/producer; directory inputs are Segment facts. The retired allocation and
Stage 2 drivers remain [historical controls](../../benchmarks/retired-drivers.md).
Shore frontend work remains planning only.

## Proposed graduation cut

Graduate one complete mutable directory implementation, with a real Segment
producer using its public API. Do not promote `GroupFixture` as a general
directory or narrow the supplied semantics to make its present code sufficient.
This is a proposed implementation cut, not an established public contract.

The first consumer is a Segment producer/query caller: it supplies finished
Segment representations and their directory facts, reads them through the
directory, and publishes actual Segment and directory changes together. A data
update whose directory facts remain unchanged emits no directory change. Record
placement, compression and summary maintenance belong to that producer.

| Public operation | Meaning |
| --- | --- |
| Read view | Borrow the caller's existing Scope and shared Store/Core, with admitted index/schema interpretation, through every byte use. |
| Exact prefix lookup | Find the Segment at the requested index, layer and physical prefix; gaps are misses. |
| Logical seek and continuation | First possible overlap and continuation within that layer, including duplicates, open/closed edges and full-key refinement. |
| Physical-prefix range | One seek and ordered continuation in that layer, with optional original-coordinate selection. |
| Part projection | Interpret inline or admitted extra locators under the selected definition and actual Segment association; return qualified material or an explicit readiness outcome. |
| Batch edit | Coalesce final deltas under the caller's basis and produce finished directory Owners/changes, including birth/death, bounds and representation/locator changes. |

The caller combines these finished changes with actual Segment/schema changes
in one existing Orbital prepare/install packet. The directory uses that shared
Core and basis. It introduces no private Store, version ledger or second commit.
A 64-command fixed-membership locator witness can produce 65 graph changes or
up to 128 named-array changes. These are not general caps for membership,
balance, schema or extras. Derive packet capacity from actual finished changes
and admitted workspace; above-64 uniqueness work is a separate cost.

Use one concrete recipe at each supplied route: ordinary L0/L1 tables, actual
Ikea Hashtable-owned inline 64B values for eligible shared-prefix routing, and
one stable-name ordered tree using actual Boundlist elsewhere. There is no
second payload map. Keep the raw-child graph as a measured read control; its
ancestor captures/child retentions need not be the first shipped write strategy.
G16 physical directory packing is provisional, not an optimal logical-leaf size
or a persistent ABI. Choose one bounded ordered leaf/Part geometry; completing
every alternative is unnecessary before implementing this consumer.

The mutable ordered path needs Segment birth/death, duplicate-aware bounds,
changed-extrema propagation and legal leaf/internal split, merge or
redistribution. Storage balancing never splits a prefix-defined Segment or
assigns record keys.

Schema support means retained definitions, per-layer allowed representation IDs,
and validation of supplied before/after interpretation; no migration manager is
required. Implement one qualified extra-locator recipe for representations
admitted with more than six Parts. Refuse unsupported recipes at admission.
Packing policies and alternative extra stores can follow. An unchanged inline
dirent never proves its extra list unchanged. Plans, telemetry and persistent
cursor stores are separate consumers.

## Remaining implementation and checks

Extract Engine-owned headers, material adapters and implementation from fixture
constants. Formats, proofs and resources must survive Scopes, escaping Parts and
candidate cleanup. The fixture's member destruction order is not a production
ownership solution. Preserve supported Lane affinity; do not advertise
unqualified cross-Lane exports.

Implement actual batch coalescing. Select each affected group/Part once, validate
expected before binding and the finished replacement's actual locator association,
and materialize each changed Part once. Reuse the request-local plan. Validate
commands even if later commands cancel them. A final no-op allocates, captures
and publishes nothing: it returns an empty directory-change proposal qualified
by its read basis, not a current-state transaction receipt. The caller still
validates/publishes any Segment/schema effects through Orbital. Unwind accumulated
Owners on failure and clear caller-owned change
slots after success. One atomic batch is never repeated scalar installations.

Complete ordinary/hash/ordered paths, membership/bounds repair, legal balancing,
schema admission and the chosen extras recipe. Current public Ikea, Orbital and
Loom APIs can express this. No new Engine MVCC, undo queue, reader registry,
Task/Ring protocol, range-source or prepared-packet protocol is a prerequisite.

Use one canonical driver through the library API with real independent Segment
Allocations. Its independent oracle checks membership, full key/bounds, every
defined dirent byte and actual Part/definition association. Cover repeated,
cancelled and invalid commands; clustered/dispersed batches; first/last and
empty transitions; duplicate fences; structural boundaries and maximum prefixes.
Old readers first discover later leaves after publication/drain/trim; fresh
readers see the new state. Capture/prepare/stale refusals, reused workspaces,
exported uses and final disposal must preserve that distinction. A drain limit
does not bound recursive last-owner graph disposal.

Retain matched native comparisons plus actual footprint and cleanup accounting.
The stronger grouped DUPFIXED peer is behavior-qualified and available, but
still unmeasured. Resident results imply no durability, remote I/O or complete
Engine performance. A general Engine driver remains a separate, owed expansion.

## Evidence and follow-on work

Checkpoint `a89b9f4e` has real module composition, six native checks, full
eight-word consumers and saved-frame range traversal. The expanded scan check
covers 9,600 cases and 105,540 ordered selected items. The newer guarded driver
also qualifies actual native run seeks, zero/nonzero reused handles and
uninstrumented timed native code.

Fresh 262K native mapping is 32.324 MiB: 16 MiB independent Segment material,
16 MiB dirents and 0.324 MiB routing. Metadata request/backing is
25.091/25.425 MiB; old spare-pool waste is gone. G16 fresh RSS is 69.738 MiB.
Those are per-Segment costs in a one-row-per-Segment stress fixture, not low
complete Engine memory per record. G256 retains 12 MiB more in the aged survivor
witness. Controlled material spacing changes point throughput by about 8%,
without identifying a cache/TLB/bank cause or universal G16 superiority.

Large unmasked scans retain tight headroom; the pointer-bank trial loses and
stops. At one selected bit/64, the stronger native run peer narrows the comparison
but Engine still leads 5.75–7.31x. Zero-mask results are modest bitset/lifecycle
differences with no entries. These component findings are not complete Engine
acceptance or new graduation gates.

Earlier requested comparisons remain accounted for: stable-name versus graph
write cost, logical 64/256 leaf/Part alternatives, fanout/custody variants,
overflow packing, broader representations/schema and the general Engine driver.
They remain research/follow-on work unless the concrete first implementation
needs them. Exact hardware attribution, universal grain selection and another
prefetch campaign need not precede the functional module. Physical-key
assignment/rebalancing, record/summary codecs and the broader end-to-end
Calico/LMDB target stay with their actual Engine producers.

## Supplied model and composition

Each of five layers has prefix-defined Segments. The overlapping 16-bit windows
are 0:16, 8:24, 16:32, 24:40 and 32:48. A Segment has at most 65,536 positions
and exists only while nonempty. The last layer contains records; earlier layers
contain the specified summaries and may contain other structures. There is no
order across layers. Seek/continuation stays in one layer; explicit descent
changes the layer.

Physical keys are unique six-byte keys. Logical keys have K bytes and may repeat;
the first J bytes coincide. Physical placement preserves full logical order,
including slots within a Segment. Equal logical keys have no internal order.
Admit J<=min(6,K) and valid populations: a shared prefix has only 2^(48-8J)
physical positions. J6 allows at most one record per shared six-byte prefix,
including distinct longer logical keys with that prefix.

A Segment has one representation. Schema may retain many historical
representations, with different allowed sets by layer. A dirent is 64 bytes:
six eight-byte locators plus sixteen metadata bytes. Directory facts are
existence, routing bounds, representation and complete Part association.
Summary validity/corrections remain data facts. Stable directory locators do not
waive the selected Segment geometry or correction law.

Ikea operates on admitted borrowed/private bytes and reports coverage before
local writes; it supplies no beforeimages or isolation. Orbital seals captured
bodies/padding, retains sources and owns snapshot selection and atomic publication.
Loom supplies physical access and execution: ordinary calls for ready work,
retained Tasks for real waits. A root Part protects directory children, not
independently versioned Segment data; that data still needs the same live Scope.
