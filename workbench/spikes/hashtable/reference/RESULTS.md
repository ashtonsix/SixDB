# Complete-table comparisons

The current wide-key prototype is below the required performance bar. The
historical quotient-map wins reproduce, but vary substantially by platform and
value width. Those are useful reference points to improve on, not evidence that
the new map is competitive yet.

## Native reproduction of the historical map

All cells below are historical map / pinned Abseil, median nanoseconds per
operation. These cases contain 4096 distinct 16-bit keys with 16-bit values,
512 primary buckets and 16 slots per bucket. There are seven balanced-order
trials, identical inputs and immediate matched warmup. Abseil uses its ordinary
typed hasher and equality. The copied historical body is unchanged.

| Machine / ISA | Independent hit | Dependent hit | Miss | Update | Erase/insert pair |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen 5 / AVX2 | 1.149 / 1.116 | 5.558 / 6.077 | 0.974 / 1.475 | 1.316 / 1.815 | 6.765 / 17.831 |
| Zen 5 / AVX-512 | 1.104 / 1.140 | 5.998 / 6.529 | 0.955 / 1.498 | 1.340 / 1.842 | 6.988 / 17.979 |
| Granite Rapids / AVX2 | 1.283 / 1.300 | 7.293 / 8.028 | 1.133 / 1.882 | 1.414 / 2.462 | 8.444 / 25.589 |
| Granite Rapids / AVX-512 | 1.262 / 1.278 | 8.099 / 8.737 | 1.130 / 1.913 | 1.435 / 2.285 | 8.669 / 25.936 |
| Neoverse V2 / NEON | 2.120 / 1.986 | 13.599 / 9.862 | 2.026 / 2.476 | 4.461 / 2.711 | 11.532 / 32.516 |

The wider-value results are retained too: the 32-bit-value historical layout
loses independent-hit throughput on both x86 machines, while 64-bit values
retain some dependent-latency wins. ARM dependent latency and updates need work.
ISA labels describe compiler exposure; the unchanged historical x86 kernel uses
its existing SSE2 comparison body, not a newly authored AVX-512 search kernel.

Sources and native receipts are in
[NEON](evidence/reproduction-bound-neon/artifact.json),
[Zen 5](evidence/reproduction-bound-zen5/artifact.json), and
[Granite Rapids](evidence/reproduction-bound-granite/artifact.json).
The frozen source digest is
`5b87acc0940c3d0d6a26f85dfd0a3866b5105894f6ffce61454950eeac1813f7`.
[Prior-art provenance and the driver](prior_art/README.md) explain the exact
workload. `report.py` regenerates paired medians from retained CSV files.

These historical update values are precomputed and include unchanged repeats;
churn is warmed repeated turnover of the entire population. They are different
workloads from the word-map driver's fresh single pass. Its query stream is also
different. Compare each candidate with its paired Abseil result; do not subtract
timings across the two drivers. The historical serial consumer follows only the
low 16 bits of a value; Clang can narrow the wider-value load accordingly in both
arms. Current new-map serial measurements additionally consume a full-value
checksum. No cache-residence label follows merely from cardinality, and modeled
historical payload bytes are not equivalent to measured allocator requests.

## Wide-key composition cost

At 4096 64-bit keys and 64-bit values, the frozen N16 row consumer which binds its
layout family outside lookup still loses substantially. These are complete-map
measurements with fresh-seed repetitions, not isolated fingerprint kernels.

| Machine / ISA | Packed dependent / Abseil | Packed independent / Abseil | Packed miss / Abseil |
| --- | ---: | ---: | ---: |
| Zen 5 / AVX2 | 10.551 / 6.452 | 4.536 / 2.376 | 2.240 / 2.616 |
| Zen 5 / AVX-512 | 10.834 / 6.917 | 4.602 / 2.474 | 2.286 / 2.348 |
| Granite Rapids / AVX2 | 13.708 / 8.591 | 5.919 / 2.890 | 2.747 / 3.130 |
| Granite Rapids / AVX-512 | 14.447 / 9.271 | 6.023 / 2.858 | 2.718 / 3.056 |
| Neoverse V2 / NEON | 16.181 / 9.657 | 6.502 / 2.395 | 2.523 / 2.145 |

On Neoverse V2, the direct row control takes 15.277 ns dependent / 4.510 ns
independent. Its suffix loads eight bytes inside the complete initialized row,
while the generic TuplePack projection loads exactly six: this is not an
identical-access-window comparison. Both preserve the same wire representation.

The selected primary-hit assembly for the frozen packed row consumer has 91
instructions, 24 load instructions and 17 branches; the direct path has
56/14/5. LLVM-MCA's Neoverse V2 model reports 39.16 versus 37.13 cycles for the
repeated dependent trace, and 14.2 versus 8.3 reciprocal cycles for the independent
trace. These models omit cache misses and branch prediction. They identify hot
dispatch and metadata work, not measured end-to-end cycle costs.

Two concrete changes are now being measured in the live source:

- SeriesPack's dense Local<8> NEON packet can load the same 16-byte extent once,
  replacing two eight-byte loads and a lane combine. Strided or residual-bearing
  tiles retain their existing loads.
- TuplePack's native author endpoint can bind a fixed transfer width once.
  Complete-byte values also admit shift zero and a full mask outside lookup.
  This removes repeated generic decisions while retaining the primitive's
  bounded transfer. Writes use the same validated full-value facts.

The narrow-key consumer now admits exact-remainder families, avoiding a suffix
comparison and unnecessary lane-mask conversion. It measures compact stride and
64-byte bucket families separately, including the historical bucket counts.
The cheap seeded affine permutation remains a diagnostic control: structured-key
seed-ensemble failures prevent accepting it as a general default merely because
its timing is attractive. The width-preserving two-multiply candidate remains a
stronger distribution control with a material scalar dependency cost.

## Complete composed values and narrow keys

The next capture binds full-value transfers and separates mutation overflow work
from primary hit/update paths. It adds exact-remainder kernels and a 64-byte
bucket family. Evidence is in `evidence/composed-values-{neon,zen5,granite}`;
each directory includes raw paired trials, source identity and the full artifact
reference. Native complete-map, width/domain and TuplePack transfer checks pass
on all five ISA exposures.

The table shows the **cheap affine diagnostic**, 4096 16-bit keys, 64-bit values,
six slots per 64-byte primary bucket. Each cell is candidate / Abseil median
nanoseconds. Values are consumed in full, including in dependent lookup. Seven
trials vary the candidate seed and balance arm order.

| Platform | Dependent hit | Independent hit | Miss | Update | Erase + insert |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen 5 / AVX2 | 7.478 / 8.238 | 1.584 / 2.666 | 1.257 / 2.573 | 2.537 / 2.659 | 14.619 / 18.403 |
| Zen 5 / AVX-512 | 7.928 / 8.678 | 1.489 / 2.716 | 1.177 / 2.757 | 2.603 / 2.727 | 14.602 / 18.523 |
| Granite Rapids / AVX2 | 9.690 / 10.153 | 1.931 / 3.068 | 1.851 / 3.214 | 3.424 / 3.207 | 17.084 / 20.772 |
| Granite Rapids / AVX-512 | 10.427 / 10.937 | 1.829 / 3.089 | 1.575 / 3.201 | 3.458 / 3.260 | 17.870 / 20.816 |
| Neoverse V2 / NEON | 10.145 / 10.573 | 2.214 / 2.465 | 1.498 / 2.155 | 4.273 / 4.422 | 20.619 / 32.428 |

This recovers useful behavior from the earlier quotient design. It does not
establish a generally acceptable table: the affine family has severe structured
seed tails in the [permutation screen](permutation_review.md). With the stronger
mixer, this same value-width case loses dependent lookup by about 11–25%, though
independent lookup still wins on Zen 5. The 16/32-bit value cases lose most read
comparisons with that mixer. Cheap-permutation churn wins across all tested
value widths and machines; update wins are narrower.

The inherited low-occupancy shape is also not a memory win. At 16/32/64-bit values,
the 64-byte family requests 36864/73728/147456 payload bytes, versus Abseil's
40952/73696/139184; candidate controls add 2040 bytes versus 16 for the ordinary
Abseil object. These are allocation requests, not measured RSS. Fuller exact-key
buckets and their actual overflow costs are now being compared: preserving the
old capacity choices would preserve an avoidable weakness.

The wide-key path remains poor. For example, Zen 5 AVX2 at 4096 keys now takes
10.121 / 6.476 ns dependent and 4.202 / 2.364 ns independent. These gains over the
previous candidate are too small. The next comparison admits the same eight-byte
initialized row read on both packed and direct paths, removing their unequal
suffix windows, and measures real Loom staging against an Abseil public-prefetch
control. Neither layout selection nor hash policy is settled by these results.

