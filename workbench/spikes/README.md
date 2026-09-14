# Spikes

A stable home for an investigation: its question, candidate designs, reading,
code, measurements, and findings belong together. Name the directory after the
question, so a different candidate answer does not require moving it.

A spike can begin with one README. Add files or subdirectories when they help;
there is no required template. Several implementations or measurement campaigns
can share the same home, which remains useful after adoption or abandonment.
Loose thoughts can stay in the [notebook](../notebook/ideas.md) indefinitely.

- [Ikea composition](ikea-composition/README.md): reusable stages, ergonomics and interface seams.
- [Layout analyser](layout-analyser/README.md): workload objectives, plane splitting, hardware fitting and reusable layout selection.
- [Tuple layout](tuple-layout/README.md): physical layout, bound operations and native batching.
- [Packed integer kernels](packed-integer-kernels/README.md): grain, coalescing and width/ISA costs.
- [SeriesPack range execution](seriespack-range-execution/README.md): ranges, clipping, stores and alignment.
- [SeriesPack head projection](seriespack-head-projection/README.md): independent planes under varied placements.
- [BEC packed metadata](bec-packed-metadata/README.md): packed lengths as a concrete SeriesPack consumer.
- [Bec256 with current Ikea](bec256-composition/README.md): exact codec operations, native pairs and larger-bitset population/length directories built with SeriesPack and TuplePack.
- [Executable placement](executable-placement/README.md): distinguishing changed code from placement effects.
- [Memory characterisation](memory-characterisation/README.md): automatic MLP, prefetch, cache sharing and host diagnostics.
- [CFT commit latency](cft-commit-latency/README.md): durable quorum latency, storage throughput cliffs and live log preparation across AZ placement and standard/Express network choices.
- [Row filter signatures](row-filter-signatures/README.md): per-row Boolean evidence and progressive refinement.
- [Aggregate maintenance](aggregate-maintenance/README.md): descendant summaries without excessive mutation cost.
- [Trie remapping](trie-remapping/README.md): natural-key tries with difficult suffixes remapped to physical positions.
- [Regexp lowering](regexp-lowering/README.md): exact LIKE rewrites and necessary-condition prefilters.

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
