# First sketches: who owns a composition?

The [second sketches](sketches-2.md) are the current comparison. They incorporate
the distinction between analysis and execution below, and Ashton's subsequent
clarification that segments in one collection can have different actual
representations indefinitely, including through lazy migration.

2026-09-08. Discussion sketches in response to Ashton's request to proceed in
small steps. These are pseudocode proposals, not selected interfaces, completed
adversarial reviews, or compiler evidence. The [charter](README.md) supplies the
wider questions; this note explores one responsibility before expanding scope.

**When a caller asks for composed work, who connects its logical operations to
layout-specific implementations and their intermediate representations?**

The same small probe appears in three sketches: read values under a prefilter,
refine the mask, and consume the retained values. The filter and consumer reuse
the read result. The choice of numeric predicate below is incidental. This
probe does not yet exercise transform dependencies, mutation or suspension.

## Clarification: analysis and execution surfaces

Ashton's response changes how to read the three alternatives below. Engine
needs both rich access when analysing and improving compositions and a small
container interface when executing them. The alternatives had mixed those
moments together. A container-shaped hot call can be backed by C's inspectable
composition and B's native functions/drivers. The remaining choices concern
the extension interfaces and ownership within those surfaces.

### Data composition: choices Engine may want to revisit

These questions guide examples; they are not a list of mandatory mechanisms.

| Workload/data question | Choices it could affect |
| --- | --- |
| Which fields are selected together, and which are rarely read? | Grouping, independent access, interleaving, shared decoding and delayed payload access. |
| Which predicates and conjunctions recur, with what correlation? | Joint versus separate evidence, encoded comparisons, summary placement and progressive access paths. |
| What is the mix of point, range and scan access, at what selectivities? | Seek/restart support, access grain, sequential traversal and materialisation strategy. |
| What regularity is present within and across fields? | Encoding/transforms, dictionaries or shared bases, dependency scope, outlier handling and variable-length metadata. |
| How do distributions and access patterns vary across the data and over time? | Where a composition may vary, representation lifetime, and when a rewrite repays its construction cost. |
| Which fields change together, and how frequently? | Update locality, write amplification, summary maintenance, relocation and historical-version cost. |
| Which space, latency and resource costs dominate? | Redundant representations, decoding work, buffer requirements and physical access shapes supplied to Loom. |

A replaceable unit may be deeply nested. A physical replacement must account
for its parents' requirements: coordinates, layout, readable bounds, restart
properties, auxiliary dependencies and effects. Preserving the outer getters
does not imply that every parent fast path remains valid. Changing a format
on existing data can require re-encoding, metadata/summary work and publication;
changing only its reader may require none of those. Their costs and invalidated
bindings need to be distinguishable.

Engine owns the composition schema and selection policy. Ikea needs to expose
enough structure and contracts to identify such a substitution and determine
its consequences. The interface need not expose the internal bit tricks of
every native kernel. Physical composition and execution dataflow are related
descriptions; no single universal graph representation is selected here.

### Execution composition: plans with an ongoing history

Ashton's proposed Engine model keeps plans and equivalence graphs long-term,
continually improving them. Periodic probes during a large scan can improve
the plan used for remaining work. The Ikea seam should accommodate this model
without defining Engine's optimiser in this spike.

One useful distinction is between:

- semantic equivalences and the conditions under which they hold;
- applicable implementations and their representation/resource requirements;
- measured or estimated costs, including the circumstances and age of evidence;
- executable revisions bound to the relevant schemas and runtime resources.

For example, direct exact filtering and a sound signature prefilter followed
by exact filtering of its candidates can produce the same final rows. The
signature result alone has a different contract. Probe results can change the
preferred complete route; observations of speed or agreement on a sample do
not establish its semantic equivalence. This connects to the
[row-signature findings](../row-filter-signatures/FINDINGS.md) and the
[distinction between estimates and pruning proofs](../../notebook/secondary-summaries.md).

Cost evidence may be conditional on selectivity, conjunction, data locality,
residency, target or distribution. A long-lived plan can retain useful
alternatives instead of treating the last winner as universally best. Schema
or semantic changes may invalidate a legality assumption; old timings may
merely become weak evidence. Persistent descriptions cannot depend solely on
process-local pointers. Details of persistence and search remain Engine work.

An improvement might only relink reusable CPS stages. Selected regions might
later justify additional compilation or manual fusion. The same logical
operation and its required observation/effect contract remain identifiable
across those revisions.

### A possible small execution surface

This pseudocode separates occasional preparation from repeated calls; it does
not fix the names, ownership types, or a runtime replacement protocol:

```cpp
// Engine preparation, when a plan is first used or a revision is selected:
bound = ikea::bind(container, selected_revision, integrity_binding);

// Repeated execution under that binding's validated scope:
result = bound.scan(range, prefilter, output, call_resources);
```

The hot call performs no equivalence search or compulsory probing. Binding
can preselect implementations, wrappers and handlers; small dynamic checks
remain where validity depends on the actual call. A container-owned prepared
handle and an Engine-held bound operation are both still possible APIs.

