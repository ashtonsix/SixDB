# NEON byte gather loop grain

This separate diagnostic investigates the retained V2 Local8/H0/u64 encode
loss to Calico. It keeps the existing eight-value TBL4 gather and varies only
its enclosing loop grain and whether pairs of gathered low halves are joined
for wider stores. It does not change production or the adjacent coalescing
diagnostic, and it does not repeat the previously tested UZP narrowing path.

`byte_grain/` cases cover 256, 8,192 and 65,536 values. For each count every arm
uses identical source values, source addresses and output addresses. All arrays
are aligned to 64 bytes; page offsets and exact footprints are recorded. The
source contains valid eight-bit values in u64 carriers. The scalar oracle is
one byte per original source value.

| Arm | Execution |
|---|---|
| `bound_local8` | Actual bound Local8/H0 encoder |
| `bound_head8` | Actual bound K8/H8 all-head encoder, same wire bytes |
| `native8` | Current native eight-value payload loop |
| `gather32_d`, `gather64_d`, `gather256_d` | Existing gathers grouped in the named region; eight-byte stores expressed separately |
| `gather32_q`, `gather64_q`, `gather256_q` | Same gathers, joining each pair of low halves for a 16-byte store |
| `calico` | Existing compiled Calico width-8 control |

The raw arms and Calico make one opaque function-pointer call per array. Bound
arms directly invoke the real bound encoder, without an additional forwarding
call inserted by this diagnostic. Their distinct boundary is intentional.
Checks happen before/after timing. The ordinary benchmark main supplies CPU
pinning; `--benchmark_enable_random_interleaving=false` keeps repetitions
sequential. These explicit counts are independent of the main application's
resident-size parameter; use the emitted source/encoded byte counters.

## Compile against a completed benchmark build

Run from the repository root on Linux, with Clang 21.1.8. Set the build path to
the retained NEON CMake build. Its production and Calico libraries must use the
same O3 / Armv8-A / Neoverse V2 settings as the diagnostic so the bound/raw
comparison does not silently become a compiler-settings comparison.

```sh
seriespack_build=build/clang/seriespack-perf
seriespack_diag=workbench/spikes/packed-integer-kernels/neon-byte
seriespack_out=build/seriespack-neon-byte-grain
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

"$seriespack_out/grain-bench" --benchmark_filter='^byte_grain/' \
  --benchmark_min_time=0.05s --benchmark_repetitions=5 \
  --benchmark_enable_random_interleaving=false \
  --benchmark_out="$seriespack_out/samples.json" --benchmark_out_format=json

clang++-21 -std=c++23 -O1 -g -march=armv8-a -mtune=neoverse-v2 \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iikea/include \
  "$seriespack_diag/check.cpp" -o "$seriespack_out/check"
"$seriespack_out/check"
```

The independent checker exercises low-bit projection from arbitrary high bits,
exact input/output allocations, all output byte offsets, write canaries,
guarded region starts/ends and empty arrays: 8,260 checks across six variants.

## Source and compiled-code hypothesis

The retained V2 trusted binary's Local8/u64 loop at `0x2503a8` processes eight
values with two LDP, SUBS, TBL4, STR D and branch: 192 instructions per 256
values. Calico's loop at `0x2b9f40` performs the same 32 TBL4 gathers over 256
values, combines low halves with 16 MOV and writes eight STP Q: 156 instructions
per 256 values. Neither original hot loop spills or calls helpers. Calico's
adapter exposes the fixed shape through its recorded unroll settings.

That is a loop/store hypothesis, not a claim that instruction counts predict
timing. The diagnostic disables further outer-loop unrolling so each arm's
named grain is inspectable. Clang may still combine adjacent stores; the
`_d`/`_q` suffix describes authoring, not a promised final opcode.

## Initial local screen

Apple host through OrbStack, pinned CPU0, O3/Neoverse V2 scheduling, 30ms ×3.
The bound and Calico paths link the retained V2 libraries, while the diagnostic
TU uses matching target/compiler flags. These are local observations, not V2
results. Every benchmark arm passes its independent scalar oracle; ASan/UBSan
passes all 8,260 projection/boundary cases.

| Arm | 256 values | 8,192 values | 65,536 values |
|---|---:|---:|---:|
| bound Local8 | .07174 | .05456 | .09470 |
| bound all-head8 | .07239 | .06204 | .09753 |
| native8 | .06349 | .05789 | .10048 |
| gather32 D | .06038 | .05414 | .09887 |
| gather32 Q | .06203 | .05344 | .09393 |
| gather64 D | .05947 | .05378 | .10358 |
| gather64 Q | .05955 | .05441 | .09534 |
| gather256 D | .06230 | .05730 | .11118 |
| gather256 Q | .06386 | .05704 | .10649 |
| Calico | .05853 | .05390 | .10630 |

Numbers are median CPU ns/value. This local screen shows only modest gains for
32/64-value regions, so hardware confirmation is needed. Both 256-value candidate
forms incur 96 bytes of vector data spills within the loop; 32/64-value forms
have no stack accesses or helper calls. Those are distinct from the large
forms' one-time callee-saved register stores.
