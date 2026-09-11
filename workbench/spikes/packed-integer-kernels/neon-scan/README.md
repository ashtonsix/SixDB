# NEON Scan4/Scan6 loop and bound-interface diagnostic

This diagnostic separates the remaining narrow ScanPack comparison into its
physical kernel, array-loop grain and real bound interface. Production headers
are unchanged by the experiment. Raw native arms use the current header with
the accepted Local3–7 coalescing and Local8/u64 byte-gather changes. The bound
control links the frozen library identified below, which predates those changes;
neither change affects Scan4/6.

The earlier shared-buffer V2 coalescing diagnostic already narrowed the issue:

| Width / operation | Native tile loop | Native256 | Predecessor256 |
|---|---:|---:|---:|
| Scan4 encode | .013566 | .010613 | .010611 |
| Scan4 decode | .017005 | .015795 | .015806 |
| Scan6 encode | .018400 | .017098 | .017077 |
| Scan6 decode | .019098 | .019881 | .022494 |

Numbers are median CPU ns/value at 8,192 u8 values, Neoverse V2, Clang 21.1.8
O3, pinned CPU0, 50ms ×5. In this shared fixture, Scan4's raw decode loss is
7.6%, not the original public cross-fixture 40.3%. The latter is not evidence of
a 40.3% leaf-kernel loss. Scan6 decode is already faster than its predecessor,
and increasing its loop grain regresses this measured case.

The retained V2 assembly has identical instructions for native256 versus
predecessor256 in Scan4 encode/decode and Scan6 encode. Scan6 native256 decode
uses 49 loop instructions per 256 values versus the predecessor's 57. Scan4's
native loop uses 8 encode / 10 decode instructions per 64 values; the grouped
loops use 27 / 35 per 256. No hot loop has helper calls or stack accesses.
These counts explain the experiment's direction; they are not cycle predictions.

The previous diagnostic used a single 8,192-value call site, which Clang
propagated into its noinline functions. This diagnostic explicitly keeps the
count runtime-visible and varies three extents.

## Arms and contracts

`scan_grain/` registers 96 cases: widths 4 and 6, encode/decode, three array
lengths (256, 8,192 and 65,536), and the eight arms below. All arms for one width
and length borrow the same 64-byte-aligned source, encoded and output buffers.
Page offsets and actual source/encoded footprints are emitted as counters.

| Arm | Work |
|---|---|
| bound | Actual bound encoder or whole-array bound reader |
| native | Current native dense entry with a runtime tile count |
| region128 / region256 / region512 | Current native entry on the named region; exact remaining complete tiles |
| predecessor_tile | Direct immediate ScanPack at one physical tile: 64 values for Scan4, 128 for Scan6 |
| predecessor128 / predecessor256 | Direct immediate ScanPack at the named fixed region |

For Scan6, predecessor_tile and predecessor128 deliberately name the same
function. This provides a repeated identical-code reference. All raw arms use
one opaque function-pointer call per array; the bound arm calls its actual
interface directly. Array counts in raw kernels pass through an empty register
constraint so constant propagation cannot replace the runtime count with one
benchmark length. Outer-loop unrolling is disabled; inner fixed-shape work is
available to the ordinary optimizer.

Input values are valid u8 values for timing. The independent oracle derives the
four-bit low/high nibble map and the six-bit shared middle stripe directly. The
separate checker also tests low-bit projection from every unsigned carrier,
source-bit bases, exact allocations, offsets and both guard-page ends. It does
not infer correctness from a native round trip.

The relocated sources compile with the pinned flags above. ASan/UBSan passes
95,232 independent wire, projection, exact, offset and guard checks across all
four carriers and tile counts 0–9, including every remainder of the named
regions. The assembly retains runtime count handling in each raw array arm.

## Compile and run with a retained benchmark build

Use the existing pinned Linux Clang 21.1.8 toolchain and Google Benchmark build.
The production library for the bound arm must be the retained stable baseline;
record its hash. Do not silently compare a newly changed bound-range driver
with old results. The raw native header, prior headers, library and compiler
commands belong in the source/binary receipt.

```sh
seriespack_build=build/clang/seriespack-perf
seriespack_diag=workbench/spikes/packed-integer-kernels/neon-scan
seriespack_out=build/seriespack-neon-scan-grain
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

"$seriespack_out/grain-bench" --benchmark_filter='^scan_grain/' \
  --benchmark_min_time=0.05s --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=false \
  --benchmark_out="$seriespack_out/samples.json" --benchmark_out_format=json

clang++-21 -std=c++23 -O1 -g -march=armv8-a -mtune=neoverse-v2 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iikea/include \
  "$seriespack_diag/check.cpp" -o "$seriespack_out/check"
"$seriespack_out/check"
```

The existing benchmark main supplies CPU pinning. The three explicit lengths
do not use its resident-size parameter; the emitted footprints are authoritative
for this diagnostic and do not assert cache residency.
