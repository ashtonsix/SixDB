# Editors

Open the Linux checkout through VS Code Remote SSH (`orb` in this workspace).
The repository recommends clangd and the Python extensions. Install clangd 21
alongside the [pinned compiler](../../BUILDING.md). Workspace settings select
`clangd-21`, prevent duplicate C/C++ IntelliSense, and keep CMake Tools from
reconfiguring the explicit study/benchmark selection. Build outputs stay out
of search, file watching, and Python workspace analysis.

## Compilation commands

Run `python3 workbench/tools/dev.py --add NAME` when starting a spike, or run
`python3 workbench/tools/dev.py` after changing its CMake sources or dependencies.
The VS Code task **SixDB: refresh editor configuration** runs the latter.
clangd reads the resulting `build/clang/dev/compile_commands.json`.
For recurring suites use `--add-benchmark NAME` / `--remove-benchmark NAME`.
Both selections are preserved on refresh; configuring does not build targets. New TUs need
to be listed in their CMake target to get its exact includes and definitions.
Standalone prototypes still get C++23 from `.clangd`, but inferred flags cannot
substitute for target-specific build configuration.
Use `--remove NAME` to deactivate a study and `--list` to see active selections.

Some study runners refresh this database as a convenience; others configure
only their captured experiment. The explicit dev command works in either case.
Experiment ISA and sanitizer choices stay in the experiment's build directory.

From macOS, prefix Linux commands with `orb -m ubuntu`; the dev helper resolves
the mount to native Linux paths. Existing selections and cache settings are preserved.

Configure a study's optional dependencies through its documented CMake cache
variables, then refresh the editor configuration. The study's own runner or
build instructions describe any prepared inputs it requires.

## Architecture-specific headers

`.clangd` selects x86-64-v3 for `*_avx2.h`, Zen 5 for `*_avx512.h` and combined
`*_x86.h` headers, and ARMv8-A for `*_neon.h` (also with `.hpp` extensions).
This lets clangd parse and complete
intrinsics even when the header targets the other development architecture.
Normal TUs retain their CMake flags; these editor overrides do not alter builds.
An additional ISA naming convention can be added as a path-scoped fragment.

Clang needs real target C++ and libc headers, not just a target flag. On Ubuntu
ARM64, `g++-x86-64-linux-gnu` supplies the x86 development headers; on x86,
`g++-aarch64-linux-gnu` supplies the ARM headers. Neither needs to replace Clang
as the compiler. See clangd's [target and system-header guidance](https://clangd.llvm.org/guides/system-headers)
and [configuration reference](https://clangd.llvm.org/config).

After editing `.clangd`, use **clangd: Restart language server** if an already
open document retains old diagnostics.

## Screening

The task **SixDB: check C++ editor diagnostics**, or
`python3 workbench/tools/check_ide.py`, checks all Git-visible C++ sources and
headers, including untracked files. Pass file paths for a narrower check. It
uses the repository configuration and writes full logs under `build/ide-check/`.
This checks parser errors; it does not execute code, instantiate every template,
or validate inactive preprocessor branches. Optional dependency sources need
their dependencies configured; intentionally failing probes may report errors.

`pyrightconfig.json` supplies repository-relative Workbench tool imports and
basic Linux type checking to both Pylance and Pyright. VS Code analyzes the
workspace, not just open Python files. Select the Linux Python interpreter that
has the study's dependencies installed. `pyright --pythonpath /usr/bin/python3`
runs the same repository configuration from a CLI with Pyright installed.
