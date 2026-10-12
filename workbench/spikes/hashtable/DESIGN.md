# Design under investigation

## Scope and operations

Keys are unsigned codes of width **w in {8,16,24,32,40,48,56,64}**, with domain
size `2^w`. This notation avoids subtracting bit counts from a domain size.
Values begin as fixed-width unsigned codes or caller-managed handles: set (zero
value bits), 8/16/24/32/40/48/56/64-bit map values, plus 16-byte payload controls.
Arbitrary object ownership and string encoding are outside this primitive.
Large values can be represented by caller-owned handles; their dereference cost
must be charged when a workload needs their contents.
For narrow multimap values, a block/rank reference can exceed the inline value
width. Compare widening every tagged cell with a tag bitmap plus sparse chain
references; include its extra lookup dependency, rank calculation and delete-swap
maintenance. Do not assume an inline value and a chain ID have the same size.

Provisional operation semantics keep the first comparisons unambiguous:

- `get`/`contains` copy results or consume them within the bounded call. Physical
  slot addresses/ranks do not survive mutation, growth or merge. Iteration has no
  stable order and reconstructs exact original keys.
- Map `set` inserts or assigns; set insertion is idempotent. Report inserted
  versus already present, with exact occupancy delta. Same-value write suppression
  is an experiment: reading/comparing the old value must earn its publication
  savings rather than become mandatory extra work.
- In exclusive/immutable-view operation, `getN` observes one coherent caller-owned
  view; `setN` applies lanes in input
  order, so repeated keys have scalar-equivalent results and the last map value
  wins. N=4 and N=8 are experimental candidates; select one common interface
  grain across platforms. A large Ring may contain many such batches.
  If admission fails, return the completed prefix and its results/deltas; every
  completed lane is a complete mutation, and the caller can grow then resume.
  Routed multi-writer batches need per-lane coherence initially, not an implicit
  cross-worker snapshot promise.
- Multimap is initially an unordered bag of values: duplicate pairs are allowed.
  Distinguish `add`, `erase_one(key,value)` and `erase_all(key)`; return key and
  pair deltas separately. Its inline value becomes a chain reference when needed.
  A separate tag is necessary when every value bit pattern is legal.
- `clear` removes logical entries; retaining storage and releasing storage are
  separately charged lifecycle choices. `reserve`/rebuild and `merge` expose
  admission failure before publishing a changed representation.
- Merge of maps needs an explicit owner-selected conflict rule. Initial tests
  use disjoint keys and ordered last-source-wins separately; set merge is union,
  bag merge concatenates multiplicities. Physical merge never chooses database
  conflict semantics. Iterator/visitor and drain support allow exact enumeration.

These semantics enable performance comparisons; they are not an invitation to
build a general STL compatibility surface. Private draft/exclusive access is the
initial mutation contract. Multi-writer entry points must define routing and
visibility explicitly; packed byte-preserving stores are not atomic records.

## Reversible key placement

For `B=2^a` buckets, require `0 <= a <= w`. A width-preserving permutation gives
`p`; use its high `a` bits as bucket address, the next `b=min(w-a,8)` as fingerprint,
and remaining `c=w-a-b` as suffix. `(bucket,fingerprint,suffix)` exactly reconstructs
`p`, and the inverse recovers the key. No sentinel consumes a key value. At `a=0`
there is no address saving; at `a=w` the bucket address is the complete permuted
key and occupancy alone answers membership. Zero-width fields are absent.
Avoid shifts by 64 and overflowing domain cardinalities in those cases.

The permutation is **not selected**. XOR with a seed and modular multiplication
is attractive because it can be inverted and batched, but fixed multipliers can
retain structured correlations even when high bits select the bucket. Taking low
bucket bits is especially bad for power-of-two strides. Compare caller-seeded
random odd affine permutations with reversible xor-shift/multiply mixing; test
distribution, inverse cost, SIMD cost and the conditional secondary distribution.
An empirical good distribution is not an adaptive-attack security proof.

The caller supplies seed material; hashtable does not draw entropy or invent a
replication policy. A shared, recorded entropy schedule can reduce replica
divergence. Growth receives new seed material that is unpredictable under the
specified attacker observations. An agreed private entropy schedule or secret
keyed derivation can support that without independent replica draws; merely
changing an integer does not establish the property. Distinct secondary seed
material must not merely rename the same overloaded partition. Reconstructing
old keys and re-permuting them is charged to reseeded growth and incompatible
merge. Seeds used for benchmarks are reproducible, not a security implementation.

