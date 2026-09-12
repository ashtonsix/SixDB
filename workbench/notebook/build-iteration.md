# Calico build lessons

Surveyed 2026-09-07 to inform SixDB's initial scaffold. Historical observations,
not new measurements or current build instructions; use [BUILDING.md](../../BUILDING.md)
for those.

Calico's [CMake graph](../../../calico/CMakeLists.txt) delegates dependency tracking
to Ninja. Its [compiled libraries](../../../calico/cmake/Libraries.cmake) share
implementation across consumers with matching settings. SixDB carried this model
forward: one object per TU and compiled libraries, without another scheduler or
Calico's legacy Make wrappers and compatibility paths.

## Header and optimization costs

At inspection, Engine had 45,681 header lines and no `src/*.cpp`; Foyer had
17,997 header lines and 522 source lines. These describe source shape, not
attributed compilation time. Header implementation changes invalidate consumers.

The [August Foyer survey](../../../calico/workbench/science/systems/foyer-build-shape/BRIEF.md)
recorded one runtime compile at 83.30 seconds with `-O2 -g0`: about 15 seconds
in the front end and 68 in the back end, on its ARM VM with Clang 20.1.8.
These measurements preceded Calico's later CMake migration. They implicated
both parsing and optimization of a broad inline graph; sharing libraries had
already addressed some duplicate work by the September inspection.

The lesson was to isolate shared orchestration and validation while preserving
useful kernel inlining, rather than imposing TU-size limits or outlining every
hot call.

## Sharing versus calling protocol

The [September CPS experiment](../../../calico/workbench/prototypes/bytepack/evidence/cps.md)
compared reusable CPS stages, reusable ordinary-call stages and directly
specialized compositions. At 64 recipes, CPS produced 6,924 text bytes in
0.49 seconds versus 1,003,860 bytes in 20.70 seconds for direct compositions.
Ordinary reusable stages were similarly small and quick. These were exploratory
ARM-VM measurements with Clang 20.1.8, not SixDB or Zen5 results.

Sharing accounted for much of the difference; the calling protocol alone did
not explain it. The experiment also caught a wrapper outlining a helper before
every CPS hop: writing `musttail` did not establish the intended machine code.
This motivated measuring runtime, compilation and code size together. SixDB's
subsequent [Ikea investigation](../spikes/ikea-composition/README.md) owns its
further experiments and decisions.