## Loom experiment coverage

`ring.cpp` follows C=1,8,32 dependent key/value chains through the complete map.
It compares ordinary Abseil, Abseil public prefetch, packed/direct lookup,
manual stages and actual Loom Ring stages, with matched hints/no-hints. Every
chain has its own accumulator. Results consume full values and validate terminal
keys and found counts. Setup, enqueue and drain are charged; repeated complete
operations give at least 262144 lookups per sample. Immediate matched warmup and
balanced order apply to every arm.

The same qualified metadata and candidate-row windows drive manual and Ring
hints. Hardware discovery and service shutdown finish before timing; a fixed
policy snapshot supplies the hint code. A one-byte demand authorizes each hinted
address, and subsequent line boundaries remain inside the actual row. No spatial
prefetch assumption authorizes access to another line. The native runner retains
full Loom hardware provenance and checks its context against each execution.

The first Ring driver measures successful chains. Secondary and overflow work is
complete but synchronous; their populations are reported. All maps and the input
vector remain allocated during each comparison, so their reported sizes describe
allocated storage rather than cache residence. Timed traversal reads C initial
keys and thereafter follows values. Width checks separately cover staged misses,
all overflow tiers, fingerprint255 at partial occupancy, deletion and clear.

The first native Ring matrix is retained under
`evidence/ring-bound-row-{neon,zen5,granite}`. This capture uses N16, the strong
width-preserving mixer and an admitted eight-byte row suffix read on both packed
and direct paths. All native width/staged checks pass. At 4,194,304 keys and 32
independent dependent chains, median ns/lookup is:

| Platform | Abseil | Abseil + public prefetch | Manual stages + hints | Ring + hints |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 / AVX2 | 16.21 | 16.55 | 11.98 | 15.18 |
| Zen 5 / AVX-512 | 16.05 | 16.40 | 11.87 | 15.08 |
| Granite Rapids / AVX2 | 16.31 | 14.57 | 12.35 | 17.41 |
| Granite Rapids / AVX-512 | 17.03 | 16.29 | 13.05 | 17.85 |
| Neoverse V2 / NEON | 17.56 | 14.29 | 13.55 | 21.09 |

Manual staging wins these cells, but actual Ring wins only the Zen 5 cells and
by a thin margin. This is not the requested broad throughput advantage. On the
same cardinality's single dependent chain, ordinary packed lookup is roughly
level on Zen 5, 6% slower on Neoverse V2 and 15–21% slower on Granite Rapids.
Small-footprint lookup remains well behind Abseil. These are cardinality and
allocation-footprint comparisons; the shared reported LLC size and effective
residence need separate qualification before calling every platform's cell DRAM.

The eight-byte row admission removes the material packed/direct read difference
in these native consumers. The remaining losses therefore implicate table/hash
organization and scheduling as well as particular kernels. Loom has the actual
manual/Ring evidence for review. The current follow-up compares N8 and N16 plus a
single-stage whole-bucket prefetch, charging its extra hinted lines and reducing
Ring visits from two to one. It remains an experiment.

The layout grammar now distinguishes logical and physical fingerprint width,
including byte-wide storage with an unused sentinel for exact narrow keys.
Structural pruning keeps each capacity and physical fingerprint family: it
cannot discard them before accounting for power-of-two bucket counts, occupancy
and overflow. Its current hit-line metric still uses uniform reserved ranks;
dense-prefix live-rank distributions and actual native windows must replace that
model for measured selection. No structural frontier is a selected preset.

A separate narrow-key experiment precomputes the one-round Feistel bucket
function for a small remainder domain. Both 512-byte arrays and construction are
charged even when a secondary geometry uses the arithmetic fallback. Exhaustive
inverse checks cover every 16-bit key at every address width; full-domain map
histories pass under ASan/UBSan. Local complete-map screening shows better miss or
independent throughput in some shapes, but has not repaired dependent-latency
parity. It is neither a chosen hash policy nor a security claim.

## Layout and ordinary-prefetch follow-up

The next native capture, `3a557f65f05cd9bd798d37315c237af685521665f1a77b0eaaef9e02536471d1`,
holds the strong mixer fixed. `layout-native.sh` compares complete operations
against the same pinned Abseil at 4096, 1,048,576 and 4,194,304 keys, including:

- Primary metadata transposed ahead of entry rows in the same Orbital allocation.
  It uses identical bytes, capacities, seed and overflow rules to the inline
  layout. Local screening improved misses but worsened dependent hits; no choice
  follows from that host. For `metadata_rows`, the legacy `primary_stride` CSV
  field is bytes per logical bucket: metadata itself has 16/24-byte stride and
  entries have the remaining stride in a separate region.
- An eight-slot primary with suffixes before a 64-byte-aligned value plane.
  With at least eight address bits it occupies 128 bytes: keys in one 64-byte
  line, values in the next. Smaller tables require 192 bytes. Secondary uses
  compact suffix/value planes, with the same byte extent as the ordinary rows.
  This is a complete layout-family comparison, not an isolated change to primary
  placement. Both physical extent and total requested storage are charged.
- Ordinary get with no hint, a first-payload hint, or hints covering the primary
  bucket. These are fixed native L1 read-hint controls, not a production heat or
  pressure policy. The all-bucket control emits 64-byte interval targets plus the
  last byte; this can repeat a line even on 64-byte-line machines. The live source
  now emits actual 64-byte boundaries once each. That corrected arm needs a new
  capture before selecting from its timings; it still pays duplicates on 128-byte
  hardware. Other arms in this capture are unaffected.
- Nine through 16 logical slots using one 16-lane SeriesPack fingerprint group.
  SIMD group width does not require 16 occupied slots. Actual pow2 bucket count,
  primary/secondary populations and overflow bytes remain part of every comparison.

