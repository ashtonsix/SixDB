# Extending and changing Ikea

Ikea separates ordinary caller contracts, supported `author/` surfaces and
internal `detail/` implementation. Bodies stay in headers where caller context
enables useful specialization; cold work belongs in compiled TUs.

## Define the operation before its execution

Begin with the value domain, physical law and operations. Specify original
coordinates, access bounds, borrowing and failure guarantees so different
representations can serve the same logical contract. SeriesPack composes
bit-window joins; TuplePack composes ordered code projections and mutation groups;
Bec256 supplies headless bitset bodies and native pairs with independent addresses.
Their value types, traversal and error vocabulary remain module-specific.

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

## Shared facilities

| Facility | Shared mechanism | Module-owned behavior |
| --- | --- | --- |
| `ikea/effects.h` | Issued byte spans qualified by named source views | Physical footprints, traversal and capacity bounds |
| `detail/overlap.h`, `src/overlap.cpp` | Cold overlap analysis for repeating spans and interleaving | Semantic conflicts, view rules and diagnostics |
| `detail/native_chain.h` | Bounded straight-through tables, native argument handoff and early completion | Carrier, mask meaning, stage grain and semantic body |

Shared mechanics need checks from affected modules. Hooks must remain
infallible and cannot suspend after admission. Persistent summary writes need
their own coverage; private contributions can instead be retained by the owner.

## SeriesPack source boundaries

Paths below are relative to [include/ikea/seriespack](../include/ikea/seriespack)
unless marked `src/`. Read-only callers can include `read.h` to avoid exposing
write instantiations through the ordinary umbrella.

| Responsibility | Owning files |
| --- | --- |
| Ordinary calls and erasure | `{read,write,read_operation,mutation_operation}.h` |
| Format, placement and recovery | `format.h`, `view.h`, `presets.h`, `representation.h` |
| Logical expressions and evaluation | `author/{expression,read,record,native,summaries}.h` |
| Typed mutation binding and preflight | `author/write.h` |
| Runtime dense read dispatch | `src/seriespack/readers.cpp` |
| Cold placement/alias rules and diagnostics | `src/seriespack/admission.cpp`; leaf walk in `detail/mutation/admission.h` |
| Descriptor parsing and encoding | `src/seriespack/representation.cpp` |
| Native reconstruction and stores | `detail/native/{avx2,avx512,neon}/{read,write}.h` |
| Shared field geometry and store coverage | `detail/mutation/{fields,footprint}.h` |
| Physical/composed traversal and reverse projection | `detail/mutation/{physical,composed,assignment}.h` |
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
| Physical description and recovery | `description.h`; `src/tuplepack/description.cpp` |
| Ordinary placement, plans and commands | `{view,plan,read,write,construction,selection}.h` |
| Decoded packet grouping | `packet_layout.h`; `src/tuplepack/packet_layout.cpp` |
| Code-map admission and ISA controls | `src/tuplepack/{prepare,packet_prepare}.cpp` |
| Construction preparation | `src/tuplepack/construction.cpp` |
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
of the chosen lowering.

## Bec256 source boundaries

Paths below are relative to [include/ikea/bec256](../include/ikea/bec256)
unless marked `src/`.

| Responsibility | Owning files |
| --- | --- |
| Ordinary sources, destinations and prepared replacements | `codec.h`; `src/bec256/codec.cpp` |
| Statistical size estimates and cutoff calibration statistic | `analysis.h`, `author/analysis.h`; `src/bec256/analysis.cpp`, `detail/estimate.h` |
| Exact framing validation and diagnostics | `src/bec256/validation.cpp` |
| Native blocks, pairs and bounded read adapters | `author/native.h` |
| Shared checked and trusted native writes | `author/write.h`; `detail/write.h` |
| Count-tree, byte-rank and bit-packing bodies | `detail/{tables,pack,scalar}.h`, `detail/native/{avx512,neon}.h` |
| Register bitstream assembly and exact stores | `detail/native/{assemble,emit}.h` |
| Native pipeline carrier | `author/chain.h` |

The [representation guide](bec256/representation.md) defines one fixed wire
format. Population, body length, placement and directory schema belong to the
caller. Native immediate writes retain values through admission and exact stores;
prepared replacements own transient bytes that can survive an owner wait.
Both report the issued body span. Metadata and summaries are separate children
of the owner's mutation, including when the body has zero bytes.

The optional heuristic shares byte populations and rank widths with encoding,
and can decline before count-tree construction or destination/effect access.
`detail/write.h` adapts this to the ordinary result. Unconditional encoding has
no heuristic branch; the statistical model is separate from exact write admission.

## Shared execution entry

The modules use `include/ikea/detail/native_chain.h`. A plan has a power-of-two
number of slots, at least two: 1..Slots−1 stages plus completion. Unused slots also contain
completion. Alignment is at least 64 bytes and at least the table size, allowing
an early-completion cursor to locate the final slot arithmetically.

Clang 21.1.8 and the selected ISA define the private `preserve_none`/`musttail`
ABI. Native payloads are flattened into separate register arguments. Carrier
changes need real typed bridges with admitted semantics. A terminating tail hop
cannot retain stage-local stack addresses or pending destructors. Driver-owned
plans and bindings survive the complete chain; explicit suspension follows return.

TuplePack’s named vector endpoints use `IKEA_TUPLE_CC`: AVX2 needs regcall to
avoid the default SysV aggregate return through memory. NEON uses a four-vector
HVA, AVX2 a pair and AVX-512/VBMI one vector. GPR endpoints use the ordinary
integer convention. Inline bodies share the same controls.

On AArch64, `run` enters through a fixed-frame AAPCS shim that preserves caller
registers and passes payloads separately. Keep this entry: Clang 21 can otherwise
retain a realigned caller’s frame base in x19 across a `preserve_none` call that
clobbers it. Only the shim excludes ASan/UBSan instrumentation so its frame stays
fixed; stages and completion remain instrumented. The
[execution regression](../test/tuplepack/execution.cpp) and
[evidence note](../../workbench/spikes/tuple-layout/module-evidence/README.md#validation-abi-and-compilation)
retain the check, reproducer and disassembly findings.

## Compilation and validation

Use the [pinned build](../../BUILDING.md) and preserve incremental TUs.
SeriesPack’s mutation tests use behavior groups and eight-width TU shards to
bound compiler memory. Test-only noinline wrappers limit repeated assertion
instantiations without changing library or benchmark inlining.

`ikea_validate` runs behavior checks and executable guides.
`python3 ikea/test/headers.py BUILD` checks ordinary/author headers independently
with that build’s profile. The [SeriesPack tests](../test/seriespack/README.md)
and [TuplePack tests](../test/tuplepack/README.md) identify focused targets;
[Bec256 checks](../test/bec256/README.md) cover its wire, replacement and pair composition.
Use the [frozen SeriesPack fixture](../test/seriespack/reference/README.md) for its
existing wire law; never change the reference just to agree with a changed kernel.

The [SeriesPack](../../workbench/benchmarks/seriespack/README.md),
[TuplePack](../../workbench/benchmarks/tuplepack/README.md) and
[Bec256](../../workbench/benchmarks/bec256/README.md) benchmark guides own
consumer comparisons and compilation probes. Owner adapters belong with their
owners or in executable integration examples.
