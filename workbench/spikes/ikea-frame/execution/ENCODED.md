# First actual encoded grouped consumer

2026-10-22. This is an initial same-image comparison within the
[execution investigation](README.md), not a Frame format or performance verdict.
The [schema-fidelity supplement](FIDELITY.md) explains the larger question and
the limited part this consumer currently exercises.

## Complete operation and controls

The consumer selects original-coordinate rows, applies nullable category/X
predicates, and produces grouped COUNT/SUM through the maintained Ikea hash
table. X is u32, categories are u64, and SUM is exact under the input's bounded
u64 law. Dictionaries retain part-local code editions; only touched local
groups are translated to logical output keys. Null categories have a separate
accumulator, with no reserved key. Logical input rows are released after encoding.

Every arm reads the same immutable category-code and X images supplied by the
encoding study's actual `frame_encoding_material`:

| Arm | Complete program |
| --- | --- |
| `opaque-complete` | Chooses useful kind-aware kernels inside the grouped operation. This is a serious opaque baseline, not per-row getters. |
| `rich-prepared` | Selects those block recipes during preparation, then uses the same kernels. |
| `decode-scratch` | Fully decodes each needed block into reusable scratch and performs local-code aggregation. This is the materialisation control. |

Constants, runs, FOR-domain predicates and selected delta reductions are useful
special cases. Explicit applicability selects them; the owner's total decoder
handles other cases, including patched blocks. There is no duplicate codec.
Coarse block bits remain coarse; fine selection, presence and independent X/C
validity intersect at the original positions. Whole-block reconstruction can
include inactive values and is not a protected-read contract.

All three arms currently share one local-code aggregation family. They do not
compare direct per-row hashing at high cardinality, joint column programs or
retained results across multiple queries. No automatic optimiser or independently
fused ISA reference is implemented by this instrument.

## First local observations

These are medians of nine rotated warmed whole-query samples on one local
AArch64 OrbStack VM, pinned to vCPU6. Each timed call constructs the actual hash
output, consumes every key/count/sum, then retires that output. Preparation and
reusable workspace are outside the timer. Sorting for the independent oracle
comparison is outside the timer. No fixed overhead is subtracted.

Units are **ns per complete query**. They are neither dependent point latency
nor interleaved reciprocal throughput, and establish no cold/DRAM/Loom result.
All raw repetitions, including the losing arms, are retained in the
[selected evidence](evidence/encoded-local/measurements.json).

| Input / query | Opaque complete | Rich prepared | Decode scratch |
| --- | ---: | ---: | ---: |
| OS / all | 738,209 | 737,084 | 741,293 |
| OS / scattered | 639,793 | 637,626 | 642,626 |
| OS / interval | 209,501 | 208,417 | 210,625 |
| OS / few categories | 457,000 | 456,126 | 463,042 |
| Region / all | 420,918 | 492,792 | 436,959 |
| Region / scattered | 382,834 | 390,167 | 382,209 |
| Region / interval | 89,250 | 90,500 | 88,166 |
| Region / few categories | 342,959 | 349,500 | 345,958 |
| IMDb / all | 11,667 | 11,625 | 11,709 |
| IMDb / scattered | 9,625 | 9,625 | 9,667 |

There is no broad runtime gain for richer preparation in this comparison.
Most differences are small; Region/all's rich median is materially worse.
That case is also variable: opaque samples span 381,417–876,792 ns, rich
393,250–709,084 ns and scratch 394,626–760,251 ns. These nine VM samples do not
establish a significant cause or a reliable ranking of close arms. Their common
aggregation/masking work and the current choice of block recipes also limit
what can be inferred about the encoding algebra.

Both ClickBench inputs have 196,608 records across three full parts, in original
row order. OS uses OS/ResolutionWidth and has no natural NULLs. Region uses
RegionID/ConnectTiming, with explicitly synthetic X NULLs where ConnectTiming is
zero. IMDb uses kind_id/production_year from the first 4,096 title records, in
two parts, with 205 real NULL years; the second part's dictionary order is
deliberately reversed. Source provenance and logical-input hashes accompany
the measurements. The query/mask patterns are authored, not captured query traffic.

