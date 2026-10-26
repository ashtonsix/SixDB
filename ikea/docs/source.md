# Extending and changing Ikea

Ikea separates ordinary caller contracts, supported `author/` surfaces and
internal `detail/` implementation. Bodies stay in headers where caller context
enables useful specialization; cold work belongs in compiled TUs.
The [interface guide](interfaces.md) connects each module's placement/preparation
phases, coordinates, effects and parent-schema descriptions.

## Define the operation before its execution

Begin with the value domain, physical law and operations. Specify original
coordinates, access bounds, borrowing and failure guarantees so different
representations can serve the same logical contract. SeriesPack composes
bit-window joins; TuplePack composes ordered code projections and mutation groups;
Bec256 supplies headless bitset bodies and native pairs with independent addresses.
Their value types and traversal remain module-specific. Shared error names
identify shared conditions; module-specific conditions retain precise names.

Keep three descriptions distinct: logical mutation destinations, issued byte
writes, and maintenance dependencies. A summary can depend on untouched fields;
a preserving store can reissue bits outside the mutation map. The owner needs
enough information to admit all accessed storage and reserve effects before
execution. [Owner integration](integration.md) owns leases, suspension, cancellation
and coordinated visibility.

Compile cold validation, control preparation and diagnostics in independent TUs.
Keep cheap command admission visible where outlining would spill live native
values. Share native bodies between ordinary and composed callers, and
keep explicit ISA instructions available for inlining. Choose applicability and
traversal at the operation boundary. Erase a useful whole operation so repeated
inner work retains the concrete traversal.

Validate wire behavior independently, then check boundaries, inactive masks,
substituted storage, effects and lifetimes. Inspect the finished consumer as well
as the leaf kernel: a vector aggregate or outlined helper can introduce memory
handoff. Compare complete operations, including their admission and effects.
Research history and timing comparisons belong with Workbench evidence.

[Building fast consumers](consumers.md) owns the usage decisions: point versus
range traversal, reuse of projections and proofs, placement visibility, selective
canonical sharing, masks and guarded execution. Keep those choices expressible
through existing ordinary and author entries rather than adding an execution
mode for each successful experiment.

For implementation, preserve shared immutable preparation separately from
block-local windows and command admission. Resolve format applicability around
a useful whole operation. A native packet should remain native across the body;
inspect the actual caller for hidden aggregate returns or outlined helpers.
An inlinable implementation still needs measurements of routing, admission,
effects and result handling, alongside its correctness and lifetime checks.

A shared stage should remove a real specialization dimension. For example,
physical decoders can normalize unsigned values to 1/2/4/8-byte lanes before a
shared consumer. Preserve value meaning and mask coordinates, and use one
canonical chain type throughout that composition; equal register shapes do not
make differently typed stage function pointers interchangeable. The maintained
[filter composition example](../examples/filters/composition.cpp) shows this
boundary. Keep direct packed alternatives available to ordinary callers.

## Shared facilities

| Facility | Shared mechanism | Module-owned behavior |
| --- | --- | --- |
| `ikea/effects.h` | Issued byte spans qualified by named source views | Physical footprints, traversal and capacity bounds |
| `detail/overlap.h`, `src/overlap.cpp` | Cold overlap analysis for repeating spans and interleaving | Semantic conflicts, view rules and diagnostics |
| `author/chain.h` | Bounded straight-through tables, native argument handoff and early completion | Carrier, mask meaning, stage grain and semantic body |
| `bytes.h`, `description.h` | Borrowed storage bytes and parent-schema framing | Numeric value types and each layout's encoded body |
| `bitset256.h` | Fixed-size position set and portable byte conversion | Whether positions name rows, ranks or another domain |
| `comparison.h`, `detail/filter/compare.h` | Unsigned predicates and explicit native lane-to-mask bodies | Stored-width admission, packed shortcuts and original-position coordinates |

Shared mechanics need checks from affected modules. Hooks must remain
infallible and cannot suspend after admission. Persistent summary writes need
their own coverage; private contributions can instead be retained by the owner.

## SeriesPack source boundaries

