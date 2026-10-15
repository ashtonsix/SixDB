# Historical quotient-map control

`unified_quotient.h` is a byte-for-byte copy of
`/home/ashtonsix/calico-simple-containers/flatmap16-prototype/experiments/unified_quotient/unified_quotient.h`,
captured on 2026-10-13. SHA-256:
`dc19d09c3bd1bfb36511c5afb10200826a2e3b08a4a492fac33f5d0b5a98109b`.
The source tree has no Git repository, so no source commit is available. The
content hash identifies the copied revision. The body and algorithms are unchanged.
This is a comparator, not a restored SixDB spike or proposed production contract.

The three historical winners all store 16-bit keys at cardinality 4096:

| Alias | Value | Buckets | Slots/bucket | Exact remainder | Nominal primary occupancy |
| --- | --- | --- | --- | --- | --- |
| `Byte16x16` | 16 bits | 512 | 16 | 7 bits | 50% |
| `Byte32` | 32 bits | 1024 | 12 | 6 bits | 33.3% |
| `Byte64` | 64 bits | 2048 | 6 | 5 bits | 33.3% |

Each primary bucket is 64 bytes. An affine bijection of the 16-bit key supplies
the address and exact remainder. There is no second suffix comparison. NEON
compares remainders and reduces matching lane numbers directly. Inline Bloom
bits reject most overflow lookups; overflow uses 128-byte blocks with promotion.
The implementation supports lookup, insertion, replacement and erasure at fixed
primary geometry. It does not supply a general table growth policy.

The sibling `results/benchmark.csv` reports independent hit/miss/90%-hit lookup
times of 1.596/1.588/1.581 ns for `Byte16x16`, 1.573/1.492/1.558 ns for `Byte32`,
and 1.664/1.484/1.662 ns for `Byte64`. Paired Abseil times were approximately
1.86–1.93 ns. These were Apple M1 Pro, AppleClang 15, Abseil 20260107.1 results;
the original lookup loop measured reciprocal throughput, not a dependent chain.
They motivate this reproduction without establishing results for another platform.

`../prior_compare.cpp` compares these unchanged bodies with normal typed
`absl::flat_hash_map<uint16_t, V>` instances at the pinned Abseil revision owned
by `../baseline.cmake`. It preserves the original shuffled key generator and
hit/miss sequences. It adds a genuine returned-value-to-next-key dependency,
identical full-workload warmup for both maps, repeated reversed execution order,
an independent full-domain membership/value oracle outside timing, and mutation
checks. Wider values retain populated upper bits; zero and all-ones are legal
values. Churn alternates between disjoint 4096-key populations and returns to the
starting population after each cycle. One churn operation means an erase/insert
pair. Update value generation is outside timing.

Run `hashtable_prior_compare check`, then `hashtable_prior_compare 4 7` on a pinned
CPU. The CSV retains every repetition. No L1/L2/LLC label is inferred from table
cardinality. The original header's memory statistics are *modeled live payload*
and *modeled resident payload*, excluding vector capacity slack, some ownership
metadata and allocator overhead. The Abseil adapter uses the common counting
allocator and reports actual requested bytes and peak requested bytes. These
different accounting kinds are explicit in the CSV; they are not a proven
end-to-end memory advantage. Ordinary owner-object sizes are reported separately.

## Current reproduction

The 2026-10-13 local Linux/OrbStack screening used pinned Clang 21.1.8,
`-O3 -DNDEBUG -march=armv8-a+simd -mtune=neoverse-v2`, explicit NEON with automatic
vectorization disabled, CPU affinity 6, four 262144-operation rounds and seven
repetitions with alternating arm order. Full correctness checks passed. Timings
below are medians in nanoseconds per operation; each cell is historical/Abseil.
This VM result establishes a useful local comparison, not native fleet performance.

| Operation | 16-bit value | 32-bit value | 64-bit value |
| --- | ---: | ---: | ---: |
| Dependent hit | 12.006 / 10.503 | 12.005 / 10.531 | 11.951 / 10.440 |
| Independent hit | 1.627 / 1.662 | 1.628 / 1.591 | 1.696 / 1.703 |
| Independent miss | 1.476 / 2.025 | 1.478 / 1.978 | 1.495 / 2.065 |
| Independent 90% hit | 1.614 / 1.674 | 1.577 / 1.637 | 1.640 / 1.740 |
| Update | 1.537 / 2.192 | 1.612 / 2.123 | 1.715 / 2.250 |
| Erase/insert pair | 8.527 / 29.992 | 9.756 / 29.582 | 12.609 / 29.995 |
| Independent hit after churn | 1.634 / 1.942 | 1.657 / 1.918 | 1.689 / 1.968 |

The historical exact-remainder design still supplies meaningful miss, update and
churn advantages, with initial hit throughput near parity. It does not meet the
desired dependent-hit latency parity: that path is about 14–15% slower here.
Churn in this driver repeatedly replaces the entire population, so its numbers
should not be compared directly with the old benchmark's different churn driver.

Before churn, historical modeled payload was 33792/67712/131840 bytes for
16/32/64-bit values, versus Abseil allocator requests of 40952/73696/139184 bytes.
The historical owners are 136 bytes; ordinary Abseil owners are 16 bytes in this
build. The differing payload accounting described above still applies.
Raw repetitions, the source hashes, compiler/kernel metadata and correctness log
are in the ignored local `build/hashtable-prior-review/` directory:
`comparison.csv`, `metadata.json`, `summary.json` and `check.txt`.
