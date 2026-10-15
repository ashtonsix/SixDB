# Reversible hash screen

The width-preserving SplitMix-style control remains the best general candidate
among the constructions screened here. Random odd multiplication is much
cheaper, but favorable average distributions hide severe seed-dependent tails.
It should not become the table default based on one favorable benchmark seed.
A geometry-dependent unbalanced Feistel is worth a complete-map experiment for
narrow exact remainders: it distributes consecutive keys perfectly at these
geometries, but its scalar hash dependency is longer than the control's.

These are component measurements and placement counts, **not hash-table
performance or a security validation**. The complete candidate-versus-Abseil
comparison remains the decision surface.

## What was measured

[permutation_review.cpp](permutation_review.cpp) contains twelve reversible
families for domain widths 8, 16, 24, 32, 40, 48, 56 and 64. No truncation of a
larger non-domain-preserving permutation is used. Each family checks both inverse
identities, exhaustively at widths 8/16 and over 65,536 deterministic samples at
larger widths, with four seeds and explicit endpoint inputs. The quality run also
checks reconstruction through the secondary permutation at its own geometry.

The quality matrix uses 32 independent deterministic seed pairs, bucket capacities
8/16, and 95% of primary capacity. Primary address width is
`a=min(10,w/2-2)`; secondary width is `a>3 ? a-3 : 1`. Thus the 64-bit case has
1,024 primary buckets and 128 secondary buckets; the 16-bit case has 64 and 8.
These are selected geometries, not all possible growth levels. At capacity 16,
there are 15,564 and 972 inserted keys respectively.

Common input sets are unique random keys, consecutive integers, power-of-two
strides, a fixed high prefix with a low counter, bit-reversed counters and Gray
counters. A seventh diagnostic deliberately constructs primary collisions using
the known inverse. It is seed-aware pressure testing, uses different key sets per
family, and is excluded from the headline comparison below. It does not model an
attacker who cannot observe the seed.

Placement counts include keys beyond the primary bucket's capacity, then keys
beyond their independently seeded secondary bucket's capacity. Those last keys
are called chain placements; no chain storage or lookup is timed here. Fingerprint
pair counts include every key assigned to a primary bucket, including displaced
keys. The conditional secondary histogram is measured on the actual primary
spill subset; a good unconditional histogram is insufficient.

Timing uses 2^21 operations, five repetitions, constructor setup outside the
loop, and median nanoseconds per key. `serial` feeds each result into the next
hash; `independent8` has eight explicit scalar dependency chains. It is **not a
SIMD measurement**. Both compiler vectorizers are disabled. Hash results remain
live, and disassembly confirms seed/control loads are outside the loop.

The host is an aarch64 Linux VM under OrbStack, pinned to logical CPU 7, built
with Ubuntu Clang 21.1.8. Other work ran on other logical CPUs. These local timings
are a diagnostic, not Neoverse, AMD/Intel, or cache-residence evidence.

## Findings

The table reports forward hash timing at width 64 and worst chain placement over
six common input shapes and 32 seeds at capacity 16. Percentages use all inserted
keys as denominator. A lower worst case in this finite matrix is not a general
concentration guarantee.

| Family | Serial ns/key | Independent8 ns/key | Worst chain %, w=16 | Worst chain %, w=64 |
|---|---:|---:|---:|---:|
| Input XOR, random odd multiply | 1.31 | 0.20 | 12.76 | 43.07 |
| Extra pre-multiply right-XOR | 2.00 | 0.37 | 12.96 | 42.90 |
| Width-preserving SplitMix-style | 4.21 | 0.80 | 0.62 | 0.12 |
| Three lightweight balanced Feistel rounds | 7.51 | 1.51 | 21.09 | 0.14 |
| Four lightweight balanced Feistel rounds | 9.86 | 1.93 | 7.61 | 4.42 |
| Unbalanced Feistel, folded full product | 3.73 | 0.82 | 15.74 | 70.11 |
| Unbalanced Feistel, strong bucket mixer | 5.45 | 1.10 | 0.41 | 0.13 |
| Same, also multiply stored remainder | 5.45 | 1.15 | 0.41 | 0.13 |
| Three mix32 balanced Feistel rounds | 14.98 | 3.01 | 0.82 | 0.10 |
| Four mix32 balanced Feistel rounds | 19.55 | 3.84 | 0.51 | 0.11 |
| One's-complement modular multiply | 2.07 | 0.41 | 6.48 | 50.13 |

The omitted folded-product-plus-remainder-multiply variant has identical bucket
placement to folded product, so mixing its stored remainder cannot rescue its
placement failures. The source retains it to distinguish these two concerns.

For the random odd multiply at width 64, consecutive keys have median primary
spill of only 1.03%, yet the worst seed spills **56.22%**. The strong control
spills 7.83% at the median and 8.47% at the worst seed. This is precisely the trap
in choosing a hash from one seed or a median alone. At width 32 the same affine
candidate looks much better in this matrix, which also cautions against treating
one tested width as evidence for the whole family.

