# StreamPack evidence

StreamPack's native byte-width geometry is competitive across the tested read workloads,
with substantial large-population and reverse-lookup wins. Ordinary writes,
shuffled projection and fragmented byte searches still expose losses. Full MVCC
can favor widths even when the isolated mutation kernel is slower, because the
complete caller does different amounts of copying and publication work.

These results support conditional recipes and capacities. No single layout,
block size, scanner or scheduling route wins the whole matrix. Timings below
identify their actual consumer and comparison boundary.

## Schema bounds and metadata placement

The replacement comparison uses explicit NEON, AVX2 and AVX-512 bodies with
**both loop and SLP auto-vectorization disabled**. Fixed sixteen-request field
packets make execution choices visible: four NEON vectors, two AVX2 vectors or
one AVX-512 vector. Ordered reads use widening loads; shuffled reads compare
explicit narrow lane loads with x86 dword gathers plus narrow-field extraction.
Rank permutation, vector address arithmetic and widened accumulation are explicit.

Complete value lookups separately follow one dependent payload chain (C1) or
eight interleaved chains (C8). Every request locates a value, reads used length
and liveness, and loads its successor from the final four used bytes. The local
Width prefix uses explicit SIMD; state/control and payload traversal are scalar,
with the eight chains explicitly unrolled. C1 measures serial operation service
time, including memory and control. C8 measures achieved overlapping throughput;
it does not establish peak reciprocal throughput. Independent field packets are
another throughput consumer, not individual-field latency measurements.

Each build/check phase precedes its timing job. The runner checks the SHA-256
pair of reviewed native binaries before executing them. Correctness includes
exact lane order under ordered, permuted and duplicated ranks, distinct values
across block boundaries, maximum/zero widths, partial groups and narrow-field
load bounds. Widened prefixes are checked at the full 65,535 slot maximum,
including carries between native sub-vectors. ASan/UBSan also exercises these
paths and the actual bulk consumers.

The new native field comparison covers Width geometry and six placements: population-wide planes,
split R/U with a 96B block directory, grouped R/U with that directory, a 128B
padded directory, block-local planes, and block-local grouped R/U. A seventh
control narrows checkpoints to 16 bits when maximum 255 permits it. The
[design](DESIGN.md) gives the exact bytes and alignment; directory padding and
R/U grouping are distinct controls. Block-local planes versus block-local hybrid
changes both grouping and directory location, so it is not a one-factor test.

Capacity is 256, with populations of 256, 1M and 16M rows and inclusive slot
maxima 255 and 4,095. This comparison does not reselect 128/256/512. Reservations
are mostly 8–135 bytes, with a schema-maximum reservation every 257 rows. Used
lengths vary from four bytes to the reservation size. Field probes use varied
tombstones; complete chains and timed projection use all-live values. Dead and
live-empty projection cases are checked independently. These are repeated warm
traces; neither RSS nor total payload size establishes the memory level serving
a metadata request. There is no resident request array or second oracle
population during timing.

A separate executable compares full projection through native, actual SeriesPack
Local and grouped TuplePack decoders. It computes an explicit widened exclusive
prefix over sixteen reserved widths. Sequential projection prepares one group;
shuffled projection pays for 256 starts/used lengths and sixteen liveness masks
inside each block operation. Selection/mask control and variable-length libc
copies remain scalar/library work. Preparation is neither excluded nor charged
to a different operation. Its C1/C8 controls stay in a separate summary because
code layout can differ between executables.

Native c9g.4xlarge has 32GiB; the corrected x86 jobs use m8a.4xlarge (Zen5) and
m8i.4xlarge (Granite Rapids), each with 64GiB. The completed campaign includes
both AVX2 and AVX-512 at every population size. Earlier layout campaigns used
c8a/c8i.4xlarge, so old-to-new timing changes cannot be attributed solely to
replacing the kernels. Comparisons below use matched controls within the
corrected campaign. Two process orders supply two samples each; medians and
ranges describe those four observations, not confidence intervals. The six main
placements reverse order; the CP16 control runs last in both orders. Both x86
builds use the existing AVX2 point-prefix body: their C1/C8 comparison does not
test a new AVX-512 point algorithm. SVE2 is untested. The private-growth table
comes from its own earlier campaign and does not depend on these SIMD consumers.

### Latency and overlapping throughput answer different questions

Keeping the same 96B directory while changing split R/U to groups of sixteen
rejects a blanket grouped-R/U penalty. At 16M rows and maximum 4,095, median
nanoseconds per complete value lookup are:

| Host / build | C1 split | C1 grouped | C8 split | C8 grouped |
| --- | ---: | ---: | ---: | ---: |
| Neoverse V3 / NEON | 223.315 | 214.835 | 33.654 | 31.297 |
| Zen5 / AVX2 | 292.726 | 288.088 | 42.468 | 38.899 |
| Zen5 / AVX-512 | 294.325 | 288.382 | 42.555 | 39.022 |
| Granite / AVX2 | 287.547 | 301.058 | 41.860 | 40.148 |
| Granite / AVX-512 | 273.953 | 290.488 | 41.623 | 39.817 |

Grouping reduces C8 time by roughly 7% on ARM, 8% on AMD and 4% on Intel in
this case. ARM and AMD also improve serial service. Intel C1 varies too much for
a close ranking: the AVX2 split/grouped ranges are 272.321–310.327 and
284.405–314.354; AVX-512 ranges are 267.905–278.672 and 281.972–330.324. These
AVX-512 grouped samples are all slower than split in this capture. The variation
and build sensitivity prevent establishing a general Intel serial ranking.

The large-population result is not a universal ranking. At 256 hot rows on ARM,
grouped C1/C8 is slightly slower: 8.765/1.985 versus split 8.474/1.899. On hot
Zen5 AVX2, C1 is nearly level and C8 is 1.894 versus 1.850. Population-wide
separate planes are faster still for hot C8, at 1.577 on ARM, 1.481 on Zen5 AVX2
and 2.118 on Granite AVX2. At 1M rows, grouping's C8 benefit over the same-directory
split control is approximately 2% on ARM, near zero on AMD and 16% on Intel.

The separate packed/native bulk executable also demonstrates why C1 cannot be
omitted. At 16M rows on ARM, TuplePack-group C1 takes 199.924 ns/value versus
native planes' 227.747, but C8 takes 59.417 versus 33.297. C1 sample ranges are
199.455–201.424 and 227.502–227.995, respectively; both process orders favor
TuplePack in this serial case. Paid shuffled projection is much closer,
7.729 versus 7.847 ns/row. The current generic point composition remains
expensive for overlapping requests even though this packed layout can improve
serial service in the large population. The x86 results preserve the same
composition concern: at 16M rows, SeriesPack/TuplePack C8 takes 84.216/80.078
ns/value on Zen5 AVX2 versus native planes' 39.763; Granite AVX2 takes
72.780/81.828 versus 36.924. Paid shuffled projection is much closer: on Zen5
AVX2, 14.373/14.722 versus 14.242; on Granite AVX2, 16.020/16.891 versus 16.614.
Sequential full projection still loses in every host/ISA case at this population:
SeriesPack costs 4–22% more than native planes, TuplePack 8–13% more. On ARM the
times are 6.514/6.558 versus 5.881 ns/row. Packed batch adapters remain candidates
for their metadata saving and closer shuffled performance, with that scan cost
explicit. Routing every point operation through the current generic composition
is not supported. Its losses are not intrinsic limits of either packing primitive.
These measurements do not isolate cache, prefetch or register-pressure causality.

[c9g](evidence/layout-explicit-c9g/summary.csv),
[Zen5](evidence/layout-explicit-zen5/summary.csv) and
[Granite](evidence/layout-explicit-granite/summary.csv) preserve all placements,
populations and sample ranges. Their separate bulk summaries preserve actual
[ARM](evidence/layout-explicit-c9g/bulk-summary.csv),
[AMD](evidence/layout-explicit-zen5/bulk-summary.csv) and
[Intel](evidence/layout-explicit-granite/bulk-summary.csv) packed/native consumers.

### Field requirements change the answer

At the same 16M rows and maximum 4,095, shuffled sixteen-request field packets
using explicit lane loads take median nanoseconds per request:

| Host / build | U-only split | U-only grouped | R+U split | R+U grouped |
| --- | ---: | ---: | ---: | ---: |
| Neoverse V3 / NEON | 3.284 | 4.320 | 6.412 | 5.563 |
| Zen5 / AVX2 | 2.508 | 3.352 | 5.912 | 5.559 |
| Zen5 / AVX-512 | 2.278 | 2.949 | 5.489 | 4.976 |
| Granite / AVX2 | 3.369 | 3.989 | 8.531 | 3.713 |
| Granite / AVX-512 | 3.245 | 3.825 | 7.932 | 3.766 |

U-only reads favor split lengths; requesting both reverses the direction on all
three CPUs. These field packets read selected R and U values, not the sixteen
R values needed by each complete Width lookup. The complete C1/C8 table owns
that comparison. Hardware gathers are a separate control and are slower than
explicit lane loads in these large-population cases. For example, Zen5 AVX-512
grouped R+U takes 5.855 with gathers versus 4.976 with lane loads; Granite takes
5.223 versus 3.766. Gather availability is not a selection rule.

