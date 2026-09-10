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
can build just one executable or object. There are no database targets yet.

For editor support, start with `python3 workbench/tools/dev.py --add NAME`;
see the [editor guide](workbench/tools/editors.md) for compilation-database setup
and troubleshooting. [Experiment helpers](workbench/tools/README.md#captured-experiment-runs)
support building captured sources while editing; [workers](workbench/tools/workers.md)
run scripts on EC2.

Floating-point settings disable fast-math and implicit contraction.
`SIXDB_MARCH` selects the ISA; `SIXDB_TUNE` independently selects `generic`,
`granite-rapids`, `zen5`, or `neoverse-v2`. See [tuning flags](workbench/design/tuning.md)
for compiler options and source-level flags. [Build conventions](workbench/design/conventions.md)
cover other settings and remaining decisions.

For a Linux executable, `sixdb_release_artifact(name)` adds a `name_dist`
target. Build it with the release preset to produce a stripped executable in
`build/clang/release/dist/bin/` and private symbols in `dist/symbols/`.
Packaging requires `llvm-objcopy-21` and `llvm-strip-21`. Project symbols are
hidden by default; future shared-library APIs will need explicit exports.

Run `python3 workbench/tools/check_build.py` to check incremental compilation
in a disposable fixture. `-DSIXDB_TIME_TRACE=ON` enables Clang time traces when
investigating compilation cost.