For improvement during one query, a first proposal is to install a new revision
at a declared boundary between completed portions of work. The boundary must
preserve snapshot, coordinates, output progress and any aggregate/transform
state, with no duplicated or omitted work. It need not coincide with a block
or SIMD group. An incompatible state representation needs a transition or a
later boundary. In particular, this proposal does not edit a BytePack-style
stage table while a live cursor is traversing it. Mid-pipeline state migration
is a separate design question, not implied by periodic probes.

For effects, a bound call could promise that physical write coverage and
specified local summary maintenance are handled, while the caller still owns
transaction publication. Another binding might return logical invalidations
for caller batching. The division must be part of the bound contract. Every
required effect needs an owner before a successful binding; the inner body
still reports the local facts its selected driver cannot derive. Failure or
partial completion must not masquerade as discharged integrity obligations.

The next useful question is what authoring interface exposes a deep
replacement and its consequences while preserving this simple execution
surface. The sketches below remain material for that discussion, not mutually
exclusive choices for Engine's entire relationship with Ikea.

## Common kernel author's code

For this illustration, active lanes contain valid signed 32-bit values, lane
coordinates stay unchanged, and the predicate is `value >= lower`. The output
mask is a subset of the input. Inactive lanes contain defined bits but need
not represent logical values. SQL NULL handling is outside this local contract.

```cpp
// One native implementation, shared by inline and CPS forms.
inline __mmask16 retain_ge_avx512(__m512i values,
                                __mmask16 active, int32_t lower) {
    return active & _mm512_cmp_epi32_mask(
        values, _mm512_set1_epi32(lower), _MM_CMPINT_GE);
}
```

The author sees values, a mask and one parameter. Another target can have a
different native body. No portable SIMD abstraction is proposed here. In this
probe the driver skips reads for an empty prefilter, and a wrapper skips the
consumer when refinement empties the mask. The comparison itself does not
promise less execution work for a sparse nonempty mask.

The read implementation is selected separately: a primitive can be accessed
through a contiguous arrangement or an interleaved arrangement, for example.
Both can supply this body after establishing its value and lane contract.
They need not expose identical storage views or have identical read costs.

## A. The container binds the requested work

```cpp
// Engine caller: values implements an erased container contract.
work = read_values() >> retain_ge(lower) >> consume(out);
bound = values.bind(lease, rows, work, target, execution::cps);
bound.run(prefilter);

// Inside one container realisation's binding implementation:
source = interleaved_reader.bind(layout, lease, rows, target);
return bind_local(work, source, numeric_kernel_catalogue, execution);
```

`retain_ge` preserves the incoming values and refines their mask; `>>` is
composition syntax, not an instruction to store an intermediate. Binding
returns a result-or-error throughout these sketches; error plumbing is elided.

The container implementation owns traversal, reader selection, intermediate
representation and connection to downstream operations. `bind_local` must
check the reader's guarantees against the consumer's requirements and select
compatible execution wrappers. Generic numerical bodies live in a shared
catalogue; the container need not reimplement them. A context-specific reader
is introduced through the relevant container realisation's binding code.

**Appeal:** the caller can request useful work without understanding storage
traversal. The container has the information needed to choose contextual fast
paths, and blocks can remain free-function building pieces internally.

**Pressure point:** the container becomes a host for compositions that callers
invent. What happens when Engine introduces a consumer using an auxiliary
container? Either the extension interface is sufficiently rich, the container
must learn about it, or the binding refuses it. Exporting an intermediate to
make that work may lead toward B or C. Engine also needs a way to inspect
available choices before committing to this container's binding decision.

## B. A driver composes operations over explicit views

```cpp
// Engine caller / reusable Ikea driver:
view   = values.open_read(lease, rows);
source = ikea::bind_reader(view, target);
keep   = ikea::bind(retain_ge(lower), source.output_contract());
sink   = ikea::bind(consume(out), keep.output_contract());
bound  = ikea::link(source, keep, sink, execution::cps);
ikea::run_read(view, bound, prefilter);

// Reader author, beside the layout-specific body:
readers.add(read_values, InterleavedView,
            requires(read_bounds, lane_mapping), read_interleaved_avx512);
```

The container supplies a validated, borrowed description of accessible data.
A view may describe multiple spans and their mapping; it is not necessarily a
contiguous pointer/count pair. The reader binder inspects that description and
selects its implementation. The driver owns iteration and links operations
whose output/input contracts agree. Binding is outside the hot loop over the
range for which its layout facts hold. The lease remains live through the run.

Kernel authors add native bodies and applicability declarations to the
appropriate family. Adding an implementation for another physical context
adds a view/reader binding, while the numerical body above remains unchanged.
An erased view is inspected during binding; the inner body receives validated
native operands rather than dynamically interrogating it.

**Appeal:** the pieces and their owners are visible. A driver can be reused by
record containers and standalone structures, and callers can compose without
extending a container's operation dispatcher.

**Pressure point:** how much layout knowledge moves into each driver? If a
caller must coordinate several views' traversal and row mappings, the simple
free functions may leave substantial authoring work outside them. A proposed
common iterator could also erase precisely the locality a specialised reader
needs. A reader requiring padding must be refused on an unpadded view or get
an explicit adapter; sharing the `read_values` name is insufficient.

