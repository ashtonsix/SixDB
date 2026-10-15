# Concurrent baseline adapter receipt

This retains the local support check for pinned Boost concurrent flat containers
and GTL parallel flat containers with actual std::mutex protection. No Ikea
candidate is measured here; these samples establish neither a winner nor scaling.

The 96 Release cases cover two libraries, all eight key widths, set/map/multimap,
and ordinary/exact-width keys. Four writers grow from empty with reads overlapping
insertion and bag growth. Independent std::map/vector results check full contents,
protected and quiescent consumption, deletion phases, contested last-value erase,
endpoint keys, modular map sums, duplicate merge and consumed-source reuse.
Twelve K40 cases also pass ASan/UBSan with leak detection.

`tiny.csv` retains one two-worker complete ingestion/consume/destruction sample
per control, with 32768 operations. Inputs and thread creation are outside timing;
growth and ingestion barriers are inside. Consumption visits every stored value.
No affinity or controlled hardware isolation was used. All four library/encoding
controls agree on each of the 24 key-width/container-kind digests. Owner bytes are
sizeof the ordinary container, not total allocations, allocator overhead or RSS.
No shared accounting allocator is present. GTL has an eight-byte portable group
on this AArch64 build; this is not a separate NEON SIMD reference.

GTL quiescent consumption can omit locks after all writers stop. Boost retains
cvisit; conversion to ordinary unordered_flat_map/set is a separate, unmeasured
ownership operation. Timed smoke rows use protected consumption for both. MM
values use ordinary absl::InlinedVector<uint64_t,1>; actual capacity can exceed 1.
Concurrent map aggregation is unsigned modular sum. Overwrites/deletes use ordered
phases; arbitrary last-writer equivalence to private worker merges is not claimed.

The full recovery bundle includes Release/ASan binaries, compressed disassembly,
actual compiler/link commands, logs and the source subset. Dependency archives are
reacquired using captured immutable revision URLs and SHA-256 values. A portable
source/fixture CMake project is supplied for rebuilding; that relocated fixture
has not itself been rebuilt, while the original equivalent fixture passed CMake,
Release and CTest. System/toolchain packages are recorded, not bundled.

After recovery, the retained Release binary accepts `check` or `bench REPS WORKERS`.
Rebuild with Clang 21.1.8: configure source/fixture into a new build directory using
Release, then build hashtable_design_concurrent_baselines_check with -j1. The root
spike integrates the CMake include only when its concurrent-baseline option is on.
