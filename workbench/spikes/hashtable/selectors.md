# Searching two fingerprint windows together

A bucket with 17–32 slots currently searches two 16-byte fingerprint windows.
The second window is visited only if the first has no matching key. On AVX2 and
AVX-512, one 32-byte comparison could remove a comparison/mask extraction and
the branch between windows. It also reads the high half eagerly, sometimes
crossing a cache line unnecessarily. Which cost dominates depends on occupancy,
placement and successful-key position.

This experiment leaves the solver recipe unchanged. `HT_RECIPE_SCAN32=1` joins
the two contiguous windows only on AVX2-capable targets, for capacities 17–32
with 16-lane physical windows. Other capacities and NEON keep their existing
execution. The ordinary default remains two windows. No new storage format or
SeriesPack interface is introduced.

The composed version combines two existing bounded SeriesPack loads. The
independent direct version loads the same byte range without those bodies.
For exact tails it uses end-aligned overlapping loads on AVX2 or a masked load
on AVX-512; a partial-vector `memcpy` produced unnecessary byte-insertion chains
and was replaced before native comparison. The overlapping reads still stay
entirely within the exact fingerprint plane.
Exact access reads only the fingerprint plane; broader access needs 32 readable
bytes inside the same initialized, stable bucket. Inactive matches are masked
before suffix access. Neither version changes stores or damage publication.
Guard checks cover all exact tails 17–32, partial counts, high-lane matches,
fingerprints 0/255, collision confirmation, poisoned neighbors and access
thresholds. The bucket checks also exercise sets, maps, multimap tags and erase.

`selector-native.sh` runs all four composed/direct × 16/32 controls in an
interleaved randomized schedule and reversed second pass. The initial complete
operation comparison uses K16/K40 two-byte maps, both cardinalities and every
geometry from [the geometry study](geometry.md). It retains ordinary and
exact-key Abseil in every binary, including unchanged-code controls where a
recipe has at most 16 slots. Content oracles and a value-dependent read cycle
remain in the driver. Reports require identical geometry, tier populations and
logical storage across execution variants before comparing their times.

This finite comparison diagnoses one execution choice. It does not qualify
sets' serial latency, multimaps' whole-operation performance, large working
sets or a platform preset. Its [native receipt](evidence/selector-native/README.md)
now retains 51,840 rows in 1,152 fresh processes on Zen5 and Granite Rapids.
All sixteen timed binaries and 2,304 read/update regions qualify, every report
replays, and all phase inventories agree across execution variants. Both workers
have terminated. Call-boundary audits do not independently prove serial dataflow.
A separate [machine-code trace](evidence/selector-native/serial-dataflow.json)
qualifies twelve selected serial cases: scan32 K16/A9/U64 and K40/A9/U128, plus
each ordinary/exact Abseil counterpart, on both hosts. Each actual two-byte
payload load feeds rank addition, modulo 6,144 and the next key/hash/address;
the delta is not synthesized. Other geometries retain their separate qualification
limits. These timings include the common rank/digest work, not just bare lookup.

The complete recipe guard/oracle suite passes under AVX2 emulation, including
the strengthened direct tails. The unchanged NEON fallback passes ASan/UBSan.
Native recipe checks pass on both hosts. The witnesses in
`selector_codegen.cpp` produce identical composed/direct instructions for the
full 32-byte window with the pinned compiler, for both x86 targets. The composed
pair folds into the same memory-operand comparison as the direct load. Exact
tails expose a useful composition question: AVX2's direct 31-byte overlap has
lower modeled throughput cost than combining existing bounded loads, while
AVX-512's masked direct load removes high-half assembly for all tested partial
tails. The saved disassembly and llvm-mca reports are local diagnostics
under `build/hashtable-design/selector32`; model throughput is not measured speed.

At the larger 6,144-key history, the composed controls show the following
trade-offs. Ratios divide candidate time by ordinary Abseil; lower is better.
Logical memory includes owner metadata and is identical between scan variants.

| Case / actual geometry | Logical memory / Abseil | Zen5 hit, 16 → 32 | Granite hit, 16 → 32 |
| --- | ---: | ---: | ---: |
| K16/W2, A8/U128 | 0.857 | 1.616 → 1.125 | 1.665 → 1.033 |
| K16/W2, A9/U64 | 0.857 | 1.380 → 1.139 | 1.314 → 1.027 |
| K40/W2, A10/U128 | 1.003 | 1.469 → 1.484 | 1.361 → 1.324 |
| K40/W2, A9/U128 | 0.502 | 4.210 → 1.462 | 4.401 → 1.323 |

The K16 improvements bring some memory-saving layouts near independent-hit
parity on Granite, while gaps remain on Zen5. For A9/U64, the value-dependent
read ratios with scan32 are 1.134 on Zen5 and 1.057 on Granite. Wider execution
is not universally better: K40/A10's Zen5 serial time grows from 11.561 to
12.031 ns, and its independent hits barely change. All geometry arms, misses,
mutations, lifecycle results, exact-width baselines and pass medians remain in
the native export; this table does not choose a policy from selected cells.

Actual K40/W2 arm 1 loops confirm the hash-count confound on both hosts and
both composed/direct paths. With scan16, the independent-hit loop copies the
previous checksum into a register, overwrites its low byte with 40, and supplies
that register to BZHI. With scan32, it freshly defines the count register from
the key-multiplier constant. Arm 0 has no checksum-count feedback in either
variant. Thus the large arm-1 gain also removes an unrelated dependency and
cannot be assigned to selector width alone. The separate
[source-mask experiment](hash-masks.md) holds scan16 fixed to address that issue.

The actual full-width composed/direct selectors lower to the same memory-operand
YMM comparison: VPMOVMSKB on Zen5, or an AVX-512 mask and KMOVD on Granite.
There is no residual two-load/insert boundary in those paths. The 32-lane
candidate loop also changes mask clearing and removes the guarded second group.
These are whole-loop execution changes, not evidence of a generic SeriesPack
overhead. Exact-tail isolated models still identify a narrower bounded-load
opportunity; the full-width complete-operation panel does not measure that cost.
