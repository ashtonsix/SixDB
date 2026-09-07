# Spikes

A stable home for an investigation: its question, candidate designs, reading,
code, measurements, and findings belong together. Name the directory after the
question, so a different candidate answer does not require moving it.

A spike can begin with one README. Add files or subdirectories when they help;
there is no required template. Several implementations or measurement campaigns
can share the same home, which remains useful after adoption or abandonment.
Loose thoughts can stay in the [notebook](../notebook/ideas.md) indefinitely.

- [Aggregate maintenance](aggregate-maintenance/README.md): maintaining useful
  descendant summaries without excessive mutation cost. The initial spike
  concluded with two count/sum probes; [conclusions](aggregate-maintenance/CONCLUSIONS.md)
  and reproducible evidence are available. The production design remains open.

## Building a study

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