This prefetch comparison closes an omitted design alternative: pinned Abseil's
[ordinary `find_large`](https://github.com/abseil/abseil-cpp/blob/2065f4ded0558c6f89fee67c8e5228feb4eb960e/absl/container/internal/raw_hash_set.h)
already requests payload before control matching, independently of public
`prefetch()`. Source inspection of F14 and Boost also found deliberate early
payload requests. The previous no-hint candidate was a weaker algorithmic choice;
the comparison does not justify removing Abseil's optimization.

Oracle histories, complete-state checks, staged misses/all tiers and guarded
suffix-word windows pass locally and on all five native ISA/platform targets.
Measurements are retained under `evidence/layout-hints-{neon,zen5,granite}`.
They have not established broad advantage or chosen a layout. Loom owns the
separate matched Ring implementation experiment.

The cold layout lowering now preserves whole fields in row-relative descriptions
instead of treating every 64-byte boundary as a storage cut. Repeated rows share
the same description. All 4158 ownership/reconstruction cases pass, including
an explicit check that every byte-aligned eight-byte value remains one projection.
Native preparation/window verification and measured solver selection are still
required; this fixes a lowering artifact without claiming a new performance win.

The native lowering bridge now prepares actual shared TuplePack descriptions
from solver output. Its 57 representative cases cover 347 shared descriptions,
559 physical unit instances and 1627 projections; release and ASan/UBSan pass
3254 guarded reads and 9762 guarded writes, complete reconstruction and preserved
neighbor checks. This is native admission evidence, not complete-container
performance evidence.

The completed layout campaign calls for a broader reassessment. Hot wide-key
lookup remains behind Abseil across these targets. Some large-footprint read
cells win, but fill and churn remain substantial losses. A memory-saving N10
table also changes from approximately 0.13% chained keys when fresh to 7.97%
after one turnover. The power-of-two cardinalities repeatedly leave N16 near
50% primary occupancy and do not cover the configured policy's high edge.
[The current direction](DIRECTION.md) makes these lifecycle, geometry and
composition questions explicit before further local tuning.

## Matched historical trace and sustained occupancy

The reassessment capture is
`041ed16634afded21fde41eddefdbc267428eca4877187328a76f18a882e7050`.
All five native ISA/platform targets completed both drivers and their checks.
The retained comparisons are [Neoverse V2](evidence/reassessment-neon),
[Zen5](evidence/reassessment-zen5) and [Granite Rapids](evidence/reassessment-granite).
These are cardinality/footprint experiments; reported cache capacity alone does
not establish L1/L2/LLC/DRAM residence.

`historical_bridge.cpp` supplies identical shuffled inputs and full-width values
to unchanged historical code, the current strong-mixer exact-remainder family,
and ordinary typed Abseil. Every value bit feeds the serial recurrence; native
NEON disassembly confirms full V64 loads in all arms. Six repetitions balance
all six arm orders. The table gives median ns/operation, ordered **historical /
current / Abseil**; turnover is one erase+insert pair.

| Target | K16/V16 serial | K16/V64 serial | K16/V16 sustained turnover |
| --- | ---: | ---: | ---: |
| Neoverse V2 NEON | 13.77 / 13.74 / 9.96 | 15.18 / 13.80 / 11.01 | 12.14 / 20.61 / 35.72 |
| Zen5 AVX2 | 5.85 / 8.13 / 6.15 | 8.19 / 10.68 / 8.99 | 7.62 / 12.53 / 20.20 |
| Zen5 AVX-512 | 6.23 / 8.48 / 6.51 | 8.53 / 11.01 / 9.40 | 7.61 / 12.71 / 19.98 |
| Granite Rapids AVX2 | 7.87 / 10.74 / 8.15 | 10.50 / 13.59 / 11.15 | 9.88 / 13.87 / 27.95 |
| Granite Rapids AVX-512 | 8.56 / 11.71 / 8.93 | 11.34 / 14.47 / 11.75 | 10.46 / 14.64 / 28.68 |

The older x86 serial advantage survives the stricter trace. The current map
preserves a substantial churn advantage over ordinary Abseil, but loses much
of the historical advantage elsewhere: on Zen5 AVX2, K16/V16 independent hit
is 1.23 / 1.89 / 1.22 ns and changed-value update is 1.22 / 2.80 / 1.72 ns.
Neither historical nor current serial lookup reaches parity on V2. Hash policy,
layout, primitive lowering and maintenance all differ; this comparison cannot
attribute the difference solely to hashing. Historical memory remains modeled
with incomplete dynamic controls. Fixed-bank turnover measures repeated reuse,
not independent fresh arrivals.

`lifecycle.cpp` instead uses fresh disjoint key generations for eight turnovers,
three seeds, N10/N16 and sizes on either side of reserve thresholds. At 65,536
primary buckets, N10 with 524,288 live keys changes from 0.13% in chains when
fresh to 7.99% after one turnover, then 5.21% after eight. N16 with 838,860 live
keys changes from 0% to 2.82%, then 1.54%. Primary occupancy after eight turnovers
is 87.87% and 91.90% respectively. These logical populations match across ISAs.

Abseil also changes with history. At 838,860 live keys it grows from 1,048,575 to
2,097,151 slots during the first turnover although logical size stays constant.
Its requested allocation rises from 17,825,720 to 35,651,592 bytes, with a
53,477,312-byte allocator peak. The candidate's requested bytes plus controls
are about 48.5% of that aged baseline. At 838,861 keys the candidate's own reserve
rule immediately doubles primary storage: it costs about 182% of fresh Abseil
and 91% after Abseil's turnover growth. One extra entry reverses the fresh memory
comparison. Neither library's reserve policy can be ignored in layout selection.

After eight turnovers at 838,860 live keys, complete read medians are:

| Target | Independent hit: current / Abseil | Serial hit: current / Abseil |
| --- | ---: | ---: |
| Neoverse V2 NEON | 17.28 / 14.87 | 138.73 / 120.55 |
| Zen5 AVX2 | 8.29 / 11.85 | 28.75 / 40.68 |
| Zen5 AVX-512 | 8.12 / 11.64 | 29.23 / 52.02 |
| Granite Rapids AVX2 | 16.86 / 7.53 | 72.54 / 50.45 |
| Granite Rapids AVX-512 | 16.63 / 7.71 | 73.63 / 50.64 |

The Zen5 aged-read win is useful evidence of a regime the previous sweep missed;
it is not broad competitiveness or a reason to keep the current organization.
N10 at 524,288 keys remains slower after aging even on Zen5 AVX2: 9.63 versus
4.45 ns per independent hit. The same-seed direct controls and all repetitions
are retained, including wins and losses.

Same-size rebuilding reduces spill, but its cost and Abseil's recovery must
also count. Construction/reinsertion and retirement are timed separately;
old/new coexistence is recorded outside both intervals. No transient candidate
RSS/allocation peak or retained-version cost is claimed. This driver still uses
scratch effects and aborts on allocation failure. It does not establish a
maintenance interval, snapshot policy or production configuration.
## Nonstandard keys and many small tables

`domain_compare.cpp` broadens the frozen comparator to K24 sets, K40/V16 maps
and K64/V64 maps. It runs 256 or 4,096 entries in one table and 4,096 entries
spread across 256 tables of 16. Each case has eight repetitions with rotated
arm order, 128 read passes and a separate whole-workload warmup. Read inputs,
values, shuffled updates and erase/insert requests are identical across arms.
The mixed serial trace depends on membership and returned value bytes, with a
checked single cycle. Its query-source load and table-pointer lookup are charged
equally; these times are not isolated lookup latency. Input storage is reported.

Abseil uses ordinary default-allocator objects and default hashing in both arms:
native integer keys and exact-byte unsigned keys. The latter uses `AbslHashValue`
on the logical integer, preserving the pinned library's default hash/seed fusion.
There is no custom transparent hasher in these timings. Requested allocations
are inspected outside timing through Abseil's debug hook, cross-checked against
counted map/set allocations at empty, reserve, fill and clear boundaries.
Instrumenting every timed table with its own counters would inflate precisely
the small-table footprint under investigation; this comparison avoids it.

All three native jobs completed successfully from capture
`00173f3f791a0d0d5d2606d6d6125c38f2e2549f162ff9a785a800a399020ce0`:

| Host | Job | Retained evidence |
| --- | --- | --- |
| Neoverse V2, NEON | `20261013T033438Z-cb796c42` | [domain-neon](evidence/domain-neon/provenance.json) |
| Zen5, AVX2 / AVX-512 | `20261013T033444Z-cd1997df` | [domain-zen5](evidence/domain-zen5/provenance.json) |
| Granite Rapids, AVX2 / AVX-512 | `20261013T033450Z-24d334ea` | [domain-granite](evidence/domain-granite/provenance.json) |

Both new drivers pass local release and ASan/UBSan checks, and native release
checks pass on all five target combinations. The archives include disassembly,
binaries and compile receipts. NEON inspection finds fully inlined Abseil lookup
and candidate primary paths; candidate downstream lookup remains a call. The
K24/K40 generic composition still carries variable transfer handling that the
admitted K64 word path has removed. Static function length is not a dynamic
instruction count or a cost attribution.

For **one K40/V16 table with 4,096 entries**, median times in ns are:

| Host / ISA | Independent hit: composed / direct / exact Abseil | Mixed serial: composed / direct / exact Abseil | Shuffled erase+insert: composed / direct / exact Abseil |
| --- | --- | --- | --- |
| V2 NEON | 8.86 / 7.40 / 3.37 | 20.13 / 20.90 / 16.30 | 31.71 / 24.51 / 30.24 |
| Zen5 AVX2 | 7.10 / 6.28 / 2.29 | 14.55 / 14.11 / 8.69 | 22.38 / 19.04 / 16.94 |
| Granite AVX2 | 8.64 / 7.62 / 2.73 | 21.78 / 20.91 / 15.36 | 27.51 / 22.84 / 21.25 |

The storage saving is real but smaller against the stronger baseline. On x86,
requested bytes plus table/control/catalog bytes are **59,288** for the candidate,
**73,720** for exact-byte Abseil and **139,208** for native-key Abseil. That is about
20% saved against the compact baseline, not the 57% suggested by native keys alone.
These are current live allocations, not allocator rounding or process RSS.

The many-small-table result changes the architecture priority. On x86:

| Domain, 256 × 16 entries | Candidate bytes | Exact-byte Abseil bytes |
| --- | ---: | ---: |
| K24 set | 592,896 | 36,352 |
| K40/V16 map | 655,360 | 69,632 |
| K64/V64 map | 802,912 | 122,880 |

The candidate embeds **2,072 bytes per table** before payload/catalog storage;
ordinary Abseil's table object is 16 bytes. Shared row controls are counted only
once per collection. The current direct control deliberately retains the same
prepared state, so this overhead is a property of the comparator's organization,
not an irreducible TuplePack cost. At 16 live keys it also allocates two primary
and two secondary buckets even though no primary bucket can overflow its
16-slot capacity. The requested single-bucket form and shared preparation are
therefore substantive missing cases, not optional small adjustments.

K24 sets also lose independent reads across these targets; the composed/direct
gap depends on operation and footprint. K64's admitted read path retains near
composed/direct parity, while both lose to Abseil here. Several direct mutation
controls beat Abseil, particularly on V2 and on small collections, but the
composed writer often gives those gains back. Both positive and negative rows
are retained. AVX-512 does not change these conclusions in this bounded panel.

This panel qualifies neither DRAM residence nor a solver layout selection. It
does not isolate hashing from table organization. Its decision is narrower:
shared immutable preparation and a real single-bucket material belong in the
next implementation; exact-byte Abseil remains a required comparison; and the
solver binding must not be treated as a sufficient explanation or cure for the
remaining algorithmic read gap. Full chains, MVCC, worker modes and cold Ring
execution remain required alongside that next consumer.


## Strong-permutation full-key bridge

The current strong K40 permutation also slows a complete full-key Abseil map.
This rejects the idea that primitive packing is the sole cause of the lookup
gap. It does not turn hash and table costs into additive timing components.

At 4,096 K40/V16 entries, medians of ten rotated repetitions in ns/query are:

| Platform | Ordinary exact-key Abseil | Custom ordinary hash | Embedded permutation | Current direct candidate |
| --- | ---: | ---: | ---: | ---: |
| V2 NEON, independent hit | 3.37 | 3.27 | 4.81 | 7.35 |
| Zen5 AVX2, independent hit | 2.28 | 2.23 | 4.45 | 6.30 |
| Granite AVX2, independent hit | 2.69 | 2.63 | 7.29 | 7.57 |
| V2 NEON, dependent mixed | 15.96 | 15.56 | 18.74 | 20.87 |
| Zen5 AVX2, dependent mixed | 8.65 | 8.14 | 10.20 | 14.11 |
| Granite AVX2, dependent mixed | 15.13 | 14.67 | 18.05 | 20.55 |

The custom ordinary arm retains Abseil's integer hash behind the custom-hasher
interface. The embedded arm uses exactly the candidate's primary permutation
and seed, then places 33 output bits in Abseil's address positions and 7 in its
fingerprint positions. That lossless embedding, custom-path seed XOR and larger
hasher object are charged. Final placement and hash instructions are therefore
not identical between candidate and Abseil. The default/custom comparison also
changes placement; it is not a pure measurement of seed-fusion overhead.

Abseil debug probe counts remain similar: fresh successful queries average
0.0104/0.0100/0.0096 for default/custom/embedded on V2, and approximately
0.0044/0.0051/0.0045 on Zen5 AVX2. The hook counts failed fingerprint candidates
and group advances, not cache misses or groups visited. Aged miss counts grow
for all three. The archived disassembly confirms a runtime fingerprint and
inlined permutation. The result merits revisiting the quotient construction
alongside table organization; it does not select a cheaper unqualified hash.

A first adapter incorrectly returned the 40-bit result directly into a library
whose fingerprint consumes bits 57..63. Disassembly caught the resulting zero
fingerprint. Those jobs were cancelled and their capture is excluded. The
corrected adapter checks every output basis bit against the pinned library's
actual H1/H2 functions and bounds capacity below 2^33. Release, ASan/UBSan and
native checks pass. All paired operation checksums match.

Corrected capture: `840ba2076a7f504ea17b520516dcd73fc56807247daf796285aef1e7b2b4a88b`.
Evidence: [V2](evidence/hash-bridge-neon), [Zen5](evidence/hash-bridge-zen5),
[Granite](evidence/hash-bridge-granite), including both x86 ISA builds, probe
histograms, repetitions and hardware/compiler receipts. These are warmed
application traces with query-source loads, not established cache residence or
isolated lookup latency. Very short destruction intervals are retained but do
not support a performance ranking.

## Solver-driven single-bucket material

A real single bucket changes the result substantially for small maps. The solver
now admits a `single` topology: full insertion requests growth rather than
storing descendants. It supplies all field offsets and strides to complete
SeriesPack/TuplePack operations. The owner object is 32 bytes of Orbital
Allocation state, versus 2,072 bytes in the frozen comparator. Shared preparation
is 864 bytes for the K24 set or 1,296 bytes for the two map cases, once per geometry.
Row and plane layouts coincide for the set, which has no value plane.

The first native panel compared 256 tables with 8/16 entries in 16-slot storage,
and 16/32 entries in 32-slot storage. Review then prompted a second panel with
**distinct key sets for each table** at 16/32 entries. This removes repeated
selector patterns shared across tables in the first panel. Both preserve common
logical inputs between candidate and baseline, full-value/membership-dependent
mixed reads, globally shuffled mutations, six rotated arm orders and normal
Abseil objects. This is a complete-family comparison: it changes topology and
selector work as well as preparation. It does not isolate those costs.

For distinct key sets, 16 entries per table, medians in ns/operation are shown as
composed row / exact-key Abseil. One churn operation is an erase/insert pair.

| Operation and domain | V2 NEON | Zen5 AVX2 | Granite AVX2 |
| --- | ---: | ---: | ---: |
| K40/V16 independent hit | 2.90 / 3.71 | 1.99 / 2.74 | 2.31 / 3.28 |
| K40/V16 dependent mixed | 12.93 / 17.96 | 8.63 / 9.93 | 13.77 / 17.48 |
| K40/V16 replacement | 3.33 / 5.82 | 2.26 / 3.38 | 3.09 / 4.09 |
| K40/V16 churn pair | 13.40 / 38.18 | 8.31 / 23.04 | 9.16 / 26.95 |
| K40/V16 construct/fill, per entry | 26.16 / 16.99 | 18.82 / 10.13 | 19.13 / 12.06 |
| K24 set independent hit | 2.16 / 3.49 | 1.54 / 2.43 | 1.81 / 2.89 |
| K24 set dependent mixed | 12.37 / 11.49 | 7.16 / 4.45 | 11.70 / 11.47 |
| K64/V64 independent hit | 3.82 / 3.97 | 2.67 / 2.65 | 3.30 / 3.22 |
| K64/V64 dependent mixed | 14.64 / 17.39 | 9.48 / 10.70 | 15.38 / 19.55 |

The memory reduction survives charging shared preparation and ordinary table
objects. For 256 full 16-slot tables on x86, requested payload + owner objects +
shared controls are 21,600 / 38,416 / 75,280 bytes for K24 set / K40/V16 / K64/V64,
versus 36,352 / 69,632 / 122,880 for exact-key Abseil. Orbital backing is separately
16,384 / 32,768 / 81,920 bytes; pool metadata is also reported. Abseil allocator
backing and allocator-private metadata are unavailable here. These are not RSS
measurements, and requested-plus-control percentages must not be called RSS savings.

There are still consequential losses. Construction is slower throughout this
panel. Set dependent latency on Zen5 misses the target substantially. The current
32-slot execution scans two 16-lane groups with an early-exit path; full 32-slot
writes and several read cases regress badly. On Granite AVX2, K40/V16 independent
hits become 5.45 ns against 3.31 for Abseil, and replacement 9.16 against 4.20.
This rejects that executed family as a general small-table winner; it does not
prove 32-slot buckets are inherently poor or select their multi-bucket use.
Most 16-slot composed/direct mutation differences are small, while some read
composition gaps remain. The solver is finally driving a real consumer, but the
read-latency, construction and lifecycle questions remain live.

Checks cover all eight key widths, all emitted row/plane 16/32 shapes, guarded
allocation ends, forced identical selectors, full-width values, dense deletion,
iteration, full-bucket duplicate replacement and unchanged grow-required. Cold
admission verifies TP read/write windows; precise and whole-bucket damage
admission preserve pre-existing effect state on refusal. Native timings use
scratch effects; they do not establish MVCC costs. The later expanded check
matrix passes release and ASan/UBSan in the shared workspace.

Common-key capture digest:
`9e38863a6cbb77ca40839a3c7bfce2ba9eb8910c07934cb09e40b752f90e6474`.
Evidence: [V2](evidence/single-neon), [Zen5](evidence/single-zen5),
[Granite](evidence/single-granite).
Distinct-key capture digest:
`f5bad69e3b97b8afcd302dcb71457ccf6bd4a671d69ba8cf321ff80441ca734f`.
Evidence: [V2](evidence/single-distinct-neon), [Zen5](evidence/single-distinct-zen5),
[Granite](evidence/single-distinct-granite), including both x86 ISA builds and
all repetitions. Every paired operation checksum matches. Both panels use
warmed application traces without establishing physical cache residence.

## Single-bucket ownership and publication

`single_lifecycle.cpp` compares the packed body, its identical-storage direct
control and ordinary exact-key Abseil through the same real Orbital ObjectStore.
Each object begins with 16 K40/V16 entries. The first measured publication adds
entry 17, changing packed storage from N16 to N32; Abseil's existing capacity
already accommodates that entry. The second publication replaces a value in a
private copy. A live original Scope discovers and verifies its original contents
after both publications. Initial population is outside the measured intervals;
private replacement construction and publication are inside them.

Six rotated repetitions, medians in ns/object, composed / Abseil:

| Operation | V2 NEON | Zen5 AVX2 | Granite AVX2 |
| --- | ---: | ---: | ---: |
| Growth + publication, 256 objects | 534.60 / 318.78 | 327.62 / 220.78 | 416.61 / 270.41 |
| Replacement + publication, 256 objects | 384.66 / 311.78 | 240.00 / 215.69 | 315.84 / 263.02 |
| Growth + publication, 4,096 objects | 551.35 / 381.63 | 331.92 / 259.00 | 423.46 / 337.68 |
| Replacement + publication, 4,096 objects | 408.00 / 388.13 | 247.63 / 263.51 | 328.97 / 342.86 |

The direct control is close to the composed path in these cases. This is a
growth/ownership loss that an improved TuplePack field transfer cannot explain
away. At the larger object count, replacement is close to parity or modestly
ahead on x86. AVX-512 results have the same overall conclusion; the evidence
retains both builds. These are actual boundary and ownership comparisons, not
equal rehash work or isolated allocation costs. No physical cache tier is claimed.

For 4,096 objects, completed packed versions peak at 2,306,048 payload bytes,
with 1,117,892 peak metadata bytes. Exact-key Abseil is 3,145,728 + 822,980 bytes
on x86, or 3,047,424 + 822,980 on V2. Thus the reported payload-plus-metadata
saving is about 14% on x86 and 12% on V2, considerably smaller than the N16
scratch comparison. Packed live allocation capacity peaks at 2,621,440 bytes;
Abseil backing capacity is unmeasured (CSV zero is a sentinel, not a zero cost).

Those counters describe **completed physical owners**, not transient allocations
inside construction, allocator-private overhead or process RSS. Shared kernel
preparation and Orbital pool infrastructure are outside these totals. The
schedule defers maintenance until the original reader closes, retaining the
unobserved middle version as well. It is not minimum retained-reader memory.
`catalog_reclaim_ns` includes catalog reclamation and Allocation return; it does
not include asynchronous physical reclamation or final Pool close.

Release and ASan/UBSan checks verify unchanged contents, publication cut and owner
identity after metadata capture refusal, preparation refusal and abort. Refused
candidates contain a genuinely different value. After `drain()`, live payload
and capacity must equal the current versions; after teardown all owner and
metadata counters must be zero. Conversion compatibility is explicitly refused.
An early sanitizer run caught a harness accounting error: Abseil's debug move
can change its reported allocation size. Charging the final owner fixes the
before-move/after-move mismatch. No timings from that failing harness are used.

Capture digest:
`cda846e07f06b53fe97d6e4aa23a75de20264c1c37cfc244e555782355a1fc0c`.
Evidence: [V2](evidence/single-lifecycle-neon),
[Zen5](evidence/single-lifecycle-zen5),
[Granite](evidence/single-lifecycle-granite).
The result strengthens the need to choose sharing/clone granularity with the
larger table's routing and packing. It does not select whole-body copying for
large mutable tables or establish full MVCC performance for the remaining spike.

## Multi-bucket maintenance and table lifetime

The solver-driven `table_compare.cpp` compares ordinary exact-five-byte-key and
native-integer-key Abseil with two K40/V16 tables differing only in update-driven
promotion. Both use 256 primary buckets of 16 slots and 32 secondary buckets of
16 slots, real geometric StreamPack links, shared primitive preparation and flat
Orbital allocations. Primary/secondary strides are 101/121 bytes. These are
executed candidates, not selected layouts.

Eight disjoint turnovers precede one complete stable-key changed-value sweep.
Promotion moves an updated downstream entry into its primary when space exists;
it does not search descendants after every primary deletion. Eight rotated
repetitions balance all four arm positions. Medians below use permuted input;
the retained evidence includes consecutive disjoint banks and both x86 ISAs.

At 3,276 entries (80% of primary slots), ns/operation, **promotion / exact-key Abseil**:

| Operation | V2 NEON | Zen5 AVX2 | Granite AVX2 |
| --- | ---: | ---: | ---: |
| Stable-key update | 12.37 / 5.06 | 8.26 / 3.04 | 8.99 / 3.26 |
| Independent hit after update | 4.79 / 2.52 | 2.96 / 1.60 | 4.06 / 2.01 |
| Dependent mixed after update | 11.24 / 10.38 | 6.26 / 4.87 | 11.54 / 9.13 |
| Build + mixed probe + destruction, per inserted entry | 27.05 / 34.02 | 14.78 / 22.47 | 24.25 / 24.64 |

Promotion reduces median downstream occupancy from 258.5 to 99 entries and
improves dependent mixed reads against the unmanaged candidate (12.46, 6.64 and
13.04 ns respectively). It makes the update sweep slower. At 3,800 entries its
dependent mixed read improves more sharply: 15.69 versus 21.88 ns on V2, 7.82
versus 9.69 on Zen5 AVX2, and 16.26 versus 22.28 on Granite AVX2. These improvements
do not establish an advantageous maintenance cadence: the benchmark updates
every key once after aging. Selective hot updates can recover less capacity.

The lower-occupancy lifetime advantage does not survive the next capacity
boundary. At 3,800 entries the corresponding promotion/Abseil times are
30.15/21.31, 16.45/15.67 and 30.05/19.07 ns per inserted entry. Abseil's reservation
crosses its capacity step while candidate geometry stays fixed. Capacity
selection and expected table reuse therefore matter to the conclusion. The
lifetime interval includes owner construction, scalar insertion, one mixed query
pass and owner destruction. Shared schema preparation, worker and pool setup,
and final asynchronous physical reclamation are outside it. This is neither a
bulk-build result nor a complete database/MVCC lifetime.

Known-distinct scalar reseeding removes duplicate-discovery work. At 3,276 keys,
ordinary/distinct rebuild costs for the unmanaged candidate are 15.46/13.13 ns
per entry on V2, 10.96/9.37 on Zen5 AVX2 and 12.40/9.96 on Granite AVX2. A
bucket-partitioned builder and actual source merge remain untested alternatives.

After the update sweep, the promoted table's median allocator-requested payload
is 29,728 bytes, versus 73,744 for exact-key Abseil and 139,272 for native-key
Abseil on x86 (Abseil is eight bytes smaller on V2). Candidate known owner
metadata is 384 bytes and shared preparation 6,144 bytes. Its Orbital backing is
32,768 bytes; Abseil backing is unavailable. The candidate also has a 57,352-byte
shared static zero image, whose address extent is reported separately, not
counted as resident memory. Shared-pointer control allocations, allocator
rounding and process RSS are not measured. Fresh exact/native Abseil payload is
36,840/69,560 bytes on x86: the much larger aged reduction partly reflects Abseil
growing during churn. No total-memory or RSS percentage follows from payload alone.

Release and ASan/UBSan checks cover inverse boundaries, forced primary/secondary/
chain pressure, replacement after primary vacancies, promotion counts, iteration,
late old-reader discovery, empty chain retirement and block reuse. Native checks
passed on all targets. These shallow immutable copies do not exercise ObjectStore
publication or damage admission. An early harness accidentally left the candidate
get wrapper out of line while Abseil inlined; that campaign was cancelled. Both
wrappers explicitly inline in this accepted capture, and assembly was checked.
No result from the cancelled campaign is used.

Capture digest:
`7dad82555cfc0384caa4d00cc8da015507cd0473b75a3eb1b6db22be0b38d036`.
Evidence: [V2](evidence/table-maintenance-neon),
[Zen5](evidence/table-maintenance-zen5),
[Granite](evidence/table-maintenance-granite).
Independent lookup remains roughly twice Abseil's cost in this measured map.
The memory and short-lifetime results justify further structural comparisons;
they do not establish broad competitiveness or select the present organization.

## Complete hierarchy through Loom

`table_ring.cpp` now executes the primary, independently hashed secondary and
real StreamPack chains through matched fused, manual and actual Loom Ring
consumers. The native comparison uses K40/V64, 16-slot buckets,
flat ownership, caller-supplied seeds and whole-bucket/link hints. Primary and
secondary have 2^20 and 2^17 buckets, with 180/184-byte strides. Each lookup's
returned value determines the next request; no external trace is read on that
timed dependency path. The map is immutable throughout each operation.

At 13,421,772 entries on Zen5 AVX2, seven balanced arm rotations, median ns/lookup:

| Workload | Ordinary Abseil | Abseil public prefetch | Fused candidate | Manual hints | Actual Ring hints |
| --- | ---: | ---: | ---: | ---: | ---: |
| Fresh hits, 32 streams | 28.80 | 26.31 | 54.74 | 12.64 | 12.96 |
| Eight turnovers, mixed, 32 streams | 29.12 | 25.62 | 62.08 | 16.64 | 17.38 |
| Eight turnovers, mixed, one dependent stream | 146.53 | 146.91 | 172.41 | 140.05 | 141.49 |

The fresh and aged rows differ in hit ratio as well as history: they are not an
isolated aging delta. Mixed queries are approximately two-thirds hits and
one-third misses; half the present keys are preceded by an absent mate. All
arms execute identical values, request counts, sums and terminal keys, including
ragged stream lengths. The actual Ring advantage over prefetched Abseil is
about 2.03x for fresh hits and 1.47x for aged mixed queries. Its gap to the
matched manual hinted path is 0.32/0.74 ns respectively. The serial comparison
is slightly ahead too, while the unhinted fused path loses substantially.

The same capture on Neoverse V2 and Zen5 AVX-512 gives a mixed cross-platform
result. Each cell below is **actual Ring hints / Abseil public prefetch**, median
ns/lookup. The hot control instead compares **fused / ordinary Abseil**.

| Workload | V2 NEON | Zen5 AVX2 | Zen5 AVX-512 |
| --- | ---: | ---: | ---: |
| Large fresh hits, 32 streams | 32.58 / 25.67 | 12.96 / 26.31 | 13.24 / 25.04 |
| Large aged mixed, 32 streams | 35.71 / 39.88 | 17.38 / 25.62 | 18.25 / 25.74 |
| Large aged mixed, one stream | 175.87 / 187.79 | 141.49 / 146.91 | 141.64 / 145.44 |
| Hot aged mixed, one stream, fused / ordinary | 12.60 / 9.27 | 6.04 / 4.54 | 6.42 / 4.84 |

On V2, fresh throughput loses about 27% against prefetched Abseil; aged mixed
throughput wins by only 1.12x. Its aged serial fused and manual-hinted paths cost
146.69 and 140.17 ns, versus Ring's 175.87 ns: scheduling overhead matters there.
Zen5 AVX-512 retains the cold throughput win (1.89x fresh, 1.41x aged), with no
uplift over AVX2 in this panel. Hot serial fused lookup loses roughly 33–36% on
all three targets. Neither the AMD success nor the ARM failure selects a layout
or scheduling policy for the whole requested space.

This supports the requested large-footprint throughput direction. It does not
establish broad competitiveness. At 3,276 entries, aged mixed fused reads cost
6.04 ns serially versus 4.54 for Abseil; independent 32-stream costs are 3.25
versus 1.84. Ring/hints add overhead in that small footprint. The interface needs
to preserve the fused path and let the caller choose scheduling using qualified
heat information; these measurements do not supply a heat oracle.

Fresh requested payload is 203 MiB versus Abseil's 272 MiB. After eight turnovers,
the candidate requests 203.35 MiB, versus 544 MiB after Abseil grows. Candidate
backing is 203.45 MiB, known owner metadata 2,824 bytes and shared preparation
6,100 bytes. Allocator-private control blocks, pool infrastructure and Abseil
backing remain unmeasured. Process peak RSS includes both maps, input vectors
and mutation transients; it is not either container's RSS. No RSS saving is
claimed from these payload counters.

The aged table has 12,339,265 primary, 1,076,155 secondary and 6,352 chained keys.
Its mixed pass executes 20,131,319 primary visits, 3,559,626 secondary visits and
76,299 link-payload visits. Real directory → block owner → payload dependencies
are staged, including next links. Fixed/full StreamPack values require no
on-block slot-control read; an unnecessary stage was removed during review.
The remaining block resolution uses a same-visit cold tail continuation, keeping
ordinary calls and stack accesses out of the inspected ARM and Zen5 AVX2 ready
callbacks. The cold payload resolver still has its ordinary call frame.

The driver records potential staged cache-line targets, not measured traffic
or Abseil's issued hints. Whole-reservation hinting can fetch unused bytes. It
uses Loom's returned policy snapshot (64-byte lines and l1_1 on these workers;
the V2 receipt reports revision 1) without background calibration. Both large footprints exceed the guests'
reported LLCs (8 MiB Zen5, 36 MiB V2), and every present key participates. Full-trace
warmups precede each arm; no cache-source PMU evidence establishes the service
level of each tier. In particular, the much smaller chain working set must not
be labelled DRAM-resident from the primary footprint.

