# Packed filters

Unsigned equality and ordered comparisons return original-position masks from
SeriesPack and TuplePack, without materializing arrays. The production contract
is [Filtering packed values](../../../ikea/docs/filters.md).
Selected kernels, checks and executable examples live in Ikea; this directory
retains the independent controls and measurements that selected them.

This study compares complete inlined consumers with independent SIMD controls
and evaluates direct comparisons on packed bits. [Results](RESULTS.md) record
absolute dependent latency and independent reciprocal throughput separately.

[Early exits](EARLY-RESULTS.md) extend the study across widths, predicates and
selectivities, including mixed windows where branches lose. Selected head-first
execution is available through composition while ordinary defaults stay unchanged;
[the method](EARLY-EXITS.md) and native evidence separate stopping gains from layout
and fusion gains. [Graduation](EARLY-GRADUATION.md) checks the production consumer.

## Measurement

`native.sh` builds with pinned Clang 21.1.8, explicit SIMD, and both loop and SLP
vectorizers disabled. Zen 5 and Granite Rapids run AVX2 and AVX-512 separately;
Neoverse V2 runs NEON. No timing from the local ARM VM qualifies a platform policy.
The retained Granite cohorts used Zen 5 tuning because their original group
override was misplaced. [Results](RESULTS.md#reading-the-numbers) records the
actual compiler profiles. The corrected `workers.json` applies Granite tuning
to future runs; archived sources and commands reproduce the historical cohorts.

The controls in `baseline.h` do not call Ikea loaders, reconstruction or predicate
bodies. They read the same wire bytes and return the same masks. GFNI-capable
controls use affine bit transposition; baseline AVX2/NEON controls expand planes
with SIMD. Standard-width controls load and compare native lanes directly.
TuplePack controls fuse bit extraction and comparison with a known fixed field;
the bound Ikea operation retains a runtime field description.

Each serial result selects the next input window. Eight independent recurrences
measure reciprocal throughput. The complete mask is multiplied and its high six
bits select the next window, so all positions contribute to the dependency.
Address mixing, query loads and loop control are included on both sides. These
are whole-consumer timings, not isolated instruction latencies. Each case has
five sequential repetitions, pinned to the worker's allowed CPU.

The retained original base-harness results used the same salt for every stream,
allowing their addresses to converge. The current base harness uses distinct
stream salts and warms every stream; the early-exit harness already did so.
The [mask/harness graduation](../filter-composition/MASK-GRADUATION.md) records
the correction and links the refreshed all-width production table. Historical
baseline rows below are not silently relabeled as distinct-stream measurements.

Cases include all five predicates, Local widths 1–8/16/32/64 in 16-value windows,
5-bit and 8-bit TuplePack byte columns, and standard widths in 64-value windows
on NEON/AVX-512. There are 64 input windows plus scalar queries: at most 8 KiB of
payload for 16-value windows and 32 KiB for 64-value windows. These are warmed
small-working-set comparisons, not DRAM or general scan-residence claims. Half
of the 16-value SeriesPack equality queries have known matches; the larger
window control retains uniform queries, so wide equality is mostly missing.
Timed selections are full. Correctness additionally covers sparse activity,
partial windows, tile gaps, stripes, both separated-head widths and substitutions.

`audit.py` rejects unexpected outlined calls in the actual measurement functions.
Review the mask-to-address dependency in the saved disassembly too: this audit
alone does not establish serial dataflow. `codegen.cpp` and `mca.py` retain bounded
witnesses and LLVM-MCA models; modeled cycles are not measured nanoseconds.
Early diagnostic cohorts were rejected after identifying outlined callbacks and
an address recurrence that discarded upper mask bits. Their timings do not select
production behavior. Tests and benchmarks are sharded to fit the small workers.

## Implementation selected by this study

These rules describe the original filter graduation. Later
[eight-row work](../filter-composition/TILE-FILTERS.md) extends native window
handling; the [current guide](../../../ikea/docs/filters.md) owns the supported
calls. The timings here retain the window sizes and harnesses described above.

- Without GFNI, Local equality uses bitplanes at widths 1–7; ordered predicates
  use them at widths 1–5. Larger ordered predicates keep decoded SIMD lanes to
  retain throughput. With GFNI, only the one-bit case selects direct bitplanes.
- The decision is private execution policy, not another public format or preset.
  It follows the actual leaf in a composition, including substituted children.
- Tight, fully active TuplePack byte columns compare masked encoded bytes against
  a shifted query. Single-row field filters use a scalar encoded comparison;
  sparse and strided groups retain the prepared reader's access rules.
- Standard-width dense Local AVX-512 groups load their complete ZMM lanes directly
  rather than assembling pairs of YMM carriers. Sixty-four-value filter windows
  use that wider composition path. The original implementation used bounded
  16-value bodies and scalar edges for other windows.

## Run and reproduce

```sh
orb -m ubuntu cmake -S . -B build/packed-filters/neon -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_MARCH=armv8-a+simd \
  -DSIXDB_TUNE=neoverse-v2 -DSIXDB_SPIKES=packed-filters
orb -m ubuntu cmake --build build/packed-filters/neon \
  --target ikea_filters_check packed_filters_bench -j2
orb -m ubuntu build/packed-filters/neon/ikea/ikea_filters_check
orb -m ubuntu python3 workbench/tools/worker_group.py run \
  workbench/spikes/packed-filters/workers.json --detach
```

`report.py label=path/to/samples.csv --output report.csv` retains medians, extrema
and the paired fused control without combining latency and throughput.
