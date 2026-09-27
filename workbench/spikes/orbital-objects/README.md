# Linux object mapping probes

These small native probes test mechanisms behind Orbital's provisional
[object/view design](../../../orbital/PHYSICAL.md). The broader design study and
Python toys are [retired](../../notebook/retired-spikes.md); this directory keeps
real Linux checks that the actor simulator cannot replace.

- [capabilities.cpp](capabilities.cpp) verifies a read-only mapping traps on write,
  queries usermode UFFD features and registers a NOP-only restriction on a disabled
  io_uring instance. Feature availability is not exercised fault handling or sandboxing.
- [views.cpp](views.cpp) executes one-page UFFD missing-page supply, minor mapping
  and write-protect COW. It checks both an existing and a late old reader. A fourth
  case destructively reuses an exclusive projection, reconstructs an old view from
  an in-memory recipe, then discards the tentative view on abort. That recipe is
  not durable logging or crash recovery.

Each view case runs in a child with a five-second timeout. Unsupported optional
paths remain explicit. The capability child intentionally faults with core dumps
disabled; these programs change no machine configuration. The retained local
AArch64/OrbStack [evidence](evidence/README.md) establishes no contention, throughput,
race coverage or target-server performance.

From Linux at the repository root, with the pinned toolchain (prefix `orb -m ubuntu`
from the Mac):

```sh
cmake -S . -B build/orbital-objects -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=clang++-21 -DSIXDB_SPIKES=orbital-objects
cmake --build build/orbital-objects --target orbital_objects_capabilities orbital_objects_views
build/orbital-objects/workbench/spikes/orbital-objects/orbital_objects_capabilities
build/orbital-objects/workbench/spikes/orbital-objects/orbital_objects_views
```

A useful continuation would combine preparation/COW/reuse, a late old reader,
partial overwrite, abort and delayed backend use under finite memory, comparing
full preparation, bounded lookahead and demand faults at the same workload.
Slow page sources and blocked workers matter. Extension protection transitions
and allocator hints remain separate native questions; these probes settle neither.