Release and ASan/UBSan checks cover forced overflow, changed generations, mixed
misses and ragged C1/C8/C32 streams; timed Ring visits match an independent
untimed stage traversal. This is one seed and value/domain shape. It does not
cover mutation publication, multi-writer modes, multimaps, C8 native throughput,
intermediate footprints, or a selected hint/layout policy. Granite Rapids needs a larger
footprint before an analogous beyond-LLC comparison is claimed.

Capture digest:
`7a2b737ed98881381953f855145c34060ce47c8f6d340f9c5fc37105ca8cfed9`.
[Retained V2 evidence](evidence/table-ring-neon),
[Zen5 AVX2](evidence/table-ring-zen5-avx2) and
[Zen5 AVX-512](evidence/table-ring-zen5-avx512) include every timing
repetition and hardware identity; their archives contain assembly, executable,
policy logs and process-resource receipts. A later local edit removes unnecessary
cold-state clearing when preparing a new primary read. It passes the forced-chain
sanitizer check but is not part of this native capture; no native uplift is
attributed to it.

## Wider records and the entry consumer

`table_values.cpp` maps K40 keys to 24-byte `{count,sum,xor}` aggregates. The table
finds an entry once and invokes an immediate typed TuplePack value consumer or
infallible update recipe. Word lookup uses the same route; sets can consume
membership without loading a value. Primary, secondary, StreamPack overflow,
dense moves, iteration, staged completion and scalar reseeding execute wider
records. Full-word projections share prepared controls; unaligned projections
use a bounded ninth-byte tail. No whole-record temporary is required for the
update or projected read. This is an experimental boundary, not a selected API.