## Memory and preparation are separate observations

The prototype retains wide, narrow and exception decoder arrays per column.
Some arrays reserve a slot even for blocks which do not need that reader. Body
compression alone therefore substantially understates its prepared footprint.

| Input | Body + descriptors + run geometry, B | Encoded buffer capacity, B | Prepared decoder capacity, B | Presence + validity, B | Image controls, B | Dictionary capacity, B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OS | 72,267 | 79,876 | 356,352 | 73,728 | 13,752 | 736 |
| Region | 114,040 | 144,802 | 356,352 | 73,728 | 13,752 | 7,976 |
| IMDb | 5,299 | 7,294 | 7,424 | 1,536 | 1,232 | 32 |

The logical-body column is an alternative view of the storage counted by buffer
capacity, not an extra allocation to add to it. These are reported container
bytes/capacities, not complete allocator costs, RSS or memory traffic. Caller
query selections and fixture controls are outside this table. The witness
currently stores three mask planes even for all-live, non-null images.

For the all-row query, opaque/rich preparation took respectively 6,542/8,292 ns
on OS, 36,916/14,542 ns on Region and 1,250/1,000 ns on IMDb. These are single
preparation observations, not stable timing estimates. Corresponding plan and
scratch capacities were 2,576/3,344 B, 22,084/22,852 B and 356/372 B. Other
queries' preparation and all arms' state are preserved in the evidence. A fine
selection currently pays per-position `vector<bool>` inspection; native mask
attachment is another candidate, not a necessary Frame cost.

The practical leads are to investigate compact shared preparation, useful
schema/content omissions, and programs that exploit different structure across
the whole consumer. These observations do not select an architecture or imply
that rich schemas must materialise large decoder objects per block.

## Qualification and reproduction

The original logical bridge passes 15 checks. The encoded consumer passes 420
authored query/program comparisons against an independent logical oracle,
covering all five current kinds, selected deltas, patched exceptional positions,
partial masks/nulls, recoded dictionaries, holes, tails, empty/null-only parts,
full 65,536-position parts and maximum admitted integers. The measured corpus
run adds 93 exact full-result comparisons across 31 queries and three programs.
These are comparisons of complete groups, not only matching checksums.

The first source cut failed on Region sums: a partial reducer treated any kind
remaining after its early branches as delta, while a four-kind statistics array
overflowed when patched was added. The owner decoder and scratch control were
correct. **All timings from that cut were discarded.** The corrected cut uses
an explicit delta condition, total fallback, extensible statistics and a
patched/null/partial-selection regression. This exposes a real composition
obligation: a correct child operation does not prove a parent's structural
shortcut remains applicable after substitution.

The accepted frozen source digest is
`36b25b3a292643f94dc98d7c1b02c36564b9da155bfa816620552d2d98801760`;
the ELF SHA-256 is
`701f7f7d0def4b9dc4c1a1e47445f7329e2d100505b65423b31c3d707d668e48`.
The [recovery reference](evidence/encoded-local/artifact.json) retains raw output,
source and disassembly; the compact evidence keeps every query/arm sample,
preparation, footprint and counter, replacing verbose group arrays with their
checked count/hash. Source and result hashes identify the accepted cut.

Pinned Clang 21.1.8, C++23, RelWithDebInfo/O2, generic CPU tuning. The grouped
consumer disables auto-vectorisation; the owner's compiled material follows its
ordinary target defaults and can auto-vectorise restoration loops. This is not
an ISA-fused parity qualification. Both arms call the same owner material.
Disassembly confirms the timer encloses `execute`, actual primary/overflow hash
entry consumption into a volatile sink, and deallocation before the second
clock read. That inspection verifies measured scope; it does not attribute the
runtime differences to a microarchitectural cause.

Use the build/check commands in the [execution guide](README.md#actual-encoded-grouped-consumers).
`encoded_check.py --input PATH... --trials 9 --output REPORT` reproduces the
comparison on prepared logical inputs. Add `workload.mask_variants(document,
positions_per_block=256, blocks_per_group=16)` to reproduce the additional five
coarse-selection controls. Retain an independent source-qualified result before
drawing a broader performance conclusion.
