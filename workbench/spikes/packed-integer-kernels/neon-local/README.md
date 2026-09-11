# NEON local coalescing and loop grain diagnostic

This isolates two questions raised by the retained V2 comparison with direct
LocalPack/ScanPack: whether Local3..7 needs coalesced memory operations across
adjacent tiles, and whether fixed 256-value regions help the smaller ScanPack
shapes. It does not change production headers.

All arms use the same per-width 64-byte-aligned source, wire and output arrays,
one opaque array-function call, and 8,192 unsigned byte values. Independent
wire verification happens outside timing. The module registers `grain/` cases
with the existing Google Benchmark application, whose main pins execution.

| Arm | Work inside each array iteration |
|---|---|
| `native` | Current native dense entry, with the count supplied at runtime |
| `pairs64` | Four current 16-value pairs; no memory coalescing |
| `coalesced64` | Four native transposes, with one coalesced 64-value wire region |
| `coalesced256` | Four coalesced 64-value regions |
| `predecessor64` | Direct LocalPack/ScanPack instantiated for 64 values |
| `predecessor256` | Direct predecessor instantiated for 256 values |
| `native256` | Current striped dense entry, with a fixed 256-value region |

`coalescing.h` derives byte-table selections from the Local wire map. A region
admits exactly 64 source values and `8*W` wire bytes. `verify.h` checks every
single source bit (including deliberately unrecorded high bits), random values,
exact allocations, all byte offsets, write canaries, and both guard-page ends.
`check.cpp` runs those checks independently of the benchmark.

## Reuse a completed SeriesPack benchmark build

Run from the repository root on Linux. Set `seriespack_build` to the CMake build
that already contains `ikea_seriespack_bench` and its object files. These flags
match the retained V2 NEON target: Clang 21.1.8, O3, baseline Armv8-A instructions,
and Neoverse V2 scheduling. Keep the target flags consistent with that build.

```sh
seriespack_build=build/clang/seriespack-perf
seriespack_diag=workbench/spikes/ikea-composition/validation/diagnostics/neon_coalescing
seriespack_out=build/seriespack-neon-coalescing
mkdir -p "$seriespack_out"

clang++-21 -std=c++23 -O3 -g0 -march=armv8-a -mtune=neoverse-v2 \
  -Iikea/include -I"$seriespack_build/_deps/googlebenchmark-src/include" \
  -c "$seriespack_diag/grain_bench.cpp" -o "$seriespack_out/grain_bench.o"

clang++-21 -O3 -Wl,--gc-sections "$seriespack_out/grain_bench.o" \
  "$seriespack_build"/workbench/spikes/ikea-composition/validation/CMakeFiles/ikea_seriespack_bench.dir/*.cpp.o \
  "$seriespack_build/ikea/libikea_seriespack.a" \
  "$seriespack_build/_deps/googlebenchmark-build/src/libbenchmark.a" \
  "$seriespack_build/workbench/spikes/ikea-composition/validation/libikea_seriespack_prior.a" \
  -lpthread -lrt -o "$seriespack_out/grain-bench"

"$seriespack_out/grain-bench" --benchmark_filter='^grain/' \
  --benchmark_min_time=0.05s --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=false \
  --benchmark_out="$seriespack_out/samples.json" --benchmark_out_format=json

clang++-21 -std=c++23 -O1 -g -march=armv8-a -mtune=neoverse-v2 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iikea/include \
  "$seriespack_diag/check.cpp" -o "$seriespack_out/check"
"$seriespack_out/check"
```

The emitted `items_per_second` uses actual logical values. Divide median
`cpu_time` by 8,192 for ns/value. Retain compiler commands, source hashes, binary
hashes and raw samples with the hardware result. Compile/inspect the diagnostic
TU with `-S` when attributing a change to load/store coalescing or loop shape;
a static instruction count must account for the loop's value count.

## Initial local screen

Apple host through OrbStack, the flags above, pinned CPU0, 30ms ×3 sequential
repetitions. This is a hypothesis screen, not a V2 result. All normal and
ASan/UBSan checks pass.

| Local width | Native encode | Coalesced64 encode | Native decode | Coalesced64 decode |
|---|---:|---:|---:|---:|
| 3 | .10741 | .09898 | .10772 | .10068 |
| 4 | .10407 | .10065 | .10421 | .10364 |
| 5 | .10970 | .10441 | .11203 | .10311 |
| 6 | .11034 | .09938 | .11130 | .10231 |
| 7 | .11417 | .10097 | .11638 | .10049 |

Numbers are median CPU ns/value. Grouping existing pairs into 64-value regions
was essentially unchanged. Coalescing reached the direct predecessor's level;
unrolling four coalesced regions usually added only about 1%. Scan4 native
versus predecessor decode already matched on the shared buffers locally
(.01308/.01312); that requires hardware confirmation before changing the kernel.