Paths below are relative to [include/ikea/seriespack](../include/ikea/seriespack)
unless marked `src/`. Read-only callers can include `read.h` to avoid exposing
write instantiations through the ordinary umbrella.

| Responsibility | Owning files |
| --- | --- |
| Ordinary calls and erasure | `{read,write,reader,writer,erased_reader,erased_writer}.h` |
| Layout, placement and recovery | `layout.h`, `view.h`, `presets.h`, `layout_value.h` |
| Scalar physical point building blocks, without effects or summaries | `point.h`, `author/store.h`; packed arithmetic stays in `detail/point.h` |
| Logical expressions and evaluation | `author/{expression,record,native,summaries}.h`; `reader.h` |
| Predicate masks from storage or native values | `filter.h`; `author/filter.h` |
| Typed mutation binding and preflight | `writer.h` |
| Runtime dense read dispatch | `src/seriespack/readers.cpp` |
| Cold placement/alias rules and diagnostics | `src/seriespack/admission.cpp`; leaf walk in `detail/mutation/admission.h` |
| Descriptor parsing and encoding | `src/seriespack/layout.cpp` |
| Native reconstruction and stores | `detail/native/{avx2,avx512,neon}/{read,write}.h` |
| Physical mutation traversal and store coverage | `author/mutation.h`; private geometry in `detail/mutation/fields.h` |
| Composed traversal and reverse projection | `detail/mutation/{composed,assignment}.h` |
| CPS authoring | `author/chain.h`; shared mechanism below |

NEON’s grouped evaluator lives in `neon/groups.h`; AVX-512 grouping lives with
its read bodies. Store widths and transpose/permutation constants remain next
to the instructions they control.

Construction and replacement share native bodies and field walks. Canonical
physical expressions select the physical range driver once. A substituted
striped child can be assembled once per tile; other nested leaves retain their
own writers. This preserves specialized traversal beneath a common operation.

## TuplePack source boundaries

Paths below are relative to [include/ikea/tuplepack](../include/ikea/tuplepack)
unless marked `src/`.

| Responsibility | Owning files |
| --- | --- |
| Physical layout and recovery | `layout.h`; `src/tuplepack/layout.cpp` |
| Ordinary placement, plans and commands | `{view,plan,packet,read,write,initialize,transfer,selection}.h` |
| Reader code predicates and native packet masks | `filter.h`, `author/filter.h` |
| Decoded packet grouping | `packet_layout.h`; `src/tuplepack/packet_layout.cpp` |
| Code-map admission and ISA controls | `src/tuplepack/{prepare,packet_prepare}.cpp` |
| Construction preparation | `src/tuplepack/initialize.cpp` |
| Recursive mutation groups | `author/composition.h` |
| Independent observation projection/law | `author/maintenance.h` |
| Erasure, native adapters and pipeline carriers | `author/execution.h`; bodies exposed by `author/native.h` |
| Route normalization | `author/routes.h`; `src/tuplepack/routes.cpp` |
| Shared command admission and observation windows | `detail/{mutation,window}.h` |
| GPR transfer bodies and endpoints | `detail/gpr.h`; `src/tuplepack/gpr.cpp` |
| 64-byte point/native and packet endpoints | `src/tuplepack/{kernels,packet}.cpp` |
| Native instructions and bounded memory helpers | `detail/native/`; small shuffles in `detail/native/shuffle.h` |
| Internal controls | `detail/{plan,packet_plan,gpr}.h` |

Preparation selects physical transfers and folds grouping into decoded-byte
routes. Short units and tight windows permit combined loads; consecutive bytes
with a common shift permit bounded GPR transfers. Scattered maps use byte/word
assembly or point bodies with a register permutation. Ordinary and inline native
operations share controls and access only active units. The declared access masks
and [contracts](tuplepack/reference.md) define caller obligations independently
of the chosen lowering. An explicit native `author::read_access::common_window`
grant additionally permits bounded reads through initialized, read-isolated
inactive interior units; stride geometry alone does not grant that access.
Sparse stores and effects still exclude inactive units.

## Bec source boundaries

Paths below are relative to [include/ikea/bec](../include/ikea/bec)
unless marked `src/`.