Equal useful bytes in an ordered R+U scan do not imply identical measured cost.
The corrected ARM controls below are nearly level. Zen5 still favors split
lengths: 0.076 versus 0.090 ns/row with AVX2, and 0.068 versus 0.080 with AVX-512.
Its hot 256-row controls are approximately level. The Granite AVX2 ordered split
control varies from 0.219 to 0.405 versus grouped 0.178–0.192; those ranges must
travel with any comparison. This residual stream behavior has not been causally
isolated. It cannot be explained by claiming that a compiler chose whether to
vectorize these bodies: both vectorizers are disabled and the ISA work is explicit.

Inspection of the exact Zen5 steady loops narrows the compute explanation.
Split/grouped process 32/64 ranks per AVX2 loop and 64/128 per AVX-512 loop.
After normalization they perform the same vector loads, widening, addition and
checksum work, reading four explicit bytes per rank. Grouped uses fewer total
instructions and fewer address-calculation sites per rank; neither has calls or
stack accesses. LLVM 21 accepts `znver5` but uses inherited `Zn4` resource labels.
Its modeled reciprocal resource throughput is equal within each ISA: 0.5
cycles/rank for AVX2 and 0.3125 for AVX-512. Finite-loop simulation differs by
less than 0.4% for AVX2 and about 3.2% against grouped for AVX-512, far below
the roughly 18% measured large-population gap. This model does not establish
hardware cycles or exclude unmodeled compute effects; it supplies no convincing
compute explanation, and no cache or prefetch attribution. Exact loops, commands
and model output are recoverable through the
[bounded diagnostic](evidence/layout-zen5-ru-model/review-index.json).

### Correcting the NEON byte-load dependency

The first explicit-kernel run exposed an implementation defect in the ordered
u8 field bridge. The scalar four-byte load followed by `vcreate_u8` lowered to
`LDR W → FMOV → ZIP1 → USHLL → AND → UADALP`. `ZIP1` read the live checksum
accumulator as irrelevant high-byte filler; the later mask removed those bits,
so values remained correct, but widening depended on the preceding checksum.
All seven u8 ordered-used placements had this dependency. The u16 path did not.

The corrected body spells the same bounded four-byte access as a SIMD duplicate
load. Actual native code uses `LDR S → USHLL → USHLL → UADALP`, with no checksum
dependency before accumulation. All 84 ordered bodies were checked in the native
binary, with no hot calls or scratch stack accesses. The exact reviewed binary
was re-run at both maxima, all populations and fully reversed placement orders.
The source variant preserves the original allocator/runtime snapshot.

At 16M rows, u8 U-only scan costs **0.143/0.144 ns/row for split/grouped R/U**,
down from 0.763/0.762. U8 scans consuming both R and U cost **0.158/0.156**,
down from 0.972/0.984. The u16 controls remain approximately 0.145 for both
placements. The corrected scans do not show a material grouped-R/U penalty for
these complete field requirements. Hot-case ranges are retained; small differences
are not a universal layout ranking. This is a correction to the explicit consumer,
not evidence that byte-width storage is intrinsically slow.

Use [the corrected ordered-field summary](evidence/layout-byte-c9g/summary.csv)
for ARM scans. [The native audit](evidence/layout-byte-codegen/review-index.json)
recovers the new instruction bodies. The earlier
[audit and diagnosis](evidence/layout-explicit-codegen-native/review-index.json)
and [byte-bridge finding](evidence/layout-explicit-codegen-native/c9g/ordered-byte-bridge-followup.json)
retain the rejected lowering. Complete value lookup, shuffled fields and full
projection use different bodies and retain their separate matched measurements.

### What locality can and cannot explain

For u16 lengths, grouped R/U places sixteen reserved widths and sixteen used
lengths in one 64B line. A U-only plane holds thirty-two used lengths in that
line. That density difference applies to U-only streams. A complete Width lookup
needs sixteen reserved widths plus one used length; grouping can put those
length inputs in one line. An ordered scan consuming both reads the same useful
R/U bytes in either organization. These address/byte facts do not establish
cache-miss counts or explain a timing difference by themselves.

Integer multiply-add/shifted-add instructions already appear in native address
calculation; the project's `-ffp-contract=off` flag concerns floating-point
contraction. Their latency/throughput impact must be established by the complete
consumers. No result here attributes a substantial penalty to multiplying the
group index by two. A mechanistic explanation must
match the measured consumer and inspected code, and claims about hardware
prefetch or memory traffic need separate evidence.

### Reserving the block directory upfront

The aligned private-growth comparison uses direct Orbital Allocations and the
same 16R+16U length groups for every owner policy. It charges creation, admitted
initialization, append journals, growth and release; final arena teardown is
outside timing. It publishes no ObjectStore versions and contains no payload.
Exact physical afterimages are replayed after every append, with independent
mixed, zero and maximum histories. Counts below are logical issued work, not
PMU traffic.

At N=256 and maximum 4,095, reserving the full 96B directory, its 32B alignment
gap and the first 64B length group requests 192B. Orbital gives it the same 256B
capacity as the compact control's 128B request. At full occupancy both request
1,152B and receive 1,280B. Both make four growth requests and relocate three
times; the fixed prefix avoids the compact control's 572B of internal repacking.
It initializes 1,152B versus 1,210B over the complete history. Its directory
*offset* stays fixed; relocation still invalidates borrowed pointers.

Median ns/append over a full 256-slot history:

| Host | Compact growing directory | Fixed full directory | Separate directory allocation | All metadata upfront |
| --- | ---: | ---: | ---: | ---: |
| c9g | 10.483 | 9.927 | 11.455 | 9.372 |
| Zen5 | 7.963 | 7.671 | 7.319 | 7.108 |
| Granite Rapids | 9.732 | 9.204 | 9.330 | 8.705 |

This supports a fixed full directory for growable grouped lengths: simpler
internal geometry with no initial capacity penalty in this case. Full upfront
metadata avoids growth when occupancy is known. A separate directory preserves
its address through private length growth but initially consumes two allocations
and 512B total capacity. The experiment does not select growing split-plane
policies: moving U when R expands, reserving all R, and separate R/U allocations
are different ownership choices. Reader-retained MVCC must still keep directory,
lengths, payload and count/extent coherent.

All six tested capacity/maximum combinations fit their fixed directory and first
length group into the same initial 256B capacity as compact growth. That does
not make the directory free throughout partial occupancy: at 64 assigned slots,
N256/u16 requests and receives 384B versus compact's 320B; N512/u16 uses 448B
versus 320B. Long-lived sparse blocks pay that difference. At full N512/u16,
the fixed directory avoids 1,660B of internal repacking, with both policies
requesting 2,240B and receiving 2,560B.

The aligned owner evidence includes N=128/256/512, maxima 255/4,095 and partial
occupancies in [c9g](evidence/layout-owner-c9g/growth-summary.csv),
[Zen5](evidence/layout-owner-zen5/growth-summary.csv) and
[Granite Rapids](evidence/layout-owner-granite/growth-summary.csv).

### Direct endpoint layouts

A separate corrected executable compares Direct endpoints through native planes,
block-local planes, tight or padded groups, and actual SeriesPack/TuplePack
adapters. It uses the same capacity, maxima and population sizes as the Width
comparison above, with all placements reversed in the second process order.
Both automatic vectorizers are disabled; the
[native audit](evidence/layout-direct-codegen/review-index.json) identifies the
exact reviewed binaries. Direct point and reverse operations read scalar
endpoints/state. Explicit SIMD serves batch decoding and endpoint-to-start
shifting; some ordered native shifts fold into scalar copy inputs. The batch
comparison charges each group's preparation for sequential projection and the
full block's starts, used lengths and liveness for shuffled projection.

The update control changes used lengths and tombstones in existing reservations
and consumes their physical damage journal. It does not change endpoints,
duplicate group headers, payload, count/extent, allocation or MVCC versions.
Its results cannot qualify a complete Direct geometry writer. Independent
correctness cases include mixed live/dead/empty slots, exact output byte order,
partial groups, repeated endpoints, the inclusive 4,096 boundary and the full
65,535 maximum. Timed projections and chains use all-live values.

ARM measurements are retained in the
[Direct summary](evidence/layout-direct-c9g/summary.csv). At 16M rows and maximum
255, native planes, tight groups and 64B groups take 216.595/201.007/200.865
ns/value for C1, and 30.447/28.874/29.511 for C8. A 64B group keeps the slot's
endpoint, used length and tombstone metadata in one line; complete lookup also
uses separate block control/base data. The tighter 54B stride slightly wins C8.
At maximum 4,095, planes/tight/128B groups take 218.285/212.013/212.301 for
C1 and 32.561/30.364/29.471 for C8. These grouping gains do not hold in the hot
256-row controls; at 1M rows grouped C8 is also slower than planes.

The same large maximum-4,095 groups lose on other operations: tight/128B reverse
lookup takes 74.403/76.030 versus planes' 59.924 ns/request, while metadata update
takes 31.498/30.458 versus 8.918. All three updates issue three logical journal
bytes per rank. Logical byte equality does not establish equal traffic or service
cost; no cache or prefetch cause is isolated here.

Native paid projection takes 5.547 sequential and 7.525 shuffled ns/row, versus
ordinary projection's 6.144/8.458. SeriesPack owns 4.125 metadata bytes/rank versus
native planes' 6.125, but its C8 takes 36.770 versus 32.561; paid projection takes
6.203/7.728. TuplePack owns five bytes/rank, with C8 of 85.415 and paid projection
of 7.471/9.095. These are costs of the measured adapters, not intrinsic codec
ceilings. Absolute timings across this Direct campaign and the separate Width
executables do not constitute a controlled geometry comparison.