## Buckets and overflow

Experiment with capacities 8/16/32 and nearby useful packing points such as 14.
Choose from complete bucket costs at each geometry transition, not SIMD width
alone. A 14-slot bucket may physically encode 16 fingerprint lanes; those bytes
and initialized inactive lanes must be counted. Occupied slots form `[0,count)`;
deletion moves the last entry and its fingerprint/value into the gap.

The first array handles ordinary placement; overflow uses a second independently
permuted table, then a secondary-bucket-owned chain. Secondary size is an
experimental input, not automatically another full-sized primary array. Chain
entries can retain the secondary bucket's implicit bits only while that context
is unambiguous; reconstruction must work for iteration, primary-accounting
updates, reseeding and merge.

Each primary bucket's downstream count is the number of live keys whose primary
address is this bucket but which reside in the secondary table or chains. The
secondary bucket separately counts its own chain descendants. These are exact
counts, independent of current occupancy. Deleting a primary entry does not
decrement its downstream count; deleting a chained entry updates the counts of
its reconstructed primary and secondary owners. Admission must prevent counter
overflow. Count width/storage is part of the memory experiment.

Initial policy leaves surviving overflow entries in place; reinsertion can use
free primary slots. `set` must first check for an existing downstream key before
using a vacancy, unless the caller has a valid absence proof. Lookup still checks
downstream when the primary has a gap.
Compare bounded backfill against this policy under churn, including the cost of
finding a suitable secondary entry. The count reaches zero when the last
descendant disappears; it is not a permanent overflow flag.

A primary bucket's overflow keys generally fan out over many secondary buckets.
Therefore a cached negative filter summarizes **that primary bucket's descendants**,
not one whole secondary bucket. Compare no filter, compact monotone filter reset
when count becomes zero, and deletion-capable summaries. Never clear a shared
fingerprint bit merely because one matching entry is deleted. Charge false
positives after churn and filter rebuild/update work, not only freshly built reads.

Geometric chain links (4/8/16/... entries) compete with a separately allocated
growable overflow buffer. Chain length in bytes must fit StreamPack's 65,535-byte
slot limit; geometric growth eventually caps and adds another link. References
name a caller-resolved StreamPack block and 8-bit rank, with explicit null/tag
encoding and block-ID budget. A live reference cannot silently name a recycled
rank. Block reassignment/compaction remaps all holders under owner publication.
Overflow and multimap chains may share encoding and allocation support while
retaining separate ownership and iteration semantics. Benchmark pool slack and
retained empty blocks for many small tables as well as large tables.

## Physical composition and execution

SeriesPack owns fingerprint encoding; TuplePack owns suffix/value/control packing;
StreamPack owns chain bytes and the chosen sidecars. Prepare schema/layouts once,
with bounded transient bucket access. Do not retain a large binding per bucket or
allocate/rebind on every lookup. Native register consumption and direct inlining
serve hot data; stable CPS frames expose the primary→secondary→chain dependencies
to Loom. Explicit NEON/AVX2/AVX-512 kernels are required; automatic vectorization
is not the SIMD implementation strategy. SVE2 is conditional on measured benefit.
Template the domain/operation facts that remove useful work, then specialize a
few suffix-byte/transfer families. Do not automatically instantiate the Cartesian
product of every exact width, growth level and policy. Measure code size and
prepared-control cost alongside kernel instructions and packed bytes.
Candidate enumeration need not force every ISA into a compact 16-bit mask. Test
NEON's natural wider lane mask against horizontal reduction, and permit a
contains-only consumer to stop at any-match when no suffix remains. Packed
1–7-bit equality may operate directly on SeriesPack bit planes. These operations
should compose with consumers in registers rather than decode to an array.

Compare locally grouped fingerprints/fields, split planes, and padded spans
including 64/96/128 bytes. Key/value widths, metadata, base alignment, growth-level
quotient width and allocation rounding all enter the calculation. With 64-byte
lines and a 64-byte-aligned array base, dense 96-byte buckets start at phases 0/32 and
touch at most 2 lines; arbitrary misalignment can touch 3. A wider load cannot cross
the admitted bucket window merely because the next bytes are mapped.

