# Borrowed inline reads promoted to Ikea

The maintained concrete table and typed native view now expose
`get_borrowed<Hint>(key)`. It returns read-only `{address, found}` without
materializing the value. Absence supplies `V` readable initialized zero bytes.
The default is predictable; automatic selection surrounds the complete consumer
as before. [Usage](../../../ikea/docs/hashtable/usage.md#borrow-wide-inline-values)
and the [example](../../../ikea/examples/hashtable/borrowed.cpp) own the interface.

Borrowing requires contiguous values: 1–8 bytes or a multiple of eight. Striped
widths keep copied reads. The owner must remain retained and unmodified through
consumption; a native capture can retain the old owner across descriptor
replacement. No typed-object or alignment promise is added. Small-value getters,
packing, mutation, growth, allocation and erased operation tables are unchanged.
The overflow address helper reuses the maintained Swiss traversal instead of
retaining a second probe implementation. The research adapter now delegates to
this API. Slot-backed values remain research. Engine integration and directory
verification are outside this promotion at Ashton's direction.

## Paired native qualification

Zen 5 EPYC 9R45, AVX-512, pinned Clang 21.1.8, O3, inline threshold 1000 and
function alignment 64. Three seeds, three warm/two cold repetitions per seed.
Baseline/candidate process pairs reverse order in seed991. Both builds use the
same benchmark and linked Loom; only the three files in the
[source pair](evidence/promotion-zen5/source-pair.json) differ. The baseline is
the original spike adapter and pre-promotion Ikea headers from `be7de91e`.
233 implementation/dependency files match the captured source after the run.

Cold intervals follow CLFLUSHOPT/SFENCE over owned backing; inputs are not
flushed. Later operations can warm earlier-prefetched lines. PF32 hints future
homes. Manual16 and actual Loom16 defer complete payload consumption. These
are independent complete-consumer throughput times, not dependent latency.
Every read checks membership, keys and actual payload output. Both full64 and
projected16 consumers are retained; the table below consumes all value bytes.
All times are ns/query, lower is better.

| Keys/value | Consumer | Spike hit → promoted | Spike random50 → promoted | Spike miss → promoted |
| --- | --- | ---: | ---: | ---: |
| W32/V8 hot | Predictable |1.487 →1.459|7.680 →7.748|1.190 →1.146|
| W32/V8 hot | Auto |1.494 →1.551|2.720 →2.732|1.219 →1.232|
| W32/V64 hot | Borrowed Auto |2.866 →2.898|6.425 →6.443|4.067 →4.061|
| W32/V64 cold | Borrowed Manual16 PF32 |13.525 →13.484|12.969 →12.876|8.221 →8.254|
| W32/V64 cold | Borrowed Loom16 PF32 |14.893 →15.015|16.059 →15.920|9.623 →9.633|
| W40/V64 cold | Borrowed Manual16 PF32 |14.576 →14.653|15.666 →15.913|8.162 →8.188|
| W40/V64 cold | Borrowed Loom16 PF32 |16.457 →16.502|17.609 →17.597|9.459 →9.617|

Small-value fixture: b11, 13,926 entries; hot V64: b9, 6,963; cold W32:
b17, 2,673,868; cold W40: b17, 1,782,579. Across 105 paired read cells the median
promoted/spike time ratio is 1.005, with cell medians ranging 0.963–1.038. Across
14 unchanged write/growth cells it is 1.003, ranging 0.959–1.032. V64 prepared
insertion is 8.253 → 8.201 ns, replacement 4.948 → 4.931 and source-retaining
same-seed growth 22.058 → 22.255 ns/source entry. The extraction preserves the
selected gains; the complete repetitions retain the small variations too.

Actual pinned peer controls run in the same campaign. Their native direct cold
consumers receive PF32 where a public home-prefetch API exists. They are separate
execution choices from our Manual/Loom rows; the original
[scorecard](evidence/zen5/tables.md) also retains deferred peer consumers.

| W32/V64 cold, full64 | Hit | Random50 | Miss |
| --- | ---: | ---: | ---: |
| Maintained copied getter, direct PF32 |23.974|22.286|6.845|
| Maintained borrowed, Manual16 PF32 |13.484|12.876|8.254|
| Maintained borrowed, Loom16 PF32 |15.015|15.920|9.633|
| Actual Abseil inline, direct PF32 |32.706|28.893|13.054|
| Actual Boost inline, native direct |33.686|26.795|11.513|
| Actual F14Fast, direct PF32 |30.972|24.101|6.387|
| Actual F14Value, direct PF32 |29.110|24.378|11.824|

Borrowing does not remove every wide-value gap. In the hot V64 fixture, Boost
hits/misses are 2.769/1.258 ns versus our borrowed Auto 2.898/4.061. Deferred
consumption also adds work to all-miss streams. Keep the execution choice tied
to the useful consumer; this promotion adds an address-return seam, not an
automatic choice of scheduling depth or a new storage policy.

## Correctness and recovery

All 30 maintained hashtable checks/examples pass under ASan/UBSan on both
AArch64 and native AVX-512; four focused native AVX2 checks pass too. Tests cover
all W1..64, contiguous values through 128 bytes, unsupported striped widths,
64/96-byte key regions, full primary/spill payloads, zero-valued hits, sticky
overflow, tombstone/refill, rebuilds and retained native captures. Twelve actual
native Loom checks cover P/U/Auto and lengths 1/7/17/4111, including drain tails.

[Retained evidence](evidence/promotion-zen5/README.md) includes all 2,889 raw rows,
407 summary cases, verification of 141 process receipts, source hashes and native check
logs. Its linked full native archive recovers the original runner, baseline
bytes, captured source, exact commands, binaries and disassembly. No new encoding
profile or AVX2/NEON performance claim is made.