The first consumer uses byte-packed rows, N16, one primary and one secondary
geometry, with ordinary native-key Abseil and an identical-storage direct control.
Four shuffled input passes build/update each key; one eighth of keys are then
erased and replaced with new keys. Count-only and all-three-field probes measure
both independent and answer-dependent execution, followed by iteration and
destruction. Abseil constructs from three scalar arguments and updates its stored
record after one search. It is never forced to copy the entire record for a
projection. The dependent consumer additionally reads a shared external recurrence;
its result is not isolated lookup latency. It visits every key, and consumes all
selected value bits. Zero/all-ones and wrapping aggregate arithmetic are legal.

Small-case probes repeat to at least 131,072 requests; larger cases use one pass.
The pipeline metric sums the named timed phases with that stated reuse schedule.
Input preparation, pool/worker setup, correctness checks and final footprint
accounting are outside those phases. Orbital's owner destruction does not include
later physical reclamation, so its near-zero large-table destruction time is not
a claim of free reclamation. Final live allocation requests, known backing and
metadata are reported separately; zero Abseil backing means unavailable. Shared
preparation and allocator-private control overhead prevent treating payload alone
as total memory or RSS.

The first native capture is retained in [V2](evidence/table-values-neon) and
[Zen5 AVX2](evidence/table-values-zen5), digest
`090761b726c53e5161c8c2d99a7b8b4956acf4292d35289c71eccb10d01eb9a6`.
It loses most complete operations. Requested payload saves only about 3% at 3,276
keys and 7% at 3,355,443 keys; wider values dilute the earlier key-packing saving.
These results reject this row family as a competitive general wider-record choice.
They do not reject every solver layout or quotient organization.

