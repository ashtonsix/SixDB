# Conversion allocation identity census

The standalone diagnostic and compact result preserve an untimed, native
composition observation at `58456a0`, with Clang 21.1.8 on Linux/aarch64.
Two Objects contain 16384 rows each. The four conversions retain original Series
primaries and repeatedly replace Tuple/Series alternatives without advancing the
logical cut. The original reader stays valid, then closes.

| Phase | Current Part extents | Live capacity | Idle capacity |
| --- | ---: | ---: | ---: |
| Initial Series | 737280 B | 737280 B | 0 B |
| Both Tuple alternatives | 1490944 B | 1490944 B | 0 B |
| First Object back to Series | 1482752 B | 1482752 B | 376832 B |
| Both Series alternatives | 1474560 B | 1720320 B | 376832 B |
| Original reader closed | 1474560 B | 1720320 B | 376832 B |

The exact same Storage and byte address changes from Object0's 376832-byte
Tuple alternative to Object1's 131072-byte Series key Part. Its prepared capacity
remains 376832 bytes: 245760 bytes of slack. Each of the sixteen live Storage
identities maps to one current Part with one use. The two original primaries
and two alternatives legitimately remain after reader release; version count is
two, cut is one. Live plus idle capacity independently equals mapped inventory.
All 376832 independent row comparisons pass; owner destruction leaves zero Arena
charge. The result is neither overlapping accounting nor an old Tuple owner leak.

At the decisive allocation that retired Tuple was the **only idle Window**.
A better-fit search cannot find another candidate in this inventory. Reuse avoids
another mapping; fresh smaller backing plus idle trimming would add mapping,
preparation and retirement work and can refuse under a tight budget. The
observation establishes a capacity/reuse mechanism, not an allocator regression
or a selected policy. No timing was taken. The remote sixteen-Object survey's
15-fold explanation is supported by this local mechanism, without claiming its
addresses were observed.

`release.json` hashes the original probe, ELF, complete census and immutable source
projection. `summary.json` keeps the small phase table and decisive identities;
addresses are local observations. `artifact.json` recovers the full frozen source,
ELF, census, commands and original operating script. That original script depends
on separately verified warm Ikea archives outside its directory; for independent
rebuilding, use the unmodified archived `source/` and a private CMake target:

```cmake
# Append to the frozen source's root CMakeLists.txt in a disposable copy.
add_executable(conversion_capacity_census /absolute/path/to/check.cpp)
sixdb_target(conversion_capacity_census)
target_include_directories(conversion_capacity_census PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(conversion_capacity_census PRIVATE orbital_versioned_performance_composed)
```

Configure that copy with `-DSIXDB_SPIKES=orbital-mvcc`, then build only this target
using the pinned Linux toolchain. The retained census is the original qualified
execution; this alternative fresh-build recipe has not been run.