## C. Engine names the dataflow; a binder connects implementations

```cpp
// Engine caller: named ports make intermediate reuse explicit.
r = recipe.read(values, rows, prefilter);
m = recipe.apply(retain_ge(lower), r.values, r.active);
recipe.apply(consume(out), r.values, m);
bound = ikea::bind(recipe, layout_bindings, target, execution::cps);
bound.run(lease);

// Kernel author, beside the native body:
kernels.add(retain_ge,
    implementation(retain_ge_avx512,
        inputs  = [native_i32x16, mask_in_same_coordinates, i32],
        output  = subset_of_input_mask,
        effects = none));
```

Engine chooses the logical work and any observation boundaries. Ikea's binder
matches layout-sensitive implementations and connects their ports, reporting
incompatibilities or available bridges to the caller. It does not acquire
authority to choose Engine's query plan. Reader implementations advertise the
views they accept and native value contracts they supply. The same value port
feeds the filter and consumer, so the binder can see that it remains live.

These are straight-through data dependencies, not a general control-flow VM.
The bound result contains executable choices; it is distinct from Engine's
persistent composition schema. No hot descriptor interpreter is implied.

**Appeal:** context-specific implementations, manual fusions and intermediate
reuse have explicit matching surfaces. Engine can inspect alternatives and
request progressive boundaries without relying on a private container recipe.

**Pressure point:** declarations may become a second programme. How much can
the signature supply, and which facts genuinely require semantic annotation?
An auxiliary dependency or coordinate conversion needs an accurate descriptor;
stale metadata could make a legal-looking composition wrong. The binder may
also turn a small kernel extension into an exercise in describing capabilities.

## What the execution boundary could look like in all three

The alternatives above concern ownership of binding. None requires an
execution technique. A selected pair of operations can inline the same body:

```cpp
kept = retain_ge_avx512(v, active, lower);
if (kept) consume_avx512(v, kept, out);
```

For a reusable CPS version, this is the proposed *wrapper shape*, not verified
C++ for the pinned compiler:

```cpp
CC void retain_step(Cursor next, const ProbeParams* p,
                    __m512i v, __mmask16 active, Output* out) {
    auto kept = retain_ge_avx512(v, active, p->lower);
    auto dest = kept ? next : completion_slot(next);
    [[clang::musttail]] return dest->fn(dest + 1, p, v, kept, out);
}
```

Every entry and completion in this small native ABI family has the same
function type and calling convention. `ProbeParams` contains this probe's
immutable parameter, not an integration service bag or a bespoke type per
recipe. The driver owns parameters, output and the stage table for the whole
immediate run. The completion entry ignores its cursor and returns; for this
probe, empty input contributes nothing to `out`. No pointer to a wrapper local
crosses the hop. The same body is intended to inline inside the wrapper.

`completion_slot` follows the recent BytePack cursor/table idea described in
[the reading note](reading.md#primary-cps-prior-bytepack): bounded stage slots,
64-byte alignment, a completion immediately after the useful stages and a
duplicate in the final slot for arithmetic early exit. The concrete eight-slot
geometry is prior art, not a new choice of Ikea's maximum pipeline depth.

The vector is proposed as a by-value argument, not a pointer into a scratch
frame. That makes the intended handoff inspectable; it does **not** demonstrate
register residency, tail-call legality or absence of helper spills. Those
remain compiler-probe questions. This signature only sketches the handoff
after reading values; reader entry and other native carrier shapes are open.

Erasure here selects a validated family of typed entry points. It does not
cast incompatible function pointers together. A different carrier requires a
compatible implementation, an explicit bridge, or a refused binding. Metadata
erasure and hot intermediate representation are separate decisions in A, B and C.

A manual fusion could replace the same logical sequence, provided it preserves
results, prefilter semantics, accesses, effects and any requested observation
boundaries. Unlike inline/CPS reuse, manual fusion need not preserve this exact
body decomposition. No fusion is implemented or selected in these sketches.

## The contrast to discuss

| | A: container binds | B: driver composes | C: binder connects ports |
| --- | --- | --- | --- |
| Caller primarily handles | A container and requested work | Views and bound operations | Logical values and dependencies |
| Layout-sensitive selection lives in | Container realisation | Reader binder and driver | Implementation catalogue and binder |
| Most attractive property | Local knowledge behind a small caller interface | Visible, reusable pieces | Explicit reuse and inspectable alternatives |
| First objection to pursue | Extending compositions across containers | Traversal/mapping burden spreading to callers | Semantic description burden on authors |

These are competing placements of a responsibility, not three complete Ikea
architectures. A could delegate internally to B; C could produce a B-style
driver. The distinction worth discussing is which surface an ordinary caller
or kernel author must understand, and which extension forces them to cross it.
All three need semantic and ABI contracts: A and B may encode more of those
in binding/adapter code, while C makes more of them queryable declarations.
The shorter sketches of A and B do not establish a lower total authoring cost.
No preference is recorded yet; further sketches should follow that discussion.