The [Zen5 controls](evidence/layout-direct-zen5/summary.csv) do not repeat ARM's
grouped C8 gain. At 16M rows and maximum 4,095, planes/tight/128B take
39.813/40.418/42.789 ns/value with AVX2 and 37.951/40.192/40.432 with AVX-512.
The AVX2 tight/planes process ratios are 1.057 and 0.992, so that small median
difference is not a stable ranking. AVX-512 grouped controls lose in both orders.
At maximum 255, all native grouped C8 medians also lose to planes in both builds.
This does not overturn the separate Width result: the two geometries need
different point inputs.

The Zen5 wide metadata-update losses are much larger: tight/128B cost
49.705/53.369 versus planes' 16.129 ns/rank with AVX2, and 51.225/52.239 versus
14.819 with AVX-512. Reverse also loses. Paid shuffled native projection helps,
at 13.926 versus ordinary 17.781 with AVX2 and 13.085 versus 15.845 with AVX-512.
Packed SeriesPack/TuplePack paid times are 16.870/15.190 and 16.171/16.136;
their C8 point times remain substantially worse, 51.933/150.818 and
54.783/174.947. These controls support operation-specific batch reuse without
selecting the generic packed point path.

The [Granite controls](evidence/layout-direct-granite/summary.csv) likewise reject
a general grouped Direct advantage. At 16M rows and maximum 4,095, planes/tight/128B
C8 is 46.395/47.951/49.509 ns/value with AVX2 and 50.445/51.371/53.251 with
AVX-512. Both grouped controls lose in both process orders. C1 varies markedly:
the AVX2 plane samples span 349.432–417.811 ns/value, so close serial medians
cannot support a robust ranking. At 1M/max-255, the AVX2 64B/96B groups have lower
C8 medians, but their process ratios reverse direction: 0.906/1.029 and
0.829/1.191. They do not establish a stable gain or generalize to the large
population or the second build. Cross-build timing differences do not by
themselves establish an ISA effect.

Granite's wide tight/128B metadata updates cost 39.078/42.615 versus planes'
12.492 ns/rank with AVX2 and 46.934/47.765 versus 12.476 with AVX-512. Reverse
also loses. Paid native shuffled projection takes 16.654 versus ordinary 18.583
with AVX2 and 17.093 versus 18.830 with AVX-512. SeriesPack/TuplePack paid times
are 17.307/17.761 and 17.862/18.241, much closer than ordinary packed projection;
their C8 point times remain 52.423/216.164 and 58.579/226.816. Sequential packed
projection also remains slower than paid native at this maximum/population.

All five host/ISA profiles and all eight consumers are now measured with the
reviewed explicit batch controls. The comparison supports simple separated
native fields as the initial mutable Direct baseline, with grouped or packed
recipes available for a demonstrated consumer. It does not select population-wide
planes as an ownership policy: block placement and allocation remain separate
decisions. Grouped Width has a different input requirement and keeps its own
matched evidence. Padding to 64/96/128B is not a general preset rule.

### Earlier layout evidence

**Earlier layout selection conclusions are withdrawn.** Those campaigns disabled
SLP but left loop vectorization enabled. Scalar field consumers received
different compiler-selected rank batches, reductions, gathers and spills. They
remain observations of those exact binaries; they do not select a SIMD layout,
establish an intrinsic grouped-R/U penalty, or explain one mechanically. The
older block-local/bulk timings have the same limitation. Physical sizes,
correctness checks and the allocation evidence above remain useful.

Native-width versus actual SeriesPack/TuplePack storage is still a useful
composition comparison. For maximum 4,095, SeriesPack Direct uses 4.125 metadata
bytes/slot versus native planes' 6.125; Width uses 3.28125 versus 4.375. These are
metadata reductions, not equal reductions in total memory. The grouped TuplePack
control owns 54B per sixteen slots at this maximum, optionally in a 64B stride.
Prepared maps are shared across the population; their sizes are not charged per
block. Generic point routing remains a tuning concern, not an intrinsic limit
of packed groups.

Original receipts remain recoverable in the layout, layout-group, layout-fields
and layout-owner evidence directories. For example,
[the original c9g receipt](evidence/layout-c9g/context.json),
[the grouped receipt](evidence/layout-group-c9g/context.json) and
[the field receipt](evidence/layout-fields-c9g/context.json) identify their own
sources and binaries. Their source-specific summaries are historical, not a
second set of current recommendations.

## Population sweep

`footprint.cpp` constructs one candidate per process, with no retained logical
oracle, request array or competing representation. Values are 4–35 bytes. A
verified permutation cycle visits every rank; its next link occupies the last
four payload bytes. Dependent reads follow one chain; throughput reads follow
eight independent segments of that same cycle. Both return lengths and terminal
ranks. Length-only requests use an independent full-domain permutation and touch
metadata only. Timings include permutation generation where used.

Every interpretation and payload is checked before measurement. Whole-trace
warming and calibration precede three samples. These are repeated steady traces,
not a cold-cache experiment. Smaps, NUMA, allocation and separate PMU receipts
record what is observable; generic L1 events do not classify LLC or DRAM service.
The first sweep has one construction per case. Follow-up runs use two fresh
processes, reverse method order and retain the scalar decoder as a control for
the explicit SIMD width decoder.

Actual DuckDB 1.4.4 `string_t` retains its inline values and only allocates the
outlined payload. The location8 control stores an offset and length. Direct16
uses narrow adjacent boundaries plus a 4-byte base per block, without the older
pilot's pointer directory. Widths8 uses byte widths and 32-bit bases every 16
slots. Flat32 uses global boundaries. All share the same logical values and
access order. This fixture is all-full/live: no optional used or dead planes.

Neoverse V3, capacity 256, median nanoseconds per value:

| Operation / values | Direct16 | Widths8 scalar | Flat32 | Location8 | DuckDB16 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Dependent / 1K | 3.692 | 5.616 | 3.333 | 3.332 | 3.626 |
| Dependent / 262K | 28.686 | 29.298 | 34.607 | 41.471 | 41.033 |
| Dependent / 4M | 152.425 | 139.939 | 175.534 | 188.777 | 165.864 |
| Dependent / 16M | 195.475 | 194.665 | 200.372 | 203.540 | 178.337 |
| Eight chains / 4M | 18.124 | 19.775 | 20.362 | 24.669 | 23.147 |
| Eight chains / 16M | 26.055 | 26.336 | 26.592 | 27.886 | 25.627 |
| Length only / 16M | 3.986 | 2.208 | 5.104 | 5.288 | 5.070 |

The 16M dependent Direct16/Widths8 result is a tie at this evidence level; sample
ranges overlap. Widths8's 4M serial advantage does not give it the throughput win.
DuckDB's inline payload can avoid a second memory access even though its complete
allocation is larger. At 16M values the requested format bytes are 332 MiB for
Widths8, 344.4 MiB for Direct16, 376 MiB for Flat32, 440 MiB for Location8 and
532 MiB for DuckDB. These are format extents, not RSS, allocator charge or touched
bytes. Length-only Widths8 touches its 16 MiB width plane, not its full 332 MiB.

The x86 hosts also show compact geometry becoming competitive at larger
populations. A useful throughput comparison uses the competent AVX2 builds:

| Eight chains / 4M values | Direct16 | Widths8 scalar | Flat32 | Location8 | DuckDB16 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Zen5, x86-64-v3 | 24.430 | 43.882 | 30.292 | 38.488 | 39.800 |
| Granite Rapids, x86-64-v3 | 12.415 | 27.739 | 17.554 | 14.442 | 28.388 |

Do not generalize this table to all operations. For example, largest-population
serial reads favor DuckDB on Zen5, while Widths8 is fastest on Granite Rapids.
Full repetitions, other populations and both x86 ISA settings are retained.

### Same-format SIMD prefix control

The repeated follow-up completed on all three hosts. It preserves the width
format and replaces the scalar prefix decoder with the bounded masked widening
sum. Two fresh processes reverse method order; each supplies three samples.
Median nanoseconds per value for **eight independent chains**:

| Host / values | Direct16 | Widths8 scalar | Widths8 native | Location8 | DuckDB16 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Neoverse V3 / 1K | 0.629 | 2.428 | 1.036 | 0.507 | 0.548 |
| Neoverse V3 / 4M | 17.737 | 19.983 | 17.090 | 24.541 | 23.024 |
| Neoverse V3 / 16M | 25.824 | 27.007 | 24.154 | 27.756 | 25.614 |
| Zen5 AVX2 / 4M | 23.829 | 41.647 | 19.888 | 38.311 | 39.587 |
| Zen5 AVX2 / 16M | 35.026 | 60.709 | 31.953 | 41.066 | 41.784 |
| Granite AVX2 / 4M | 12.653 | 27.470 | 12.634 | 15.495 | 29.422 |
| Granite AVX2 / 16M | 27.575 | 46.163 | 27.751 | 36.241 | 49.210 |

The descriptor numbers above are the actual compiler outputs, **not the final
strongest baseline**: the separate x86 no-SLP control below improves them.
Native checkpoint widths are now a serious large-population throughput contender.
They do not universally improve dependent latency: at 1K on c9g, scalar→native
is 5.774→7.416 ns/value, and at 16M it is 194.722→195.750. On Zen5 at 16M the
native dependent path takes 276.644 ns/value versus DuckDB's 264.980. The
complete-operation tradeoff remains architecture, population and access dependent.

