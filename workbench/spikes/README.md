# Spikes

A stable home for an investigation: its question, candidate designs, reading,
code, measurements, and findings belong together. Name the directory after the
question, so a different candidate answer does not require moving it.

A spike can begin with one README. Add files or subdirectories when they help;
there is no required template. Several implementations or measurement campaigns
can share the same home, which remains useful after adoption or abandonment.
Loose thoughts can stay in the [notebook](../notebook/ideas.md) indefinitely.

- [Heterogeneous bitsets and integer metadata](ikea-heterogeneous/README.md) (closed): actual
  BEC bodies with replaceable packed metadata, dependent addresses and native
  range/decode/consumer composition, whole-window analysis and masked Boolean algebra.
- [Ikea packed integers](ikea-integers/README.md): locality and composition
  exercise with two 1–7-bit tail formats, native kernels, a 12-bit body/tail
  reconstruction, measured 56-bit bodies, and exact placement bounds through 64 bits.
- [Ikea bitset primitives](ikea-blocks/README.md): fresh headless plain/Bec256
  codecs, native-width compute, size-prediction experiments and actual stage
  ABI/graph-lowering probes. Three-target measurements and remaining value-reuse
  limits follow the rejected initial implementation.
- [Ikea composition and ergonomics](ikea-composition/README.md): an open
  design investigation into reusable blocks, containers, transforms and kernels;
  competing pseudocode, adversarial review, interface seams and authoring guides.
- [Row filter signatures](row-filter-signatures/README.md): compact per-row
  evidence for conjunctive and factored Boolean filters, progressive refinement,
  8/16/32-bit planes, and block rollups. The spike concluded with a resident
  study and [findings](row-filter-signatures/FINDINGS.md) on conditional
  rollup/text wins, direct-column controls, Boolean placement, and maintenance
  costs. The production design remains open.
- [Aggregate maintenance](aggregate-maintenance/README.md): maintaining useful
  descendant summaries without excessive mutation cost. The initial spike
  concluded with two count/sum probes; [conclusions](aggregate-maintenance/CONCLUSIONS.md)
  and reproducible evidence are available. The production design remains open.
- [Trie remapping](trie-remapping/README.md): preserve natural-key trie regions
  and pack difficult suffixes into ordered physical positions with growth room.
  The first spike is concluded, with [findings](trie-remapping/FINDINGS.md) on
  density, column movement, gap policy, and partial ordering. The broader design
  question remains open.
- [Regexp lowering](regexp-lowering/README.md): replace regexp predicates with
  exact LIKE expressions, or prefilter with necessary LIKE signatures before
  FSST decoding and RE2 evaluation. The spike is concluded, with
  [provisional recommendations](regexp-lowering/CONCLUSIONS.md) from studies of
  exact coverage, richer constraints, survivor bounds, switching, and factored
  filters with explicit relative order. FSST execution and timing remain open.

## Building a study

Use the parts of the [shared tooling](../tools/README.md) that help the question:
[datasets](../datasets/README.md) can be reused across studies,
[`Run`](../tools/experiment.py) can capture sources and reuse a build workspace,
and [retention](../tools/artifacts.md) can keep selected counters or timing
samples and recover full runs later. The [regexp runner](regexp-lowering/run.py)
combines these; the [aggregate runner](aggregate-maintenance/run.py) shows a
Google Benchmark study. A standalone prototype can start with the build below.

The editor uses one stable development configuration. Activate a study with
`python3 workbench/tools/dev.py --add NAME` (repeat `--add` for several), or
use its runner when that integrates the helper. `--remove NAME` deactivates it;
`--list` shows the selection. Refresh with no arguments after changing target
dependencies. Configuration may fetch declared dependencies; clangd only reads
the resulting flags and never invokes CMake or Ninja. Benchmark builds remain
independent. No spike-specific editor settings are needed.

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

For an executable that needs a distribution artifact, add
`sixdb_release_artifact(example_bench)` and build `example_bench_dist` in a
Release configuration. Stripped binaries go to `dist/bin/`; private debugging
information goes separately to `dist/symbols/` within that build directory.
