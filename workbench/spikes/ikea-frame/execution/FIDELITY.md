# What Frame should let Engine understand

Research-plan supplement, 2026-10-22. This revisits the Engine planning and
layout studies for [Frame's architectural review](../REVIEW.md). It proposes
questions and discriminating comparisons, not a schema format or an optimiser.

## The ambition

Frame should describe enough of its actual composition that Engine can discover
better algorithms, replace nested components and understand the consequences.
An enumeration of codecs and widths is insufficient. A complete description of
every native instruction would be expensive and usually unhelpful. The useful
fidelity lies in the relationships that change the problem being computed:
reconstruction, evidence, address discovery, shared work and maintenance.

For example, a dictionary does more than compress a column. Its code stream can
survive a predicate or transform; a grouped operation can reduce locally before
translating only the touched codes. A predicted column can require another field
even for an isolated projection. A compacted stream changes coordinates. These
relationships determine possible programs and their costs, even when every
alternative supplies an identical logical getter.

## What the earlier studies establish

| Prior work | Consequence for Frame research |
| --- | --- |
| [Layout analysis as information access](../../layout-analyser/design.md), especially its dependency distinctions and connected candidates | Investigate representation and operation together. A plane, the information it contains, its placement and the execution stage reading it are separate choices. This is a proposed model, not an implemented Engine analyser. |
| [Connected record case](../../layout-analyser/case-study.md) and measured [prefix/bucket findings](../../layout-analyser/findings.md) | Similar byte widths can hide different refinement, addressing and repair paths. More compact or more informative is not automatically faster. The worked case is a proposal; the findings own the actual measured controls. |
| [TuplePack finite reference](../../layout-analyser/tuplepack-reference/README.md#checks-and-what-remains-open) | In one retained 56-plan universe, isolated-cost choices had about 65% regret for the 50%/90% write mixtures. Complete mixed consumers must qualify final choices. The counterexample does not invalidate enumeration or establish the cause of the runtime difference. |
| [Palette selection study](../../layout-analyser/selection/README.md) | A richer useful menu can still be chosen badly: the held-out-class worst fast-selection regret grew from 13.55% to 44.92% with two plans. This reused captured rows/binaries; it did not validate learning across independent containers. |
| [Inspectable composition sketches](../../ikea-composition/sketches-2.md) and [analysis/execution distinction](../../ikea-composition/sketches.md#clarification-analysis-and-execution-surfaces) | Deep inspection can coexist with a small prepared execution call. Actual composition, preferred replacement and executable revision have different identities and lifetimes. These remain design alternatives. |

These sources favour an explainable set of structural alternatives and local
fitting. They do not justify choosing storage first, deriving a universal cost
from a few field properties, or requiring a general graph framework before
testing useful alternatives.

## Which information could earn its place?

Consider an actual image and the operations Engine wants to perform on it.
Keep several kinds of description distinguishable, even if one implementation
eventually represents them together.

| Description | What reasoning it enables |
| --- | --- |
| Value and operation laws | Exact versus conservative answers; NULL, arithmetic, ordering and equality rules; whether dictionary lifting or regrouping is legal. Engine chooses database meaning. |
| Reconstruction and coordinate relations | Bases/residuals, dictionaries and their editions, runs, shared codes, reference columns, exceptions and rank mappings. These expose algorithms and the inputs those algorithms require. |
| Address and access dependencies | Which metadata locates a body; restart state; dependency work outside an output selection; readable bounds and grants. This informs traversal, protection and Loom staging. |
| Operation expansions and structured results | Ways to answer an operation using children, retain codes/runs/selections, share an acquired value or use a fused implementation. Equivalent results need not have identical cost or intermediate form. |
| Change and lifetime consequences | Logical destinations, issued byte damage, dependent repair, and the interpretation facts a prepared operation relies on. A child can remain callable while invalidating a parent's shortcut. |

Not all of this belongs in durable per-block metadata. Separate the logical
schema, the image's resolved representation and parameters, facts proved about
that image, query preparation, and empirical cost evidence. A native kernel may
remain opaque while its operation and applicability are inspectable. Measured
speed does not establish equivalence; an old measurement can become unhelpful
without making an operation illegal.

The same separation permits cheaper adaptation. Engine may rebind unchanged
bytes, choose a new format for future data, or migrate an existing image. An
advice update must not reinterpret old bytes. An old reader and its dictionary,
coordinate maps and dependent material retain one coherent interpretation.

### Leading planes make the distinction concrete

Ashton's additional requirement is to carry SeriesPack's separated leading
one/two-byte planes through Frame. The split should occur late enough to expose
useful entropy, while retaining a predicate that can be interpreted before
fetching the suffix. This tests both representation structure and operation
laws: the existence of a head plane alone does not establish its logical meaning.

For an admitted unsigned FOR image, `x = base + residual`, a logical predicate
can be translated into the residual domain. A comparison can settle rows from
the residual head and request the suffix only where necessary. Splitting a
zero-extended u64 working value earlier may expose much less useful information.
Prediction residuals and arbitrary dictionary codes need different laws; their
heads cannot inherit the FOR predicate interpretation merely because the same
packing primitive stores them. Query preparation, exceptions and coordinate
mapping must preserve the applicability of the chosen route.

The [head-plane experiment](../filter/README.md) keeps two decisions distinct:
where/how much to split changes the image; guarded versus ordinary filtering can
change the program on identical bytes. Engine should be able to inspect the
meaning and dependencies of these alternatives without knowing their native
mask-reduction instructions. Complete-consumer measurements determine whether
the extra interpretation and preparation earn their cost.

### Evidence and execution need not follow codec boundaries

The subsequent [filter findings](../filter/FINDINGS.md) strengthen the case for
independent query evidence. Late residual heads help some FOR predicates, but
predicted residual heads need not describe logical value order. A diagnostic on
two Jena columns finds that logical block bounds could settle about 60–63% of
their median-query blocks at 0.5 bits/value. This counts potentially settled
blocks; it does not measure a bound-assisted query or its maintenance cost.
Investigate logical bounds or finer logical-head evidence alongside splitting
logical values before prediction, with exact checkpoint-restarted work for
unresolved regions. The schema should make the evidence's soundness, coordinates
and repair dependencies visible without requiring it to share the body codec.

The [point findings](../point/FINDINGS.md) also separate cheaper preparation from
a cheaper operation. Sharing decoder kernels saves substantial capacity but
leaves large point gaps against native global FOR. The filter study removes
significant wrapper/dispatch work yet still loses several comparisons to strong
narrow native controls. These local observations motivate comparing native/plain
forms and larger homogeneous execution regions with locally byte-minimising
blocks. Compression grain, evidence grain, kernel grain and binding reuse are
different choices; exposing one block kind must not force one dispatch per block.

Finally, fallback quality is operation-specific. The filter's patched decode
fallback loses to the existing count path, suggesting reuse of an owner count
operation rather than another patch decoder. An inspectable composition should
retain useful child operations as well as their decompositions. A total getter
establishes correctness, but does not establish a strong complete-operation
fallback. These are research implications of the linked local experiments,
not selected presets or cross-platform policy.

## How to investigate fidelity without deciding an interface first

Compare several views of the same small, manually authored candidate universe:
logical access; a good opaque complete operation; codec/width facts; and richer
relations with operation expansions. The manual complete programs are the
reference for opportunities missed by each description. Opaque complete kernels
remain serious competitors: a richer description earns value when it enables
reuse, a better composition or a better choice, rather than duplicating an
already excellent opaque operation.

Useful contrasts span more than one kernel family:

- **Structure across a boundary.** Independent columns versus shared codes,
  conditioned bases or routed values, consumed by grouping or projection into
  another Ikea container. Include an isolated field projection and a change to
  the conditioning field, so a compression win cannot hide its dependency cost.
- **Demand and discovery.** A coarse block selection, fine selected rows and
  actual presence/validity can imply different required inputs. Compare staged
  acquisition, eager shared acquisition and retained traversal state. A selected
  output does not by itself authorize reading its reconstruction dependencies.
- **Evidence and exact work.** A conservative prefilter plus exact refinement
  competes with direct evaluation. Preserve Boolean obligations and unresolved
  group topology; marginal selectivity and co-location alone lose information.
- **Change without mandatory migration.** Keep the bytes and vary the program;
  then separately price new encoding, conversion, overrides, cleaning and old
  readers. Carry parent applicability and dependent repair through substitution.

These are connected exploratory cases, not a staged implementation plan. For
each, identify the particular hidden relationship that prevented a useful
program. Remove that information again to test the explanation. Also retain
cases where the richer description only adds setup, state or code size.
Measure preparation and complete consumption, with latency, throughput and
memory distinguished; do not estimate the compound winner from isolated leaf
costs. Diversity should cover different mechanisms, rather than many parameter
variants of one algorithm.

## What the current execution witness can say

The [encoded grouped consumer](ENCODED.md) preserves codes, nullable values and
original coordinates through an actual hash output. Its rich and opaque arms
currently use the same local-code reduction algorithm and the same useful body
kernels. Their principal distinction is selecting block recipes in preparation
or inside the complete operation. Mostly similar runtime therefore answers a
narrow preparation/dispatch question. It neither demonstrates nor refutes the
larger schema-fidelity ambition above.

It already supplies two useful cautions. Prepared decoder storage can exceed
the encoded body, so description and binding costs need actual accounting. And
adding a patched codec exposed a parent shortcut that assumed every remaining
kind was delta: correct child getters did not preserve that shortcut. Explicit
applicability and a total fallback fixed it. Those observations should shape
further experiments. The subsequent [cross-column case](CROSS-COLUMNS.md)
compares independent encodings with a common code stream and two projected maps.
Counting codes before applying those maps changes the complete program; lifted
predicate weights also reuse an existing owner SUM without reconstructing rows.
Decorrelation, isolated demand and prepared state preserve important losses.
Forward operations remain valid on duplicate projected values while a one-code
equality inverse does not. This gives a concrete reason to expose relations,
operation applicability and interpretation identity independently of codec names.
It does not establish a general optimiser, a universal shared layout or a
completed lifecycle policy.