Several direct/composed cells differ far more than a primitive attribution can
justify without further examination. Disassembly shows Clang merged both
candidate workloads into the arm-selection function while outlining the Abseil
workload. The candidate phase control flow is consequently shared across arms.
A matched follow-up keeps all complete workloads behind the same explicit outer
call boundary; lookup and recipes still inline within each arm. The first capture is retained as a diagnostic, not used to claim a
primitive speedup or select a format. This is a measurement correction, not a
change to the container algorithm.

The follow-up is complete: [V2](evidence/table-values-isolated-neon) and
[Zen5 AVX2](evidence/table-values-isolated-zen5), source digest
`ab53c60a8847e146885dabf7d09045021b25037ca7e8f08bfa88d95fc19fd3ef`.
Native disassembly confirms separate complete workload functions for every arm.
Most former composed/direct discrepancies disappear. Median ns/operation over
six rotated repetitions, **composed / direct / ordinary Abseil**:

| Target / keys | Update | Independent count | Dependent count | Complete pipeline per key |
| --- | ---: | ---: | ---: | ---: |
| V2 / 3,276 | 9.67 / 9.40 / 8.24 | 4.49 / 4.31 / 2.35 | 17.05 / 17.15 / 12.63 | 1962 / 1944 / 1390 |
| V2 / 3,355,443 | 64.42 / 64.24 / 44.47 | 38.10 / 35.88 / 17.13 | 188.06 / 188.30 / 143.78 | 799 / 790 / 510 |
| Zen5 / 3,276 | 6.44 / 6.39 / 4.06 | 3.08 / 3.06 / 1.54 | 11.55 / 11.57 / 8.14 | 1299 / 1297 / 865 |
| Zen5 / 3,355,443 | 80.22 / 78.32 / 35.37 | 43.02 / 44.30 / 15.94 | 217.95 / 214.37 / 141.42 | 895 / 931 / 487 |

