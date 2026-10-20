# Exact metadata heap with original trim reserve

[`heap-only-cut.json`](heap-only-cut.json) applies the same
[`heap-control.patch`](heap-control.patch) directly to frozen
[Direct4](../routes/direct-domain/direct-domain4-cut.json). It retains the original
full trim-span reserve. This is separate from the qualified bounded16-plus-heap
cut described in [README.md](README.md).

The Clang 21.1.8 native smoke and requested-byte census pass on the 4 KiB guest.
The smoke covers ordinary zeroed allocation, a retained reader, release and close;
the census reads actual owner and vector capacities.

| Requested bytes per Group | A | B |
|---|---:|---:|
| Group | 1,448 | 1,448 |
| FiniteGroup | 448 | 448 |
| Exact metadata heap | 1,968 | 2,488 |
| Live buffer | 80 | 144 |
| Full trim buffer: 318 / 548 spans | 1,272 | 2,192 |
| Discard buffer | 40 | 72 |
| **Total metadata charge** | **5,256** | **6,792** |
| Payload VA, unchanged | 2,605,056 | 4,489,216 |

Storage remains 4 bytes, and the Domain prefix remains inside Group. Bindings,
grids, page states and scratch keep stable pointers within the exact metadata
heap block. Payload geometry and VM, Group/reply ownership, and full inventory
are unchanged. Requested charge excludes the first 8-byte Pool owner-vector
capacity and allocator overhead. Allocator usable extent is an alternative
observation, not an additional charge; no RSS or timing result follows. The
16 KiB geometry remains modeled and was not natively qualified here.

[`heap-only-native-readiness.json`](heap-only-native-readiness.json) records
source, patch, build-input, binary and log hashes, plus the actual census.
Only the existing four heap-patch files differ from Direct4; independent replay
is byte-identical. No bounded16 or comparison counters enter this cut. Its
qualification is one native smoke and census; the failure-history matrix belongs
to the separate bounded16-plus-heap cut and was not rerun here.

The frozen runtime remains under `build/orbital-mvcc-metadata/heap-only/`.
The small [smoke](https://github.com/ashtonsix/SixDB/blob/c8f9081104ef61ee897fbbaf954223ae0a3d7656/workbench/spikes/orbital-mvcc/metadata/orbital/allocation/heap-control/probes/smoke.cpp), [census](https://github.com/ashtonsix/SixDB/blob/c8f9081104ef61ee897fbbaf954223ae0a3d7656/workbench/spikes/orbital-mvcc/metadata/orbital/allocation/heap-control/probes/census.cpp) and
[CMake recipe](https://github.com/ashtonsix/SixDB/blob/c8f9081104ef61ee897fbbaf954223ae0a3d7656/workbench/spikes/orbital-mvcc/metadata/orbital/allocation/heap-control/CMakeLists.txt) are retained here; they use the existing tracked
NativeFixture. Rebuild the two checks from the repository:

```sh
orb -m ubuntu cmake -S workbench/spikes/orbital-mvcc/metadata/orbital/allocation/heap-control \
  -B build/orbital-mvcc-metadata/heap-only/reproduced -G Ninja \
  -DCMAKE_CXX_COMPILER=clang++-21 \
  -DRUNTIME_SOURCE=/home/ashtonsix/sixdb/build/orbital-mvcc-metadata/heap-only/orbital
orb -m ubuntu cmake --build build/orbital-mvcc-metadata/heap-only/reproduced \
  --target heap_only_smoke heap_only_census -j 2
orb -m ubuntu build/orbital-mvcc-metadata/heap-only/reproduced/heap_only_smoke
orb -m ubuntu build/orbital-mvcc-metadata/heap-only/reproduced/heap_only_census
```

To replay the runtime patch into a fresh sibling tree on Linux, copy frozen
`routes/direct-domain4/orbital`, then apply the same patch with an explicit target:

```sh
orb -m ubuntu mkdir -p build/orbital-mvcc-metadata/heap-only-fresh
orb -m ubuntu cp -a build/orbital-mvcc-metadata/routes/direct-domain4/orbital \
  build/orbital-mvcc-metadata/heap-only-fresh/orbital
orb -m ubuntu patch -p1 -d build/orbital-mvcc-metadata/heap-only-fresh \
  -i /home/ashtonsix/sixdb/workbench/spikes/orbital-mvcc/metadata/orbital/allocation/heap-control/heap-control.patch
```

The cut records all 13 runtime-input hashes for checking the replay. An initial
`git apply` within the ignored repository subtree skipped paths without an error;
the census caught the unchanged mmap costs. The explicit-target application,
changed hashes and rebuilt binaries establish the passing receipt above.
