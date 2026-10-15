# Cheaper fingerprint arithmetic with unchanged placement

This experiment changes how the table computes a fingerprint. It preserves the
bucket address and stored suffix, so every insertion takes the same primary,
secondary or key-overflow position. It can reduce hash work and change false
fingerprint matches; it cannot improve occupancy or allocation by moving keys.
The control remains the default. Complete reads, mutations and lifetime beside
ordinary and exact-key Abseil are part of the comparison from the first build.

For a K-bit key and A-bit bucket address, let F=min(8,K−A), C=K−A−F. After XOR
with the input seed, split the key into its low A bits and remainder r. The
current reversible representation stores suffix s=r>>F and fingerprint
`lowF(r) XOR H(s)`. Reconstruction recovers lowF(r) using the known suffix,
then recovers the low key bits using the unchanged bucket function. H itself
does not need to be invertible.

The control H folds the two halves of a 128-bit product. The alternative uses
the high F bits of `(a*s+b) mod 2^W`, with W=32 when C+F−1<=32 and W=64
otherwise. It permits even and zero coefficients. With no suffix, both variants
retain the same cached constant and perform no variable selector arithmetic.
`HT_SELECTOR_HASH=1` enables the alternative; the permutation still occupies
four words. The former cached constant word supplies b when a suffix exists.

