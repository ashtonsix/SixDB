# StreamPack investigation

The design question is how to combine dense variable-byte storage with cheap
ordinary operations, flexible caller composition and correct version publication.
The [design](DESIGN.md) defines the value/ownership model. [Results](RESULTS.md)
records observations. A measured layout is not automatically a production preset.
The [implementation recommendation](RECOMMENDATION.md) separates the selected
primitive scope from further performance questions.

## Questions that select a composition

1. What does the consumer touch: lengths, complete values, a payload-derived
   successor, or bytes across a whole block? Is it sequential, selected, shuffled,
   repeated-hot, or dependent? Does it emit spans, masks, rows or occurrences?
2. Which facts can the schema derive? Full reservations, immortal values,
   fixed widths and optional fields need different metadata. Neither a tombstone
   nor an empty reservation encodes logical null automatically.
3. What changes together? Account for geometry, lengths, liveness, holders,
   summaries, allocation movement and version retention as one logical operation.
4. What is the execution grain? Compare directly inlined work, paid preparation
   reused inside an operation, manually interleaved accesses, and scheduled memory
   continuations. Keep one-operation latency separate from concurrent throughput.
5. What pressure causes movement? Rank capacity, byte extent, reservation slack,
   live/dead fraction, hot/cold updates and retained readers have separate effects.

## Concrete comparisons

| Comparison | Existing coverage | Limits and next useful discriminator |
| --- | --- | --- |
| Point/length lookup | Direct16, global Flat32, checkpoint widths, location8 and actual DuckDB descriptors; independent and dependent traces, large isolated populations | Small values dominate the full-domain cycle. Skew, descriptor mutation and varied payload sizes need separate consumers. |
| Sequential/shuffled projection | Exact bytes and row boundaries, full/live and slack/dead histories, direct16/32 and widths; paid preparation and 1-of-16 selection | Dense preparation can amortize reconstruction; sparse or fragmented requests may lose. Full MVCC charges preparation per projection. |
| Primitive writes | Initialize, append, same-reservation edit, retire and soft packing with materialized source-qualified effects | Command checks, copying and journal work all contribute. Matched helper-visibility controls retain those semantics; a trusted-body comparison remains distinct. |
| Full MVCC | Direct32 and Widths8 through matched real ObjectStore capture, prepare/install, retained old readers, holder repair and sustained compaction | One owner/copy policy; no multiwriter contention or external transaction-engine baseline. Phase clocks are separate diagnostic runs, not a sum of all operation work. |
| Scheduled reads | Actual Loom Worker/Ring; direct interleave, manual stages and Ring, hints on/off, widths 1/8/32 | Full-domain equal-length chains test overlap and scheduling cost. Irregular completion/refill, mixed requests and poll/I/O latency are not established. |
| Physical byte scans | libc per-value, sparse body scanning and segmented masks; explicit sink/output contracts; NEON/AVX2/AVX-512 | Hit density and stale slack can reverse rankings. Full string encoding, lexical comparison and regex belong to later work. |
| Reverse byte ownership | Exact dead/slack/zero-width rules; direct16/32, scalar and SIMD checkpoints; block-local/scattered probes | Real whole-body consumers may amortize ownership differently from independent reverse queries. |
| Growth and sealing | Real Orbital grow/rebind, independent rank/byte limits, known-layout allocate-once versus online growth, tiny/mixed/maximal values | Final charge is not transient peak; sidecar trimming and colocation policies are not selected. |
| Compaction and retention | Soft and hard, stable/traversal order, cross-block dedup/repartition, continued edits, complete remaps, reader-phase and finite-budget controls | A reader-aware cleaning policy is not implemented. Repair indexes, bounded incremental maintenance and full codec benefits remain separate questions. |
| Derived geometry | Group-relative, width16, fixed/presence read controls, absent used/dead planes | They have not earned additional complete mutable recipes. Extra checkpoint spacing and decoded caches need a concrete reuse case. |
| Metadata placement and component reuse | Direct/Width with schema-sized used fields, no terminal checkpoint; separate planes, native groups at tight/64/96/128-byte strides, actual SeriesPack and TuplePack controls | New comparison fixes capacity 256; correctness includes maxima 255/4,095/4,096, partial/empty groups, reverse owners and source-qualified metadata damage. Native timings use 255/4,095, all-live short values with slack and occasional maximum reservations. No new complete-MVCC claim. |

