# Prior uniform branchless lookup

**The primary-match cliff is removable, but this scalar rewrite is not a default
replacement.** The best tested value-returning kernel beats Boost and Abseil on
all four random hot 50/50 fixtures. It loses predictable hot throughput and some
cold map throughput. Keep it in research; no maintained Ikea kernel, default or
consumer API changes in this study.

[Complete scorecard, counters and rejected variants](evidence/zen5/tables.md)
retain every hit/miss endpoint and both cold access paths. This follows the
[branch diagnosis](../hashtable-boost-adoption/HIT-MIX.md), which established that
speculative Swiss memory traffic did not explain the original hot cliff.

The subsequent [predictable-first study](HINTS.md) preserves the fast path and
selects a different complete loop only when useful; this report describes the
rejected uniform replacement.

## Decision and measured result

Independent hot reads on native Zen 5 AVX-512, ns/lookup, lower is better. All
arms perform the same full hit/key/value reduction. These are whole-table
lookups, including normal Swiss routing when needed. These are paired controls
from the final cohort; its maintained adapter uses ordinary `find` to produce
an explicit zero-on-absence result, just as the vendor adapters do.

| Surface | Maintained adapter | Branchless | Boost | Abseil |
| --- | ---: | ---: | ---: | ---: |
| W32 set, random 50/50 | 6.918 | 1.620 | 6.347 | 6.424 |
| W32 map8, random 50/50 | 7.401 | 2.357 | 6.768 | 6.576 |
| W40 set, random 50/50 | 8.775 | 2.874 | 6.376 | 6.446 |
| W40 map8, random 50/50 | 9.408 | 4.088 | 6.744 | 6.478 |

Branch misses fall from about 0.501/op to 0–0.0025/op. The result corroborates
the primary-branch diagnosis. It does **not** satisfy the preservation target:

| Final paired preservation check | Maintained adapter | Branchless | Change |
| --- | ---: | ---: | ---: |
| Hot W32 set, all hit | 1.005 | 1.649 | 64% slower |
| Hot W32 map8, all hit | 1.554 | 2.357 | 52% slower |
| Hot W32 map8, all miss | 1.242 | 2.249 | 81% slower |
| Cold W40 map8, prefetch32, all hit | 20.514 | 26.881 | 31% slower |
| Cold W40 map8, prefetch32, 50/50 | 19.202 | 22.850 | 19% slower |
| Cold W50 map8, prefetch32, all miss | 7.080 | 7.821 | 10% slower |

Cold sets improve at 50/50: W40 falls from 8.317 to 5.883 ns and W50 from
9.234 to 6.533 ns. Their endpoints still regress. Cold W50 map8 improves only
slightly at 50/50, from 30.525 to 29.789 ns, and remains behind Abseil's
22.663 ns. The default cannot be replaced on these results.

The tables also retain the original callback consumer, whose W32 hot all-hit
set/map8 results are 0.972/1.417 ns. The result adapter itself changes codegen
and is not free. Comparing only against that adapter would understate the cost
of replacing today's best path. Cross-cohort audits verify identical useful
answers; performance claims about a kernel use its own paired baseline.

## What the experiment changed

`overlay.py` copies the maintained headers into an ignored include directory
and adds research code there. Bucket geometry, encoding profiles, hash mixing,
Swiss reads/writes and storage representation remain fixed. `bench.py` changes
only read consumption in a generated copy of the existing real-vendor driver.
The shared CMake recipe now accepts that optional source path; its ordinary
driver remains the default.

The investigated paths are:

- **Callback:** combine absence and overflow into a rare slow condition; use a
  masked map value load before invoking the existing consumer only on hits.
  Sets lose the cliff but regress at endpoints. LLVM retains the map's hit
  branch, so this is rejected.
- **Result:** return `{found, value}`, with exactly zero bytes on absence. The
  caller can sum values and mask hit keys without a conditional callback. An
  AVX-512 masked load removes the hot map cliff, but cold prefetch32 map misses
  regress from 5.248 to 26.393 ns for W40 and 7.044 to 37.303 ns for W50.
  Architecturally disabled lanes are not a free cold miss on this measured CPU.
  The study does not isolate the internal translation/cache mechanism.