The one's-complement candidate computes an end-around-carry product modulo
`2^w-1`, with a coprime multiplier, after XORing the input seed. It preserves both
zero representations (zero and all-ones); no interior input can map to either,
so the entire `2^w`-element domain is permuted. The inverse uses the inverse
multiplier under that modulus. The attractive native64 `MUL`/`UMULH` pair operates
in parallel, but bad structured-subset concentration disqualifies the candidate
as a general replacement: worst width-64 sequential spill is 63.29%, and width-32
fixed-prefix keys reach 45.60% chain placement.

## Geometry-dependent unbalanced Feistel

For the table's current address width `a`, split the seeded input into
`l=x & mask(a)` and `r=x >> a`. Store the pair

```
bucket = l XOR H(r)
remainder = r                  // optionally M*r modulo 2^(w-a), M odd
```

Reconstruction recovers `r`, then `l=bucket XOR H(r)`, then the original input.
`H` need not be invertible; the pair is invertible. Each table level uses its own
address width and independent seed. A growth change must use the old geometry for
reconstruction and the new geometry for placement.

The strong variant uses a two-multiply 32-bit mixer when the remainder fits 32
bits, otherwise the 64-bit control mixer. A complete run of low `a` bits visits
every bucket once for each fixed remainder. This explains the zero primary spill
for consecutive inputs in the tested geometries; it is a useful workload property
rather than uniformly random placement.

Keeping the remainder raw is attractive if the bucket compares its complete
remainder in a single 8/16-bit SIMD lane. It is **not** automatically a good
fingerprint representation. With wide consecutive keys, the high eight remainder
bits are identical: the within-bucket fingerprint collision ratio is 100% at
width 64 here, versus about 0.39% for the control. An odd multiply of the stored
remainder repairs that example while preserving reconstruction, but adds work.
An exact-remainder candidate and a wide fingerprint-plus-suffix candidate should
therefore not inherit an identical hash policy without evidence.

## Disassembly and modeled dependency

The width-64 serial control's loop body is:

```asm
eor x11, x2, x8
eor x11, x11, x11, lsr #30
mul x11, x11, x9
eor x11, x11, x11, lsr #27
mul x11, x11, x10
eor x2, x11, x11, lsr #31
```

The affine loop is one input `EOR` and one `MUL`. The strong unbalanced variant
has two serial multiplications too, followed by bucket/remainder assembly. The
one's-complement body is `EOR; MUL; UMULH; ADDS; CINC`, with independent low/high
products. Strong three-round balanced Feistel needs six dependent 32-bit
multiplications across the rounds.

LLVM MCA 21.1.8 modeled the extracted width-64 bodies for Neoverse V2 and V3.
Both scheduling models returned the same numbers here:

| Body | Cycles for 100 dependent repetitions | Block reciprocal throughput |
|---|---:|---:|
| Affine | 303 | 0.5 cycles |
| Width-SplitMix | 803 | 1.0 cycles |
| One's-complement | 603 | 1.0 cycles |
| Strong unbalanced | 1,203 | 1.5 cycles |

These exclude loop control and setup. Repeating a body with its result register
as the next input exposes its recurrence; MCA's separate block throughput assumes
sufficient independent work. These are scheduling-model outputs, not native
Neoverse measurements, and do not prove equal V2/V3 hardware performance.

For explicit batch SIMD, up-to-32-bit arithmetic fits NEON/AVX2 lane multiplication.
Balanced Feistel's halfwords also fit those lane sizes, though it pays for rounds.
Full-width low multiplication needs decomposition on NEON/AVX2; AVX-512DQ provides
packed 64-bit low multiply. Full-product folding and one's-complement products
need high halves as well. These are implementation prospects, not measured SIMD
wins; independently scheduled scalar work should remain a comparison path.

## Recommendation and limits

1. Keep the width-preserving strong control in complete-map comparisons. It is
   the best general tradeoff in this screen; it is not a final security choice.
2. Try the strong unbalanced construction in the exact-remainder full-map path.
   Its longer hash dependency could be offset by reduced spill on important key
   distributions and a compact one-cacheline bucket. Only the full map can decide.
3. If cheap affine hashing remains compelling, make bounded seed reselection an
   explicit build/growth experiment: measure actual spill, retry only a declared
   number of times, then fall back to the strong policy. Charge every attempt and
   test subsequent churn. Do not choose a favorable seed outside the measured
   policy, and do not assume initial admission prevents future bad subsets.
4. Keep primary and secondary seed generation independent under the caller's
   entropy policy. None of these mixers is established here as a cryptographic
   PRP or adaptive hash-flood defense. A hidden affine seed is not a security proof;
   bucket observations, timing feedback and seed changes require an explicit
   attacker-observation contract if a guarantee is claimed.

Remaining experiments include more growth geometries/load levels, richer
structured subsets and insert orders, mutation pressure after admission,
explicit SIMD kernels and native target hardware. They are not reasons to defer
complete-map measurements: the useful candidate set and obvious bad defaults
are already distinguishable.

## Reproduction

From Linux, with outputs in ignored storage:

