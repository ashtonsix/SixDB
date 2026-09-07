# Prototypes

Experiments and spikes that develop SixDB's design. No experiments or order
of execution have been chosen yet. Recording conventions are pending.

## Building a study

Create a directory such as `workbench/prototypes/example/` and declare its
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
cmake --preset dev -DSIXDB_PROTOTYPES=example
cmake --build --preset dev --target example_bench
```

For several studies, pass `'-DSIXDB_PROTOTYPES=example;another'`. Selection is
cached per build directory; clear it with `-DSIXDB_PROTOTYPES=`. List sources
explicitly. Ninja tracks included headers and compiler options. Editing
`bench.cpp` compiles that TU and relinks; the compiled `kernel.cpp` can be
reused by every executable linked to `example_core`.

For compile-only iteration, request the object target:

```sh
ninja -C build/clang/dev \
  workbench/prototypes/example/CMakeFiles/example_core.dir/kernel.cpp.o
```

Inspect names with `ninja -C build/clang/dev -t targets all`. Normal builds need
no clean step. Use separate build directories for concurrent builds or different
compiler/ISA/optimization settings.

For an executable that needs a distribution artifact, add
`sixdb_release_artifact(example_bench)` and build `example_bench_dist` in a
Release configuration. Stripped binaries go to `dist/bin/`; private debugging
information goes separately to `dist/symbols/` within that build directory.
