# Build iteration: starting from Calico

Inspected 2026-09-07. This investigates build scaffolding; it does not report
new measurements of Calico compilation speed.

## Keep the useful build model

Calico's maintained [CMake graph](../../../calico/CMakeLists.txt) delegates
source, header, and command-line dependency tracking to Ninja. Its
[compiled libraries](../../../calico/cmake/Libraries.cmake) share session,
checkpoint, runtime, and other implementation across consumers with matching
settings. Separate assertion variants account for differing header semantics.
Its [build check](../../../calico/tools/test_build.py) exercises invalidation.

CMake already emits one compile command per source per target. Put reusable
implementation in one compiled library instead of listing its source in every
consuming executable. SixDB needs no additional compilation scheduler.

## Improve the boundaries

**Headers.** Engine currently has 45,681 lines across its `include/*.h` tree
and no `src/*.cpp`; Foyer has 17,997 header lines and 522 source lines. These
are source-shape observations, not compile-time attribution. Implementation
changes in a header invalidate every TU that includes it.

Calico's [August build survey](../../../calico/workbench/science/systems/foyer-build-shape/BRIEF.md)
recorded one runtime compile at 83.30 seconds with `-O2 -g0`, including about
15 seconds in the front end and 68 seconds in the back end, on its stated ARM
VM and Clang 20.1.8. These historical timings precede the latest CMake migration.
The survey found costs from parsing and optimization of a broad inline call
graph. Current library sharing already addresses some earlier duplication.

Keep shared orchestration and validation behind small declarations. Keep
inlining where it serves kernel execution. Arbitrary TU-size limits or calls
inserted inside hot kernels can exchange compile-time savings for runtime cost.

**Combinatorial composition.** The [September CPS experiment](../../../calico/workbench/prototypes/bytepack/evidence/cps.md)
compared reusable CPS stages, reusable ordinary-call stages, and directly
specialized compositions. At 64 recipes its CPS object had 6,924 bytes of text
and compiled in 0.49 seconds; direct compositions had 1,003,860 bytes of text
and compiled in 20.70 seconds. Ordinary reusable stages were similarly small
and quick to compile. These are exploratory ARM-VM results with Clang 20.1.8,
not SixDB results or a Zen5 performance claim.

The evidence separates sharing from calling protocol. For SixDB, reuse stage
implementations through CPS, preserve useful inlining inside stages, and measure
selected specializations instead of generating every composition. The exact
ABI and handoff boundary remain to investigate. The Calico experiment also
caught a wrapper that outlined a helper before every CPS hop; merely writing
`musttail` did not establish the intended generated code.

**Prototypes.** Historical prototypes retain independent build recipes. For
example, compact-row-filters compiles each executable directly with Make and
handwritten header prerequisites. That recipe does not automatically track
transitive includes or rebuild when compiler flags change. SixDB's studies
should get Ninja's tracking without configuring unrelated studies or requiring
their dependencies.

**Build declarations.** Calico's wrappers carry legacy Make paths, compatibility
symlinks, and test-specific behavior. SixDB can use ordinary CMake declarations
and one small settings helper. Libraries and dependencies belong near their
owning module or study, rather than in a central implementation inventory.

## Initial support

- One graph and a stable build directory per configuration.
- `SIXDB_PROTOTYPES` selects which studies to configure. Their targets are
  excluded from `all`; build one requested target and its dependencies.
- One object per TU, with compiled libraries reused within the configuration.
  Ninja object targets support compile-only iteration before linking.
- No default unity build or LTO. `SIXDB_TIME_TRACE` emits Clang time traces to
  investigate parsing, instantiation, and optimization costs.
- Compiler caching can use normal
  `-DCMAKE_CXX_COMPILER_LAUNCHER=ccache` when installed. A cache does not remove
  header coupling on a miss.
- Explicit release packaging strips executables while retaining separate debug
  information. Visibility and section garbage collection reduce exposed names
  and unreferenced code; no bespoke obfuscator is introduced.

The [build check](../tools/check_build.py) uses a disposable multi-TU fixture to
check selection, shared objects, compile-only targets, source/header/flag
invalidation, and symbol stripping. This checks scaffold behavior, not
iteration speed on a substantive SixDB implementation.