These point fixtures have full reservations and no tombstones. Neither them nor
length-only metadata reads select a mutable recipe. `mutable_population.cpp`
adds genuine append/edit/retire/soft histories, complete projection and reverse
lookup with slack and empty values; its evidence is separate.

### 64M-row Granite extension

Granite reports a 480 MiB LLC, so the earlier 16M comparison was insufficient
for a claim about substantially larger working sets on that host. The extension
uses 64M rows, one candidate per process, three repetitions and the same
`-fno-slp-vectorize` policy for **every** contender. The complete format footprints
are approximately 1.30–2.08 GiB. This is one process order, not a repeated
order-interleaved experiment.

| Method | Dependent ns/value | Eight chains ns/value | Length-only ns/value | Measured RSS MiB |
| --- | ---: | ---: | ---: | ---: |
| Direct16 | 325.223 | 39.019 | 7.304 | 1381.4 |
| Widths8 native | 291.587 | 33.719 | 3.934 | 1331.9 |
| Location8 | 359.553 | 45.202 | 10.017 | 1764.0 |
| DuckDB16 | 317.871 | 42.027 | 12.465 | 2131.9 |

Native checkpoints beat the 8-byte descriptor by 25% in eight-chain time and
19% in dependent time in this run. The native eight-chain samples span
33.551–34.175 ns/value, versus descriptor 44.946–45.291. Dependent native samples
span 290.866–302.963, versus descriptor 359.083–359.853. Compact geometry also
beats Direct16 and the actual DuckDB descriptor here. This reverses the earlier
small-population preference; it is not proof that compact metadata wins every
request distribution or mutation history. Length-only reads still touch different
planes, and RSS/LLC size do not identify each access's service level.

## Assembly, scheduling and profiling

The scalar checkpoint decoder is not a strong final implementation. In the
linked c9g binary, the dependent Widths8 paths average 59.3 instructions, 10.5
loads and 11.1 branches per value over the uniform rank cycle. Direct16 uses 13
instructions. `prefix.h` now supplies a masked 16-lane widening reduction for
NEON and AVX2, with unsigned-max and unaligned checks. On c9g the linked native
dependent body is 19 instructions per value; eight-chain execution is 17.25.
It uses a vector mask and `uaddlv`, not a scalar branch tree. Sequential geometry scans keep running offsets rather
than invoking point-prefix reconstruction for every rank.

There is a separate compiler confound in the x86-64-v4 location8 eight-chain
loop. Both Intel and AMD builds pack four independently calculated payload
addresses and use `vpgatherqd`, joining otherwise independent recurrences.
The v3 loop uses scalar payload loads. On Granite, the hot v4 loop is
`0x111f0–0x112e8`, with the gather at `0x112b4`; the v3 scalar loop is
`0x11260–0x113b9`. `slp-control.sh` compares the normal compiler policy with
`-fno-slp-vectorize` on the same source. These are code-generation controls,
not evidence that AVX-512 itself or the descriptor format is unsuitable.

The Zen5 no-SLP control completed with both fresh process orders. Location8
eight-chain median normal→no-SLP times are 1.114→0.321 ns/value at 1K,
4.541→3.058 at 262K, 42.030→31.821 at 4M and 44.293→36.982 at 16M. The
paired Direct16 no-SLP times are 0.454, 2.628, 23.165 and 33.371. Thus the
descriptor has the strongest small resident-data result while compact geometry
retains a larger-population advantage. The native-width times above came from
another run, so their ratio to this corrected descriptor is indicative rather
than an interleaved pair. Linked code confirms scalar payload loads in no-SLP
and byte-identical normalized C1 bodies. Direct16's C8 body has four spill
stores/reloads; that is another possible caller improvement, not a format law.
Zen5's generic L1D-miss event reported zero throughout and is unusable as cache
evidence. Granite's corresponding worker was interrupted by Spot; its incomplete
coverage cannot supply a two-order/16M conclusion.

LLVM21's Neoverse V2 and V3 MCA outputs were byte-identical for all 26 extracted
ARM bodies, with V2 resources and fixed load latency. They explain dependencies
under that model; they cannot establish c9g cache-hit scheduling or the
measured large-working-set behavior. Widths paths were extracted separately;
concatenating mutually exclusive branches would give a meaningless MCA trace.
The earlier `c9g-direct16-length-loop.s` artifact belongs to the previous pilot's
contiguous range operation, not the new footprint experiment.

CPU-clock profiles of the geometry pilot are archived, including setup and
calibration; footprint PMU traces are separate exact-operation batches with
item counts and coverage. Neither substitutes for a lifecycle profile.

## Mutable populations and access order

The mutable campaign uses actual Orbital allocations and materialized issued-write
journals. Each repetition initializes/appends/retires, edits one quarter of rows
without changing reservations, reads, shrinks with soft packing, then reads again.
Reservations are 0–63 bytes. Both full/live and slack/dead populations include
empty slots. The independent oracle checks physical ownership, complete payloads,
projection boundaries and every timed reverse query. Output buffers are bounded;
no population-sized oracle or request array stays resident during measurement.

The body capacity is identically 64 bytes per input row, including after soft
packing. This deliberately separates geometry from an allocation-policy contest:
16M rows allocate 1 GiB of body capacity. Widths8 saves about 11.9 MiB of geometry
versus Direct16 there; used lengths, tombstones and counts are equal. Allocated
body, live bytes, format extent, RSS and touched bytes are distinct quantities.
Soft packing does not refund that capacity in this experiment. At 16M sparse
rows on c9g, measured RSS is 1,081.7 MiB for Widths8, 1,093.6 MiB for Direct16
and 1,125.7 MiB for Direct32. Reverse touches only geometry/dead/count state:
that large RSS does not make each reverse query a DRAM access. Hardware receipts
report 48 MiB LLC on c9g and 480 MiB on Granite; reported cache sizes alone do
not establish which accesses hit there. A 64M-row Granite point control extends
its population beyond the previous working-set range.

At 4M sparse rows, N256, median ns/input row **before soft packing**:

| Host / operation | Direct16 | Direct32 | Widths8 |
| --- | ---: | ---: | ---: |
| c9g / physical projection | 2.532 | 2.582 | 2.637 |
| c9g / shuffled projection | 4.273 | 4.183 | 4.668 |
| c9g / reverse | 12.963 | 13.214 | 24.321 |
| Zen5 AVX2 / physical projection | 6.431 | 6.661 | 6.472 |
| Zen5 AVX2 / shuffled projection | 11.556 | 11.531 | 13.363 |
| Zen5 AVX2 / reverse | 33.002 | 12.219 | 21.493 |
| Granite AVX2 / physical projection | 7.418 | 7.364 | 7.670 |
| Granite AVX2 / shuffled projection | 12.179 | 12.392 | 13.774 |
| Granite AVX2 / reverse | 40.335 | 37.818 | 33.770 |

Here shuffled projection visits blocks in permuted order, then all ranks within
each block in permuted order. Reverse makes N uniform body probes per block
before moving on. These consumers **amortize block metadata**; increasing their
whole population does not turn them into unrelated point queries. The 16M runs
preserve physical projection near parity and the width penalty for shuffled
projection. Globally scattered reverse is measured in the separate control below.

Same-reservation edit times at 4M sparse rows are 2.324/2.360/2.673 ns/input row
for c9g Direct16/Direct32/Widths8, 3.931/3.908/6.606 on Zen5, and
4.745/4.824/6.280 on Granite. Only one quarter of input rows are edited: these
are **not ns/edit**. Widths save memory but their current writer has a material
edit penalty, especially on Zen5. Append/retire and soft packing also have small
to moderate losses. Read-throughput wins do not establish mutable dominance.

The actual c9g reverse bodies explain a specific implementation weakness:
Direct16 uses conditional selects in its endpoint search; Widths8 first searches
checkpoints, then performs a variable-length scalar cumulative walk. The latter
averages 8.5 width loads before shrink and about 9.2 afterward. This is observed
code structure, not measured branch-miss attribution. In shuffled projection,
width reconstruction occurs only for nonempty live values, but adds instructions
and register traffic around the real `memcpy`. Experimental SIMD selected-group
reverse retains the scalar implementation as a paired control.

The x86 linked audit finds Direct16's Zen5 search uses direction branches, while
Direct32 uses two conditional moves per decision. On Granite both searches are
branch-based; Direct16 adds address arithmetic and a count reload. For each
method/host, the reverse functions have identical opcode bytes in the v3 and v4
builds. Their timing differences do not establish an AVX-512 effect. The
[retained codegen audits](evidence/codegen-audits/) recover the exact bodies,
hashes and independent comparisons.

## Scattered reverse and fair byte scans

The replacement control gives libc the same running geometry as the native
width scans; an earlier launch with avoidable libc point reconstruction was
cancelled and supplies no selection evidence. Reverse includes Direct32, the
strongest observed Zen5 baseline. Two fresh process orders each provide two
repetitions per method. The new scattered trace is a permutation of the exact
queries independently checked in the blocked trace, with no retained query array.

c9g N256, sparse population, before soft packing, median ns/query:

