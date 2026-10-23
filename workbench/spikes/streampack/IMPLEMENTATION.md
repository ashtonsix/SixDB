# StreamPack implementation qualification

StreamPack is implemented in [Ikea](../../../ikea/docs/streampack/usage.md), with
caller-owned payload, directory, length and control windows. Reservation metadata
now belongs to [Boundlist](https://github.com/ashtonsix/SixDB/blob/719b0f5709c9190bd592888cf8fbbbd379bce960/ikea/docs/boundlist/usage.md); its
[extraction qualification](../boundlist/GRADUATION.md) covers shared kernels and
the later reverse/search changes. This note preserves the original qualification
against its captured sources. It separates
evidence for that module from the earlier comparative spike. Native comparisons
and real owner qualification are recorded separately below.

## What crossed into Ikea

Direct and Width share checked commands, typed author bodies and explicit NEON,
AVX2 and AVX-512 geometry. Schema bounds determine native field widths. Fixed,
full, immortal and maximum-zero facts remove fields. Width has exactly one start
checkpoint per sixteen ranks and grouped R/U by default; separate U remains an
option. Capacity is fixed at **256** for both geometries, independently of byte
limits. The [capacity decision](RECOMMENDATION.md#selected-storage-choices) replaces
the earlier three-capacity API; the portable schema is now eight bytes (version 2).

The implementation supplies point/length/reverse reads, carried ordered ranges,
sparse and paid dense selection, append/replacement/retirement, rank-preserving
packing and selected transfer. It reports issued writes through Ikea's existing
source-qualified effects. Allocation, publication, holders, remaps, compaction
policy, string prefixes and encoding remain with their respective owners.

Schema preparation is shared; a named view binds windows without scanning their
contents. External content validation is separate. Whole-operation author dispatch
resolves the geometry and native lane types before a rank loop or CPS chain.
`rebind` changes block windows while retaining the exact shared immutable schema.

## Fixed capacity and schema simplification

The implementation now fixes N=256 instead of exposing 128/256/512. The choice
and its publication trade-offs are recorded in the [recommendation](RECOMMENDATION.md#selected-storage-choices).
Capacity is absent from `description` and its wire representation. The eight-byte
version-2 encoding combines version/flags, a u16 maximum, u24 block limit and u16
optional fixed reservation. It rejects the old 16-byte version. Native N256
payload/sidecar bytes, lane widths and effects are unchanged; transfer coordinate
scratch falls from 4 KiB to 2 KiB.

The six selected module/example and real Loom/Orbital checks pass locally in
Release and ASan/UBSan, including stack-use-after-return and leak detection. The
fixed-capacity Loom matrix now has 262 panels over twenty schemas. New independent
wire fixtures cover all flags, bounds, absent versus fixed zero and rejected old
versions; maximal blocks exercise all 256 ranks and the final byte of 16,776,960.

A local OrbStack ARM before/after check ran both process orders, with two internal
repetitions, at 256 and 1,048,576 values and maxima 255/4095. All 216 timing cells
validated their checksums. Unchanged controls varied substantially too, so the
run supports no speedup or strict timing-parity claim. Linked disassembly has the
same mnemonic sequence in all 200 matched measurement bodies; native schema-field
displacements change. Both compiler vectorizers remain disabled. These are
regression checks, not replacement native architecture measurements. Local details
are in ignored `build/streampack-n256`; the retained native evidence below remains
qualified to its original source capture.

## Corrections made during implementation

Performance inspection changed the implementation while correctness was being
established. The first checked reader outlined an `expected<span>` return through
a hidden stack buffer; the small wrapper now stays fused with its consumer.
A CPS example initially dispatched inside each stage and materialized the batch
packet. Dispatch now encloses chain construction. Native group iteration also
keeps both its fold and callback invocation inline, eliminating a second packet
buffer. The final ARM cut has 24 typed stages with no calls or stack traffic;
linked native-host confirmation is recorded below.

Selected transfer first admits a bounded 2 KiB coordinate plan, then writes it.
This avoids a source × destination codec instantiation cross-product and duplicate
prefix decoding. It retains refusal-before-write semantics and preserves ordered
selection and duplicates. It does not impose scratch on point reads.

Independent review caught stock journal/source aliasing during transfer and
borrowing hazards from temporary destination views. Admission now rejects the
former and the API deletes temporary-view mutation/reader overloads. Native tests
include actual 65535-byte reservations and widened carries, rather than relying
only on small values under a large declared maximum.

## Checks and measurements

The production history and guarded-boundary checks use independent value/byte-owner
oracles, damage afterimage replay and refusal snapshots. They cover empty/live/
retired values, tails, native group output, aliasing, omitted facts, full blocks,
byte-limit boundaries and packing/transfer. The original captured checks also
covered the then-selectable 128/512 capacities. The four module checks/examples passed
locally in Release and ASan/UBSan. The broader Ikea validation passed all 31 checks.
Ten public/author headers compile
independently. The final local checks also consume and reproduce a literal stored
byte fixture that is independent of the production writer/reader pair, and refuse
corrupt geometry, U and count.

`implementation_bench.cpp` compares the module with the same-byte spike body,
a checked same-byte control and an eight-byte location/length descriptor over the
same values. One dependent chain measures serial service time; eight independent
chains measure achieved overlap, not a processor's peak reciprocal throughput.
Ordered, rotated and permuted projection, carried range and paid block selection
measure whole consumers. Each path warms itself and proves its checksum before timing. All
candidates use the same forced-inline read-callable boundary and both compiler
vectorizers are disabled. PMU reports preserve event availability and multiplexing.

The maintained `rotated` projection visits `(i+17)&(rows-1)`. Its additional
`permuted` projection uses an invertible xor-shift and odd multiplication, without
loading a rank array; a cold property check proves each row is visited once and
the traversal is not sequential. Both retain the full-population checksum and
include their index arithmetic in the measured consumer. Historical captures
named `shuffled` use `(i*65537+17)&(rows-1)`: this is a rotation at 256 and 65,536
rows, and an affine permutation at larger populations. Those retained timings do
not qualify the new permutation. The interface-cohesion study captures the
projection-only harness change identically in both comparison arms.

Populations are 256, 1,048,576 and 16,777,216 values, with maxima 255 and 4095. Pointer
chases perform 1,048,576 visits over the population; projection traverses all rows.
The mutable view/metadata representation is included in module/control costs.
The flat descriptor is a primitive read control, not a claim about a complete
transactional container. Native runs target c9g NEON and Zen5/Granite Rapids AVX2
and AVX-512. Source captures and worker receipts identify exact measured code.

The Loom and Orbital leads own the real consumer fixtures. They exercise returned
scheduling boundaries, drain/overflow, growth/rebinding, coherent retained readers,
actual damage publication and holder repair. The earlier spike's publication results do not qualify the new layout; the
production consumers are assessed separately below.

## Native primitive findings

The original native qualification source capture is
`dfd278c9010f775c2faf64a7bac5c034e5e8320b35994623978166f851e646da`.
The subsequently added literal wire fixture, explicit empty optional initializer
and repository formatting do not change the measured hot bodies. Clang 21.1.8
builds and runs all four module tests/examples under each execution profile.
Retained [c9g](evidence/implementation-c9g/artifact.json),
[Zen 5](evidence/implementation-zen5/artifact.json) and
[Granite Rapids](evidence/implementation-granite/artifact.json) comparisons contain
both repetitions, every path and all three population sizes. Their archives also
contain PMU observations, linked code, compile commands and exact captured sources.

For hot N=256, maximum=4095, the complete typed Width reader is close to its
same-byte body in dependent service time. The overlapping route retains a gap on
c9g and Granite, whereas Zen 5 is near parity. Times are ns/value, two-repetition
means, not confidence intervals or portable architecture ratios:

| Target | Typed C1 | Body C1 | Typed C8 | Body C8 |
| --- | ---: | ---: | ---: | ---: |
| c9g NEON | 9.714 | 9.826 | 1.824 | 1.609 |
| Granite Rapids AVX2 | 10.749 | 10.750 | 2.189 | 2.029 |
| Granite Rapids AVX-512 | 10.819 | 10.783 | 2.135 | 1.990 |
| Zen 5 AVX2 | 9.970 | 9.934 | 1.582 | 1.549 |
| Zen 5 AVX-512 | 9.993 | 10.009 | 1.577 | 1.574 |

Typed Direct takes 5.51/0.93 ns for C1/C8 on c9g, 5.87/1.13 on Granite AVX2 and
4.27/0.80 on Zen 5 AVX2. The remaining roughly 13% overlapping Width cost on c9g
and 7% on Granite is a tuning opportunity, not a dependent-latency gap.

The c9g max 255 unchecked Width call boundary was removed during qualification:
C1 changed from 12.43 to 9.06 ns and C8 from 2.88 to 1.60 ns/value in successive source
cuts on the same host. Hardware counters and disassembly identified the extra call; the final
checked path is 1.88 ns/value versus 1.91 for its checked same-byte control.
Explicit Direct paid-start decoding reduces the final hot block-selection result
to 1.32–1.42 ns/value on c9g and 1.31–1.35 on Granite, from the earlier scalar
1.57–1.61 and 1.78–1.90 respectively. The full operation pays scratch preparation. Zen 5 finishes this selection in about
0.87–0.90 ns/value for maximum 4095.

The flat eight-byte descriptor wins many of these read-only controls, including
large-population dependent lookup. Its global location directly identifies a
payload address; the block-oriented paths include owner-window lookup and sidecar
decoding. Both are useful costs, but this fixture does not demonstrate universal
StreamPack superiority or a complete transactional-container comparison. The
large-population results also vary more between repeats than the hot controls.
The earlier spike's compact-layout wins remain scoped to its own matched callers.

Ordered work should use running geometry: for max 4095 c9g reads at 16M values,
Width range is about 1.2 ns/value while repeatedly calling ordinary point lookup is
about 4.9 ns/value. Zen 5 Width range is about 0.82 ns/value at the same
population, versus 1.30–1.34 for Direct. Dense shuffled reuse pays one group
decode per block; a sparse selection need not pay that preparation. The ordinary one-off route remains
convenient but repeated use inside a tight loop retains schema dispatch costs.

PMU counts cover the calling thread in user space. On c9g, cycles, instructions
and branch misses are available without multiplexing; the generic cache-miss event
is unavailable (`EINVAL`), not zero. All four events are measured on both x86
hosts, also without multiplexing. The [hot Width counter selection](evidence/implementation-counters/selection.json)
keeps both controls, checks, maxima and repetitions across all five profiles.
On c9g, the maximum-4095 C8 path retires 37.375 instructions/value versus 31.750
for the body. Exact successful control-flow paths execute 299 versus 254
instructions per eight values; both perform sixteen reductions, and stack-transfer
counts are 27/26. This identifies extra schema/control-flow/address work without
attributing the whole timing gap to a single branch.

Prefix arithmetic is unchanged from the spike;
llvm-mca models for those bodies do not model these added callable boundaries,
metadata cache misses, publication or scheduler service time. Actual linked-code
inspection and hardware timings qualify those boundaries instead.

The final c9g linked-code audit verifies all 24 typed CPS stages (12 point and 12
native-group) have no ordinary calls or stack traffic and each forwards through
one continuation branch. The ARM entry shim's 160 B frame is intentional ABI
protection, separate from those stages. All selected C1/C8 typed and same-byte
successful read paths have no ordinary helper calls; cold fixture aborts remain
on checked failure paths. Paid dense selection has a 1024 B caller scratch array
plus 48 B saves, with explicit SIMD Direct endpoint preparation; this is paid
operation state, not a hidden per-point result buffer.

On both x86 hosts, each AVX2/AVX-512 build emits twelve native packet stages with
no calls or stack traffic and one continuation jump. The example optimizes away
its x86 scalar point stages, so that example does not qualify emitted scalar hops;
the Loom consumer supplies that assessment. Paid Direct preparation uses explicit
endpoint vector loads/stores (VPERMD/VPBLENDD or VALIGND for the first preceding
packet). Its 1024 B scratch remains visible; total frame footprints are
1128–1144 B on Granite and 1176 B on Zen 5. The [final codegen receipt](evidence/implementation-codegen/review-index.json)
identifies exact sources, executables, assembly and inspected bodies. Recoverable
archives include the selected disassembly and all original worker output.

## Real allocation and publication

The [Orbital assessment](IMPLEMENTATION-ORBITAL.md) owns the complete-operation
interpretation and reproduction details. Its caller uses real Allocation storage,
Object scopes, retained Parts and a caller-owned holder map. Release and sanitizer
histories cover growth/rebinding, late readers, escaping Parts, issued effects,
count-only and extent-only changes, soft packing and hard-transfer holder repair.
It also publishes at a returned Loom boundary while pending reads retain their
original coherent interpretation. Native Zen 5 histories pass under AVX2 and
AVX-512; all 60 operation cases and three repeats per profile are retained.

That whole-clone control exposes ownership costs beyond the kernel: a three-row
Width replacement copies 1,194 bytes while issuing 19 command bytes. Separate
allocations cost more than grouped storage in this caller. These measurements do
not select an optimal point-publication policy. Existing Orbital Hold-backed
`independent_shared` capture can share unchanged payload while metadata is cloned;
old SIMD-readable tails must still remain immutable. No new allocation protocol
or hidden slot allocator was required in StreamPack.


The live-tail extension in `implementation_orbital_check.cpp` covers Direct and
Width with 256 short values, metadata before payload, and deliberately initialized
surplus backing. An older published representation and escaped Part keep the
wide footprint readable through trimming. After they retire, the surplus is
reclaimed while a newer prefix-only representation remains live; values, sidecars,
address reservation, budget refund and regrowth are checked. Both Part-retention
modes pass local Linux ARM release and ASan/UBSan, including stack-use-after-return
and leak detection. This is a lifetime/integration result; the
[Iceberg–Swiss study](../../notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026) owns complete-consumer memory and
throughput measurements for its migrated allocation policy.

The [retained-Part comparison](IMPLEMENTATION-REUSE.md) now isolates holder-only
updates: exact full copying, reuse through a protected selection, and reuse
through escaping Parts. It exercises the public Orbital composition seam
without putting StreamPack policy inside Orbital.

## Scheduled dependent reads

The real [Loom consumer](implementation_loom.h) shares typed read bodies between
serial, point-chain, manually interleaved and Ring routes. It exercises ragged
chains, refusal/refill, overflow, returned quanta and complete drain. All 526
correctness panels pass, including ASan/UBSan with stack-use-after-return and leak
checks. The owner fixture above also publishes during this consumer's returned
boundary without changing its retained reads.

The [retained scheduling experiment](evidence/implementation-loom/handoff.json)
has 64 arms, three samples each, with widths 1/8/32 and hints on/off. Its large trace demands 138,832,512 distinct payload-line
bytes. Median timed-loop ns/link are:

| Geometry | Serial | Manual 32 + hints | Ring 32 + hints |
| --- | ---: | ---: | ---: |
| Direct | 61.427 | 20.905 | 23.719 |
| Width | 66.415 | 23.408 | 25.069 |

Ring provides about 2.6× serial throughput here, but remains 13.5%/7.1% behind the
matched manual route. Hints lose on the hot trace, which demands 33,920 payload-line
bytes. This is local OrbStack VM evidence, without PMU or HardwareService policy;
it is not a native c9g timing. Sample time includes resets, admission, refusal,
refill, drain and result delivery, while setup and close are separate. For example,
the large hinted Direct Ring arm has about 27.35 ms setup and 0.72 ms close.
Request storage totals 176 B: a 152 B Frame plus 24 B Outcome. The CLI
`frames_bytes` already includes that Outcome breakdown. Ring remains an optional
owner composition.

The [current callback audit](evidence/implementation-loom/codegen/20261012-final-cut/analysis.json)
checks 96 actual point-chain/Ring callbacks per target (Neoverse V2, Zen 5 and
Granite Rapids), with both compiler vectorizers disabled. No ordinary helper calls
or vector stack transfers remain. Hinted x86 branches retain scalar stack
references; their static site counts are not hardware cycle or stall measurements.
The persistent Ring continuation tuple is 40 B, separate from callback stack use.

## Remaining opportunities

The concrete next opportunities are owner copy/publication policy, the hot Width
schema-routing cost, and scheduling overhead where manual overlap already wins.
These are measured limits of the implemented composition, not reasons to hide
more machinery inside StreamPack. Flat location/length descriptors still win many
read-only controls. This work does not claim an optimal transactional container,
a full string vertical, SVE2 tuning or a universal preset winner. The fixed
256-slot capacity is a product decision informed by the opposing measured costs,
not a claim of winning every publication pattern.
