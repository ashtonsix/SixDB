# Spikes

A stable home for an investigation: its question, candidate designs, reading,
code, measurements, and findings belong together. Name the directory after the
question, so a different candidate answer does not require moving it.

A spike can begin with one README. Add files or subdirectories when they help;
there is no required template. Several implementations or measurement campaigns
can share the same home while the question remains useful. When another home
takes over, carry the surviving findings there and retire the redundant machinery;
the [September curation](../notebook/retired-spikes.md) is an example with recovery pointers.
Loose thoughts can stay in the [notebook](../notebook/ideas.md) indefinitely.

For Orbital, the [research map](../notebook/orbital/reference/README.md) groups briefs,
experiments and checks under Object, Allocation, Journal, Port and Worker.

- [Ikea composition](ikea-composition/README.md): reusable stages, ergonomics and interface seams.
- [Filter composition](filter-composition/README.md): compiler expansion and native AVX2/AVX-512/NEON consumer costs select fused hot filters and shared canonical CPS consumers; latency and throughput remain separate.
- [Packed filters](packed-filters/README.md): SeriesPack/TuplePack predicate masks, direct packed comparisons and independent native latency/throughput controls.
- [Layout analyser](layout-analyser/README.md): workload objectives, plane splitting, hardware fitting and reusable layout selection.
- [Tuple layout](tuple-layout/README.md): physical layout, bound operations and native batching.
- [StreamPack](streampack/README.md): variable-byte packing and sidecar comparisons, with production qualification through Ikea, Loom and Orbital.
- [Boundlist](boundlist/README.md): Ikea extraction and ordered point/range search, with adopted AVX2/AVX-512/NEON kernels, complete-consumer scorecards, Loom staging and retained-reader qualification.
- [Index directories](index-directories/README.md): planning-only operation budgets against Calico/LMDB, composition review, layouts, prior art and full pseudocode; no directory implementation yet.
- [Hashtable multimap](hashtable/README.md): unpromoted owning multimap, captured read/write comparisons and payload/lifetime questions. The set/map core is maintained in [Ikea](../../ikea/docs/hashtable/usage.md); superseded studies are archived.
- [Hashtable K-way merge](hashtable-merge/README.md): bucket-wise traversal, external workspace and retained accumulation comparisons. Single-source mutable accumulation is maintained in Ikea.
- [Hashtable overflow](hashtable-overflow/README.md): completed tag/continuation and occupancy comparison; eight-bit tags are maintained in Ikea.
- [Hashtable Swiss probes](hashtable-swiss-probes/README.md): completed 32/64-byte comparison with actual Boost/Abseil controls; eight-bit tags and 16-byte groups retained, rejected variants archived.
- [Hashtable Swiss batches](hashtable-swiss-batch/README.md): graduated ISA-selected 64/32-byte read comparisons over 16-byte logical groups; whole-table qualification with actual Boost/Abseil controls, prototypes archived.
- [Boost table adoption](hashtable-boost-adoption/README.md): pinned source/dependency and linked-footprint audit; ordinary ownership, Loom, ordered accumulation and byte-preserving MVCC seams, without a production migration.
- [Bec256 with current Ikea](bec256-composition/README.md): exact codec operations, native pairs and larger-bitset population/length directories built with SeriesPack and TuplePack.
- [Ikea on Neoverse V3](ikea-neoverse-v3/README.md): C8g/C9g comparisons, compiler scheduling, lookup and exact-span experiments.
- [Ikea CPU comparison](ikea-cpu-comparison/README.md): October 2026 snapshot across six processors, matched case evidence and a frozen cloud-price model.
- [Executable placement](executable-placement/README.md): distinguishing changed code from placement effects.
- [Memory characterisation](memory-characterisation/README.md): automatic MLP, prefetch, cache sharing and host diagnostics.
- [Loom execution](loom-execution/README.md): memory microtask rings, fixed hint classes, worker channels/groups, synchronization and affinity-crossing operations.
- [Loom hardware policy](loom-hardware-policy/README.md): startup lookup, background calibration and abstract demand codes mapped to inline native hints.
- [Loom locality and execution](loom-locality-execution/README.md): direct/Ring selection and contiguous-demand hints, with retained native comparisons and the handover to caller-qualified execution policy.
- [Assembly module calls](assembly-calls/README.md): assembly-first prototypes linked to real Ikea/Loom operations, ABI sketches and caller-owned result frames.
- [Call boundaries](call-boundaries/README.md): Loom/Orbital caller and callee assembly, fat returns, native carrier limits and frequency-sensitive ABI alternatives.
- [Memory descriptors](memory-descriptors/README.md): arena-selected palettes, one-byte trampolines, placement and retained native access.
- [Allocation palettes](allocation-palettes/README.md): native 4/16/64 KiB timing, independent spacing/count comparisons and selected Group profiles.
- [Allocation temperature reuse](allocation-reuse-temperature/README.md): bounded HOT/WARM queues, reuse ordering and actual allocator costs.
- [Live-tail reclamation](allocation-live-tail/README.md): explicit extent relinquishment, retained-reader protection and native costs of reclaiming and regrowing pages.
- [CFT commit latency](cft-commit-latency/README.md): directional network measurement and host/flow selection; durable quorum latency, storage throughput cliffs and live log preparation.
- [Local message handoff](orbital-local-handoff/README.md): native Zen 5 measurements of batching, payload readiness, polling CPU, queue admission and instrumentation sensitivity.
- [Orbital reservation policies](orbital-reservation-policy/README.md): broad-waiter progress, WAN convoys and deterministic bypass, with formal and native comparisons.
- [Orbital prepared retention](orbital-root-fusion/README.md): deriving a prepared root from an existing same-owner record, with acquisition and recovery comparisons.
- [Orbital views](orbital-views/README.md): embedded object histories, versioned parts, sparse views, copying/taking, worker masks and allocation reuse.
- [Orbital MVCC](orbital-mvcc/README.md): selected embedded modules, disposable callers and retained controls; [outcomes and handover](orbital-mvcc/OUTCOMES.md) connect performance and ownership lessons to future Ikea/Engine work.
- [Orbital Stage 2 composition](orbital-stage2-composition/README.md): historical selection/Representation/View seams and real SeriesPack–TuplePack conversion; its findings precede the selected modules.
- [Selected access](orbital-selected-access/README.md): historical borrow, escape and delayed-completion lifetime falsifiers; current contracts live in Orbital.
- [Representation conversion](orbital-representation-conversion/README.md): historical 1–8-column conversion fixture and independent encoding/interpretation oracles.
- [Durable object composition](orbital-objects/README.md): earlier composition of state, native views and physical lifetimes; Linux protection, UFFD and COW probes.
- [Row filter signatures](row-filter-signatures/README.md): per-row Boolean evidence and progressive refinement.
- [Aggregate maintenance](aggregate-maintenance/README.md): descendant summaries without excessive mutation cost.
- [Trie remapping](trie-remapping/README.md): natural-key tries with difficult suffixes remapped to physical positions.
- [Regexp lowering](regexp-lowering/README.md): exact LIKE rewrites and necessary-condition prefilters.