```sh
clang++-21 -std=c++23 -O3 -g -fno-vectorize -fno-slp-vectorize \
  -fno-fast-math -ffp-contract=off -march=armv8-a+simd \
  workbench/spikes/hashtable/reference/permutation_review.cpp \
  -o build/hashtable/permutation-review/permutation_review
taskset -c 7 build/hashtable/permutation-review/permutation_review quality 32 \
  > build/hashtable/permutation-review/quality.csv
taskset -c 7 build/hashtable/permutation-review/permutation_review timing 5 \
  > build/hashtable/permutation-review/timing.csv
llvm-objdump-21 --demangle --no-show-raw-insn -d \
  build/hashtable/permutation-review/permutation_review \
  > build/hashtable/permutation-review/disassembly.txt
```

The retained summary is from 43,008 quality rows and 1,920 timing rows. A separate
ASan/UBSan build passed inverse checks and the quality matrix with two seeds.
MCA used the extracted bodies described above, `-march=aarch64`, `-mcpu=neoverse-v2` or
`-mcpu=neoverse-v3`, and `-iterations=100`; local detailed outputs are under
`build/hashtable/permutation-review/`.

## Further one-multiply screen: rejected

[permutation_candidates.cpp](permutation_candidates.cpp) tests PCG RXS-M-XS,
fixed midpoint right-XOR/multiply/right-XOR, and right-XOR/multiply/left-XOR
against the existing two-multiply control. The PCG input is XORed with the
caller seed; this wrapper is our hypothesis, not a PCG hash-family guarantee.
The [reference output permutation and inverse](https://github.com/imneme/pcg-cpp/blob/master/include/pcg_random.hpp)
establish reversibility, which does not establish distribution on subsets.

The expanded screen uses 64 entropy words, all eight key widths, and 15 patterns.
These include consecutive/strided/Gray keys and public-seed inverse images
constructed against **both** PCG and the strong control. Primary capacity is
16 slots at 95% total occupancy, with `a=min(10,W/2-2)` and secondary
`b=max(1,a-3)`. Only actual primary spills enter the secondary. The
[retained summary and worst-case witnesses](evidence/rejected-permutations/summary.json)
cover 38,400 rows; they are deterministic logical counts, not measured timings.

| Width | Strong maximum chained / all keys | PCG maximum chained / all keys |
| ---: | ---: | ---: |
| 16 | 0.823% | 5.556% |
| 32 | 0.154% | 3.219% |
| 64 | 0.135% | 19.217% |

Ordinary keys spaced 256 apart cause the decisive 32/64-bit PCG failures.
At 64 bits one seed sends 5,039 of 15,564 keys to secondary and 2,991 to chains.
Other one-multiply candidates fare worse. A final fixed right-XOR cannot repair
top-bit bucket loads: it only bijectively relabels their existing indices.

PCG's arithmetic was faster on the local Apple VM, but Neoverse V2 MCA modeled
the same eight-cycle dependency as the strong control. Neither observation
rescues its distribution. The complete-map PCG arm was removed before native
layout capture. None of these one-multiply families is selected.

The 8-bit control also has a finite-domain limitation: in this ensemble, a public
seed-zero inverse-image pattern meets an entropy word whose low byte is zero.
That is the 1/256 exposure of truncating input-XOR seeding to eight bits, rather than
evidence of broad concentration under unrelated seeds. The current mixer is
still an experimental noncryptographic control.

Reproduce with the same compile flags above, substituting
`permutation_candidates.cpp`; run `quality 64` and `timing 7`.
Inverse identities exhaust 8/16-bit domains and sample 65,536 keys at wider widths
for four seeds. ASan/UBSan also passed all 15 pattern constructors and one-seed
placement histories. This screen covers one geometry per width; it is not a
concentration bound or a claim of adaptive attack resistance.

## Reusing the bucket product for fingerprints: rejected

A bounded review of the solver-driven table considered storing
`t = lowR((r XOR bucket_seed) * M)` as the reversible remainder while leaving
bucket placement unchanged, with `M=0x79d5f9e0de1e8cf5`. Its top bits would become
the fingerprint. The odd multiplier gives a correct inverse and would remove
the independent selector product. Conditional fingerprint quality rejects it.

At K40/A8, choose `r_i=(i<<2)|(i<<6)` for i=0..11 and include every low key byte
for each remainder: `key=(r_i<<8)|low_byte`. Every bucket contains exactly twelve
keys under every seed. Yet `(0x44*M) mod 2^32 = 0x001d7114`: the products can stay
close within fingerprint bins even after XOR seeding. Across the driver's first
32 entropy words, mean extra suffix comparisons per hit were 0.771 versus 0.0104
for the independent selector; worst-seed means were 2.50 versus 0.333. Missing
cohorts i=12..15 averaged 0.781 versus 0.0313 candidates per miss, with worst-seed
means 4.50 versus 1.00. One seed puts the twelve residents into two fingerprints,
six entries each. Inputs require no seed knowledge and cause no bucket overflow.

The peer review verified inversion and unchanged placement on 49,152 keys across
16 seeds. These are logical comparison counts, not timings. The construction was
rejected without changing the container or launching a new native hash campaign.