| Responsibility | Owning files |
| --- | --- |
| Shared vocabulary: failures, diagnostics, destination plane | `common.h`; `src/bec/common.cpp` |
| Bec256 ordinary sources, destinations and prepared replacements | `bec256.h`; `src/bec/bec256.cpp` |
| Statistical size estimates and cutoff calibration statistic | `analysis.h`, `author/analysis.h`; `src/bec/analysis.cpp`, `detail/estimate.h` |
| Exact framing validation and diagnostics | `src/bec/validation.cpp` |
| Native blocks, pairs and bounded read adapters | `author/native.h` |
| Shared checked and trusted native writes | `author/write.h`; `detail/write.h` |
| Count-tree, byte-rank and bit-packing bodies, including the stream-generic level step | `detail/{tables,pack,scalar}.h`, `detail/native/{avx512,neon}.h` |
| Register bitstream assembly and exact stores | `detail/native/{assemble,emit}.h` |
| Native pipeline carrier | `author/chain.h` |
| Bec4096 ordinary API, validation and prepared replacements | `bec4096.h`; `src/bec/bec4096.cpp` |
| Bec4096 native blocks over Bec256's carrier; framing, block access, fused sixteen-stream walk and emission | `author/bec4096.h`; `detail/bec4096.h` |

The [representation guide](bec/representation.md) defines Bec256's fixed wire
format; the [Bec4096 guide](bec/bec4096.md) builds on it. Population, body length, placement and directory schema belong to the
caller. Native immediate writes retain values through admission and exact stores;
prepared replacements own transient bytes that can survive an owner wait.
Both report the issued body span. Metadata and summaries are separate children
of the owner's mutation, including when the body has zero bytes.

The optional heuristic shares byte populations and rank widths with encoding,
and can decline before count-tree construction or destination/effect access.
`detail/write.h` adapts this to the ordinary result. Unconditional encoding has
no heuristic branch; the statistical model is separate from exact write admission.

## Intervals and StreamPack source boundaries

[Intervals](intervals/usage.md) owns the common non-owning search and geometry
contract. StreamPack owns its compact image and retirement/value interpretation.

| Work | Source |
| --- | --- |
| Ordered lower/upper boundary and borrowed rank selection | `intervals/{types,search}.h` |
| Minimal geometry, carried ranges, filters and staged prepared windows | `intervals/geometry.h` |
| Direct, projected, sparse sources and borrowed checked edits | `intervals/{search,projected,sparse,edit}.h` |
| Native comparison/prefix/SIMD bodies and explicit G64 prefix policy | `intervals/detail/` |
| Prepared numeric binding, scans and independent native search | `intervals/author/search.h`; `src/intervals/search.cpp` |
| StreamPack compact geometry/admission/encoding/retirement implementation | `streampack/detail/compact/`; `src/streampack/compact/` |
| Complete value layout, admission, fused readers, writers and transfer | `streampack/`; `src/streampack/` |

The compact implementation is private to StreamPack, including historical
standalone compact-description decoding used by owner-private controls.
Intervals consumers do not import that storage policy, and Intervals has no
reverse dependency. The old Boundlist public headers/target are removed; historical
source recovery stays with the study. No persisted bytes were retagged.

Native packets and CPS stages remain visible inside complete owner operations.
The retained [Intervals qualification](../../workbench/spikes/intervals/GRADUATION.md)
and [Boundlist portfolio](../../workbench/spikes/boundlist/PORTFOLIO.md) explain why
flattened generic value wrappers and default wider inverse routes were rejected.
The owner retains coherent source/schema/backing/filter state through staged
completion and coordinates allocation, locks, publication and old-image lifetime.

## Frame source boundaries

Frame's [source and extension map](frame/reference.md#source-and-extension-map)
separates canonical description, manifest/content proof, checked reader, fitting
and per-stage algebra. `ikea::frame` compiles the drivers once; SeriesPack owns
width-specific packing/reductions and Intervals owns reconstruction/search.
The `author::stage` contract defines decode and optional whole-operation laws.
A new image shape lands with its description, proof and conformance; unsupported
placeholders refuse. Owner retention/publication lives in a separately linked
[Orbital example](../examples/frame/owner/history.cpp). Private preparation and
Frame mutation remain research.