## Earlier investigations

- [Orbital first formal investigation](orbital-formal-first-pass/README.md): superseded bounded checks, counterexamples and sizing evidence; the maintained formal models are in [Orbital](../../orbital/spec/README.md).

## Building a study

Activate a study in the editor with `python3 workbench/tools/dev.py --add NAME`;
[editor setup](../tools/editors.md) owns refresh and dependency instructions.
For execution, use its runner or configure the target directly as below.

Create a directory such as `workbench/spikes/example/` and declare its
sources and dependencies in a small `CMakeLists.txt`:

```cmake
add_library(example_core STATIC kernel.cpp)
sixdb_target(example_core)

add_executable(example_bench bench.cpp)
sixdb_target(example_bench)
target_link_libraries(example_bench PRIVATE example_core)
```

`example` is illustrative; no such study is supplied. Prefix target names with
the study name because names are shared across the graph. Only selected studies
are configured, and their executables are excluded from the default build:

```sh
cmake --preset dev -DSIXDB_SPIKES=example
cmake --build --preset dev --target example_bench
```

For several studies, pass `'-DSIXDB_SPIKES=example;another'`. Selection is
cached per build directory; clear it with `-DSIXDB_SPIKES=`. List sources
explicitly. Ninja tracks included headers and compiler options. Editing
`bench.cpp` compiles that TU and relinks; the compiled `kernel.cpp` can be
reused by every executable linked to `example_core`.

For compile-only iteration, request the object target:

```sh
ninja -C build/clang/dev \
  workbench/spikes/example/CMakeFiles/example_core.dir/kernel.cpp.o
```

Inspect names with `ninja -C build/clang/dev -t targets all`. Normal builds need
no clean step. Use separate build directories for concurrent builds or different
compiler/ISA/optimization settings.

[BUILDING.md](../../BUILDING.md) covers distribution packaging.