The complete pipeline uses the explicitly different small/large reuse schedules
above; do not compare its per-key values across sizes as a scaling curve.
Requested payload is unchanged: 131,104 versus about 135,000 bytes at the small
size and 129,269,760 versus about 138,411,860 bytes at the large size. That is
only a 6.6% large-case payload saving before owner/preparation overhead. Timed
tables have no chained keys; forced-chain correctness is separate evidence.

Zen5 large whole-record independent reads still have an unexplained control
discrepancy: composed/direct/Abseil are 51.55/93.73/17.88 ns. The direct trials
range from 83.29 to 128.52 ns. This cell remains unsuitable for attributing a
primitive cost or benefit. The broad loss survives in both controls and in the
other read/update phases; that is sufficient to change architectural direction
without another round of local tuning around this anomaly.

Release and ASan/UBSan checks pass for composed/direct byte rows and composed
tight rows, forced pressure in all tiers, dense moves, old readers, new seeds and
wide staged completion. A guard page plus independent bit oracle checks every
byte phase of three full-word projections and preservation of neighboring bits.
The existing word Ring callback remains free of ordinary calls/stack accesses in
the inspected ARM build. Native hot word sentinels remain behind Abseil; these
separate captures do not prove cycle-identical before/after performance.

## Exact remainder search on byte planes

The next complete-table comparison changes the search architecture, keeping
bucket placement, capacity, seeds and histories fixed. It compares ordinary
Abseil, existing selective rows, selective byte planes, exact encoded byte planes,
exact unmixed byte planes and the latter's identical-window direct control.
SeriesPack supplies the selector and TuplePack supplies suffix planes. Exact
matching combines explicit SIMD equality results before selecting a rank.
Removing selector whitening retains the seeded quotient bucket equation; the
low fingerprint bits and remaining suffix together retain the unmixed remainder.
The term `raw` names that absence of whitening, not the numeric layout of the
intermediate packed hash word.

TuplePack's existing bounded native unit-load endpoint executes the once-admitted
identity plane. There is no new primitive API or expectation that dynamic shuffle
controls optimize away. The N16 implementation uses four byte comparisons for
the hot 32-bit remainder: NEON and AVX2 combine vector masks; AVX-512 combines
comparison masks. It does not establish the performance of interleaved native
32-bit remainder lanes. Partial suffix bytes have initialized zero high bits,
maintained by every replacement; exact whole-byte comparison relies on this
physical invariant.

All three native targets pass the checks. Retained source digest is
`3a174468a78cf9d55becea6c59dd210118cf54ef78e734a1057a549018f853e3`:
[V2](evidence/table-exact-neon), [Zen5 AVX2](evidence/table-exact-zen5-avx2),
[Zen5 AVX-512](evidence/table-exact-zen5-avx512).
The operation matrix uses 3,276 K40/V64 keys, six rotated repetitions and both
permuted and consecutive disjoint generations over eight turnovers. Each native
target has 540 matched candidate operation cases with identical tier populations
and requested payload. The matched direct control does not explain the broad
remaining gap. Representative medians, **rows / exact raw / ordinary Abseil**,
ns/operation for the permuted inputs:

| Target | Fresh independent hit | Aged changed update | Eighth turnover pair | Build/probe/destruction per entry |
| --- | ---: | ---: | ---: | ---: |
| V2 | 4.83 / 3.91 / 2.32 | 11.15 / 9.96 / 4.68 | 34.25 / 30.33 / 37.64 | 28.88 / 28.27 / 32.63 |
| Zen5 AVX2 | 2.85 / 2.45 / 1.52 | 7.07 / 5.73 / 2.41 | 23.65 / 21.25 / 15.98 | 15.82 / 15.37 / 21.96 |
| Zen5 AVX-512 | 2.92 / 2.45 / 1.57 | 7.09 / 5.95 / 2.36 | 23.42 / 20.90 / 16.28 | 15.82 / 15.99 / 22.49 |

These improved independent rates do **not** repair latency. The existing Ring
driver supplies true answer-dependent requests without the operation matrix's
external recurrence. Its fresh hot hit case gives **fused rows / fused exact raw /
ordinary Abseil from the exact-format run**, ns/lookup:

| Target | Hot dependent hit |
| --- | ---: |
| V2 | 14.38 / 16.26 / 9.78 |
| Zen5 AVX2 | 8.49 / 9.44 / 6.21 |
| Zen5 AVX-512 | 8.80 / 10.28 / 6.67 |

Every ordinary Abseil row-format control is retained too; its corresponding hot
medians are 9.79/6.23/6.65 ns. The hot aged mixed case also worsens on every target.
These are warmed-footprint measurements, not proof that every load hits L1.
The simultaneous operation matrix helps separate mechanisms: on V2 fresh mixed
dependent queries, selective planes improve rows from 12.59 to 11.46 ns, but exact
encoded planes rise to 13.54 ns and unmixed planes remain 13.45 ns. Eager matching
can lose even when key-first placement helps. The direct exact control does not
remove that loss.

The large cases reuse 13,421,772 keys and C32 through the actual all-tier Ring,
with seven balanced execution-arm rotations inside each format's run. Median
**actual Ring / its own public-prefetch Abseil control**, ns/lookup:

| Target | Fresh rows | Fresh exact | Aged mixed rows | Aged mixed exact |
| --- | ---: | ---: | ---: | ---: |
| V2 | 31.62 / 24.93 | 30.12 / 24.70 | 33.63 / 36.37 | 35.71 / 40.94 |
| Zen5 AVX2 | 12.87 / 26.25 | 12.17 / 26.13 | 17.28 / 25.72 | 16.92 / 25.64 |
| Zen5 AVX-512 | 12.79 / 25.58 | 12.18 / 25.12 | 17.61 / 26.45 | 17.10 / 25.24 |

Formats run sequentially, with format order changed across workload cases, not
paired within every timing rotation. The V2 aged Abseil drift is material; do not
attribute its change to our layout or derive a precise cross-format speedup from
it. The AMD cold benefit survives, with only a modest increment over rows.
Fresh-hit versus aged-mixed differences still include a hit-ratio change.

Requested payload is unchanged across candidate formats: 57,376 bytes in the
small map and 213,230,396 in the large aged map. Shared preparation rises from
6,116 to 7,588 bytes on V2/AVX-512, or 8,228 on AVX2. Owners/backing, inputs and
logical hinted windows remain separately reported. Large aged populations stay
12,339,265 primary, 1,076,155 secondary and 6,352 chained keys. No new chain-heavy,
publication or multi-writer claim follows.

Release and ASan/UBSan checks cover ordinary and raw inverses, unchanged bucket
indices, guarded 8/16-byte planes against ordinary prepared TuplePack reads,
dense moves, forced overflow, retained readers, reseeding and actual Ring misses.
Python ownership/reconstruction checks expand to 5,544 cases. The historical
native layout bridge has not yet gained the new fragmented-field grammar.
Inspected ARM ready callbacks remain free of ordinary calls and stack accesses;
Zen5 assembly confirms removal of the selector product and scalar suffix loop,
replaced by four byte comparisons and mask reduction. This is implementation
evidence, not a cache/branch-cost attribution.

**Decision:** reject N16 exact byte-plane search as the general hot lookup family.
Its independent and cold gains do not meet the required serial bar. Preserve the
comparison and the useful primitive admission, but do not expand it into a
production preset or use larger consumers to hide that failure. This rejects
the executed byte-plane family, not every possible exact-remainder encoding.


