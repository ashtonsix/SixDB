# Building SixDB

Use CMake 3.24+, Ninja, and Clang **21.1.8** (the initial pinned compiler).
From the SixDB directory on Linux (prefix commands with `orb -m ubuntu` from
the macOS/OrbStack workspace):

```sh
cmake --preset dev
cmake --build --preset dev
```

`dev` uses C++23 and `-O2 -g`; `release` uses `-O3 -g`. Both keep assertions
enabled. Builds live under `build/clang/<preset>/`. Clang is selected before
configuration and its version is checked; `-DCMAKE_CXX_COMPILER=...` can select
another installation of that version. Use a fresh directory when changing
compiler versions or target platforms. The initial pin uses an installed
compiler; standard-library and linker package versions remain unpinned.

Targets use ordinary CMake plus `sixdb_target(name)` for project settings.
Each `.cpp` compiles independently; reusable implementation goes in compiled
libraries. [Spike builds](workbench/spikes/README.md) are opt-in and
can build just one executable or object. [Ikea's examples](ikea/README.md#build-and-run)
exercise the first module; database functionality is still to come.
[Recurring benchmark suites](workbench/benchmarks/README.md) use
`-DSIXDB_BENCHMARKS=NAME`, independently of spike selection.

For automatic editor support, install `python3 workbench/tools/dev.py --install-watch`
once on Linux. New spikes and CMake changes then refresh IntelliSense without
building targets. The [editor guide](workbench/tools/editors.md) covers manual
selection and troubleshooting. [Experiment helpers](workbench/tools/README.md#captured-experiment-runs)
support building captured sources while editing; [workers](workbench/tools/workers.md)
run scripts on EC2.

Floating-point settings disable fast-math and implicit contraction.
`SIXDB_MARCH` selects the ISA; `SIXDB_TUNE` independently selects `generic`,
`granite-rapids`, `zen5`, `neoverse-v2`, or `neoverse-v3`. See [tuning flags](workbench/design/tuning.md)
for compiler options and source-level flags. [Build conventions](workbench/design/conventions.md)
cover other settings and remaining decisions.

For a Linux executable, `sixdb_release_artifact(name)` adds a `name_dist`
target. Build it with the release preset to produce a stripped executable in
`build/clang/release/dist/bin/` and private symbols in `dist/symbols/`.
Packaging requires `llvm-objcopy-21` and `llvm-strip-21`. Project symbols are
hidden by default; future shared-library APIs will need explicit exports.

The [Orbital runtime](orbital/README.md) and [Loom](loom/README.md) implement
Stage 1 allocation, preparation, worker and execution support.
`orbital_validate` and `loom_validate` build and run their checks.
Select `-DSIXDB_BENCHMARKS=orbital` for the
[allocation comparison](workbench/benchmarks/orbital/README.md).

The embedded `orbital::ObjectStore<Physical>` catalog uses the production
Orbital storage library. A concrete material compiles its cold operations once
with `orbital/src/object/store_impl.h`; its ordinary read definitions stay visible.
The [Orbital MVCC study](workbench/spikes/orbital-mvcc/README.md) retains the rejected
baseline and experimental controls. Select `-DSIXDB_SPIKES=orbital-mvcc` independently
of benchmark selection, then build `orbital_mvcc_composed_check` and
`orbital_mvcc_composed_read_fusion_check` to exercise the selected module through
the real TuplePack/SeriesPack driver. `orbital_mvcc_validate` also checks baseline
histories, actual Loom callers, consumers and LMDB. The retained baseline header
is `orbital/mvcc_baseline.h`; it cannot shadow the production `orbital/object.h`.

Build the `ikea_validate` target to build and run Ikea's behavior checks and
examples. They are registered with CTest: `ctest --test-dir BUILD -N` lists them;
after building, `ctest --test-dir BUILD -R NAME --output-on-failure` reruns a
selection. The default build does not build tests.

Run `python3 workbench/tools/check.py build` to check incremental compilation
in a disposable fixture. `check.py --list` discovers other helper suites;
`check.py all` runs them all, reporting failures without stopping at the first.
For expensive TUs, use the optional [compile probe](workbench/tools/compilation.md)
for isolated timing, memory and Clang traces.
