# Editors

Open the Linux checkout through VS Code Remote SSH (`orb` in this workspace).
The repository recommends clangd and the Python extensions. Install clangd 21
alongside the [pinned compiler](../../BUILDING.md). Workspace settings select
`clangd-21`, prevent duplicate C/C++ IntelliSense, and keep CMake Tools from
reconfiguring the deliberately opt-in spike selection. Build outputs stay out
of search, file watching, and Python workspace analysis.

## Compilation commands

Run `python3 workbench/tools/dev.py --add NAME` when starting a spike, or run
`python3 workbench/tools/dev.py` after changing its CMake sources or dependencies.
The VS Code task **SixDB: refresh editor configuration** runs the latter.
Only selected spikes are configured; neither command builds them. New TUs need
to be listed in their CMake target to get its exact includes and definitions.
Standalone prototypes still get C++23 from `.clangd`, but inferred flags cannot
substitute for target-specific build configuration.

From macOS, prefix Linux commands with `orb -m ubuntu`. The dev helper maps a
home-directory bind-mount alias back to the native Linux home by filesystem
identity, so launching it through the macOS mount does not change source paths
in the editor's compilation database. Existing selections and cache settings
are preserved.

Configure optional dependencies through ordinary CMake cache variables, then
refresh as usual. For example, the Ikea prior comparison needs its prepared,
pinned headers (the existing helper reuses cached bytes):

```sh
prior=$(python3 workbench/spikes/ikea-blocks/prepare_prior.py)
cmake --preset dev -DIKEA_PRIOR_DIR="$prior"
python3 workbench/tools/dev.py --add ikea-blocks
```

Keep ISA/optimization experiment variants in their separate build directories;
the stable dev database describes local development.

## Architecture-specific headers

`.clangd` recognizes `*_avx2.h`, `*_avx512.h`, and `*_neon.h` anywhere in the
repository, also with `.hpp` extensions. These names select x86-64-v3, Zen 5,
and ARMv8-A editor targets respectively. This lets clangd parse and complete
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
