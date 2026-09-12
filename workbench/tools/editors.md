# Editors

Open the Linux checkout through VS Code Remote SSH (`orb` here). Install
clangd 21 alongside the [pinned compiler](../../BUILDING.md) and the recommended
Python extensions. Repository settings select `clangd-21`, disable duplicate
C/C++ IntelliSense and automatic CMake reconfiguration, and exclude build output
from search and Python analysis.

## Compilation commands

```sh
python3 workbench/tools/dev.py --add NAME           # activate a spike
python3 workbench/tools/dev.py --add-benchmark NAME # activate a recurring suite
python3 workbench/tools/dev.py                     # refresh after CMake changes
```

Run on Linux, or prefix with `orb -m ubuntu` from the Mac. Selections and cache
settings survive refresh; `--remove NAME`, `--remove-benchmark NAME` and `--list`
manage them. The VS Code task **SixDB: refresh editor configuration** also refreshes.
Configuration does not build targets, though it may fetch declared dependencies.

clangd reads `build/clang/dev/compile_commands.json`. Ikea headers use a generated
Ikea-only subset to avoid borrowing unrelated flags from similarly named spike
files. Add TUs to their CMake target and configure optional dependencies as the
study describes. `.clangd` supplies C++23 to standalone prototypes; target-specific
includes and definitions still need a compilation command. Experiment builds
and ISA/sanitizer settings stay in their own directories.

## Architecture-specific headers

`.clangd` supplies ISA overrides for `*_avx2.h`, `*_avx512.h`, `*_x86.h`,
`*_neon.h` (also `.hpp`) and `native/{avx2,avx512,neon}/` headers. Normal TUs keep
CMake's flags. These editor settings do not change builds.

Cross-architecture parsing also needs target C++/libc headers: Ubuntu's
`g++-x86-64-linux-gnu` on ARM, or `g++-aarch64-linux-gnu` on x86. See clangd's
[system-header guidance](https://clangd.llvm.org/guides/system-headers).
Use **clangd: Restart language server** if diagnostics persist after a `.clangd` edit.

## Diagnostics

`python3 workbench/tools/check_ide.py FILE...` checks parser diagnostics and writes
logs under `build/ide-check/`. With no paths, it screens all Git-visible C++ files;
the VS Code task **SixDB: check C++ editor diagnostics** does the same. Optional
dependencies and intentionally failing probes can produce expected diagnostics.
This does not execute code or check every template/conditional branch.

`pyrightconfig.json` provides Workbench imports and basic Linux type checking.
Select a Linux Python interpreter with the study's dependencies. With Pyright
installed, `pyright --pythonpath /usr/bin/python3` uses the same configuration.
