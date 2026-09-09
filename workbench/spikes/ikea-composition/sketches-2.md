# Second sketches: authoring an inspectable composition

2026-09-08. Three discussion alternatives following Ashton's clarifications.
These explore an authoring seam; they do not select an architecture or claim
compiler evidence. They supersede the [first sketches](sketches.md) as the
initial comparison. The [consolidated working design](synthesis-1.md) now carries
forward their common mechanism while retaining unresolved comparisons. These
sketches preserve the earlier arguments. PyTorch's ordinary composition, explicit operator contracts
and access for transformation inform the [reading](reading.md#pytorch-composition-and-transformation-surfaces).

**How does an author make a composition both callable and open to Engine's
deep inspection and substitution?** All three alternatives provide both
surfaces. Container simplicity and optimiser access are shared requirements.

## Shared setting: the collection has several actual representations

Segments in one collection may have different representations because of data
distribution, access patterns or migration history. An older representation
can remain in use indefinitely when migrating that segment does not repay its
cost. There is no requirement to converge to one collection-wide format.

| Identity/fact | What it governs in these sketches |
| --- | --- |
| Collection's logical contract | Common field meanings and operation semantics. |
| Each segment's actual composition | The representation descriptor/schema and versions that interpret its visible bytes; potentially different for each segment. |
| Engine's preferred replacement | A possible future composition for selected segments; does not reinterpret bytes already stored. |
| Selected executable revision | How an operation runs on an applicable actual representation, under its stated assumptions. Can change without rewriting data. |

Engine owns composition schemas. Descriptions below refer to actual nested
components through schema identities; live C++ objects and function pointers
are not the persisted schema. A segment also supplies its own byte locations,
parameters and validity information. Representation identity alone is not a
proof of every fast path's applicability. The segment metadata and data views
in these sketches belong to the same validated snapshot/binding scope.

For all alternatives, the execution surface could be:

```cpp
// Engine preparation; a plan retains applicable executable alternatives.
prepared = engine.prepare(Select(predicate, fields), snapshot, target);

// At segment entry: cheap selection and parameter attachment.
c = prepared.attach(segment.view(snapshot));

// Repeated hot call, same contract across segment representations:
c.select(range, prefilter, output);
```

`attach` matches the actual representation and required facts against prepared
entries, then attaches per-segment resources. It does no equivalence search,
probing or compilation. A retained supported representation has a correct
reusable route even when it has no specialised entry. An entirely unsupported
format is diagnosed before entering the hot loop; access does not implicitly
migrate a segment. This is a proposed shape, not a chosen cache/dispatch design.

Code can be shared by segments meeting the same implementation requirements;
the design must not compile a distinct pipeline for every segment. Segments
with the same format can still favour different execution alternatives.

## A. Named components with explicit composition methods

A component author declares replaceable children and how operations expand
through them. This example's `EncodedValues`, `payload` and `Restore` merely
make a nested edge visible; they prescribe no codec or physical geometry.

```cpp
component EncodedValues {
    child<PayloadContract> payload;
    Parameters params;

    expose Read(rows, active) =
        Restore(Call(payload, ReadPayload(rows, needed(active))), params);
}
```

`expose` returns operation structure during preparation. Native functions
implement its leaves. The author explicitly publishes the child edge and
the expansion; no tracing of a native body is involved. The local transform
defines `needed`: a prefilter does not necessarily remove decoding dependencies.

```cpp
// Engine inspects a particular segment's actual composition.
repr = schemas.get(segment.representation_id);
edge = repr.field("x").child("encoding").child_edge("payload");
candidate_repr = repr.with_child(edge, alternative_payload);
check_parent_contracts(candidate_repr);

plan = repr.expose(Select(predicate, fields));
// Engine can rewrite exposed operation structure before binding it.
```

The edit constructs a candidate description. Installing it for existing data
requires the relevant migration/publication work. Other segments keep their
own descriptors. A plan-only rewrite operates on `plan` and leaves `repr` alone.

**Attractive:** a discoverable answer to "what can I replace inside this
container?" Adding a new physical context supplies a component and its local
operation expansions. It is possible to keep specialised native implementations
alongside those expansions.

**First challenge:** the child tree does not itself expose execution reuse.
Suppose a predicate on one child should prevent decoding another child. Engine
needs the enclosing operation's expanded dataflow, not just both children's
individual read methods. The defence is explicit expansion of that enclosing
composition; the authoring cost is another responsibility to keep coherent.

## B. Ordinary composition code over recordable operations

An author writes the composition as a function. Calls through `ops` have
contracts that let a builder record their dependencies. An inline executor can
evaluate the same composition using native implementations.

```cpp
auto select_x(auto& ops, auto source, auto active, auto predicate) {
    auto payload = ops.read_payload(source.payload,
                                   source.needed(active));
    auto values = ops.restore(payload, source.parameters);
    auto keep = ops.test(values, active, predicate);
    return ops.project(values, keep);
}

// Engine records this composition against actual representation metadata.
g = record(select_x, source_from(segment.representation_id),
           symbolic_mask(), symbolic_predicate());
g2 = engine.rewrite(g, selected_equivalences);
entry = bind(g2, execution::cps, target);
```

Physical descriptions still exist in Engine's schema. The source connects
recorded operations to those descriptions and child identities. Replacing a
physical child edits a candidate schema and re-records/revalidates the affected
composition; replacing a pipeline fragment edits `g` without changing the data.

The recording interface is deliberate. It records these composition operations,
not arbitrary C++ instructions, sampled data or branches inside native kernels.
Masks remain symbolic while recording. Branching on a known representation
property can specialise the composition under a recorded applicability condition.

The recorder produces a runtime graph whose CPS binding connects reusable
compiled stages. It must not instantiate a C++ template for every graph revision.
Selected inline recipes can accept that compilation cost. New primitives need
semantic/representation contracts and native bodies; an ordinary new combination
of existing operations should require only a composition function.

**Attractive:** the shared `values` local makes reuse explicit in ordinary code.
The function is recognisable to an author, and a new combination need not become
a new named operator or component class. Native bodies retain explicit intrinsics.

**First challenge:** `if (any(active))` is natural code but must not record one
sampled outcome as a universal plan. The recorder needs explicit symbolic
mask/early-completion operations or restricted control flow. The defence is a
small composition vocabulary; the open ergonomic question is how frequently an
author encounters that restriction in real work.

## C. Operations with declared expansions and implementations

An author registers how logical work can be realised for a representation.
Engine's equivalence machinery can retain several alternatives and their
conditions instead of immediately choosing one expansion.

```cpp
rule Read(EncodedValues(payload, params), rows, active)
    expands_to Restore(ReadPayload(payload, rows, needed(active)), params);

implementation Read(EncodedValues(...), rows, active)
    using read_encoded_avx512
    when supported_layout_and_target;

rule Select(source, predicate, active)
    equivalent_to SignatureThenExact(source, predicate, active)
    when signature_covers_source_and_predicate;
```

The last rule denotes a complete exact selection route. A candidate signature
mask alone is not equivalent to exact selection. Its soundness conditions and
residual obligations are part of the rule, not consequences of measured speed.

```cpp
equivalents.add(Select(segment_source, predicate, active));
equivalents.expand(ikea.rules, actual_representation_facts);
chosen = engine.choose(equivalents, contextual_evidence);
entry = bind(chosen, actual_representation, execution::cps, target);
```

Rules can match nested representation terms, subject to parent contracts.
An alternative physical term is a candidate layout, with any migration cost
accounted for separately; an execution rewrite runs over actual available data.
The rule store is not traversed on every container call.

**Attractive:** the relationship between an opaque specialised implementation
and an inspectable composition is explicit. Incremental addition of alternatives
fits Engine's proposed long-lived plan model directly.

**First challenge:** a straightforward new composition may require a logical
name, an expansion, applicability conditions and native implementation mapping.
An absent expansion can leave a correct callable opaque to deep rewrites. The
defence is reusable rules and contracts for existing primitives; the burden
should be compared with writing B's ordinary composition function.

## Common kernel and execution boundary

These alternatives change how the surrounding composition is authored. A
native leaf remains a small body, for example:

```cpp
inline __mmask16 retain_ge_avx512(__m512i v, __mmask16 active, int32_t n) {
    return active & _mm512_cmp_epi32_mask(
        v, _mm512_set1_epi32(n), _MM_CMPINT_GE);
}
```

The illustrative contract is signed comparison over valid active lanes;
inactive lanes have defined bits, and the returned mask refines `active`.
Driver/wrapper code can skip work on empty masks. This one comparison is not
a choice of Ikea's primary operation, scalar type or execution grain.

Inlining calls this body directly. A CPS adapter calls the same body and
tail-forwards `v` and the refined mask through a compatible native ABI family,
using the BytePack cursor/completion scheme. The [earlier wrapper](sketches.md#what-the-execution-boundary-could-look-like-in-all-three)
makes that proposed boundary explicit; register residency remains unverified.
Metadata erasure does not require intermediate values to live in a scratch
frame. An incompatible carrier needs a compatible implementation or an explicit
bridge. Manual fusion is another implementation of the same logical contract.

Binding also fixes the driver and its integrity duties. The inner author
supplies local effect facts, while the bound call knows which are handled and
which remain with the caller. This read probe does not settle mutation APIs.

## Two changes to walk through in discussion

**Replace one nested physical child in one segment.** Inspect that segment's
actual descriptor; construct the candidate; validate affected parent contracts;
account for re-encoding and maintenance; publish through Engine's protocol if
selected. Keep old bytes/descriptors available for readers that still require
them. Prepared entries validate the representation they actually receive.
Neither sibling segments nor cold legacy segments acquire a migration duty.

**Improve a pipeline without changing any representation.** Retain the new
equivalent route and conditional evidence, bind reusable stages or selectively
compile a region, and adopt the revision for applicable future work. A switch
during a scan needs a compatible progress/state boundary; the existing CPS table
remains intact while a cursor uses it. Different segments may choose different
revisions or methods within the same logical query plan. Improving one segment's
route does not establish a collection-wide winner.

| Candidate | What the composition author writes | Main question to discuss |
| --- | --- | --- |
| A: named components | Children plus explicit operation expansions | Does the hierarchy help discovery without hiding useful cross-child dataflow? |
| B: recordable functions | Ordinary functions calling supported operations | Is the symbolic composition vocabulary small and natural enough? |
| C: declared alternatives | Operation expansions, implementations and conditions | Does explicit rewrite knowledge repay the declaration burden? |

These could share machinery, but using all three as mandatory authoring steps
would defeat the ergonomics question. Compare what an ordinary extension needs
to expose, and who supplies anything beyond that. No candidate is selected.
