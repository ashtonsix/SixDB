# Boundlist investigation plan

This records the original research questions and candidate space. The spike's
19–20 October 2026 outcome is in the [recommendation](RECOMMENDATION.md) and
[findings](FINDINGS.md). Proposed experiments below are not claims of completed
coverage or a production API; the final findings state the measured limits.

Extract StreamPack's sidecar into an Ikea submodule named **boundlist**, with
point-to-rank search and useful composition for both byte storage and keyspace
partitioning. Correctness, performance, interface shape and migration costs
must inform each candidate together. The spike must end with an extraction
recommendation and measured implementation choices, not a catalogue of every
algorithm tried.

The primary operation is now **the first and last ranks overlapping a range**.
Point searches remain constituent kernels and useful consumers, not the sole
objective. Measure paired boundary resolution from the first panel, including
reuse/interleaving of checkpoint work versus two independent searches. Define
open/closed endpoints, equality and empty/reversed ranges explicitly. Distinguish
an exact range result from a conservative candidate window requiring local checks.

## Questions the spike must settle

- What is the reusable sidecar contract, including optional fields and mutation?
  Compare extraction of the existing sidecar with a smaller bounds component
  composed with sibling metadata. Do not decide this from naming alone.
- Which coordinate domains and representations are useful? Ordered endpoints
  need comparison; widths and lerp additionally need meaningful arithmetic.
  Neither a geometry-only API nor a 64-bit coordinate ceiling is assumed.
- How should point search work across repeated bounds, inactive ranks and partial
  groups? Which algorithms win for one request and for independent requests?
- Can StreamPack retain its current compact storage and complete-consumer
  performance through the shared implementation? What must change for a mutable
  keyspace consumer, rather than just a read-only separator microbenchmark?

The initial measurements use the existing 256-slot StreamPack block as a control.
That is not yet a boundlist capacity decision. Similarly, Direct/Width, groups
of sixteen and current native field widths are established StreamPack choices,
not requirements for every new consumer.

Unique-key point access is a separate first-class consumer. With exact inclusive
occupied maxima, one `>=` boundary search selects the only child that can hold
the key; searching that child establishes presence. It needs no minima fetch or
duplicate-run traversal. Test equality at the first/last maximum, singleton nodes,
gaps, inactive ranks and boundary edits. A unique full key may still share a lossy
stored prefix with another key: such a projection does not satisfy the exact-bound
admission and cannot silently use its fast path. Range queries remain the primary
consumer, rather than forcing point requests through two boundary resolutions.

## Existing code and constraints