The main native targets are Neoverse V3, Zen5 and Granite Rapids. Source-level
ISA controls cover NEON, AVX2 and AVX-512; each result identifies the emitted path.
SVE2 is optional exploratory work, not a prerequisite for the primitive contract.

## Correctness and fair measurement

Use an independent logical history and exact physical owner oracle. Include empty,
dead and maximal slots; repeated endpoints; partial SIMD groups; absent sidecars;
schema/body/rank maxima; naturally aligned typed metadata at non-SIMD-aligned
addresses; exact tails; aliasing and refusal.
Verify complete result identities and output boundaries, not only a checksum that
could hide wrong-row answers. The large fixtures generate expectations without
retaining a second population or request array during measurement.

A mutation's issued byte coverage must feed real owner publication. Omitted body,
used-length, tombstone, checkpoint, count, holder and summary updates need negative
checks. Preserve the exact predecessor, reject stale or wrong-source acceptance,
retain old readers and continue meaningful edits after hard rewriting. A private
candidate can be destroyed before its owned afterimages are reconstructed.

Match complete semantics across contenders. Charge actual descriptor/sidecar
bytes, builders, preparation, holders, output, effects and lifecycle work where
the claim includes them. A direct ready-read control need not pay for a decoded
cache it does not need. A library scan gets equally cheap running geometry.
An actual library component comparison is not a benchmark of its entire database.

Keep these quantities distinct: encoded extent, allocated capacity, touched
planes, RSS, Arena charge, retained bytes, logical copy counters and measured
traffic. Repeated warmed traces do not establish cold-cache latency. A large RSS
alone does not show that the timed operation reached DRAM.

Inspect linked consumer code when performance differs. Check load dependencies,
helper calls, spills, compiler-created gathers and exact native bounds. Use
llvm-mca only for the extracted path and its actual scheduling model; synthetic
recurrences and unsupported CPU models cannot establish hardware throughput.
Profiles must distinguish fixture validation from useful timed work.

## Field placement and partial occupancy

The first field-isolation campaign left LLVM loop vectorization enabled. Its
layout conclusions are withdrawn: different generated batches and reductions
confounded the intended explicit-SIMD comparison. Its archived timings identify
those binaries only. The replacement uses `layout_native.h` with both automatic
vectorizers disabled and a fixed logical packet of sixteen requests:

- NEON uses four four-lane vectors; AVX2 uses two eight-lane vectors; AVX-512
  uses one sixteen-lane vector. Rank generation, addresses, extraction and
  accumulation are explicit. Sums widen to 64 bits before accumulation.
- Ordered fields use contiguous native widening loads. Permuted fields use
  explicit narrow lane loads; x86 additionally tests hardware dword gathers
  with bounded extraction. NEON staged loads are not hardware gathers.
- Reserved-only, used-only, liveness, checkpoints, live used lengths and R+U
  are distinct consumers. A U-only probe does not model a complete value read,
  which also needs its reservation prefix and checkpoint.
- Complete lookups follow the same full-domain payload cycle under C1 and C8.
  C1 measures dependent operation service time. C8 measures overlapping service
  rate; eight chains do not by themselves establish peak reciprocal throughput.
  Scalar state/control and payload loads surround an explicit SIMD prefix.
- Packed SeriesPack/TuplePack controls enter the matched full-block consumer
  through their native group decoders. An explicit u32 exclusive prefix carries
  across native sublanes and groups. Used lengths, tombstone masks, prefix
  preparation and variable-length copies are all charged. This is distinct
  from arbitrary-rank packed extraction.