| Rows / access | Direct16 | Direct32 | Widths8 scalar | Widths8 native |
| --- | ---: | ---: | ---: | ---: |
| 16K / block-local | 12.379 | 12.586 | 21.101 | 8.396 |
| 4M / block-local | 12.940 | 13.245 | 24.229 | 9.717 |
| 16M / block-local | 13.130 | 13.426 | 24.444 | 10.032 |
| 16K / scattered | 16.056 | 16.232 | 26.705 | 11.622 |
| 4M / scattered | 36.285 | 42.871 | 51.855 | 23.984 |
| 16M / scattered | 62.234 | 99.158 | 83.831 | 38.181 |

The native selected-group inclusive prefix removes the scalar local walk without
changing stored bytes. After-soft rankings are similar. This closes the c9g
reverse implementation loss; it does not erase mutable-write costs or establish
other architectures' selection; their results follow below. Larger populations
now visibly affect reverse when the caller stops amortizing an entire block.

The same reverse improvement reproduces on Zen5 with AVX2. At 16M before-soft
scattered, native widths take **51.906 ns/query**, compared with Direct16 92.306,
Direct32 227.758 and scalar widths 83.062. Block-local native takes 9.855 versus
the stronger Direct32's 14.496. AVX-512-build results at 4M retain that ranking;
the selected-group implementation still uses 256-bit widened lanes. This is not
an AVX-512-specific algorithm claim. Direct32 wins the small/block-local direct
comparison but loses to narrower geometry when scattered over the large set.

The [actual c9g audit](evidence/codegen-fair-c9g/) confirms one bounded 16-byte
width load, in-register widened prefixes and ownership reduction, and no helper
calls, decoded array or hot vector spills. Its direct16/direct32 controls both
use conditional-select binary searches. Native beats every baseline sample at
16M in both access patterns and both states. The same instructions are available
before V3; this result does not prove V3-specific acceleration. Explicit native
empty-body and outside-extent refusal checks were added afterward and pass
ASan/UBSan.

