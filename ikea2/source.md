# Source and compilation boundaries

Ikea separates ordinary caller contracts, composition authoring and implementation.
The concrete files below belong to its first component, SeriesPack. Additional
components can follow the same audience distinction without inheriting its array
operations or moving speculative shared abstractions into a common layer.

Headers have three audiences. This is a support distinction, not a promise that
ordinary callers parse no implementation: C++ template bodies that need their
concrete context remain visible. Link the compiled library in every case.

| Surface | Owning files | Responsibility |
| --- | --- | --- |
| Ordinary callers | Umbrella `seriespack.h`; `{read,write,read_operation,mutation_operation}.h` | Checked operations, explicit trusted entries, whole-operation bindings |
| Storage/configuration | `format.h`, `view.h`, `presets.h`, `representation.h`, `selection.h`, `effects.h`, `status.h` | Unsigned domains, placement, recovery metadata, masks and local outputs |
| Composition authors | `author/{expression,read,write,record,native,chain,summaries}.h` | Logical trees, admitted typed operations, evaluators, native carriers, CPS wrappers and maintenance laws |
| Implementation | `detail/` | Wire arithmetic, traversal, footprint derivation and native code; not a supported include surface |
| Compiled implementation | `src/{readers,admission,representation}.cpp` | Runtime dense dispatch; cold placement/alias checks and diagnostics; descriptor parsing/encoding |

The paths in that table are relative to [include/ikea2/seriespack](include/ikea2/seriespack),
except the umbrella and [src](src). Read-only code should include `read.h`; using
the ordinary umbrella also exposes write instantiations to the compiler.

## Where to change a behavior

A native store or reconstruction belongs in
`detail/native/{avx2,avx512,neon}/{read,write}.h`. NEON's grouped evaluator has
`neon/groups.h`; AVX-512 grouping lives with its read bodies. ISA instructions,
exact store sizes, transpose/permutation constants and register-level reductions
stay visible for inlining. Comments explain bounds and choices that affect codegen.

A placement/alias rule belongs in [src/admission.cpp](src/admission.cpp), with
small declaration/state headers under `detail/`. The templated writable-leaf walk
is in `detail/mutation/admission.h`; it supplies actual source identity and field
geometry to that compiled analysis. Allocation and error text stay there.
`author/write.h` owns typed binding, preflight and endpoint adapters; journal
storage, erased interfaces and physical overlap analysis have separate homes.

Physical field descriptions in `detail/mutation/fields.h` are shared by
construction, clearing and admission. `footprint.h` describes actual point/native
store coverage. Coverage cannot always equal a field's abstract bit extent because
preserving neighbors can issue a wider store. This is a deliberate distinction.

`physical.h` owns specialized physical range traversal; `composed.h` owns traversal
through substituted leaves; `assignment.h` supplies native reverse projection.
Construction has physical and composed drivers. They share native bodies and field
walks. Canonical physical expressions select the physical range driver once at the
operation boundary. The chosen substituted striped child can be assembled once
per tile; other nested leaves retain their own writers. Keeping these traversal
families explicit preserves the gains that a generic per-region visitor lost.

An owner adapter belongs with its owner or a teaching example, not in native
headers. [examples/integration.cpp](examples/integration.cpp) is short;
[test/integration/ownership.cpp](test/integration/ownership.cpp) owns exhaustive
event-order checks. Neither establishes a Loom/Engine production API.

## Compilation and validation

The library has three independently compiled TUs. No unity build, LTO requirement
or header-only replacement is introduced. Mutation tests are grouped by behavior,
with eight-width TU shards beneath that organization to control compiler memory.
Test-only noinline wrappers avoid cloning the same optimized operation for every
assertion scenario; they do not change library/benchmark kernel inlining.

`python3 ikea2/test/headers.py BUILD` checks each ordinary/author header independently
using the configured profile. `ikea2_validate` runs the behavior tests and executable
guides. [Benchmark instructions](bench/README.md) include isolated catalog compilation
and representative incremental edit probes. A source split is useful only if its
measured rebuild scope and responsibilities improve; file count is not the measure.

Keep [frozen wire fixtures](test/reference/README.md) independent. Broad comparative
history and a frozen predecessor implementation live in
[Workbench](../workbench/spikes/ikea-composition/ikea2-campaign/README.md), behind an
opt-in comparison target. Neither candidate tests nor ordinary consumers depend
on the old Ikea module.
