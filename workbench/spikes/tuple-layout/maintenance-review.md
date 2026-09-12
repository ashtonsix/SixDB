# TuplePack mutation maintenance review

This is a design review for the first Ikea TuplePack implementation, not a
selected Engine interface or promotion of row signatures into production.
Engine's analyser stays spike-only; manual layouts remain acceptable for
higher-level spikes. The recommendations below preserve the ownership boundary
in [Ikea integration](../../../ikea/docs/integration.md) while allowing TuplePack's
code maps and native carriers to differ from SeriesPack's.

## Reuse the ownership contract, keep the maintenance law explicit

The reusable surface is small: issued byte spans, journals qualified by actual
substituted source, whole-call admission, infallible/non-suspending hooks, and
owner-controlled publication. Moving the span and qualified journal types into
Ikea with SeriesPack aliases is consistent with their current meaning in
[effects.h](../../../ikea/include/ikea/seriespack/effects.h). A source pointer is
a borrowed execution identity; the owner resolves it into stable storage and
generation coordinates before the named view expires.

Keep code maps, execution grains, traversal, native carriers and semantic
maintenance recipes module-specific. Ikea supplies raw code observations and
physical effects. The caller binds their field meaning, transforms, hash/bin
parameters, summary dependencies and visibility edition. A layout change can
preserve values while changing owners and footprints; changing bin boundaries
can invalidate evidence without changing any bytes. The signature spike's
[semantic model](../row-filter-signatures/semantics.py) gives an explicit witness:
130 encoded under boundaries 25/50/75 is incorrectly certified as greater than
150 when its unchanged code is interpreted under boundaries 125/150/175.

SeriesPack's [sum_change](../../../ikea/include/ikea/seriespack/author/summaries.h)
is an optional modulo-u64 replacement law. It does not generalize to hashes,
extrema, nullable predicates, or arbitrary code fragments. For an unsigned byte
split into low/high nibbles, `0x0f -> 0x10` changes the value by one, but the
unweighted fragment sum changes by minus fourteen. Weighting can repair this
particular law; TuplePack cannot infer a universal semantic reduction.

## Separate observation demand from invocation

Current SeriesPack
[replacement code](../../../ikea/include/ikea/seriespack/detail/mutation/assignment.h)
uses `Summary::needs_before` both to request old values and to decide whether
to invoke `observe`/`observe_scalar`. An after-only or coordinate-only observer
cannot use that seam without pretending it needs old values. TuplePack should
separate observer invocation, requested before/after observations, and original
logical coordinates plus caller semantic context. No universal summary IR is
needed to express those independent requirements.

Reads needed to preserve neighboring bits are independent of reads needed for
a before-image. An after-only recomputation may also read unchanged fields.
Conversely, invalidation may need coordinates but no value observation at all.
An empty observation projection must therefore not suppress maintenance on a
nonempty selected mutation. An empty execution selection should remain a no-op.

Do not derive row identity from callback order. SeriesPack's
[striped traversal](../../../ikea/include/ikea/seriespack/detail/mutation/composed.h)
may observe four 16-row groups in order `0,32,16,48`; an incrementing callback
cursor assigns two groups to the wrong rows. TuplePack packet lanes can also
name multiple codes per row. Observation ordering and row/code coordinates must
be explicit, while native consumers remain free to accumulate without an
unnecessary scalar or fixed-16-row reduction.

## Pressure-test a separate observation projection

Let **M** name the mutation destinations and **O** the code dependencies supplied
to maintenance. Keeping them separate is useful: replacing one field need not
write every field used by a shared signature. It requires these constraints:

- **Resolve both through the current composition.** Admit the union of their
  used leaves, with readable access for O and writable access for M. An untouched
  dependency can live in another child or owner. O must not silently read a
  retired parent field after substitution. The enclosing binding supplies a
  compatible logical and semantic context for both maps.
- **Define whole-operation before/after values.** If M replaces A and B in
  different packets and O computes a hash of `(A,B,C)`, after means final new A,
  final new B, and preserved C. Observing after only the first packet produces
  an intermediate signature. Requested before-values must be captured before
  intersecting writes. An implementation can merge replacements into retained
  observations or read final storage under exclusion; it must not accidentally
  substitute execution order for these semantics.
- **Treat dependency overlap deliberately.** O reading storage that M writes
  is legitimate and needs the ordering above. This differs from mutation input,
  selection, journal or binding metadata aliasing storage whose mutation could
  corrupt the command. Shared bytes do not imply overlapping semantic fields.
- **Bound the observation unit.** O can require more codes than M or more than
  one native packet. Either admit a complete supported observation unit or
  expose its coordinates and completion to a retained native consumer. Do not
  claim complete-row recomputation from one partial packet. Whole-operation
  admission must reject unsupported shapes before any packet stores.
- **Keep wider dependencies explicit.** A projection of the changed row cannot
  determine whether another row is the last witness for a rollup bit. Exact
  removal needs caller-owned counts, an explicitly bound wider read/rebuild,
  or another maintenance choice. O need not become an unbounded dependency
  discovery or scheduling mechanism.

