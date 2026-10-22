# Editors

Open the Linux checkout through VS Code Remote SSH (`orb` here). Install
clangd 21 alongside the [pinned compiler](../../BUILDING.md) and the recommended
Python extensions. Repository settings select `clangd-21`, disable duplicate
C/C++ IntelliSense and automatic CMake reconfiguration, and exclude build output
from search and Python analysis.

## Automatic compiler configuration

The Linux user watcher notices newly added `workbench/spikes/*/CMakeLists.txt`
and activates those spikes for clangd. It also refreshes on CMake edits and
removes renamed/deleted selections. It configures metadata only; it never builds
targets or changes experiment configurations. The watcher is installed on this
device. On another checkout, install it once:

```sh
python3 workbench/tools/dev.py --install-watch
```

It runs as a per-checkout systemd user service, including after the next login.
Edits settle for one polling interval before configuration. An incomplete new
target keeps the previous working commands, retries quietly, and cannot block
another valid new spike. The latest failure is in
`build/clang/dev/.editor-watch-error.log`. Historical tracked spikes remain
opt-in; existing untracked spikes are discovered on first installation too.
Add source files to their CMake target so its includes and definitions reach
clangd. The repository enables vscode-clangd's automatic config reload; the
installer applies the same settings to an existing Remote SSH installation for
multi-root workspaces. A changed-byte-only root `compile_commands.json` mirror
triggers that extension's watcher, so already-open files reload too. No manual
language-server restart is needed after setup. If installing into an already
open VS Code session, restart clangd once to enable its extension watcher.

## Manual selection and diagnostics

```sh
python3 workbench/tools/dev.py --add NAME           # activate a spike
python3 workbench/tools/dev.py --add-benchmark NAME # activate a recurring suite
python3 workbench/tools/dev.py                     # refresh active targets now
python3 workbench/tools/dev.py --sync              # run automatic discovery once
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

## Python analysis scope

`pyrightconfig.json` provides Workbench imports and basic Linux type checking.
It excludes build output, dependency trees and captured evidence from background
discovery. Authored tools and spike scripts remain included. Select a Linux
Python interpreter with the study's dependencies; with Pyright installed,
`pyright --pythonpath /usr/bin/python3` uses the same configuration.

In a multi-root workspace, each root has its own analysis configuration. SixDB's
exclusions do not cover sibling checkouts or archives. Workspace-level
`python.analysis.exclude` can exclude generated trees across roots and entire
historical archive roots. **Python: Show Language Server Output** identifies the
root and source count behind a slow-enumeration warning.
[Pylance exclusions](https://github.com/microsoft/pylance-release/blob/main/docs/settings/python_analysis_exclude.md)
still allow analysis of opened files and their imports.