The [selected-group MCA audit](evidence/codegen-reverse-mca/) uses the actual
25-instruction c9g region. LLVM 21.1.8 estimates 27 uOps and 4.8 cycles reciprocal
throughput under its scheduling model. Its V2 and V3 outputs are identical because
[both CPU definitions select NeoverseV2Model](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/llvm/lib/Target/AArch64/AArch64Processors.td#L1224-L1227).
The isolated repeated region creates an artificial output-to-checkpoint-base
dependency that the complete consumer breaks with a fresh load. Neither its
17.021 cycles per repeated region nor its 38-cycle single-iteration simulation
is query throughput or measured latency. The full-consumer timings above decide
the selection; this model supplies no evidence of V3-specific scheduling uplift.

Granite also selects native widths in these reverse controls. Its AVX2 16M
before-soft scattered medians are 47.502 ns for native widths, 71.877 scalar
widths, 82.685 Direct16 and 90.017 Direct32. Block-local native is 22.999 versus
Direct32's 37.380. All five measured ISA/host configurations favor the native
owner path across the tested populations; the spike now makes it the byte-width
default and retains `reverse_scalar` as a diagnostic control. This selection
changes execution, not storage or caller ownership.

For fair exact occurrence counts at 4M rows on c9g, native physical scans win on
full/live data (Direct16 roughly 3.2–3.5 ns/input row versus libc 7.6–7.7).
Fragmentation changes the answer. With slack/dead reservations before soft pack,
searching zero costs Direct16 libc/sparse/segmented **6.244/37.020/14.553**
ns/input row. Initialized stale slack contains many zeros; reading and resolving
those physical candidates is real wasted work. Searching byte255 on the same
layout gives **5.310/3.255/3.148**. These are the same logical output contract,
not mismatched any-hit versus count measurements.

After soft pack, the sparse population's Direct16 zero scan costs
4.912/1.362/1.562 ns/input row for libc/sparse/segmented; Widths8 gives
5.064/2.736/1.710. Checkpoint geometry favors the segmented route over sparse
rank advancement. There is no universally best physical scanner: preserve the
natural per-value library route for fragmented data, and expose physical mask
processing where useful. These measurements motivate a caller choice, not an
unmeasured adaptive selector or a compulsory compaction after every shortening.

On Zen5 and Granite, sparse masks beat segmented masks for the compact low-hit
populations tested. At 4M full/live rows, Direct16 zero scans take 2.925/2.657
ns/row on Zen5 AVX2/AVX-512 and 3.486/2.917 on Granite, while libc costs
8.575/8.232 and 11.400/11.388 respectively. Both retain the fragmented-zero libc
win. The library baseline uses the host's libc dispatch; it is not artificially
restricted to the candidate compilation ISA. These results support different
execution presets for ARM and x86 without changing packed storage.

## Sustained allocation and publication control

The c9g lifecycle run completed all 30 timing cells: 48 epochs, capacity 256,
16 or 4096 Objects, four full logical projections per epoch, and a reader kept
for two epochs. Both publication paths use equal reserved headroom. Direct
capture still constructs issued damage; the damage control additionally freezes
afterimages and reconstructs from the actual predecessor. This is an explicit
current-integration cost, not a claim about a future Orbital damage API.

At 4096 Objects (1,048,576 live logical rows), median accumulated wall time
excluding separately timed oracles was 9.491 s for direct/stable/pressure and
12.102 s for damage/stable/pressure. Paired damage work is 27.5% greater, with
edit time 45.4% greater. Pressure rewrites occur at epochs 22 and 43. Rewriting
every eight epochs instead costs about 5.0% more complete work despite fewer
counted copied body bytes. Traversal ordering at cadence eight reduces measured
projection by about 3.4% versus stable ordering, but complete work changes only
about −0.3%; it has not earned a general policy recommendation.

The pressure case reaches a phase-sampled charge of 901.4 MiB and returns to
364.3 MiB after readers retire while the Store remains live. Boundary samples
miss operation peaks. Phase accounting is a separate, expensive replay and is
excluded from timing medians. Both original 512 MiB runs refuse in epoch one,
before a compaction choice; they establish insufficient capacity, not a cadence
threshold.

### Capacity and reader-phase controls

The follow-up keeps 1,048,576 logical rows, changing Objects and publication
batch size with N. All capacities perform 3,145,728 logical edits and 2,097,152
holder repairs over 48 epochs, with almost identical counted copy work.
Median accumulated `wall_ns − oracle_ns`:

| Host | N128 | N256 | N512 |
| --- | ---: | ---: | ---: |
| c9g | 14.708 s | 9.555 s | 6.294 s |
| Zen5 | 13.852 s | 9.876 s | 6.833 s |

N512 reduces Object count and allocation/publication frequency while increasing
edits per publication from 16 to 32 relative to N256. It earns a recommendation
for this batched whole-copy caller, not a universal primitive capacity. One-row
publication and Direct16 byte-budget sealing remain different questions. The
c9g Store-live settled charges are 373.67/364.33/347.13 MiB for N128/256/512;
N512 lacks phase accounting and a tight-budget run, so its lower final charge is
not evidence of a lower true peak.

Shifting cadence-eight rewriting relative to the fixed reader schedule is decisive:

| c9g N256 policy | Measured work | Phase-sampled peak | 832 MiB outcome |
| --- | ---: | ---: | --- |
| Pressure | 9.555 s | 901.41 MiB | Refuses during epoch 43 |
| Cadence 8, phase 0 | 10.022 s | 762.43 MiB | Completes 48 epochs |
| Cadence 8, phase 7 | 15.622 s | 902.34 MiB | Refuses during epoch 7 |

Both cadence variants perform six rewrites per Object and nearly identical
counted copy work. Rewrites on retained generations cost 56% more complete work
in this trace. Zen5 replicates the same budget outcomes and approximately 47%
phase penalty. Therefore "cadence 8 fits" requires the release-alignment
qualification. A useful future policy candidate is pressure with a lookahead at
reader-release opportunities; that is a hypothesis, not an implemented policy.

The c9g whole-process userspace CPU-clock profile has 13,137 samples and no
reported loss. Explicit checks occupy 36.17%; allocator symbols total 23.97%,
and `Draft::finish` 5.42%. This flat profile includes setup/oracles and has no
callchain/phase attribution. Unresolved libc samples (15.01%) cannot be labeled
copy traffic. It identifies substantial caller costs but cannot provide inclusive
mutation/rewrite costs. This earlier lifecycle campaign used Direct32; the
matched Widths8 comparison is reported under full widths publication below.

### One-edit publication and byte-budget sealing

The c9g counter-control keeps the initial population at 1,048,576 rows and
publishes one row edit per Object per epoch, for eight epochs. It executes
65,536/32,768/16,384 edits for N128/256/512; all perform the same 33,554,432
projected rows. Consequently total work divided by edits would confound the
read/edit ratio. Separate median costs are:

| N | Edit | Edit + admission + maintenance | Counted predecessor copy/edit | Projection/row |
| --- | ---: | ---: | ---: | ---: |
| 128 | 9.607 µs/edit | 19.512 µs/edit | 19,330 B | 7.821 ns |
| 256 | 15.715 µs/edit | 24.194 µs/edit | 38,306 B | 7.464 ns |
| 512 | 26.832 µs/edit | 42.210 µs/edit | 76,258 B | 6.996 ns |

All complete without refusal or rewriting. This is the counterexample to a
universal N512 choice: N128 favors isolated publication in the whole-copy caller,
while N512 amortizes batched publication and modestly improves projection.
These eight epochs test retained-reader turnover, not rank-pressure cleaning.
Zen5 replicates the update tradeoff: N128/256/512 edit cost is
8.765/14.529/24.787 µs/edit, and edit+admission+maintenance is
15.318/20.648/33.654 µs/edit. Its projection costs are 13.039/12.219/12.178 ns/row.
Granite also favors smaller Objects for isolated edits: 11.520/21.852/38.624
µs/edit, versus projection 14.847/13.121/12.098 ns/row.

`sealing.cpp` independently varies rank capacity and byte budget. It appends the
same 8192-row stream through Direct16/Direct32, with no retained input population.
One reusable 65,535-byte source buffer supplies values, so this is allocation/
construction work, not source-memory bandwidth. Direct32 at the same 65,535-byte
budget isolates endpoint width; unrestricted Direct32 permits N×65,535 bytes.
Unknown-stream construction grows geometrically through Orbital; known-layout
construction pays its sizing pass and allocates each block once. Exact values,
geometry, reverse owners and every nonfinal greedy sealing boundary are checked.

For a mixed population (seven of eight lengths 0–255, one 4096–65,535), the total
payload is 35.527 MiB. At N256 on c9g:

| Format / builder | Blocks | Build time | Requested body | Final Arena charge |
| --- | ---: | ---: | ---: | ---: |
| Direct16, 65,535 B bound / grow | 743 | 10.215 ms | 44.401 MiB | 46.871 MiB |
| Direct16, 65,535 B bound / exact | 743 | 8.820 ms | 35.527 MiB | 37.541 MiB |
| Direct32, same bound / grow | 743 | 10.458 ms | 44.401 MiB | 47.325 MiB |
| Direct32, unrestricted / grow | 32 | 8.814 ms | 50.573 MiB | 56.053 MiB |
| Direct32, unrestricted / exact | 32 | 7.259 ms | 35.527 MiB | 35.647 MiB |

The bound is exactly 65,535 bytes, not 65,536. Wider endpoints alone do not cause
the gain: reducing byte-driven sealing removes hundreds of block allocations,
but online doubling retains more headroom. For tiny lengths (0–63), rank pressure
determines block count and Direct16's smaller metadata remains useful. For
alternating maximal/empty slots, Direct16 makes 4096 blocks regardless of N;
unrestricted Direct32 makes 64/32/16. Larger N with narrow endpoints can therefore
waste sidecar capacity without improving occupancy in this full-capacity-sidecar
control. Trimming sealed sidecars would be a separate policy.

Exact construction generally wins here when future layout is known; it is not
an available online policy for an unknown stream. Requested bytes, physical
capacity, metadata, std::vector control backing, idle backing and RSS are separate
columns. Arena/mapping figures are final snapshots, **not transient peaks**.
`relocated_old_size` records requested extent preserved across address changes,
not observed bus traffic. The comparison uses exclusive builder writes without
a journal; the separate mutation and publication experiments own those costs.

## Correctness and selection limits

The two-block caller now independently checks stable/traversal order, complete
partial/many-to-one remaps and exact logical multiplicity. It tests damage from
both blocks in one batch, omitted payload patches, wrong source identity,
shared detach and last-alias retirement, old physical readers, and repeated
hard rewriting after useful edits. Logical holder capacity is independent of
physical block count: a two-to-one rewrite is adopted, edited through a logical
row beyond one block's capacity, reconstructed from damage and published.
Latest local Release and ASan/UBSan checks pass. Those are correctness results,
not lifecycle performance or bounded-retention evidence.

The native width writer now passes exact mutation/geometry and six-plane
afterimage replay checks, including omitted-checkpoint/padding negatives,
partial groups, continued append, absent sidecars and refusal atomicity. Its
real material caller passes grouped damage, retained-reader and continued-hard-
rewrite publication checks in Release and ASan/UBSan. The native mutable, reverse,
scan, publication-granularity and byte-budget controls above expose the trade-offs
used for the candidate compositions. String encoding/full string optimization
remains later work.

Orbital currently cannot capture an unchanged allocated Part natively into a
new representation, and adoption retains the current primary alongside the
alternative. Both seams were reported to Orbital LEAD. This caller prices whole
copies and continues from `bind_alternative`; a subsequent real logical edit
allows the former version to retire. It does not invent an allocation adapter
or publish a fictitious edit to hide those costs.

## Full widths publication and scheduled consumers

`lifecycle.cpp` now compares Direct32 and Widths8 through identical ObjectStore
histories: two blocks per object, the same holders and summaries, reservations
128/144/160, used lengths 64–95, same-reservation edits and changed-reservation
appends, pressure-triggered rewriting, and the same two-epoch reader retirement.
The damage arm destroys the first private draft and consumes frozen afterimages.
Direct publication still materializes issued coverage. This is a comparison of
two geometries in one complete caller, not a comparison with another database's
transaction engine. Direct16 would require a different byte-sealing policy for
the full capacity range and is not substituted silently here.

The main campaign uses two fresh process orders. Each large run has 4,096 objects,
256 logical rows per object, 48 epochs, 16 edits per publication and four full
projections per epoch. **Update time includes edit, hard rewrite, admission and
maintenance**, normalized by actual edits. It excludes read projections, history
oracles and initialization. The evidence also preserves non-oracle epoch wall
time, copied/issued bytes, holder repair, charge and the smaller-population cases.

Midpoints of the two complete process totals:

| Host / publication | Direct32 update µs/edit | Widths8 update µs/edit | Direct32 projection ns/row | Widths8 projection ns/row |
| --- | ---: | ---: | ---: | ---: |
| Neoverse V3 / direct | 2.505 | 2.202 | 7.82 | 8.24 |
| Neoverse V3 / damage | 3.347 | 3.105 | 7.92 | 8.27 |
| Zen5 / direct | 2.280 | 2.018 | 13.43 | 17.54 |
| Zen5 / damage | 2.839 | 2.642 | 14.08 | 17.70 |
| Granite / direct | 3.178 | 2.855 | 14.45 | 16.49 |
| Granite / damage | 4.050 | 3.816 | 14.53 | 16.41 |

On c9g, Widths8 takes 7–12% less update time, while projection takes 4–5% more time.
Non-oracle epoch wall time falls about 6–9%. Granite's update saving is 6–10%,
but projection costs 13–14% more; non-oracle wall improves only about 2–5%.
On Zen5, a 7–11% update saving is cancelled by 26–31% slower projection:
direct-publication wall time is flat and the damage arm is about 1% slower.
**The width kernel's mutation loss does not predict the full update result,
and the update result alone does not predict the complete mixed workload.**
Issued bytes rise about 1% because
checkpoints also change; counted source-copy/rewrite body bytes fall about 2.5%,
and sampled peak charge falls about 2%. These observations do not assign the
entire timing change to saved bytes. At 16 objects the projection penalty is
about 20–21%, and damage-arm non-oracle wall time is effectively flat. Phase-clock
diagnostics use separate 256-object runs: cloning occupies 72–74% of direct edit time; clone plus reconstruction
occupies 67–70% of the damage arm. Actual mutation commands occupy only 2–4%.
The phase totals leave a reported unclassified remainder and exclude maintenance
and rewriting, which are counted separately in the complete update figure. This
different scale supports cost attribution hypotheses, not proof of the cause of
the larger-population timing difference.

`ring.cpp` uses an actual Loom Worker and Ring with the same complete dependent
cycle as the population fixture. The Ring alternates geometry and tail consumption,
retaining its jobs and backing until drain. Direct and manually staged loops
use the same values, starting ranks and terminal-rank oracle, at widths 1/8/32.
Every format uses no-SLP. Hints address qualified metadata and the actual tail4
link; class zero selects the worker's one-line irregular hint. Ring setup is
reported separately; push, drain and stage transitions are timed.

On c9g at 16M values and 32 chains, native widths take 14.432 ns/value directly,
13.102 with manual stages, and 20.126 with the hinted Ring. Location8 takes
11.094 with manual stages, versus 20.188 in its direct loop. DuckDB takes 11.336
with manual stages. **The Ring does not win this regular two-stage workload.**
At one chain, memory dominates and large-population results are much closer;
in the hot 1K case the Ring adds several nanoseconds to sub-two-nanosecond
concurrent direct work. A scheduler cannot create independent successors for a
single chain. These are throughput times per visited value, not whole-request
latency, full-payload bandwidth, or an irregular admission/refill test.

The [c9g](evidence/composition-c9g/), [Zen5](evidence/composition-zen5/) and
[Granite](evidence/composition-granite/) composition evidence retains every sample,
both process orders, all compared formats/routes and phase/cost counters. Derived
bundles contain exact raw inputs and the analysis script; separate worker
references recover original binaries, source captures and disassembly.

### Write and projection losses

The first helper-inline control is rejected: append and same-reservation edits
regress on all three hosts. Targeting the journal call too improves append, but
does not create a universal faster writer. At 4M full/live/empty input rows:

| Host | Widths8 append ordinary → fused ns/input rank | Widths8 replace ordinary → fused ns/command | Direct32 replace ordinary → fused ns/command |
| --- | ---: | ---: | ---: |
| Neoverse V3 | 19.419 → 17.987 | 9.651 → 12.225 | 9.204 → 11.808 |
| Zen5 | 25.436 → 22.805 | 22.655 → 19.927 | 14.328 → 19.030 |
| Granite | 32.535 → 27.186 | 22.738 → 21.732 | 15.553 → 19.971 |

Append includes block initialization; replacement touches one in four input ranks,
so its ns/input-row counter is multiplied by four here. All variants produce
identical command and damage-record counts. Direct append also benefits from
fusion, so comparing fused widths only with ordinary direct would hide a stronger
competitor. The width writer still pays for a checkpoint update on append and a
prefix reconstruction on non-empty point edits. Blanket helper forcing is not a
selected preset. Useful follow-on work is operation-specific visibility and
keeping empty payload work free of prefix reconstruction; it must preserve the
checked command and issued-coverage contract.

Dense shuffled projection benefits from paying for starts once on x86. At 4M
full/live rows, ordinary Widths8 point/prepared times are 14.892/13.107 ns/rank on
Zen5 and 15.047/13.611 on Granite. Unprepared Direct32 takes 13.030/12.941.
The preparation closes most of the gap; it is charged inside every projection,
including scratch writes. c9g only improves 6.279→6.143 and remains behind
Direct16's 5.824; its fragmented case gets worse, 4.556→4.771. Sparse 1-of-16
selection does not generally amortize full preparation. For example c9g's
fragmented widths case rises 6.681→9.802 ns/selected assigned rank. Dead and empty
selections remain in that denominator; sample ranges are retained.

The same paid two-block preparation was also tested in full MVCC at **256
objects**, rather than reusing a free directory from setup. In the damage arm,
Widths8 projection changes 5.18→4.99 ns/row on c9g, 4.70→4.17 on Zen5 and
7.19→7.05 on Granite. Ordinary Direct32 is still 4.74/3.81/6.84. Preparing direct
geometry makes it slower. Zen5's non-oracle workload time improves about 2%;
Granite's is slightly worse. This smaller test supports a dense-operation lowering
but cannot establish its benefit at a larger scale; the following comparison
tests that directly.

The final paired test repeats **4,096 objects / 1,048,576 logical rows**, damage
publication, 48 epochs and four full projections per epoch, with two reversed
process orders. Each prepared projection pays for both blocks' temporary starts;
nothing persists into the next projection. Each trace includes 3,145,728 edits
and 201,326,592 projected logical rows. All 18 histories complete without refusal.
Non-oracle epoch wall excludes initialization, history checks and final teardown.
Midpoints of the two process totals:

| Host | Direct32 projection ns/row | Widths8 ordinary → prepared ns/row | Non-oracle wall seconds, Direct32 / ordinary widths / prepared widths |
| --- | ---: | ---: | ---: |
| Neoverse V3 | 7.890 | 8.263 → 8.190 | 12.182 / 11.505 / 11.491 |
| Zen5 | 13.965 | 17.516 → 14.022 | 11.689 / 11.806 / 11.150 |
| Granite | 14.130 | 16.127 → 14.673 | 15.616 / 15.152 / 14.914 |

On Zen5, preparation removes essentially all of the projection penalty against
Direct32 and makes the combined workload 4.6% faster. Direct32's two projection
samples span 13.531–14.400 ns/row: this is near parity, not a consistent projection
win. Granite retains a 3.8%
projection penalty but the combined workload is 4.5% faster. c9g's preparation
benefit is too small to select it from two orders: ordinary widths already save
about 5.6% of the combined workload, and preparation changes that by only 0.1%.
Prepared-width update costs are 3.112/2.629/3.782 µs/edit respectively, compared
with Direct32's 3.351/2.805/4.040. The preparation itself is read work and included
in projection, not hidden inside those update figures.

This closes the missing full-MVCC evidence for the width recipe and the large
Zen5 dense-projection loss in this caller. It does not select preparation for
sparse queries, prove universal mutable superiority, or measure confidence
intervals. Exact samples and raw-input recovery:
[c9g](evidence/projection-large-c9g/), [Zen5](evidence/projection-large-zen5/),
[Granite](evidence/projection-large-granite/).

### Hint fusion and Ring cost

Inlining the hint lambdas helps manually staged width reads at 16M/C32:
18.020→12.280 ns/value on c9g, 13.510→9.531 on Zen5 and 18.662→16.582 on
Granite. These are paired comparisons within each follow-up host. The corresponding
tuned Ring times are 17.399/13.849/19.813, still slower. Compare against the best
ordinary/manual alternative, not only an untuned Ring or a single dependent chain.
Some direct timings differ between worker instances, so cross-campaign timing
differences are not attributed to the source change.

Paired follow-up evidence: [c9g](evidence/composition-followup-c9g/),
[Zen5](evidence/composition-followup-zen5/),
[Granite](evidence/composition-followup-granite/). No global inlining policy is
promoted by these controls. The direct, prepared and scheduled routes remain
explicit research alternatives.

The final Granite extension runs the tuned binary at **64M values and 32 chains**,
with two reversed process orders and three samples per cell. Median ns/value:

| Geometry | Direct | Manual stages | Manual + hints | Ring + hints |
| --- | ---: | ---: | ---: | ---: |
| Direct16 | 24.598 | 25.177 | 22.677 | 26.454 |
| Widths8 native | 24.046 | 25.651 | 25.404 | 25.568 |
| Location8 | 26.548 | 27.039 | 26.927 | 26.951 |
| DuckDB16 | 26.102 | 24.073 | 24.217 | 24.329 |

The strongest route is now hinted manual Direct16. Widths still beat location8,
but the earlier eight-chain width win does not select the 32-chain composition.
The Ring approaches parity for some formats at this scale but beats none of
their best direct/manual alternatives. This is evidence against selecting it
for this regular two-stage trace, not against scheduling heterogeneous or
irregular requests. The hint policy remains the worker's default one-line L1
hint; no calibrated HardwareService policy was installed. The source capture
predates Loom's separate hardware-policy update `7eb0002`.
[Full 64M evidence](evidence/ring-large-granite/) retains every sample and setup cost.

## Composition code generation

The checked writer's small effect wrapper was an accidental boundary in the
linked consumer. On the local AArch64 Clang 21.1.8 build, forcing only the wrapper
and alias helper inline moved the boundary into `source_write_journal::before`:
append frames grew from 80 to 128 bytes. A second, spike-only callsite control
also requests inlining of the actual journal invocation. It removes those calls;
append/replace frames are 64/48 bytes, and append calls only the copy/clear library
routines. Executable text grows by 3,040 bytes against the ordinary build.

This is a comparison of compiled variants, not an isolated measurement of call
overhead. Replace remains outlined, the width prefix is computed even for an
empty replacement in the fused variant, and already-inlined soft packing gains
redundant plane stores on some journal coalescing paths. Checks, named sources,
issued coverage and coalescing semantics are unchanged. Production `effects.h`
is unchanged. The empty replacement exposes a specific implementation opportunity:
zero payload work should not need an address reconstruction merely to feed a
zero-length effect helper.

The actual Loom Ring caller exposed another boundary. Its hinted payload stages
outlined lambdas that invoked an already-inline prefetch API. Targeting both the
outer metadata call and inner hint invocation removes all 28 hint helpers from
the local binary. All 16 native geometry/payload bodies are then call-free and
stack-free; hinted payload frames fall from 48 bytes to zero. Executable text grows
9,984 bytes. Hinted manual C8 also changes from unrolled stages to loops with a
64-byte tail array, so timing that control cannot isolate the native-stage change.

These findings support inspecting the complete caller and its effect/sink
boundaries. They do not justify forcing every Ikea operation inline or scheduling
ready prefix arithmetic. Exact body/binary hashes, frame counts, caveats and full
diagnostic recovery are in the [composition audit](evidence/composition-codegen/review-index.json).
This is local emitted-code evidence; native timings determine whether a variant
is useful on a target CPU.

The [native audit](evidence/composition-codegen-native/review-index.json) confirms
the intended helper elimination in the actual c9g, Zen5 and Granite worker
binaries. Replacement remains outlined on all three. All 16 native Ring stages
per host are call-free, but only ARM is wholly stack-free. X86 hinted geometry
saves 8 bytes of RBP; the direct/width hinted payload stages also have small
red-zone scratch areas on wider policy branches. Their static presence does not
show those branches execute under the measured policy. Worker receipts recover
the exact captured sources and binaries as well as the inspected assembly.

## Design direction and remaining headroom

The evidence supports a small set of complete compositions, with independent
choices for storage, operation lowering and scheduling:

| Consumer | Supported direction | Remaining qualification |
| --- | --- | --- |
| Compact small-value collections | Checkpoint widths with native point/reverse decoding and running-offset sequential scans | Point latency, mutation rate and concurrency can favor direct geometry. |
| Growable Width sidecars | Full-capacity tombstone/checkpoint directory and growable groups of 16 R + 16 U | Grouping helps the measured large complete consumers, but hot points and U-only scans can favor planes. Partial occupancy still costs space. |
| Predominantly lengths-only scans | Keep a dense U plane available | Do not extrapolate its wins to complete Width lookup, which needs the sixteen-width prefix. |
| Packed geometry/checkpoints | Resolve actual SeriesPack/TuplePack adapters behind the same Direct/Width semantics | Paid batch projection is competitive; the current generic point composition loses substantial C8 throughput. |
| Dense shuffled widths projection | Paid temporary starts on x86 when the full block is consumed | Do not make sparse readers or every ARM caller pay preparation. |
| General mutable geometry | Keep direct endpoints available, including native 32-bit endpoints for full legal extents | 16-bit endpoints need a proven block bound at most 65,535, derived from `N*M` or an independently enforced body limit. |
| Initial mutable Direct sidecars | Separated native endpoint/used fields and a bitmap, with optional facts omitted | Grouped lookup gains are conditional; scattered U/dead updates and reverse often lose. Population timing does not prescribe the allocation layout. |
| Append-heavy caller | Fuse the effect boundary where measured useful | Global helper forcing regresses replacements; preserve an operation-specific choice. |
| Physical byte search | Native sparse/segmented masks for suitable compact data; cheap per-value libc path for stale/slack-heavy data | The caller's hit/output contract and fragmentation determine the work. |
| Publication granularity | N128 for the measured isolated-edit case; N512 for the measured batched whole-copy case | These are owner-policy results. N256 is not an evidence-backed universal compromise. |
| Regular dependent throughput | Direct/manual interleaving before adopting a Ring | Irregular refill, mixed work and calibrated hint choices remain unmeasured. |

Use schema bounds for all length fields and checkpoints, with native byte widths
as optimized implementations rather than public family names. A narrower
checkpoint is a space opportunity, not an automatic speed improvement: at 16M
rows and maximum 255, Zen5 AVX2 C1 improves from 241.183 to 233.904 ns/value, but
C8 regresses from 36.007 to 37.074. CP16's fixed trailing process position further
limits small timing claims. Keep the exact `ceil(N/16)` checkpoint capacity.

There is no consistent complete-value gain from padding a population directory
from 96B to 128B. It slightly loses on the ARM/AMD maximum-4,095 C8 controls;
Intel's change is small. The block-local fixed-prefix owner already needs a
32B alignment gap after a 96B directory to align its u16 length groups. Select
that owner geometry for its complete allocation and access requirements, not
from an unverified spatial-prefetch hypothesis. Neither directory comparison
reselects the slot capacity: the 128/256/512 decision still follows publication,
byte-sealing and occupancy evidence.

There is still concrete implementation headroom: skip address reconstruction for
empty replacement payloads, fuse journal work without harmful code growth, carry
running geometry through ranges, and retain dense preparation only when it pays.
The corrected explicit controls add two specific questions: why the remaining
Zen5 ordered R+U stream favors planes despite equal useful bytes, and how to
remove generic packed-point composition costs without sacrificing its small
storage. Native audits and LLVM-MCA narrow implementation hypotheses; the
model does not establish cache behavior or a hardware latency improvement.
There is larger owner-level headroom in unchanged-Part capture and retained
generation management, already reported to Orbital. These are distinct from
making the prefix kernel faster.

Before selecting production presets, the most useful additional evidence is a
strong mutable-container baseline with matching semantics, skew/changing hot-set
consumers, and bounded-maintenance latency under reader retention. Descriptor
mutation/relocation repair remains less explored than descriptor reads. If a
scheduled preset is proposed, qualify irregular request refill and useful work
between dependencies; regular equal-length chains cannot answer that question.
The timed populations emphasize short values: size-skewed full-payload bandwidth
and multiwriter contention are also outside these conclusions. Maximum-length
correctness and allocation checks do not establish those throughput results.
Additional formats, checkpoint spacings, SVE2 and the full string vertical can
wait for a consumer that makes their complexity worthwhile.

The proposed small interface in [DESIGN.md](DESIGN.md) has explicit ownership,
binding, point/range/selection, mutation/effects and author-composition boundaries.
The spike aggregates still need that checked/trusted public binding surface
exercised in ordinary and fused callers; the research types are not the finished
API. The broader Ikea lesson is to share semantic bodies while preserving useful
operation grain, coherent versioned sidecars and native sink outputs. Those
lessons are carried into Ikea's source and integration documentation.
The [implementation recommendation](RECOMMENDATION.md) distills the whole spike
into an initial scope; further workload coverage does not gate starting it.

The most useful next interface exercise is one small mutable Width caller using
the new fixed-directory recipe: checked binding, partial-group append, relocation
and rebinding, refusal without changes, and exact afterimage publication with an
old reader retained. It should include zero-width append, where count changes
without byte extent, and a soft rewrite where byte extent changes independently.
Existing full-MVCC tests establish those obligations for the earlier mutable
recipe; they do not qualify new physical placements automatically. This is a
focused handoff from research into a public binding design, not a request to
freeze every measured option into a preset.

This is substantially stronger evidence than an isolated kernel exercise:
independent ownership/history oracles, real afterimage consumption and old readers,
large populations, corrected compiler baselines, emitted-code review and matched
complete callers can now overturn an attractive local result. The deleted
predecessor is not a controlled before/after baseline, so no numerical improvement
against it is claimed. Broad universal superiority remains unproven.

## Recovery

Capture hashes below identify the captured source tree. Worker archives and
`source.tar.gz` have separate hashes recorded in the recovery manifests.

Initial population/geometry/ready campaign source capture SHA256:
`8b6062d6260a2a9dfa69916bac93609526990c945b46d1aec75d3d66f9a5cd4e`.

- [c9g measurements](evidence/initial-c9g/): worker `20261011T192028Z-ad787aa1`.
- [Zen5 measurements](evidence/initial-zen5/): worker `20261011T192028Z-e242dc47`.
- [Granite measurements](evidence/initial-granite/): worker `20261011T192028Z-07002048`.

The evidence references recover full sources, binaries, linked disassembly,
profiles and mapping receipts from the existing worker archives. The selection
retains timing repetitions, PMU and allocation records, hardware and worker
context. It includes losses and unsupported counters.

The same-format prefix control uses capture
`83903b6d7ae8b50ea344fc09adc389ae5d212e428ea41055623706d25c7ada7d`:
[c9g](evidence/prefix-c9g/), [Zen5](evidence/prefix-zen5/),
[Granite](evidence/prefix-granite/). These retain both process orders, all
competitors and populations, PMU/allocations and the worker's host receipt.
The capture omitted the extra hardware collector; no precise cache-tier claim
is made. Full mapping receipts and linked code remain in the worker archives.

Lifecycle capture:
`f926ee6ff8df00d25cf8b9f8a06108b5184fa26da6bd184406bbeeea710f2b63`, worker
`20261011T195723Z-3d287869`, [retained lifecycle evidence](evidence/lifecycle-c9g/).
Zen5 no-SLP capture:
`6c86fa8c8f5c1883b0602fc4f8a41eff303859c1aa61721d708333af07446960`, worker
`20261011T200235Z-e134c03f`, [retained Zen5 control](evidence/slp-zen5/).
[Granite's interrupted control](evidence/slp-granite-partial/) retains its failure
receipt and available coverage, not a completed paired 16M result. Full results
are in the verified worker archives.

Mutable/phase/capacity capture:
`dc0b2e4f93fd828b6c8fb64680fcb9678494c396a6d240ca310bdbc38d04049f`:
[c9g](evidence/mutable-c9g/), [Zen5](evidence/mutable-zen5/),
[Granite](evidence/mutable-granite/). Workers are respectively
`20261011T201709Z-6ea6a65a`, `20261011T201709Z-8a6d1444`, and
`20261011T201709Z-36e84386`. This capture predates the widths material recipe,
grouped physical scans and SIMD reverse additions; do not attribute those to it.

Fair scan/reverse capture:
`1846f5305dde67359a4d364ea447a71471f4c4120a6d62dc0a5345048f1721dc`,
[c9g evidence](evidence/scan-c9g/), worker `20261011T204358Z-016ec459`.
The initial cancelled capture `6e60baca...` is not a result used here.

Capacity/sealing capture:
`811fe0696e60a9e67f2ff7a8f01137d2ffa63819cf76ebbd9d30cdf8b54b0db7`,
[c9g evidence](evidence/capacity-c9g/), worker `20261011T205409Z-5ad8a180`.
Granite also completed this capture, including the 64M point extension, worker
`20261011T205247Z-f54ebfb4`, [retained evidence](evidence/capacity-granite/).

The same fair-scan capture also completed on Zen5, worker
`20261011T204358Z-e32743c4`, [retained evidence](evidence/scan-zen5/).
The capacity capture completed on Zen5, worker `20261011T205409Z-10f2dcee`,
[retained evidence](evidence/capacity-zen5/).

The fair scan/reverse capture completed on Granite, worker
`20261011T204358Z-e446fc76`, [retained evidence](evidence/scan-granite/).

Composition capture:
`079b1b8266d5b7f31f601de8cb3088138aa835ac9178eb8a3c1ba88879ff3622`.
Workers: c9g `20261011T213842Z-f845afb4`, Zen5 `20261011T213842Z-1e5dab1f`,
Granite `20261011T213842Z-f53c1fa2`. Follow-up capture:
`800da5d04b29fd2860089300673a44176ce84e715317208635f0ca947ae7fef9`.
Workers: c9g `20261011T220947Z-c3fd7a3c`, Zen5 `20261011T220947Z-312ca65d`,
Granite `20261011T220947Z-1cf88cff`. The main and follow-up evidence links above
contain exact inputs plus analysis for the derived summaries; their
`worker-artifact.json` separately recovers original source/binary artifacts.

Large paid-projection capture:
`0c6ccfb8f91440fa1b67998445ede90afda38413817ab2468a7cbfba8a860893`.
Workers: c9g `20261011T222327Z-2b7e7373`, Zen5 `20261011T222327Z-c729a754`,
Granite `20261011T222327Z-95509b6a`. Large Ring capture:
`f7b1d8b7eb7eb0853df5f738b2487ab735f9c932f0feeb033dd9650edb4636d3`,
Granite worker `20261011T221600Z-de895461`. The same input/analysis and separate
worker-recovery distinction applies.
