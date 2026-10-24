# Before the initial implementation: what to explore or decide

2026-10-24, for Ashton. Merges this thread's draft with Ikea LEAD's reply
(`build/frame-collab/phase3/CODEX-REPLY.md`); items from that reply are
marked **[LEAD]**. Each item says why it matters, what settles it, and where
it stands: **decided** (Ashton), **evidence** (a prototype answered it, with
its caveat), **partial** or **open**.

The checklist has three parts, following Ikea LEAD's split, which I agree with:
commitments that are expensive to change once public; evidence that the
first core is useful; and choices that stay reversible after landing.

## A. Decide before fixing the public contract

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| A1 | **Five descriptions, kept apart [LEAD]**: logical contract, admitted fitting policy, actual representation, bound execution, evidence and costs | Today one C++ tree plays all five roles. Recalibrating policy, rebinding or converting must not reinterpret stored bytes | Change policy and rebind over unchanged bytes; old and new readers each keep their own interpretation | Open |
| A2 | **A standard pipeline description** with stage identifiers, parameters, child identities and coordinates, including shared state | Engine must name, store and rebuild a pipeline. Independent trees cannot describe a shared TuplePack record or a framed-checkpoint child | Encoded description (Ikea's envelope) round-trips into fresh bindings for the landing pipeline, shared record and rank map | Open (only C++ constructors today) |
| A3 | **Logical types and operation results [LEAD]** | NULL, ordering, overflow, empty aggregates and selection order cannot be added later by relabelling u64 | State the first domain (unsigned integers), SUM result width, empty MIN/MAX, `select` order and count; declare what is unsupported | Partial |
| A4 | **The composition byte law is Frame's [LEAD]**: descriptors, offsets, exception membership, slack, group sections | Leaf-valid bytes can still be inconsistent; calling it glue pushes the obligation onto Engine | Canonical encoding, versioning, `validate()`; malformed or partial images refused before unsafe access | Prototyped informally (group images) |
| A5 | **Selection is demand, not read authority [LEAD]** | A late delta row needs earlier residuals and a checkpoint; a packed write reads neighbours. The README conflated access masks with query selections | A delta read under a broad and a restricted grant: dependency closure admitted explicitly, or an early refusal | Open; README to be corrected |
| A6 | **Partial binding [LEAD]**: attach metadata, leave unselected group payload absent | Eager attach of every group defeats coarse skipping and Loom staging | Sparse group selection works with excluded payload inaccessible | Open |
| A7 | **Change protocol in full**: metadata classify → admit reads → payload-dependent proof → admit stores and resources → apply without failure → resolve effects → publish | Classification alone is not admission; a rebuild after refusal is real work that also needs admission | Multi-child change refuses on value, capacity or effects with all state unchanged | Partial: metadata classify, envelopes, capability check before rebuild |
| A8 | **Preparation lifetime across waits [LEAD]** | A plan built on one basis can be wrong after another publication | Prepare, interleave a publication, resume with the original proof or revalidate | Open |
| A9 | **Update versus pure conversion** | A SET that rebuilds advances logical time; compaction must not | Slack-exhausting SET publishes N+1 with one contribution; compaction-only re-encode adopts N with none | Open |
| A10 | **Interpretation travels with the material [LEAD]** | In the prototype the driver, not the Representation, knows the tree | Hold an old selection, append, convert between two programs, drop transient objects, read both histories | Open |
| A11 | **Ownership and grains are recipe facts, not assumptions**: fitting block, checkpoint, leaf batch, re-encode region, allocation, Part, Object | A shared record or dictionary need not follow one column/group split | One shared-record update and one independently retained group under one contract | Open |
| A12 | **Three published outputs** of a change: logical contributions, issued bytes, evidence invalidations | An unchanged field can still change a summary; metadata-only changes can change every answer | Data published with matching COUNT/SUM/extrema validity, including abort and removed-extremum cases | Partial: SUM exact, extrema precision |
| A13 | **What replay promises [LEAD]** | Equal digests on one layout do not prove representation-independent replay | Same ordered commands on two legal representations give the same results and contributions | Partial: digest-equal replay |
| A14 | **Laws that decline; costs that are unknown [LEAD]** | A map that does not preserve order must not inherit an ordered compare; an unpriceable child must not look free (split did) | One law declines on a false premise; an unpriced child is "unknown" or refused, never zero | Open |
| A15 | **Leaf kernels compiled once in Ikea** | Each 16-width TU costs 4–7 GB to build inside Frame | SeriesPack erased range operations; curated compiled operations kept distinct from the author route | Decided; owner work in progress |
| A16 | **Reserved capacity for point writes outside bounds** | Writes must not force re-encode | Per-group exception slack | Decided |

## B. Establish through the first complete consumer

| # | Item | Why it matters | Small evidence that settles it | Status |
| --- | --- | --- | --- | --- |
| B1 | **Three seam cases on the same images [LEAD narrowing]**: (1) descriptor-heavy COUNT in one traversal; (2) predicate on A then selected work on B, retained mask against fused consumption; (3) independent points through the real dependency chain, adding Loom staging only where there is a wait to hide | Ashton asked which seams we compared; only virtual calls and one point plan were. Templates, CPS, plans and Loom are partly orthogonal, so a full matrix would not settle an interface | Each case with the ready path, then CPS or inlining where a handoff is the identified cost, then Loom where acquisition waits | Open |
| B2 | When a cell earns a bind-time plan | Plans are "critical paths hand-fused"; too many re-create a fixed pipeline | Rule from B1 | Points: evidence |
| B3 | **Delta lands with useful routes, not a mandated algebra**: sizes and relative ends; fused weighted consumer or fused prefix-and-consume; monotone predicates | Delta on the fallback is 5–6× slower; the masked-sum law lost to decoding | Point, selected aggregate and monotone predicate for both forms, fastest sound route | Partial: framed checkpoints pay |
| B4 | Signed values and offset binary | Delta needs contiguous windows | Checks on signed columns | Partial |
| B5 | Constants, and incompressible full-width values | Common boundaries; CounterID costs 2–7× per block; URLHash SUM loses 2.6× to u64 | Descriptor-only work stays cheap; an identity leaf where compression cannot repay | Open |
| B6 | One traversal, several results | Phase 2's one-decode scan gave 1.4–3.1× | SUM with COUNT and MAX on one selected input | Open |
| B7 | Cross-column selected movement | Frame's stated strength; tests the row-domain contract | A filters B; B reduced and projected; clustered and dispersed at equal selected counts | Open (single column only) |
| B8 | **Group-local draft [LEAD]**: prepare once, draft the touched group, several ordered changes, emit once | Draft/apply/emit costs 9–17 µs per write, mostly attaching all groups | Copied, new and retained bytes reported per publication | Open |
| B9 | A complete write/read history | Cheap reserved writes can hide deferred cost | Exhaustion, fold or conversion, old readers, exact derived state, on one history | Partial (e2e) |
| B10 | Scan over many groups through one prepared operation | Group-local placement costs scans 1.1–1.6× from calls per group and width | A leaf call spanning groups, or a scan/write weight | Open |
| B11 | **One consequential Engine choice, judged by regret [LEAD]** | Ashton's bar is analysis that makes impactful decisions; the preset test was too easy and absolute costs were off 34–83% | Choose delta or identity, or scan- or point-oriented policy, across a workload reversal, charging build, change and conversion | Open |
| B12 | Strong controls and honest residence | This thread's own controls are weak; the brief's 2× bar is untested | Front A's native and fused pair controls; existing Polars and DuckDB controls timed; resident, flushed and bandwidth-bound runs labelled | Open |
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
  controls (B12). First concrete step: the group-local draft and
  interpretation-lifetime witness.
- **This thread:** stage laws, the execution driver and seam cases (B1–B7,
  B10, B13), and the Engine-choice witness (B11).
- **SeriesPack owner:** compiled leaf operations (A15).

This is Ashton's call. It matches the Claude/Codex split by strength that
he proposed for phase 2.
