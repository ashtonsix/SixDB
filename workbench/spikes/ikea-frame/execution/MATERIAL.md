# Borrowed encoded material across physical versions

Source-based Engine contribution to Frame's lifecycle investigation, 2026-10-22.
This examines a small physical witness and its prerequisites, not a Frame API,
durable format, mutation policy or new Orbital mechanism. Ikea LEAD owns the
[lifecycle work](../lifecycle/README.md). The packed-FOR witness below passes on
the shared isolated encoding cut. No timing result is claimed.

## Starting point

Current [Orbital material composition](../../../../orbital/docs/material.md)
already supports a successor assembled from retained source bytes and fresh
Allocations. The public two-TU [extension](../../../../orbital/test/extension.cpp)
and [material adapter](../../../../orbital/test/extension_material.cpp) show actual
Object publication, old Scopes and an escaping selected Part. The
[source composition checks](../../../../orbital/test/part_sources.cpp) add
cross-Control slices, certified padding, grouped versus independent owners and
rollback. These are implemented mechanisms, unlike the wider Draft/recovery
proposals discussed in Frame's [write notes](../WRITES.md).

Prior costs discourage treating finer ownership or smaller metadata as automatic
wins. The [composed-material study](../../orbital-mvcc/metadata/composed/README.md)
charges proof, binding and allocation capacity separately. Its
[transient-Schema negative](../../orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
saved resident state but made repeated tiny Tuple construction roughly nine
times slower in that particular comparison. Reuse those accounting distinctions;
do not revive its retired driver or assume a Frame result from its timings.

## Three things to preserve independently

**Physical bytes and their read grant.** A PartSource names an actual contiguous
body, its backing and the Control's readable padding/alignment. `source(selection,
index)` borrows an existing certificate without allocating or retaining. Keep
the selected source, proof and resource alive and stationary through capture,
including callbacks. The new body plus padding must fit its old certified
footprint; spare allocation capacity is not an extension of that certificate.

**Interpretation.** The new Control independently preserves every descriptor,
coordinate/count, checkpoint mode and dictionary edition its readers need.
Capturing retained backing does not retain the ancestor Part, Representation or
Control. A Control is a trivially copyable non-owning handle; its retain/release
operations retain an immutable proof, or its small proof can be inline. An
escaping selected copy must preserve the selected interpretation independently.

**Reclamation.** Distinct Parts are distinct interpretations/access spans, not
necessarily distinct physical owners. Several Parts cut from one owner retain
that owner's siblings when any derived use remains. Independent allocation
entries can have independent token lifetimes, while finite-class Homes or page
rounding can still retain physical capacity. Window tail trimming reclaims an
eligible suffix, not arbitrary interior holes. Count useful bytes, owner-held
capacity, metadata and actual allocator charges separately.

Capturing `sizeof(frame_encoding::column)` would certify its vector headers and
decoder handles, not their nested byte ranges. Even retaining the whole column
through a shared pointer would not turn those nested pointers into individually
protected Parts or independently reclaimable blocks. The public seam needs
actual payload spans and explicit interpretation dependencies.

## A minimal concrete composition

Use one Object with two encoded regions as a first forcing case, without deciding
whether production regions are blocks, groups or whole Parts. Construct real
encoded bytes in two ordinary Orbital Allocations, retaining pointer-free
interpretation records. The implemented case uses the existing encoder's `pack`
directly in each Allocation, so payload
copy bytes are zero; encoding and initialization still perform real work.

The material adapter follows the public extension's split: an ordinary header
declares Control/Physical, and one implementation TU includes the two public
instantiation opt-ins. A fixed two-region inline Control is sufficient for the
witness. It admits each Part's identity, exact body extent, initialized readable
padding and alignment, with the corresponding reconstruction parameters. It
need not impose a serialized header or a general region directory.

After publishing the basis and opening an old Scope, construct a successor:

```text
kept   = source(basis_selection, region_0)
fresh  = actual encoded replacement bytes in a fresh Orbital Allocation
next   = capture(new_control, independent_allocation, [kept, fresh])
prepare(basis_scope, object -> next)
install(prepared)
```

The retained source is relabelled only if required by the destination Control;
its physical certificate does not supply an Object/version identity. Both
regions' interpretation belongs to the new Control. No write touches the basis's
sealed body or padding, even a preserving store. A changed logical value uses a
new publication; a logically equivalent re-encoding can separately exercise
exact-version conversion/adoption.

Read both versions through their own bound material and existing owner kernels.
Confirm equal pointer/byte identity for region 0 and independent fresh bytes for
region 1. Releasing the old Scope permits its changed old owner to retire;
region 0 must remain readable through the successor. An exported selected Part
can then outlive the catalog with only its own necessary interpretation and
physical dependencies. Final cleanup must close actual owner/metadata uses,
without equating each token release with immediate page reclamation.

The contrasting control puts both old regions in one physical owner. Reusing
region 0 retains that owner, including the obsolete region 1, after the old
reader ends. It can still save copying; it cannot claim the same reclamation.
A full-copy successor is the other useful control. No new allocator, replay
format or transaction frontend is needed for any of these comparisons.

## The smallest useful encoding-owner capability

The current `column` decoder interface owns vectors and prepared readers tied
to their original addresses. The existing [plane catalogue](../encoding/kernels.h)
and [point reader](../point/compact_read.h) already separate reusable native code
from most per-block preparation; reuse those bodies rather than cloning codecs.
Neither yet supplies a general bounded borrowed block to the whole consumer.

Ask the encoding owner for one private attach/read capability over:

- An actual block descriptor and its original-coordinate population/count.
- The block's admitted encoded byte span, with the reader's exact padding and
  alignment requirements; offsets resolved against this span rather than hidden
  old vector addresses.
- The image-specific interpretation needed by that kind, initially just its
  checkpoint choice for uncoded blocks. Dictionary-coded blocks additionally
  need the correct part's dictionary material/edition and a clear stored-code
  versus logical-value operation.

The owner must report the required byte windows or body extent. Inferring it
from the next descriptor's offset can accidentally include a later dictionary
or miss side material. Bind once to the supplied addresses, then reuse the
owner's existing reconstruction/COUNT/SUM bodies. A borrowed view owns no
Allocation, version or liveness policy. Its lifetime is bounded by the caller's
selected material; a cache of such a view cannot silently outlive that scope.

This can begin with the already-supported uncoded FOR body to prove the physical
composition. Runs and predicted/checkpoint/exception bodies then test whether
the seam describes their real dependencies. This is a bounded witness choice,
not a proposal to narrow Frame to FOR or suppress dictionary obligations.

### Bounded packed-FOR witness

[`material-witness/material.h`](material-witness/material.h),
[`material.cpp`](material-witness/material.cpp) and
[`check.cpp`](material-witness/check.cpp) implement the agreed first case.
Two 4,096-position planes use widths 13 and 9; the changed second plane uses
width 21 and a different base. Original logical values are the independent
oracle. The existing `pack` constructs residual planes in native Allocations;
the existing shared point/range catalogue reads them. Adding the inline base
is the explicit FOR law, with admission preventing arithmetic wrap. There is
no pointer-rich column capture, new bit-packing kernel or general Frame adapter.

The inline Control preserves each plane's base, width, count, original-position
origin and initialized readable padding. Its fixed two-region capacity is a
test fixture, not a proposed Frame limit. Construction Holds cover the exact
body-plus-padding spans and end before retention observations. Both ownership
arms retain the first region's certified source and encode the changed second
region into fresh backing, then use actual Prepared publication. The check
compares every point, full ranges and an unaligned partial range with original
values; it also exercises old/current Scopes and a selected-copy exported Part.
Padding and metadata-capacity refusals must preserve fresh sources and answers.

The plane sizes deliberately use Window backing rather than asking a tiny
finite-slot reuse policy to reveal retirement. Public Pool mapped/idle counters
can then distinguish an old independent owner becoming idle from a shared
owner retaining its obsolete sibling. The reported provenance owner capacities
are derived from actual allocated owners and the observed lifetime phases;
they are distinct from Pool counters, RSS or physically reclaimed pages.
`successor_encoded_plane_bytes` is the constructed plane extent, not an issued
store count. `payload_copy_bytes` is zero. Metadata live/peak counts cover this
ObjectStore/material resource, excluding test oracles, encoder scratch and the
shared code catalogue. Final Pool closure separately checks eventual drainage.

The target is `frame_execution_material_check`. The local native lifetime check
passes all these oracles and refusal/release cases. There is no timing or general
representation-policy result from this check. Delta/patched checkpoints,
dictionary editions, presence/NULL masks and staged reconstruction dependencies
still need the broader borrowed-body seam and lifecycle experiments above.

Observed capacity of distinct physical owners still referenced, bytes:

| Ownership | Old reader held | Old reader released | Only exported region 0 remains |
| --- | ---: | ---: | ---: |
| Independent owners | 28,672 | 20,480 | 8,192 |
| Two slices of one original owner | 24,576 | 24,576 | 12,288 |

The public Pool counters corroborate 8,192 bytes becoming idle after the old
independent owner ends; the shared original owner remains entirely nonidle.
Both exported selected Parts use 136 metadata bytes in this fixed fixture, and
all material/catalog metadata returns to zero after final release. At that point
all mapped payload is idle; final Pool closure separately checks zero Arena
charge. Idle capacity is not already-unmapped or resident memory. Grouping saves
4 KiB while both versions remain held here, but retains 4 KiB more afterwards.
Keep both effects when considering ownership grain.

The [qualification record](evidence/material-integration/qualification.json)
keeps all eight phase observations, the binary hash, encoding core commit,
capture basis and exact consumer-source hashes. It uses the isolated
`f760ca39` encoding core with maintained Intervals, pinned Clang 21.1.8 and
O3/native ISA/generic tuning in the local AArch64 VM. The
[grouped regression record](evidence/material-integration/grouped-checks.json)
also records the passing 15 logical, 420 authored encoded and 93 corpus
query/program comparisons after removing obsolete run controls. Those are
correctness checks; earlier timing cohorts are unchanged.

Build the target through the ordinary spike configuration, then run it directly
on Linux (prefix commands with `orb -m ubuntu` from macOS):

```sh
cmake --build build/clang/frame-execution --target frame_execution_material_check --parallel 1
build/clang/frame-execution/workbench/spikes/ikea-frame/execution/frame_execution_material_check
```

## Costs, negative cases and proposed file ownership

Measure initial encoding/copy, successor encoding/copy, capture and publication,
query preparation/consumption, peak simultaneous backing, held-old-reader state
and eventual retirement. Report original and changed useful bytes alongside
allocation capacity and metadata. Independent entries cost owner slots/pins and
can increase rounding; grouped entries reduce owner bookkeeping but retain
sibling bytes. Dictionary or predictor dependencies can widen an otherwise
small repair. Rebuilding large preparation per tiny replacement can lose even
when fewer payload bytes are copied.

Current native/shared/mixed independent entries are 16/24/32 bytes respectively,
before the Representation header, Control/proof, catalog and allocator costs.
These source-defined entry sizes are not a complete Frame footprint. New Parts
or finer regions must earn their additional preparation and ownership cost.

Refusals need unchanged published answers and intact fresh sources: insufficient
metadata, invalid slice/padding/alignment, stale publication and an unsupported
borrowed interpretation. Coarse no-op selections need no payload copying, but
required empty-result/catalog work is distinct. Existing Orbital tests establish
mechanisms; the new witness must still check the actual encoded consumer and
its complete read footprint.

Agreed ownership: Engine supplies only
`execution/material-witness/{material.h,material.cpp,check.cpp}`, based on the
public extension adapter; Claude owns any extraction of a borrowed block
attach/read into encoding material; Ikea LEAD owns lifecycle traces, fold/thaw
choices, performance and publication integration. No overlap with Directory or
Intervals files is necessary. A simpler
owner-supplied native material witness is preferable if it already provides
these capabilities; do not add a second adapter merely to preserve this split.
