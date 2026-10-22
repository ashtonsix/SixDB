# Building SixDB

Use CMake 3.24+, Ninja, and Clang **21.1.8** (the initial pinned compiler).
From the SixDB directory on Linux (prefix commands with `orb -m ubuntu` from
the macOS/OrbStack workspace):

```sh
cmake --preset dev
cmake --build --preset dev
```

`dev` uses C++23 and `-O2 -gline-tables-only`; `release` uses `-O3 -g`. Both
keep assertions enabled. Line tables keep stack traces and profiler attribution;
configure with `-DSIXDB_DEBUG_INFO=full` when a debugger needs variables and
types (full DWARF roughly doubles compile memory and takes 1.6x as long to
build). The heaviest Ikea checks compile in a Ninja pool sized from host memory,
so the default `-j` does not swap or OOM; `-DSIXDB_HEAVY_COMPILE_JOBS=N`
overrides it. Builds live under `build/clang/<preset>/`. Clang is selected
before configuration and its version is checked; `-DCMAKE_CXX_COMPILER=...` can
select another installation of that version. Use a fresh directory when changing
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

[Local storage maintenance](workbench/tools/storage.md#local-builds-captures-and-other-ignored-output)
budgets ordinary `build/` output as well as worker results. Old idle CMake objects
can be rebuilt after expiry; use `.keep-local` for incremental work that must stay.
Pinned and unique output remains visible in accounting. Scheduled maintenance
is disabled on this device; the storage guide covers explicit pruning and recovery.

Floating-point settings disable fast-math and implicit contraction.
`SIXDB_MARCH` selects the ISA; `SIXDB_TUNE` independently selects `generic`,
`granite-rapids`, `zen4`, `zen5`, `neoverse-v2`, or `neoverse-v3`. See [tuning flags](workbench/design/tuning.md)
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
The [old Orbital drivers](workbench/benchmarks/retired-drivers.md) are retired.
Use their original captured sources for historical reproduction.

Concrete Orbital materials instantiate cold operations once through public
opt-in headers; see [supplying material](orbital/docs/material.md#compile-once).
Ordinary read TUs omit those cold definitions.
The [Object integration checks](workbench/spikes/orbital-mvcc/README.md) are
selected with `-DSIXDB_SPIKES=orbital-mvcc`. Build
`orbital_mvcc_metadata_validate` for the retained production consequence,
codec/publication, Loom and pack-growth checks. They do not need LMDB or the
rejected baseline. For new peer comparisons,
[native LMDB discovery](workbench/peers/README.md) is independent of those fixtures.

Build the `ikea_validate` target to build and run Ikea's behavior checks and
examples. They are registered with CTest: `ctest --test-dir BUILD -N` lists them;
after building, `ctest --test-dir BUILD -R NAME --output-on-failure` reruns a
selection. The default build does not build tests. Width-matrix checks default to
`SIXDB_TEST_SCOPE=fast`, which compiles 33 of the 64 SeriesPack widths (every
narrow and striped width and each side of every byte boundary). Configure with
`-DSIXDB_TEST_SCOPE=full` before landing changes to width-specialized code.

Run `python3 workbench/tools/check.py build` to check incremental compilation
in a disposable fixture. `check.py --list` discovers other helper suites;
`check.py all` runs them all, reporting failures without stopping at the first.
For expensive TUs, use the optional [compile probe](workbench/tools/compilation.md)
for isolated timing, memory and Clang traces.
