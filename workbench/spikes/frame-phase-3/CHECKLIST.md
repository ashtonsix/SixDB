# Before the initial implementation: what to explore or decide

2026-10-24, for Ashton. Merges this thread's draft with Ikea LEAD's reply
(`build/frame-collab/phase3/CODEX-REPLY.md`); items from that reply are
marked **[LEAD]**. Each item says why it matters, what settles it, and where
it stands: **decided** (Ashton), **evidence** (a prototype answered it, with
its caveat), **partial** or **open**.

The checklist has three parts, following Ikea LEAD's split, which I agree with:
commitments that are expensive to change once public; evidence that the
first core is useful; and choices that stay reversible after landing.

Ashton clarified on 25 October that Engine research needs an initial Frame
foundation. This inventory is not a requirement to settle every open item
before landing. Initial capabilities need strong semantics and qualification;
uncertain policies need caller steering, visible facts and useful defaults;
known future mechanisms need explicit extension points. Unsupported modes stay
explicit. B11 and broader consumer-driven interface research follow the core.

Ashton subsequently excluded checked private preparation from the initial build.
Change protocols and write histories below are research evidence/extension
obligations, not initial capabilities. Construction/emission and immutable binding
land; `prepare_group`, its mutation driver and scratch do not.

## A. Contract obligations and explicit initial capabilities

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| A1 | **Five descriptions, kept apart [LEAD]**: logical contract, admitted fitting policy, actual representation, bound execution, evidence and costs | Recalibrating policy, rebinding or converting must not reinterpret stored bytes | Change policy and rebind over unchanged bytes; old and new readers each keep their own interpretation | Evidence: program, policy, image metadata, bound execution and evidence are separated in the checked owner; immutable interpretation and policy-change witnesses are retained. Public extraction remains. |
| A2 | **A standard pipeline description** with stage identifiers, parameters, child identities and coordinates, including shared state | Engine must name, store and rebuild a pipeline. Independent trees cannot describe a shared TuplePack record or a framed-checkpoint child | Encoded description (Ikea's envelope) round-trips into fresh bindings for the landing pipeline, shared record and rank map | Evidence within the admitted subset: canonical bounded DAG and sharing/coordinate round-trips; RLE, H8/H16, TuplePack and rank/split shapes are explicit and executable factories refuse unsupported kinds. Map/route/domain shapes remain unproven. |
| A3 | **Logical types and operation results [LEAD]** | NULL, ordering, overflow, empty aggregates and selection order cannot be added later by relabelling u64 | State the first domain (unsigned integers), SUM result width, empty MIN/MAX, `select` order and count; declare what is unsupported | Defined initial owner domain: ordered non-null UInt64, exact UInt128 SUM, UInt64 COUNT, zero empty SUM/COUNT and ordered SELECT. Public MIN/MAX/multi-aggregate wrapper semantics still need reconciliation with the stage prototype. |
| A4 | **The composition byte law is Frame's [LEAD]**: descriptors, offsets, exception membership, slack, group sections | Leaf-valid bytes can still be inconsistent; calling it glue pushes the obligation onto Engine | Canonical encoding, versioning, `validate()`; malformed or partial images refused before unsafe access | Evidence: checked group/stream image admission, malformed/guarded images and form-specific interval proofs, including the zero-checkpoint-field aggregate counterexample. Frame owns this law. |
| A5 | **Selection is demand, not read authority [LEAD]** | A late delta row needs earlier residuals and a checkpoint; a packed write reads neighbours. Query masks must not imply read grants | A delta read under a broad and a restricted grant: dependency closure admitted explicitly, or an early refusal | Evidence: explicit section availability/dependency closure, inaccessible-payload refusal and payload-free domain settles. Fine selection supplies no read authority; grants currently cover admitted immutable sections. |
| A6 | **Partial binding [LEAD]**: attach metadata, leave unselected group payload absent | Eager attach of every group defeats coarse skipping and Loom staging | Sparse group selection works with excluded payload inaccessible | Evidence: complete metadata first, sparse section attachment and cached per-group readiness; missing checkpoint payload refuses demanded work before output. |
| A7 | **Change protocol in full**: metadata classify → admit reads → payload-dependent proof → admit stores and resources → apply without failure → resolve effects → publish | Classification alone is not admission; a rebuild after refusal is real work that also needs admission | Multi-child change refuses on value, capacity or effects with all state unchanged | Research only: basic private-afterimage correctness evidence is retained, but preparation and all Frame mutation APIs are excluded from the initial build. Preserve these obligations without freezing that implementation. |
| A8 | **Preparation lifetime across waits [LEAD]** | A plan built on one basis can be wrong after another publication | Prepare, interleave a publication, resume with the original proof or revalidate | Evidence: retained Scope/basis, intervening publication, full same_material comparison and explicit re-preparation on a named fresh basis. A retained asynchronous Loom lease/cancellation API is not established. |
| A9 | **Update versus pure conversion** | A SET that rebuilds advances logical time; compaction must not | Slack-exhausting SET publishes N+1 with one contribution; compaction-only re-encode adopts N with none | Evidence: real Orbital logical updates and same-version adoption, independent equivalence checks, retained old readers and bidirectional sizes/relative conversions. Current conversions use full rebuilds. |
| A10 | **Interpretation travels with the material [LEAD]** | In the prototype the driver, not the Representation, knows the tree | Hold an old selection, append, convert between two programs, drop transient objects, read both histories | Evidence: material owns canonical program, row/schema/Part geometry and stream map; selected Parts and old readers retain interpretation/backing after transient builders are destroyed. |
| A11 | **Ownership and grains are recipe facts, not assumptions**: fitting block, checkpoint, leaf batch, re-encode region, allocation, Part, Object | A shared record or dictionary need not follow one column/group split | One shared-record update and one independently retained group under one contract | Partial: stream domains and physical Parts are independent, including checkpoint domains and disjoint co-packing; shared-record update/ownership remains a placeholder. |
| A12 | **Three published outputs** of a change: logical contributions, issued bytes, evidence invalidations | An unchanged field can still change a summary; metadata-only changes can change every answer | Data published with matching COUNT/SUM/extrema validity, including abort and removed-extremum cases | Research evidence: basic afterimages carry exact SUM contribution/final extrema and private apply stores. Keep the distinctions for future changes; no initial mutation/publication API is implied. |
| A13 | **What replay promises [LEAD]** | Equal digests on one layout do not prove representation-independent replay | Same ordered commands on two legal representations give the same results and contributions | Research evidence: ordered/repeated/restored commands and both interval-form histories match original values/contributions. No Frame command/replay API lands initially. |
| A14 | **Laws that decline; costs that are unknown [LEAD]** | A map that does not preserve order must not inherit an ordered compare; an unpriceable child must not look free (split did) | One law declines on a false premise; an unpriced child is "unknown" or refused, never zero | Open |
| A15 | **Leaf kernels compiled once in Ikea** | Each 16-width TU costs 4–7 GB to build inside Frame | SeriesPack erased range operations; curated compiled operations kept distinct from the author route | Done: ikea 0d44d527 adopted as the runtime leaf, timing parity, build halved ([findings §3](FINDINGS.md#seriespacks-compiled-once-operations-adopted)) |
| A16 | **Reserved capacity for point writes outside bounds** | Writes must not force re-encode | Per-group exception slack | Capacity/layout facility retained; point-write execution and private preparation deferred. Reserved bytes do not establish an initial mutation capability. |

## B. Foundation evidence and research through consumers

| # | Item | Why it matters | Small evidence that settles it | Status |
| --- | --- | --- | --- | --- |
| B1 | **Three seam cases on the same images [LEAD narrowing]**: (1) descriptor-heavy COUNT in one traversal; (2) predicate on A then selected work on B, retained mask against fused consumption; (3) independent points through the real dependency chain, adding Loom staging only where there is a wait to hide | Ashton asked which seams we compared; only virtual calls and one point plan were. Templates, CPS, plans and Loom are partly orthogonal, so a full matrix would not settle an interface | Each case with the ready path, then CPS or inlining where a handoff is the identified cost, then Loom where acquisition waits | Evidence ([findings §7](FINDINGS.md#7-three-seam-cases-on-the-same-images)): the descriptor COUNT pass helps on Zen 5 only and still loses 1.5× to the native count on OS and EventTime; per-part fusion beats a retained mask in every cell; dispersed pairs lose 1.6× (Zen 5) to 3.4× (Graviton 4) to the native pair; the pipelined point plan is 2.6–6.4× behind front A's materialising gather, and entries co-located with bodies are the next test |
| B2 | When a cell earns a bind-time plan | Plans are "critical paths hand-fused"; too many re-create a fixed pipeline | Rule from B1 | Points: evidence |
| B3 | **Delta lands with useful routes, not a mandated algebra**: sizes and relative ends; fused weighted consumer or fused prefix-and-consume; monotone predicates | Delta on the fallback is 5–6× slower; the masked-sum law lost to decoding | Point, selected aggregate and monotone predicate for both forms, fastest sound route | Partial: framed checkpoints pay; relative ends make SUM 1.1–8× and points 1.4–3.4× faster than sizes but cost bytes on four of eleven columns, so the form is a per-column fit choice; sizes still needs a fused weighted SUM |
| B4 | Signed values and offset binary | Delta needs contiguous windows | Checks on signed columns | Partial |
| B5 | Constants, and incompressible full-width values | Common boundaries; CounterID costs 2–7× per block; URLHash SUM loses 2.6× to u64 | Descriptor-only work stays cheap; an identity leaf where compression cannot repay | Open; front A's corpus shows URLHash emitting 528,864 B against 524,288 B native, so Engine must keep a real native column where Frame does not repay; `pack` alone is not that choice |
| B6 | One traversal, several results | Phase 2's one-decode scan gave 1.4–3.1× | SUM with COUNT and MAX on one selected input | Partial: one-pass `summarise_selected` is 1.8–2.1× faster than two passes on Graviton 4 with a clustered predicate, mixed on Zen 5; bound's block-bound MAX law is oracle-checked, not timed |
| B7 | Cross-column selected movement | Frame's stated strength; tests the row-domain contract | A filters B; B reduced and projected; clustered and dispersed at equal selected counts | Evidence (exactness only): A's filter selects B under a different tree, clustered and dispersed, partial final part; B's SELECT in row order, SUM and MAX checked. Not timed, and B's read grant is the owner's (A5) |
| B8 | **Group-local draft [LEAD]**: prepare once, draft the touched group, several ordered changes, emit once | Draft/apply/emit costs 9–17 µs per write, mostly attaching all groups | Copied, new and retained bytes reported per publication | Closed experiment, excluded from initial build at Ashton's request. Local one-edit preparation is 21–26 µs; saving 9–12 µs versus attach-all does not qualify a write path. Revisit architecture/performance later; see owner/EVIDENCE.md. |
| B9 | A complete write/read history | Cheap reserved writes can hide deferred cost | Exhaustion, fold or conversion, old readers, exact derived state, on one history | Research write history: real Orbital old/current readers, exhaustion, refusal/stale retry, append/conversion and exact facts. Initial conformance retains interpretation/old readers over externally built replacements; private mutation stays research. |
| B10 | Scan over many groups through one prepared operation | Group-local placement costs scans 1.1–1.6× from calls per group and width | A leaf call spanning groups, or a scan/write weight | Open |
| B11 | **One consequential Engine choice, judged by regret [LEAD]** | Ashton's bar is analysis that makes impactful decisions; the preset test was too easy and absolute costs were off 34–83% | Choose delta or identity, or scan- or point-oriented policy, across a workload reversal, charging build, change and conversion | Follow-on Engine research using the landed core, per Ashton's 25 October ordering steer. Keep the real native candidate, reversal and lifecycle accounting; this is not an initial-landing prerequisite. |
| B12 | Strong controls and honest residence | This thread's own controls are weak; the brief's 2× bar is untested | Front A's native and fused pair controls; existing Polars and DuckDB controls timed; resident, flushed and bandwidth-bound runs labelled | Evidence with explicit limits: source-qualified native count-only/gather/pair on Zen 5/Graviton 4 and actual DuckDB/Polars plus Frame/native six-cell local comparisons. Supplied-mask/native-system/residency and strong writer comparisons are deferred; latest combined source qualification is still needed. |
| B13 | Placeholder shapes not yet tried: map (part state), presence (rank map), route (n children), domain (ALP exceptions) | Untried shapes may force more contract changes | Fit, emit, decode and a declining law for each | Partial: runs, split, record done |
| B14 | Row protection as an owner-supplied grant, separate from selection | Ashton listed row-level protection as Frame's | Grant enforced before address formation | Open |
| B15 | Concurrency over one group | Real workloads | Overlapping envelopes admitted or refused | Open |

## C. Keep reversible after landing

Group and block size, slack amount, checkpoint spacing, fusion thresholds,
fit speed versus quality, cost coefficients and plan caching. Each can change
locally once its interpretation and invalidation rules are explicit (A1, A4).
Deferred encodings and operation families stay in research; their
placeholders need only the shape, sharing, coordinate, dependency and
ownership hooks.

## Not required for the first landing

A universal kernel registry, a general optimizer, user-defined operation
compilation, every topology sweep, a new lock manager, or durable
transactions and recovery. Existing owners' contracts are the boundary.

## Division of work Ikea LEAD proposes

- **Ikea LEAD:** the public description and binding contract (A1, A2, A4,
  A6, A10), one maintained owner-composition example and conformance fixture
  (A7–A9, B8, B9), and a complete-program scorecard against qualified
  controls (B12). The group-local draft experiment is closed and deferred;
  immutable description/binding and interpretation-lifetime conformance feed
  extraction.
- **This thread:** stage laws, the execution driver and seam cases (B1–B7,
  B10, B13), and the Engine-choice witness (B11).
- **SeriesPack owner:** compiled leaf operations (A15).

This is Ashton's call. It matches the Claude/Codex split by strength that
he proposed for phase 2.
