# Building an investigation

[Investigation catalog](README.md) · [Research map](../notebook/README.md)


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
