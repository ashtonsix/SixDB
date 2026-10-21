# Predictable-first primary lookup

The current spike meets Ashton's revised W32 budget on native Zen 5 AVX-512:
predictable reads retain their fast path, while optional selection between two
compiled lookup/consume loops brings random throughput well above 220M QPS.
The hinted getter and selector remain research implementations.

W32 whole-table independent reads, ns/lookup; map8 carries an eight-byte value:

| Surface / path | All hit | Random 50/50 | All miss |
| --- | ---: | ---: | ---: |
| Set, original | 0.964 | 6.909 | 1.089 |
| Set, selected | 1.000 | 1.565 | 1.074 |
| Map8, original | 1.408 | 7.094 | 1.126 |
| Map8, predictable getter | 1.366 | 7.024 | 1.087 |
| Map8, unpredictable getter + predicated fold | 2.090 | 2.157 | 2.396 |
| Map8, selected | 1.420 | 2.493 | 1.161 |

Use `lookup_hint::predictable` / `lookup_hint::unpredictable`: neither says that
a key is present or absent. Keep predictable as the default. Selection belongs
to a caller's operator/stream and switches an entire compiled loop, without
per-key dispatch or table-owned profiling state. It is cheap on larger batches;
it is unsuitable for individual gets or very short batches.

- [Current design, boundaries and recommendation](HINTS.md).
- [Full hot/cold, vendor, pattern and microbatch scorecard](evidence/hints-zen5/tables.md).
- [Prior uniform branchless experiment](SCALAR.md): why replacing the fast path everywhere failed.

No maintained Ikea API, kernel, occupancy or setter semantics changed. The
spike reuses the maintained encoding map and the existing actual Boost/Abseil
comparison driver.
