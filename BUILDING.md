# Building SixDB

Use CMake 3.24+, Ninja, and Clang **21.1.8** (the initial pinned compiler).
From the SixDB directory on Linux:

```sh
cmake --preset dev
cmake --build --preset dev
```

`dev` uses C++23 and `-O2 -g`; `release` uses `-O3 -g`. Both keep assertions
enabled. Builds live under `build/clang/<preset>/`. Clang is selected before
configuration and its version is checked; `-DCMAKE_CXX_COMPILER=...` can select
another installation of that version. Use a fresh directory when changing
compiler versions or target platforms. The initial pin uses an installed
compiler; it does not yet pin the standard library, linker, or worker image.

Targets use ordinary CMake plus `sixdb_target(name)` for project settings.
Each `.cpp` compiles independently; reusable implementation goes in compiled
libraries. [Prototype builds](workbench/prototypes/README.md) are opt-in and
can build just one executable or object. There are no database targets yet.

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

This workspace is exposed to macOS through OrbStack. Run the Linux toolchain
from macOS with `orb -m ubuntu cmake --preset dev` and
`orb -m ubuntu cmake --build --preset dev` in this directory.