Known composition question: ordinary SeriesPack payload admission currently
requires a 64-byte-aligned base, while narrow Local native loads appear to support
unaligned exact windows. Ordinary global multi-tile views lack grouped bucket
placement; native bucket-addressed bodies already express it.
Resolve the actual admission/authoring seam and test shifted/guarded storage; do
not bypass proofs or encode a second private fingerprint format. TuplePack's
64-byte unit limit can be handled with several prepared units per bucket.

Benchmark a manually fused matched-wire control to quantify any composition
cost. Candidate improvements to the packing primitives must preserve their
contracts and have independent correctness/codegen evidence.

Effects are a compile-time composition choice: exact source-qualified child
coverage, parent coverage of all changed buckets/chain/control windows before
stores with silent children, or `no_coverage` for caller-owned scratch. Existing
Ikea trusted hooks already support substitution. A single damaged bucket does
not cover a remote overflow count, chain allocation or root/seed change.
Occupancy deltas are logical outputs separate from physical store coverage.
Pre-admit effects/storage before writes; no hook may fail or suspend mid-group.

## Ownership, heat and concurrency

Use real Orbital allocations and existing borrow/Hold rules. Growth happens at
returned owner boundaries; bind again after relocation. A Hold protects lifetime,
not immutable contents. Suspended Ring lookups retain coherent geometry, seed,
allocations and chain directories. A growth candidate is built privately before
owner installation; peak memory includes old generations and pending readers.
Damage granularity and sharing granularity must be investigated together: marking
one bucket does not permit mutating an immutable array Part observed by old
readers. Do not inherit a whole-array-clone implementation as the only MVCC
comparison. Orbital's current ObjectStore/reference counts are single-owner;
independent worker stores do not offer a common atomic publication cut.

Orbital can capture two whole-array Parts from two distinct Allocations. Multiple
Parts sliced from each of two backing Allocations do not fit its current simple
`independent_allocation`/`grouped_allocation` helpers; measure a real material or
Hold-backed ownership if that sharing is useful. A successful capture seals and
consumes the source allocation owners even if later preparation refuses. Stage
the complete replacement privately; failure of a second allocation must not
leave the old table partially replaced. Count unique backing storage and retained
allocator charges rather than summing aliased Part extents.

StreamPack rank 255 is valid. A chain encoding needs an explicit null/tag scheme,
not an absent sentinel stolen from the rank byte. Compaction must repair both
embedded successors and external aliases. Foreign references keep source Lanes
servicing returns; moving an Allocation handle does not rehome its storage.

Caller occupancy is coherent input to growth decisions; emit exact deltas.
Caller heat steers direct versus Ring entry. Loom owns any heat indicator/oracle:
the proposed 16-bit timestamp/confidence encoding is an experiment, not an asserted
cache-residency fact. Measure update cost, wraparound, aging, prediction mistakes
and false-prefetch pollution; no racy non-atomic shared updates. First compare
forced direct and forced Ring so an unproven oracle cannot hide kernel behaviour.

Multi-writer has two requested experiments with their own full routing cost:

1. Key-domain partitions owned by workers, including mismatched-key routing,
   skew, queue pressure and read visibility. Compare an equally partitioned
   Abseil implementation as well as shared concurrent maps.
   For aligned power-of-two slices, test omitting the known partition prefix
   from local keys; reconstruction includes the owner partition. Routing must
   check the slice before using that reduced local domain.
2. Same-domain worker-local tables with common seed and geometry, plus merge.
   Hold a common geometry only for a bounded phase and measure its spill cost.
   Compare bucket-wise merge with ordinary reconstruction/reinsertion, duplicate
   reconciliation and independently seeded inputs. A compact union often needs
   more buckets and fresh seeding; same-seed compatibility is not free growth.
  A global read also needs routing/ownership evidence or probing the separate
  instances. Price that fanout and reconciliation; build-then-merge alone does
  not establish competitive online global reads.

Do not label worker-local instances a linearizable shared concurrent map. Shared
concurrent baselines use their native safe visitation/access patterns, including
bulk APIs. The spike must identify which semantics each comparison provides.

For a tiny table, compare an unpermuted full-key list with no hashing, a larger
ordinary bucket, and normal table allocation, including the first permutation
on transition. For high domain occupancy, publish a memory advisory
for an occupancy bitset plus dense values; do not implement that representation.
The advisory states encoding assumptions and compares full memory costs; it is
not merely a key-count threshold detached from value width.
