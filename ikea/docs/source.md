# Source and compilation boundaries

Ikea separates ordinary caller contracts, composition authoring and implementation.
Both modules use ordinary caller headers, supported `author/` headers and internal
`detail/` headers. Template bodies remain visible where they need the caller's
concrete context; consumers also link the compiled library. The
[extension guide](extension.md) describes shared facilities.

## SeriesPack source boundaries

| Surface | Owning files | Responsibility |
| --- | --- | --- |
| Ordinary callers | Umbrella `seriespack.h`; `{read,write,read_operation,mutation_operation}.h` | Checked operations, explicit trusted entries, whole-operation bindings |
| Storage/configuration | `format.h`, `view.h`, `presets.h`, `representation.h`, `selection.h`, `effects.h`, `status.h` | Unsigned domains, placement, recovery metadata, masks and local outputs |
| Composition authors | `author/{expression,read,write,record,native,chain,summaries}.h` | Logical trees, admitted typed operations, evaluators, native carriers, CPS wrappers and maintenance laws |
| Implementation | `detail/` | Wire arithmetic, traversal, footprint derivation and native code; not a supported include surface |
| Compiled implementation | `src/seriespack/{readers,admission,representation}.cpp` | Runtime dense dispatch; cold placement/alias checks and diagnostics; descriptor parsing/encoding |

The paths in that table are relative to [include/ikea/seriespack](../include/ikea/seriespack),
except the umbrella and [src](../src/seriespack). Read-only code should include `read.h`; using
the ordinary umbrella also exposes write instantiations to the compiler.

## Where to change a behavior

A native store or reconstruction belongs in
`detail/native/{avx2,avx512,neon}/{read,write}.h`. NEON's grouped evaluator has
`neon/groups.h`; AVX-512 grouping lives with its read bodies. ISA instructions,
exact store sizes, transpose/permutation constants and register-level reductions
stay visible for inlining. Comments explain bounds and choices that affect codegen.

A placement/alias rule belongs in [src/seriespack/admission.cpp](../src/seriespack/admission.cpp), with
small declaration/state headers under `detail/`. The templated writable-leaf walk
is in `detail/mutation/admission.h`; it supplies actual source identity and field
geometry to that compiled analysis. Allocation and error text stay there.
`author/write.h` owns typed binding, preflight and endpoint adapters; journal
storage, erased interfaces and physical overlap analysis have separate homes.

Physical field descriptions in `detail/mutation/fields.h` are shared by
construction, clearing and admission. `footprint.h` describes actual point/native
store coverage. Coverage cannot always equal a field's abstract bit extent because
preserving neighbors can issue a wider store.

`physical.h` owns specialized physical range traversal; `composed.h` owns traversal
through substituted leaves; `assignment.h` supplies native reverse projection.
Construction has physical and composed drivers. They share native bodies and field
walks. Canonical physical expressions select the physical range driver once at the
operation boundary. The chosen substituted striped child can be assembled once
per tile; other nested leaves retain their own writers.

An owner adapter belongs with its owner or an executable example.
[examples/seriespack/integration.cpp](../examples/seriespack/integration.cpp) demonstrates the contract;
[test/seriespack/integration/ownership.cpp](../test/seriespack/integration/ownership.cpp) owns exhaustive
event-order checks.

## Compilation and validation

SeriesPack has three independently compiled TUs, with common cold overlap analysis
linked from `ikea_core`. Mutation tests are grouped by behavior,
with eight-width TU shards beneath that organization to control compiler memory.
Test-only noinline wrappers avoid cloning the same optimized operation for every
assertion scenario; they do not change library/benchmark kernel inlining.

`python3 ikea/test/headers.py BUILD` checks each ordinary/author header independently
using the configured profile. `ikea_validate` runs the behavior tests and executable
guides. [Benchmark instructions](../../workbench/benchmarks/seriespack/README.md) include isolated catalog compilation
and representative incremental edit probes.

The [frozen wire fixture](../test/seriespack/reference/README.md) independently checks
compatibility. Historical comparisons are available through the opt-in targets
documented in [Workbench](../../workbench/spikes/ikea-composition/ikea2-campaign/README.md).

## TuplePack source boundaries

Ordinary `tuplepack/{description,view,plan,read,write,construction,selection}.h`
separates placement, prepared controls and commands. `author/composition.h` owns
recursive groups; `author/maintenance.h` owns independent observations;
`author/execution.h` owns whole-operation erasure/native adapters and the module's
pipeline carrier. `author/routes.h` exposes route normalization. Row shapes belong
to the ordinary reader/writer plans and carry through native adapters and groups.
`detail/mutation.h` shares checked mutation admission and driving;
`detail/window.h` owns row masks, tails and maintenance brackets. Physical
traversal remains specialized. `detail/{plan,packet_plan,gpr}.h` and
`detail/native/` are internal. `detail/native/shuffle.h` groups the small ISA
shuffle bodies; bounded memory access, packet traversal and route selection
have their own headers beside it.

`src/tuplepack/{description,prepare,packet_prepare,construction,kernels,gpr,packet,routes}.cpp`
compile cold work and named kernel endpoints independently. The physical native
functions share inline bodies with authors. GPR point and multi-row operations
share bounded word transfers and code extraction; preparation selects ordinary
endpoints once. Arbitrary distant maps retain the complete byte-coalesced path.
Prepared controls contain only one ISA's data.

The [TuplePack tests](../test/tuplepack/README.md) are organized by wire, operations,
packet shapes, execution and ownership. Its [benchmarks](../../workbench/benchmarks/tuplepack/README.md)
separate primitive controls, ordinary calls, scans and CPS consumers. A change to
shared effects, overlap or pipeline mechanics must run both modules' affected checks.

## Shared execution entry

`include/ikea/detail/native_chain.h` owns pipeline entry and continuation hops for
both modules. On AArch64, `run` enters through a fixed-frame AAPCS shim that
preserves caller registers and passes register payloads as separate arguments. Keep this
entry when changing the chain: Clang 21 can otherwise retain a realigned caller's
frame base in x19 across a `preserve_none` call that clobbers it.

Only the shim excludes ASan/UBSan instrumentation so its frame stays fixed;
stages and completion remain instrumented. The regression in
[execution.cpp](../test/tuplepack/execution.cpp) exercises a realigned caller and
deliberate stage clobber. The [evidence note](../../workbench/spikes/tuple-layout/module-evidence/README.md#validation-abi-and-compilation)
retains the reproducer and disassembly findings.