Joint pattern presence makes the distinction concrete. If two four-bit field
tags share one byte, replacing A's tag changes the complete A/B byte that a
joint summary records, including preserved B. Marginal summaries instead need
individual tags. The native [summary build](../row-filter-signatures/model.cpp)
indexes joint presence by both plane and `row / block_size`. Physical write
coverage alone identifies neither the represented feature nor that membership.

## Exact, conservative and invalidating examples

The three choices cover the required integration alternatives, provided examples
exercise different data demands and evidence meanings:

| Maintenance example | Required observation/state | Correct result before publication |
| --- | --- | --- |
| Exact dedicated tag replacement | New field code and bound tag semantics; preserve neighboring tag slots | Updated tag for that row and edition |
| Exact shared row-mask recomputation | Complete new dependency projection, including untouched fields | Recomputed mask; no subtraction of overlapping contributions |
| Exact modulo sum replacement | Before/after interpreted values or an explicitly equivalent weighted law | Retained native delta applied under the owner's protocol |
| Conservative rejection-only mask/presence | New contributions and existing summary; old contributions may remain | Include all newly visible contributions at every consulted pruning level |
| Invalidate and repair/bypass | Original row/block and semantic identity; O may be empty | Reader-visible bypass or unknown evidence until a compatible repair is published |

These are proposed parity examples, not newly implemented guarantees. The
row-signature spike [implements](../row-filter-signatures/model.cpp) dedicated
tag replacement and full shared-mask recomputation in a single-thread probe;
it explicitly excludes rollup mutation. Its concurrent publication options are
[design proposals](../row-filter-signatures/design.md#mutation-drift-and-physical-design).

Three counterexamples distinguish the choices:

1. Two fields contribute the same shared-mask bit. Clearing the old contribution
   when replacing one field removes evidence for the other. Even the changed
   field's exact old/new pair cannot establish the other witness.
2. Tags `01` and `10` have OR `11`. Equality against `01` falsely rejects an
   existing tag; “old OR new” is not a multi-version equality tag. Conservative
   OR applies to compatible positive-containment evidence, not every encoding.
3. A stale positive predicate bit can falsely certify acceptance after a value
   changes. Safe invalidation must remove unsupported acceptance as well as
   rejection: conceptually empty lower/full upper truth bounds over incoming
   visible rows. A stale rejection-only superset has a different contract.

Exact rollup deletion, empty-block occupancy and row movement remain additional
owner cases; see [rollup maintenance](../row-filter-signatures/rollups.md#maintenance-and-visibility).
A row move can require repair of both blocks and positional masks while leaving
the row payload unchanged. Initialization likewise has no general replacement
before-image; creating summaries and publishing row liveness are separate from
initializing codec storage.

## Physical bounds and publication

Distinguish selected logical destinations, actually changed values/bits, and
issued physical bytes. A selected same-value replacement can still issue stores;
a wide store can preserve and reissue neighbors. The journal covers issued
bytes, not minimal differences or semantic dependency ranges. An optional
changed-value comparison may refine maintenance, but exact byte-difference
discovery need not become a mandatory old read for invalidating operations.

Store footprints and capacity bounds must follow the bound recipe and actual
substituted leaves. Contributions sharing a destination byte need a correct
merged store, whose full issued span is covered. Bounds must include wider
preservation stores and exact tail behavior, without claiming ownership of
stride gaps. Journal record capacity is distinct from byte coverage; coalescing
can reduce actual records. SeriesPack's
[capacity/admission interface](../../../ikea/include/ikea/seriespack/author/write.h)
is the useful precedent, rather than one fixed packet's footprint.

That data journal does not account for arbitrary stores performed by maintenance.
If a callback updates persistent signatures, counters or dirty markers, those
are additional owned outputs requiring admitted access, disjointness where
needed, capacity and pre-write coverage. Alternatively, accumulate contributions
in retained private state and let the owner apply them. Some current SeriesPack
paths invoke the summary before the data coverage hook; that hook cannot be
treated as a barrier preceding all maintenance storage writes.

All packets, children, observation requirements and effect resources for one
checked invocation must be admitted before mutation. An invalid second packet
must leave the first packet, summary state and journal unchanged. After
admission, coverage hooks precede their associated local write groups and
maintenance hooks neither fail nor suspend. This is not transaction-wide
pre-notification, isolation or rollback. Earlier successful chunks remain dirty
after cancellation, and the owner retains bindings, views, contributions and
effects across a completed work frontier.

Before publishing changed data, the owner supplies exact summaries, compatible
corrections, conservative evidence or a valid bypass for every consulted pruning
level and reader edition. Adding a value behind a stale clean ancestor is unsafe;
publishing one atomic signature word does not coordinate all planes and data.
Latest-only summaries do not cover older snapshots. Queuing repair is sufficient
only when a valid retained visibility path already protects readers. Ikea should
make those obligations possible without selecting Engine, Loom or Orbital's
publication protocol.

This review inspected the linked source and research models. It introduced no
implementation, concurrency proof, new experiment or timing claim.