The current [description](../../../ikea/include/ikea/streampack/description.h)
combines geometry, grouped/separated R/U placement, optional used lengths,
tombstones, fixed reservations and a byte budget. Its native R/U fields are
8/16 bits, endpoints/checkpoints are 8/16/32 bits, and count has 256 capacity.
The version-2 wire description (see [current descriptor compatibility](../../../ikea/docs/streampack/reference.md#portable-descriptor-and-further-reading))
occupies eight bytes. Width has sixteen group-start checkpoints, with one
checkpoint per sixteen ranks; Direct stores exclusive ends.

[Typed reads](../../../ikea/include/ikea/streampack/author/read.h) already share
point location, carried ranges, paid dense selection, liveness and reverse lookup.
[Prefix bodies](../../../ikea/include/ikea/boundlist/detail/prefix.h) use explicit
ISA operations. Current reverse uses binary search over Direct ends or Width
checkpoints, then a native within-group search. It includes retired reservations
and slack. [Content validation](../../../ikea/src/streampack/view.cpp) currently
also requires an actual payload span covering the extent.

The [earlier recommendation](../streampack/RECOMMENDATION.md) and
[implementation findings](../streampack/IMPLEMENTATION.md) retain why current
choices exist. Reuse their controls and failure cases; do not repeat their entire
layout search or inherit byte-specific constraints without justification.

## Define search independently of its encoding

The provisional interval-only rule has been reopened following Engine's actual
index consumer. The search model is now ordered rank endpoints plus independent
population/liveness. Experimental equality policies select the first eligible
endpoint `>= p` or `> p`; neither policy infers population from adjacent equality.

```text
smallest assigned rank i such that
    E[i] >= p, or E[i] > p, according to the operation
    i is eligible      // population/liveness is defined separately
otherwise no rank
```

Strict equality with an end advances past that end. Choose and document whether
absence is count, an optional rank or another existing Ikea result form. A batch
must preserve query order and duplicates; sorted-query hints are optional inputs,
not hidden preconditions. Clarify eligibility before choosing a liveness layout.

For geometric reservations, lower fence L and ends E describe [E[i-1], E[i]),
using L for E[-1]. Only in this adapter does equal adjacent bounds mean a
zero-width interval. Example ends `[3, 3, 7, 10]`: strict search at point 3 selects
rank 2. If rank 2 is retired, a live-successor search can select rank 3, which
does not contain point 3. That is distinct from physical byte ownership.

Keep three meanings distinct in the reference model and tests:

- Positive range versus zero-width range.
- Eligible/live versus retired. Retirement need not erase an interval.
- StreamPack used length U versus reservation R. A live empty string can have
  U=0 and R>0; it still owns reserved bytes.

Existing `reverse` must retain its physical-owner contract, including retired
storage. Do not silently implement it with a live-successor operation. Test
below/at/above each fence, complete absence, duplicate checkpoints, all-empty and
all-retired inputs. Separate successor semantics from containment and exact match.

The Engine index consumer preserves full logical-key order between and within
segments, with unique physical keys but nonunique logical keys. A duplicate run
can span several populated segments: equal logical fences must not classify such
segments as empty or skip the first duplicate. Keep partition fences distinct
from occupied min/max and absence. Its logical lower-bound lookup must find a
containing segment when one exists, otherwise the first successor.

Compare owner-level total-order fences such as (logical key, physical key), with
virtual before/after-all-duplicates query bounds, against explicit duplicate-run
routing. Neither selects a composite-fence type for boundlist or requires a new
row identifier. Test all-equal runs, runs crossing several segments, equality at
the first/last record and missing-prefix successor routing against an oracle
built from the ordered records, not just the same proposed fences. A strict end
search or an exact hash hit/miss alone does not establish this consumer result.
This context does not select a global versus partitioned directory, coordinate
width or additional boundlist operation.

## Representation and extraction experiments

Evaluate a bounded set of contrasting candidates, extending it when a measured
loss or concrete consumer exposes a missing case:

| Dimension | Controls and questions |
| --- | --- |
| Coordinate meaning | Byte positions; compact integer domains including nonstandard widths; wide/multiword ordered keys; a supplied order/projection that does not require distances. Test beyond 64 bits before treating the numeric prototype as the architecture. |
| Boundary representation | Explicit ordered ends; widths plus checkpoints where differences are representable; fixed/derived bounds. Compare relative origins with absolute bounds where meaningful. Include terminal fences that are not ordinary point values. |
| Field packing | Current native lanes; SeriesPack/TuplePack-based representations where their contracts fit; independently fused controls. Charge decoding, controls and mutation rather than comparing helper identities. |
| Rank capacity and grouping | Retain the StreamPack 256/16 control; compare smaller/larger node populations and checkpoint groups for the keyspace caller. Include non-power-of-two counts and exact tails. Choose what belongs in a schema versus an implementation after measurement. |
| Optional metadata | Existing R/U/D combinations versus bounds plus composed siblings. Preserve omitted U, full/fixed/immortal cases and grouped R/U locality where useful. Do not impose byte-prefix meaning on keyspace metadata. |
| Placement | Separate versus colocated fields, actual alignment/strides and cache-line crossings. Count padding, initialized tails, checkpoints, summaries and scratch. |

Full-range integer tests must cover the upper fence after the maximum key, a
single interval spanning the domain, large gaps, narrow local ranges at large
origins, and overflow in sums/interpolation products. These are representation
tests, not a decision to store every boundary in a wider lane. Ordered nonnumeric
points require a correct comparison path even when lerp and width encoding are
inapplicable. Key encoding and collation policy remain explicit consumer inputs.

Prototype both extraction boundaries through complete calls. In particular,
separating ownership of U must not force separate allocations, lose useful
interleaving or add an indirect call per rank. Conversely, common physical
storage is not evidence that every field belongs to one public abstraction.
Resolve format and domain selection around a useful consumer; keep native
groups/fusion available and use existing CPS only at useful boundaries.

## Search algorithms and strong controls

Every candidate must return the same rank, including liveness and tail handling.
Benchmark the complete checkpoint choice plus within-group resolution, not just
the outer search.

| Candidate | Required comparison |
| --- | --- |
| Scalar bisection | Branching and conditional-select variants; actual `std::upper_bound` over materialized ends; current StreamPack reverse where semantics match. Inspect generated branches rather than assuming either variant wins. |
| Full SIMD scan | Scan all checkpoints, then resolve the chosen group; also flat endpoint SIMD search. This is a first-class baseline for small directories, not an afterthought. |
| Lerp | One estimated probe plus bounded correction/binary fallback; iterative or reused-slope control only when useful. Duplicate bounds, skew and arithmetic cost must not produce unbounded correction or incorrect answers. |
| Galloping | From the beginning and from a caller-supplied hint; compare nearby/monotone queries with random queries and bad hints. Establish safe behavior across changed geometry. |
| Shotgun | Interleave several independent searches, both on one list and on distinct lists; test 2/4/8/16 as experimental widths. Charge per-query preparation and batch setup/tails. |
| Multiway | Probe several pivots for one query, followed by SIMD group resolution. Keep this distinct from both shotgun and value-based interpolation. |
| Convergence hybrid | Scalar bisection until a native-width checkpoint/endpoint window remains, then explicit SIMD comparison and mask-to-index. Compare with full SIMD scans and scalar probes; width, bracketing probability and integer/vector issue resources can change the winner. |
| Optional search summaries/layouts | Nonempty-group masks, cached slopes or reordered checkpoint layouts only with explicit storage/build/update costs; retain the original-rank mapping. |

For Width, checkpoint selection alone does not finish the operation. Include
prefix work, repeated-checkpoint handling and skipping eligible empty/dead runs
across groups. Preserve bounded readable tails; wide loads must have a real grant.
Compare paid decoded-end scratch only when the consumer can reuse it, charging
setup and invalidation. Do not introduce a persistent decoded cache for free.

Lemire's [shotgun article](https://lemire.me/blog/2019/01/16/faster-intersections-between-sorted-arrays-with-shotgun/)
interleaves independent searches. His
[separate-list experiment](https://lemire.me/blog/2019/09/14/speeding-up-independent-binary-searches-by-interleaving-them/)
is relevant to populations of small lists. The
[SIMD Quad article and source](https://lemire.me/blog/2026/04/27/you-can-beat-the-binary-search/)
offer multiway-plus-SIMD controls; its index-quartile probes are not value-based
lerp, despite interpolation wording in the article.

Also inspect [CRoaring](https://github.com/RoaringBitmap/CRoaring), the
[SIP/TIP paper](https://jigneshpatel.org/publ/Revenge_of_the_Interpolation_Search.pdf),
and Khuong/Morin's [array-layout paper](https://arxiv.org/abs/1509.05053) and
[implementations](https://github.com/patmorin/arraylayout). Pin inspected code
revisions and licensing before using external baselines. Adapt exact-match or
lower-bound controls to this contract explicitly; do not compare different answers.

## Mutations and lifetime must shape the design

Retain StreamPack's initialize/append, reservation-preserving replacement, retire,
soft packing, selected transfer, range/selection and native-group behavior. Its
payload writes remain part of the complete operation. Metadata extraction cannot
publish a new count/checkpoint while its payload or U belongs to another version.

The keyspace consumer must exercise edits as well as reads: move a separator,
split a range, insert a boundary in the middle, merge adjacent ranges and rebuild
a node with its child references. Changing a separator is different from adding
to one width and translating every later boundary. Deleting a boundary is not
equivalent to reclaiming byte reservations. Measure rank shifts/remapping and
child-array repair; do not carry append-only or stable-rank assumptions into the
public contract without deciding them.

Use caller storage and real Orbital allocation/publication, with retained readers
for old and new interpretations. No replacement allocation layer. Preserve
admission/refusal guarantees, source-qualified issued-write effects, alias rules,
and protection of packed sibling bytes. Shared count/fences, summaries, rank remaps
and child references need coherent publication. Test allocation refusal and
relocation/rebind. Never record an ephemeral projected-view address in damage
that outlives it; test the chosen leaf/source identity through the owner.
StreamPack's payload/directory/lengths/control coordinates must still map to the
correct named source, including tombstone prefixes and grouped R/U strides.
Exercise first append into a group: initializing a complete SIMD-readable tail
can touch bytes still interpreted by an older reader, even with unchanged payload.

Preserve the existing StreamPack version-2 byte law unless a separately justified
migration is necessary. Boundlist need not adopt that byte-specific schema for
new domains. Decide versioning and compatibility deliberately; avoid duplicate
stored facts or an accidental ISA-dependent wire format.

## Measurement from the first candidate

Start each prototype with an independent logical oracle and the corresponding
timed complete consumer. Extend both as semantics/layouts evolve. No
correctness-only architecture followed by a later performance rewrite.

Run explicit AVX2, AVX-512 and NEON, with separate AMD/Intel tuning when evidence
warrants it; Neoverse V2/V3 controls where available. SVE2 is exploratory. Disable
automatic loop/SLP vectorization for the compared first-party kernels. Scalar
search remains a legitimate control. Record actual emitted ISA and flags.

| Axis | Required coverage |
| --- | --- |
| Distribution | Equal, uniform, clustered, geometric/skewed and adversarial gaps; repeated ends; long empty/dead runs; boundary-heavy and out-of-range queries. Vary the distribution of queries separately from bounds. |
| Population | Tiny/partial/full lists, multiple group and node sizes, narrow/wide fields, hot shared upper nodes and scattered lower nodes. Large populations of small lists matter independently of one large list. |
| Latency | Result-dependent next-list/next-point chains; include domain/query preparation, metadata resolution and child/payload result consumption. |
| Throughput | Independent requests at several concurrency levels; single-list and scattered-list batches; sorted streams separately from random ones. Keep scalar, shotgun, manual staged and actual Loom Ring consumers distinct. |
| Residence | Qualified L1/L2, LLC and above-LLC populations on the measured host; varied RSS/hot sets. Size alone or a conditioning scan does not prove DRAM accesses. |
| Memory and writes | Encoded/assigned/accessible/reserved bytes, scratch and retained versions separately; initialization, append, separator edits, split/merge, retirement, packing/transfer and full publication cost. |

Use current StreamPack unchanged, a plain endpoint implementation and independent
fused ISA kernels from the first timing panel. For the keyspace case, compare
separator search plus child lookup and edits against a plain sorted-node control.
Include complete multi-level dependent descents and independent descents through
Loom/Orbital; a new B+ tree implementation is not the spike's deliverable.
Cross search strategy with execution strategy separately. Keep ready local work
inline and rotate only at useful unresolved dependencies. Match manual/Ring
requests, hint windows, refill, useful results and drain; charge bootstrap/tails
and frame memory. Existing Loom streams/Ring and caller-held policy suffice for
these experiments; a new scheduler or heat API is not indicated.

Inspect complete linked consumers early: address dependencies, branches, gathers,
loads, mask reductions, spills, helper calls and wide arithmetic. Use llvm-mca for
selected bounded kernels and supported CPU models, and profiles/counters for
whole-consumer bottlenecks. Report absolute ns latency, ns/query reciprocal
throughput and bytes separately, retaining losing cells. Setup, hint maintenance,
summary updates, damage and publication must be included where the claim needs them.

## Execution and graduation recommendation

1. Establish the domain/operation model, extraction alternatives and independent
   endpoint controls. Time the first read/mutation slices immediately: StreamPack
   compatibility and a mutable keyspace node, including a wider ordered domain.
2. Compare complete search/layout candidates across representative distributions
   and the three ISA families. Inspect/profiling-guided follow-ups must address a
   diagnosed loss, rather than hill climbing one narrow favorable case.
3. Reuse the promising bodies in StreamPack and the keyspace consumer. Measure
   composition overhead against independently fused paths, then exercise retained
   readers, mutation histories and publication through Orbital/Loom.
4. Recommend a small public/author surface, chosen representations and search
   defaults, any justified opt-ins, and the exact migration. Reject experimental
   options that have no distinct useful role. Leave unsupported domains/claims
   explicit rather than pretending a sampled grid proves universality.

The recommendation must account for headers/namespace and `ikea::boundlist`,
CMake dependencies, shared-kernel ownership without cycles/duplication, standalone
header checks, wire/admission/effects tests, StreamPack regressions, and executable
usage examples for both consumers. Documentation should introduce boundlist on
its own terms, then explain StreamPack composition and measured fast paths.

Keep implementation, controls and selected findings in this new spike root.
Use shared [capture/worker tools](../../tools/README.md) and
[measurement conventions](../../benchmarks/README.md); retain useful evidence with
[recovery references](../../tools/artifacts.md), not whole build trees in Git.
Production migration and encoding selection follow the measured recommendation;
the authorized work currently remains in the spike.