- **Dummy/select:** load from a shared zero word on absence. Ordinary C++ pointer
  selection can become a branch again; explicit `CMOV` preserves the intended
  selection. This avoids the masked-load miss collapse but costs hot endpoints.
- **Lean/carry:** check the bucket overflow byte first and use ordinary lookup
  for any bucket with overflow. The common path's branch depends on bucket
  state, not membership. The final map kernel uses `TZCNT`'s carry flag for both
  presence and `CMOV` to the safe zero word. It is the best tested final kernel,
  with the qualified gains and failures above.

This is a separate value-returning contract. An arbitrary callback still must
run only on a hit, and its subsequent side effects may require a branch. The
experiment does not claim to eliminate every consumer's membership decision.
No result API has been added to maintained Ikea.

`tails.s` and retained LLVM-MCA output explain the resource tradeoff: selected
maintained hit/miss tails model at 1.17/1.00 cycles, masked selection at 3.50,
and carry-pointer selection at 2.50. These are literal backend sequences, not
full-operation predictions. LLVM 21's `znver5` uses its Zen4 scheduling model;
it does not model branch prediction or the cold memory system. Native PMU and
throughput determine the decision. Complete linked read loops are recoverable,
with the eight final hot caller loops included in the selected evidence.

## Scope and verification

The four accepted cohorts contain **744 fresh processes and 8,856 measured read
intervals**, three seeds and two counterbalanced processes per arm/case. Each
process measures three one-million-query intervals per mix/access path. All PMU
groups complete with 100% coverage, no multiplexing or scaling. They count only
the calling thread's user-space timed loop. Query generation, construction,
validation, cache conditioning and output are outside timing.

Actual Boost **1.92.0** and pinned Abseil
`2065f4ded0558c6f89fee67c8e5228feb4eb960e` use the existing verified providers.
All arms receive the same inputs and pass exact hit/key/full-value checks. The
cross-cohort audit also checks the callback and result consumers agree. No
MVCC, locks, damage publication or Loom work is timed.

Geometries and table-owned memory come from the retained
[memory comparison](../hashtable-boost-adoption/MEMORY.md), rather than timing
selection. Hot populations are 13,926: W32 b11 for sets/maps, W40 set b12 and
map8 b11. Cold populations are 14,260,633: W40/W50 set b22 and map8 b21. Cold
intervals follow a 256 MiB conditioning scan; direct and home-prefetch32 are
retained. Boost has native candidate prefetch but no equivalent public home
prefetch hook. The W50 map remains 25% smaller than Boost, explicitly retained
as the earlier upper-memory reference rather than exact equality.

This targets the primary cliff in those fixtures. It does not measure an
85%-loaded/overflow-heavy frontier or establish AVX2/NEON results. Optimized
research paths cover generic and AVX-512 small-exact kernels, sets and map8;
other recipes fall back to ordinary lookup.

Native ASan/UBSan checks pass for callback, result, dummy and final carry
variants. Sixteen geometries plus small-exact recipes cover bitmap/packed
boundaries, zero/max encodings and quotients, first/last buckets, every primary
row, 300 colliding keys, saturated overflow, erase/refill and side-effecting
consumer counts/locations. Missing result values must be exactly zero; map
callbacks must execute exactly once on hits and never on misses. The
maintained header beforeimage is verified identical across every overlay.

## Reproduction

Accepted worker IDs, full raw recovery references, source/overlay identities,
all timing/counter samples, binaries, assembly and compiler commands are in the
[recoverable bundle](evidence/zen5/artifact.json). The source audit and selected
scorecards are retained in Git. Three pre-timing setup/compile failures are
listed separately and contribute no measurements.

Regenerate all audits and tables without new timing:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-primary-branchless/evidence/zen5 \
  build/reports/hashtable-primary-branchless
```

Run the final candidate with actual controls and native sanitizers using
`native.sh` through the worker helper. Its default cohort is `carry`; `--arg callback`, `--arg result` or `--arg lean` select the retained earlier protocols.
The code stays in this research directory. No production throughput gain is
claimed from an unpromoted candidate.