Native build-only runs precede timing. Linked-code review checks the actual
packet, helper boundaries, scratch traffic and unintended dependencies through
lane assembly or checksums; the timing runner verifies the reviewed binaries.
Exact lane checks include mixed/maximal widths, live empty
and dead slots, repeated/final ranks, multiple blocks and both 96-byte directory
phases. New operation names and separate bulk summaries prevent old/new or
field/full-consumer rows from being silently combined.

Measure fields separately before attributing a composed result to shuffling.
Sequential/permuted rank requests measure access order; native full-group
projections measure packed decoding and lane rearrangement. Reserved/used lengths,
bitmap consumption and checkpoint decode have different useful carriers and
explicit item counts. Mixed per-lane oracles must check ordering, not just sums.

The hybrid separates 16-slot R/U groups from a full-block tombstone/checkpoint
directory. Matched controls isolate directory placement, R/U grouping, checkpoint
narrowing and 96/128-byte stride. The growth comparison then charges upfront
initialization, partial occupancy, actual Orbital capacity, private growth and
internal repacking at N=128/256/512. It keeps schema checkpoint width fixed and
checks mixed, all-zero and all-maximum histories. These are private journaled
append lifecycles; new-format MVCC qualification remains a separate owner test.

The Direct follow-up closes a gap in that replacement: the initial explicit
campaign only instantiated Width. `streampack_layout_direct` keeps both automatic
vectorizers disabled and compares native population/block planes, grouped
endpoints/used/dead at admitted tight/64/96/128B strides, actual SeriesPack and
actual TuplePack row layouts. C1/C8, ordinary running or shuffled projection,
paid sixteen-slot projection, reverse lookup and journaled metadata updates have
separate operation names. Direct batches shift endpoints into starts rather than
computing a width prefix. TuplePack projects byte-code planes and reconstructs
native endpoint/length lanes explicitly, including three-code used lengths at
the inclusive 4,096 boundary. Native measurements now cover all three hosts and
both x86 builds; point and reverse endpoint/state access remains scalar. The
update control changes only used lengths and tombstones, not endpoints, payload
or publication. Earlier Direct layout timings remain withdrawn as selection
evidence; [results](RESULTS.md#direct-endpoint-layouts) owns the corrected comparison.

## Prior art and what is actually compared

| Source | Relevance | Evidence status |
| --- | --- | --- |
| [Arrow variable-size and view layouts](https://arrow.apache.org/docs/format/Columnar.html) | Separates offsets/payload and logical views from physical storage | Layout reference. Flat32 is an authored offsets control, not execution of Arrow's engine. |
| [DuckDB string_t](https://github.com/duckdb/duckdb/blob/6ddac802ffa9bcfbcc3f5f0d71de5dff9b0bc250/src/include/duckdb/common/types/string_type.hpp) | Inline tiny values and direct outlined descriptors | Actual pinned component in point/projection controls. Its transactional update machinery is not the MVCC baseline. |
| [Velox StringView](https://velox-lib.io/blog/stringview-api-changes/) | Separates buffer lifetime from views; temporary inline descriptors can invalidate borrowed spans | Additional lifetime/interface reference. No Velox performance claim. |
| [PostgreSQL page layout](https://www.postgresql.org/docs/current/storage-page-layout.html) | Stable item identifiers and movable variable-sized storage | Structural prior art. No actual PostgreSQL page-update baseline has been timed. |
| [StringZilla](https://github.com/ashvardanian/StringZilla), [memchr/Finder](https://github.com/BurntSushi/memchr) | Strong SIMD/prepared substring search; distinguish setup and output semantics | Pinned StringZilla in search preparation/checks; memchr is literature/source review. Primary exact-byte results use the host libc and native controls. |
| [AMAC](https://www.pure.ed.ac.uk/ws/files/23617672/AMAC_VLDB16_1.pdf) | Independent retained lookup state can overlap dependent memory work and accommodate irregular completion | Informs the Loom caller; published speedups do not transfer to this workload. Equal-length Ring traces do not test its irregular-refill advantage. |
| [Group/software-pipelined prefetching](https://www.pdl.cmu.edu/PDL-FTP/Database/icde04.pdf) | A simpler staged-batch competitor for independent memory accesses | Motivates the manually staged control alongside Ring. |
| [Log-structured filesystem cleaning](https://www.cs.cmu.edu/afs/cs/academic/class/15712-f08/www/readings/Rosenblum92.pdf) | Utilization and update age matter to repeated cleaning | Policy inspiration; no imported cleaning formula or filesystem cost model. |
| [DuckDB update storage](https://duckdb.org/2024/10/30/analytics-optimized-concurrent-transactions), [Bw-tree](https://www.microsoft.com/en-us/research/publication/bw-tree-latch-free-b-tree-log-structured-flash-storage/) | Separate stable data from updates, then price consolidation | Bounded base/update overlays remain unmeasured alternatives. |
| [FSST](https://ir.cwi.nl/pub/32922), [BtrBlocks](https://github.com/maxi-k/btrblocks) | Encoding changes useful layout and downstream work | Preparation for the later string vertical, not primitive benchmark results. |
| [F14 tags](https://github.com/facebook/folly/blob/main/folly/container/F14.md), [SDSL sparse bitvectors](https://github.com/simongog/sdsl-lite/blob/master/include/sdsl/sd_vector.hpp) | Separate equality filters; succinct directories need duplicate-end semantics | Unselected hypotheses, not implemented performance competitors. |

SixDB's [SeriesPack/TuplePack authoring](../../../ikea/docs/source.md),
[call-boundary study](../call-boundaries/README.md), [Orbital MVCC](../orbital-mvcc/README.md)
and [Loom Rings](../../../loom/README.md#memory-rings) supply actual composition
contracts and consumer examples. Calico is prior work, not an inherited format.

## Carrying the findings into implementation

The performance question is whether a small set of complete compositions offers
useful wins without recurring ordinary-operation surprises. It is not necessary
to win every consumer with one layout.
The spike supports beginning implementation. Binding, mutation and publication
obligations must be proved for the selected recipe; remaining workload coverage
limits performance claims rather than gating all primitive work.

- Use the complete Widths8 MVCC results alongside the primitive measurements:
  saved owner work can outweigh slower commands, and projection can consume that
  saving. Keep copying, summary maintenance and retention visible.
- Select operation-specific improvements from the write/projection controls.
  Blanket helper inlining is rejected. Dense paid preparation is a separate
  lowering from cheap point or sparse reads; checks and effects remain intact.
- Qualify the small checked/trusted interface with an ordinary caller, an inlined
  range/selection consumer and a bounded scheduled caller using the same bodies.
- State the remaining boundaries: skew and changing hot sets, mixed/irregular
  requests, inline/outline mutation and repair, colocation/retention choices, and
  bounded maintenance latency. Add a case when its result could change a selected
  composition; avoid treating an exhaustive catalog as a graduation requirement.
- Add a strong mutable-container comparison with matching ownership and effects
  at the relevant boundary. Read descriptors and libc searches are stronger
  external evidence today than the write comparisons. Transaction-engine numbers
  with different semantics would not fill this gap.

The full-block projection follow-up pays for native group decoding in the caller.
Sequential output consumes one group at a time; shuffled output prepares all
256 starts and lengths inside each timed block operation. Matched ordinary
projection controls prevent confusing a better consumer with a better encoding.
Live-empty and tombstoned byte/order oracles precede the all-live timing.

Independent reviewers have challenged geometry, native code, ownership,
measurement and lifecycle semantics. Architecture, Orbital, Engine and Loom leads
supply integration judgment. The root author owns the design and implementation;
review boundaries do not prescribe architectural seams.
