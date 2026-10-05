# Durable object views

[Orbital research map: Object](../../notebook/orbital/reference/README.md#object)

The [embedded views spike](../orbital-views/README.md) follows Ashton's October 5
notes and develops the current operation picture. This study retains the earlier
composed caller and lifetime comparisons.

How should an object operation obtain its state, expose ordinary memory and
publish changes while preserving readers and outstanding I/O? The
[composition brief](BRIEF.md) compares private images, shared backing with private
changes, and exclusive reuse backed by reconstruction. It develops Orbital's
provisional [object/view design](../../notebook/orbital/reference/PHYSICAL.md).

The [composed caller](composition_fixture.h) filters selected record bodies and
publishes an output and summary. [Findings](FINDINGS.md) explain the lifetime
boundary and compare all three strategies, including held and late old readers.
The experiment uses actual mappings, byte stores, file reads and socket sends,
with supplied snapshot outcomes. It is not a durable commit implementation.

## Prototype and checks

- [composition.h](composition.h) and [composition.cpp](composition.cpp) implement
  independently retained state, prepared views, physical uses and working changes.
- [composition_probe.cpp](composition_probe.cpp) checks byte results, concurrent
  publication, delayed borrowers, protection, stale acquisition and injected failures.
- [composition_bench.cpp](composition_bench.cpp) compares prepared operation costs
  with and without a bounded page cache. [compose.sh](compose.sh) runs the Zen5
  comparison; [composition-summary.py](composition-summary.py) checks exact work
  counters, independent checksums and coverage before summarising timings.

The existing native probes below test individual Linux mechanisms. The earlier
design study and Python toys are [retired](../../notebook/retired-spikes.md);
these checks retain their narrow scope and evidence.

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
cmake --build build/orbital-objects --target orbital_objects_composition_probe \
  orbital_objects_composition_bench orbital_objects_capabilities orbital_objects_views
ctest --test-dir build/orbital-objects -R orbital_objects_composition_probe --output-on-failure
build/orbital-objects/workbench/spikes/orbital-objects/orbital_objects_capabilities
build/orbital-objects/workbench/spikes/orbital-objects/orbital_objects_views
```

Select the architecture flags from [BUILDING.md](../../../BUILDING.md); the local
AArch64 run uses `SIXDB_MARCH=armv8.2-a` and `SIXDB_TUNE=generic`.