Thorup's *High Speed Hashing for Integers and Strings*, §3.3, Theorem 3.5,
establishes strong universality for this multiply-add-shift family when the
arithmetic width is at least input width plus output width minus one, and a and
b are independent uniform parameters. See the [paper](https://courses.cs.washington.edu/courses/csep521/21wi/readings/Thorup.pdf).
Our conditional-bucket argument additionally requires those parameters to be
independent of routing and admission history. Equal suffixes in one bucket have
distinct low remainder bits; unequal suffixes would have independent selector
outputs under those premises. The current single-word entropy expansion does
**not** establish the independence premises. Its quality is an empirical
experiment, and no seed-hiding, adaptive-adversary or worst-seed guarantee follows.

`hash_packet.h` implements explicit NEON, AVX2 and AVX-512 arithmetic. The 32-bit
case uses packed low multiplication/addition. The 64-bit case uses three widening
32-bit partial products on NEON/AVX2 and native low multiplication with AVX-512DQ.
Bucket hashing retains its full-product fold. Both compiler vectorizers remain
disabled. A SIMD arithmetic improvement alone does not establish getN/setN gains;
hierarchy batch integration remains separate work.

## Quality and correctness controls

`hash_packet_check.cpp` compares scalar and explicit SIMD results with independent
128-bit arithmetic for all eight key domains, every address width, packet sizes
four/eight, guarded inputs, carries, extrema and duplicate lanes. Additional
coefficient/bias cases cover zero/even/all-ones parameters and both sides of the
C25/C26 arithmetic-width boundary. `hash_mask_check.cpp` independently checks
masked inputs and inversion. The W2 value check exercises all domains, container
kinds, promotion, downstream traffic, cloned owners and retained publication.

`selector_quality.cpp` compares both families over identical fresh admission
histories. It uses all eight key widths, A3/6/7 and A8/10 where valid, capacities
8/16/24, and twelve structured/random key patterns where their domains fit.
Every history checks identical placement and exact point results. Its counters
measure false suffix verifications before success, separately in primary and
secondary buckets. They include unsuccessful bucket work for chained hits but
exclude full-key chain search. They are logical counts, not timings.

The fixed-low-remainder cohorts hold twelve complete low-A groups and probe
twelve held-out suffixes. Consecutive, strided and carry-boundary suffixes give
exactly twelve entries per primary bucket for every seed. Eight-slot buckets
therefore force four spills per bucket; sixteen/twenty-four-slot buckets are
primary-only controls. Ordinary cohorts target 90% primary capacity, subject to
domain limits. K40/A8 also exercises the retained `0x44` witness, with a larger
miss cohort than the original diagnostic; its totals are not a numerical replay.
The screen does not establish quality after deletion/reinsertion histories.

`selector-quality-report.py` preserves mean, p95 and maximum per-seed rates by
geometry, pattern and tier. Complete-container `selector-mix` runs use the same
thirteen-phase driver as the occupancy experiment, including deletion, aged
hits/misses and reuse. They hold count encoding, plane layout, scan width and
the paired x86 mask control fixed. Neither unselected row consumption nor
inactive-fingerprint encoding is combined into this comparison.

## Results and disposition

Release scalar/packet checks pass. ASan/UBSan also passes those checks and the
integrated W2 histories. The packet test covers 576 geometries and 6,885,144
keys, plus 131,872 full-fold and 98,304 low-product checks on local NEON.
The quality screen completes 82,240 paired histories: 1,285 valid
geometry/pattern cohorts, each with 64 deterministic seed words. Placement and
point-result checks pass; no fresh-admission allocation benefit is possible
because placement is identical.

The alternative has worse structured-key tails in this screen. At K32/A6 with
16-slot buckets and strided suffixes, the old selector makes no false suffix
checks; the alternative's worst seed averages one extra check per hit. At
K24/A3, the same shape reaches 1.833 false checks per miss, versus zero for the
control. These are query averages within particular seeded histories, not
population-wide expectations. Both families have unfavorable cases: some
carry-boundary misses approach one false check per query with the old selector.
At K40/A8, the `0x44` cohort's mean false checks per hit change from zero to
0.0208, and its mean per miss from 0.00911 to 0.0638. The added quality coverage
therefore prevents treating the cheaper arithmetic as an unqualified upgrade.

The first local complete-operation panel has 5,616 phase rows from 144 fresh
processes: both variants, K40/W2, all sixteen geometries and two Abseil controls,
two cardinalities, forward/reverse passes and three repetitions per process.
Every paired phase inventory agrees. At A9/U128 with 6,144 keys:

| Operation | Fold ns/op | Multiply ns/op | Multiply / fold |
| --- | ---: | ---: | ---: |
| Independent hit | 3.748 | 3.537 | 0.944 |
| Miss | 3.452 | 3.531 | 1.023 |
| Dependent read | 15.685 | 14.964 | 0.954 |
| Replacement | 4.659 | 4.269 | 0.916 |
| Mutation lifecycle, per initial key | 21.325 | 20.400 | 0.957 |

The alternative's hit ratio against its ordinary Abseil control is still 1.278,
and dependent reads 1.079. Logical table memory is 0.502 times Abseil's for both
hash variants. Other geometries include gains and regressions. These are warmed
ARM/OrbStack diagnostics, not target-native or established residence results;
short lifecycle phases also vary between passes. They time the standard driver
key history, not the structured quality witnesses above.

[Selected evidence](evidence/selector-mix-local/summary.json) retains paired
quality witnesses, per-cohort tails, representative complete timings and raw
input hashes. Full local binaries, raw reports, compiler receipts and source
hashes are in `build/hashtable-design/selector-mix-local`. The native comparison is complete on V2 NEON, Zen5 AVX2 and Granite AVX-512,
with five set/map/multimap cases. All thirty timed ELFs pass the source,
compile-control and call-boundary audit (6,480 reads and 1,080 updates). Frozen
report replay reproduces all 140,400 rows from 2,160 fresh processes, including
paired inventories and both process-order passes. All native checks pass and
all workers have terminated. The 4 GiB ARM member was killed compiling the
exhaustive packet checker, before runtime checks; the successful replacement
used the same frozen sources and flags on an 8 GiB c8g.xlarge.

### Actual-table structured histories

`selector_witness.cpp` runs the real two-byte map and both ordinary/exact-key
Abseil controls on the quality screen's K24/A3 and K32/A6 strided suffixes and
K40/A8 `0x44` cohorts. Every primary bucket has twelve keys; placement and memory
are unchanged between selectors. Seeds 0 and 57 are runtime inputs to the same
binary for each family. Independent hit/miss order is shuffled, and serial reads
use the actual stored two-byte value as the next key-array index. Full point,
miss and cycle oracles pass; ASan/UBSan passes all eighteen alternate-family
key/seed/contender histories.

The local panel contains 2,520 rows from 72 fresh processes, two opposite-order
passes and five repetitions. Selected regressions are consistent in both passes:

| History | Operation | Multiply / fold | Multiply / ordinary Abseil |
| --- | --- | ---: | ---: |
| K24/A3, stride, seed 57 | Miss | 1.637 | 1.811 |
| K32/A6, stride, seed 57 | Hit | 1.385 | 2.117 |
| K32/A6, stride, seed 57 | Dependent read | 1.136 | 1.277 |
| K40/A8, `0x44`, seed 0 | Miss | 1.477 | 1.130 |

These are complete-consumer costs, not isolated collision penalties. The K32
multiply loop rematerializes the bucket-fold constant inside each primary lookup,
while the fold loop retains it in a register; multiply also reloads an outer
round counter. Both retain actual lookups and payload consumption. Seed0/57 use
the identical runtime-seeded loop within each variant, supporting seed
sensitivity without attributing the whole between-family gap to collisions.
The tiny K24 hierarchy is a diagnostic and does not select the small-list policy.
[Selected witness evidence](evidence/selector-witness-local/summary.json) keeps
both families, both seeds and all read phases, with baseline ratios and hashes.

**The multiply-add selector is rejected as a general default.** Its modest
ordinary-history gains do not justify the demonstrated structured-history losses.
Native ordinary-history measurements can still explain platform arithmetic costs;
they cannot overturn those counterexamples or select a production preset.

### Qualified native ordinary-history comparison

The preselected K40/W2 A9/U128 history (6,144 keys) retains identical placement
and logical memory, 0.502 times ordinary Abseil. Ratios below divide the new
selector's complete operation time by its respective control. All layouts and
operations, including losses, remain in the [Zen5](evidence/selector-mix-zen5/summary.csv)
[Granite](evidence/selector-mix-granite/summary.csv) and
[V2](evidence/selector-mix-neon/summary.csv) exports. The
[x86 qualification](evidence/selector-mix-native/x86-qualification/qualification.json)
and [V2 qualification](evidence/selector-mix-native/neon-qualification/qualification.json)
bind frozen sources, actual timed ELFs, the replayed reports and audit scope.

| Target | Hit / fold | Hit / Abseil | Dependent / fold | Dependent / Abseil | Update / fold |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen5 AVX2 | 0.975 | 1.413 | 0.998 | 1.157 | 0.947 |
| Granite AVX-512 | 0.980 | 1.166 | 1.001 | 1.080 | 0.901 |
| V2 NEON | 0.960 | 1.359 | 1.027 | 1.134 | 0.983 |

The lower update cost is useful attribution, but this arithmetic change does
not resolve hot-read competitiveness. These finite warmed workloads do not
establish L1/L2/LLC/DRAM residence or a seed-distribution guarantee. The
structured-history rejection above remains the disposition.