## Native-word remainder discriminator

The final exact-search discriminator changes the R32 primary key tile from four
byte planes to sixteen 32-bit words. It retains the 197-byte primary bucket,
value offsets, N16, raw reversible quotient hash, secondary byte planes, insertion
history and capacity. All five complete-operation arms have identical candidate
tier populations and requested payload. This is one geometry, not a layout
selection across the solver's space.

An authored SP byte projection reads/writes fingerprint byte 3 of each word;
TP reads/writes the three suffix bytes. The parent coalesces their physical reads
into an initialized 64-byte tile. This is real leaf composition, but **not** a
SeriesPack Local view: the current tile-stride contract cannot express these
interleaved fingerprints. No second fingerprint copy or native-word padding was
added. The direct control uses the same physical window and storage.

Guarded loads, independent prepared TP projections and scalar matching, disjoint
SP/TP stores, inactive `FF` tags, dense erasure, forced overflow, retained readers
and reseeding passed locally with ASan/UBSan and on all five native builds.
Native disassembly confirms four `cmeq .4s` comparisons on NEON, two `vpcmpeqd`
on AVX2, and one ZMM `vpcmpeqd` on AVX-512. These instructions establish the
intended execution change; they do not attribute cache or branch costs.

Pure answer-dependent fresh hits, ns/lookup (medians over three table seeds and
seven balanced execution rotations per seed):

| Target | Rows | Raw byte planes | Raw word tile | Abseil in word run |
| --- | ---: | ---: | ---: | ---: |
| V2 NEON | 14.43 | 16.37 | 16.20 | 9.78 |
| Zen5 AVX2 | 8.43 | 9.37 | 10.20 | 6.18 |
| Zen5 AVX-512 | 8.78 | 10.27 | 9.56 | 6.64 |
| Granite Rapids AVX2 | 11.17 | 12.51 | 14.80 | 8.26 |
| Granite Rapids AVX-512 | 11.93 | 13.72 | 13.12 | 9.06 |

**Stop expanding eager exact matching as the proposed hot-latency solution.**
The native-word representation still loses to rows and ordinary Abseil on every
fresh serial target. Aged mixed serial lookup also fails to recover parity.
AVX-512 improves on byte-plane matching, but that improvement does not meet the
requirement. The result rejects this proposed remedy; it does not prove that
all possible exact-match organizations are intrinsically inferior.

Independent fresh hits improve over rows, especially on ARM: V2 rows/words/
Abseil are 4.87/3.49/2.32 ns; Zen5 AVX2 2.83/2.38/1.49 ns. The word arm also
improves Zen5 build/probe/destruction and churn, while still losing its changed
updates and churn to Abseil. Direct/composed operation differences are usually
small; isolated dependent differences on V2 and Granite AVX2 remain unsuitable
for broad primitive attribution. These gains do not select a general family.

Payload remains 57,376 bytes versus ordinary Abseil's fresh 69,552 bytes. Shared
preparation is 8,164 bytes on AVX2 and 7,524 on NEON/AVX-512, compared with 6,116
for rows. These are requested payload and known shared-control counts, not total
RSS. The hot footprint does not place all accesses in L1. The experiment adds no
new DRAM, publication, growth, multimap or concurrency result. Extending R20 to a
32-bit lane would add padding and is deliberately not hidden in this comparison.

`table-word-native.sh` rotates the three separately constructed format processes
across three seeds. Each process balances its seven execution arms; this is not
interleaving formats within each repetition. Ordinary Abseil retains its own
internal salt. Independent validation matched 450 operation cases and six Ring
histories per target; `report.py` validated all 100 retained CSVs.

Source capture: `c95e2ce2523f387a911cfa95375d1ccd64117ff31b6b81eefec6432ce1bc39e3`.
Retained measurements and archive recovery references:

- [V2 NEON](evidence/table-word-neon/artifact.json), job `20261013T092605Z-12319c47`.
- [Zen5 AVX2](evidence/table-word-zen5-avx2/artifact.json), job `20261013T092611Z-8322ee08`.
- [Zen5 AVX-512](evidence/table-word-zen5-avx512/artifact.json), job `20261013T092617Z-2d1618b0`.
- [Granite AVX2](evidence/table-word-granite-avx2/artifact.json), job `20261013T092623Z-e2fed1de`.
- [Granite AVX-512](evidence/table-word-granite-avx512/artifact.json), job `20261013T092629Z-9427fcbe`.

Full binaries, assembly, source and resource receipts remain recoverable in the
worker archives. The later clear/reuse implementation is not part of this capture.

## Bounded online growth and publication

`growth.cpp` starts empty, builds a 32-slot list, then crosses two actual geometry
boundaries into 64 and 128 primary slots. It compares N8 and N16 organizations,
including a single secondary bucket, against ordinary Abseil starting empty with
no reserve or advance cardinality. K40 keys and full V64 values form dependent
cycles. A real Orbital ObjectStore publishes 31, 50 and 97 entries, retains a late
31-entry reader through full turnover, publishes a new empty realization, refills
with fresh caller seed pairs, drains history and destroys the current owner.
Limits 32/51/102 are bounded experimental choices, not a growth policy selection.

The completed capture includes 128 differently seeded lifecycles per repetition,
eight balanced repetitions and a separate first-seed accounting replay. This is
a hot small-table comparison. It does not qualify LLC/DRAM residence. The charged
mutation/publication sum includes catalog creation, growth, turnover, reset/refill,
drain and destruction; reads, verification, long-reader scope entry/exit and
shared Pool setup/teardown are outside that sum.

| Target | Charged lifecycle, Abseil / N8 / N16 (µs) | 97-entry cohort dependent read, Abseil / N8 / N16 (ns) |
| --- | ---: | ---: |
| V2 NEON | 15.46 / 16.85 / 15.31 | 9.72 / 14.61 / 14.10 |
| Granite AVX2 | 14.51 / 15.46 / 13.45 | 8.02 / 10.81 / 10.63 |
| Granite AVX-512 | 14.31 / 15.35 / 13.58 | 8.75 / 11.54 / 11.21 |

N16's lifecycle is near parity or modestly faster here, but the multi-bucket hot
read deficit remains. Its same-storage direct control remains close. This does
not select N16 for other sizes, operations or targets. The 31-entry list's cohort
read is near Abseil on V2 and faster on Granite. These results support keeping a
separate list regime, not retaining this particular growing owner.

Point access in the captured source uses `std::visit`. Local assembly inspection
found outlined visitor calls on ARM; the cohort path fuses the selected kernel.
For N16 the captured point/cohort results are 17.73/14.10 ns on V2,
10.86/10.63 on Granite AVX2 and 11.65/11.21 on Granite AVX-512. This is an actual
implementation cost, not an inherent dispatch penalty. A later explicit switch
and inline visitor remove the ARM primary-path call locally and pass release and
sanitizer checks. That edit has **no native timing claim**; further runs were
stopped for the clean-sheet reassessment.

On V2, accounting after history reclamation has N8/N16/Abseil requested
payloads of 1,970/1,921/2,168 bytes. N8 and N16 each require 11,316 bytes of shared
preparation in this build, including inactive geometries, plus known owner/catalog
metadata. Payload savings alone are not an end-to-end memory win. Accounted backing
for the candidates is 2,048 bytes; Abseil backing is unknown. Shared_ptr control
blocks and metadata allocator rounding are excluded. Intermediate published owners
remain until the chosen deferred drain, so this is not minimum MVCC retention.
Migration-overlap diagnostics expose old/new packed allocations, but Abseil's
internal rehash peak is unobserved; those peaks cannot be compared.

Release and ASan/UBSan checks include independent map oracles, late readers,
capture refusal, prepare refusal, abort, timely retirement, and payload allocation
refusal at both migrations under an actual fixed Orbital Arena budget. Full K64
keys exercise single-secondary-bucket reconstruction, forced StreamPack chains,
dense deletion and retained copies without shifts by 64.

Capture `a9ecdde3f966be72f9b11a3d3702647607998812f1ab147c19ff0a3a58f40340`:
[V2](evidence/growth-neon/artifact.json),
[Granite AVX2](evidence/growth-granite-avx2/artifact.json),
[Granite AVX-512](evidence/growth-granite-avx512/artifact.json).
`growth-report.py` validates all 1,632 timing rows and their matched operation
counts/checksums. The two Zen jobs produced no measurements: Spot capacity was
unavailable and the group stopped launching. No Zen result is inferred. Full
sources, binaries and assembly remain in the three completed worker archives.
