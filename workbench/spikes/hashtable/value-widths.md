# Value widths and inline storage

Sets have no value. The map and multimap byte APIs admit a compile-time width of
1–64 bytes, including widths between native integers. Values are opaque bytes;
there is no sub-byte encoding or variable-length inline element. A caller can
store a reference as its fixed-width value. This is a spike implementation,
not a graduated production contract. These are the exclusive-writer compact-list
and hierarchy owners. The actual Worker intake and fold drivers now cover W2/W64 across all eight
key domains in checks, with targeted native performance panels. They do not yet
qualify the full W1–64 range across multi-writer execution; see
[Worker folding](worker-fold.md).

The earlier integrated multimap experiments measured only eight-byte values.
The solver's payload grammar and a generic standalone buffer did not establish
end-to-end support for other widths. Wide maps also lacked hierarchy mutation
and logical cloning. The current implementation closes those byte-operation
gaps in the compact list and hierarchy, retaining word conveniences for widths
up to eight bytes. Byte callbacks borrow a nonsuspending view: consume or copy it
before returning; do not mutate the owner while consuming its values.

Multimap inline capacity is a consequence of an experimental **byte budget**.
The initial panel reserves either eight or sixteen payload bytes. With value width W
and budget B, floor(B/W) values fit inline. A value wider than B goes directly to
external storage. An external record uses eight bytes for a representation-local
head reference and insertion hint; unused bytes in the larger budget remain
part of the bucket's physical cost.

The later [coupled policy study](bag-policies.md#wide-inline-values) admits
budgets through 64 bytes. The discriminator is separate: a 64-byte budget needs
a 65-byte canonical multimap record in key overflow, while the user-value limit
remains 64 bytes. Thus the original panel below describes two controls, not the
maximum inline capacity of the implementation.

| Value width | Eight-byte budget | Sixteen-byte budget |
| --- | ---: | ---: |
| 1 byte | 8 | 16 |
| 2 bytes / 16 bits | 4 | 8 |
| 3 bytes | 2 | 5 |
| 4 bytes | 2 | 4 |
| 8 bytes | 1 | 2 |
| 9–16 bytes | 0 | 1 |
| 17–64 bytes | 0 | 0 |

These budgets are measured alternatives, not defaults. More inline values save
allocations and indirection for short bags but enlarge every record, including
singletons. Bucket slot capacity and overflow traffic must be charged alongside
the avoided spill. The slot solver takes the actual payload and discriminator
width. Tags use zero for one inline value, one for external storage, and N>=2 for
N inline values; packed discriminator widths are rounded to 1/2/4/8 bits.

`linked_bags<W>` stores W-byte values without a fictitious key field. It shares
the existing StreamPack allocation, reservation, damage and retirement machinery
with key-overflow links. The first spill reserves one link large enough for the
entire inline group plus its new value; only then does it commit the group and
publish the record reference. This includes a seventeen-value spill for W1/B16.
Refusal leaves reachable values and cardinality unchanged. Grown buffers remain
a separate owner experiment behind the same byte reservation surface.

`value_width_check.cpp` checks complete byte histories: duplicates and zero/high
bytes, active-mask byte batches, packed tags, promotion, all three key tiers,
mutation/refusal, deletion/reuse, cloning, source retirement and actual ObjectStore
retained discovery. Targets cover W1/2/3/4/7/8/9/16/17/32/64. W2 additionally runs
all eight key domains. Target existence is not a passing receipt; retain the
results of completed runs separately.

`value_width_bench.cpp` compares complete map and multimap operations with
ordinary/exact-key Abseil, using actual uint16_t for two-byte values (and native
integer types for 1/4/8 bytes). Other widths use exact-size byte arrays. Abseil's
`InlinedVector<T,1>` can physically hold more than one value; its actual initial
capacity is recorded. All contenders consume every value byte and verify full
contents outside timing. Dependent reads use a proved shuffled rank cycle, so
narrow values need not encode a wider key. Rank deltas vary; values wider than
eight bytes also vary in every byte while their complete digest encodes the
required delta. Inputs are prepared outside timing equally. Serial timings
include digest, modulo and key generation, not only table lookup latency.

The initial panel holds K40, A6/A10 and cardinality fixed, compares eight/sixteen
payload bytes in 128-byte buckets, and adds a 192-byte sixteen-payload control.
The last control does not guarantee identical slot counts for every width.
Construction, build, independent hit/miss, serial read, append/update, deletion,
release, phase-summed lifetime and logical memory are reported. Pool setup and
draining are outside the phase sum. It is a diagnostic width panel, not complete
cache-residence, platform, growth-policy or full-lifetime qualification.

The [native width panel](evidence/value-widths-native/README.md) has completed on
Zen5 AVX2, Granite Rapids AVX-512 and Neoverse V2 NEON: 53,460 phase rows and 540
source-resolved read regions. All native width checks pass; read consumers fuse
without per-key or per-value calls. In the K40/A10 two-byte singleton case, the
eight-byte payload budget uses 0.517 times ordinary Abseil's initial logical
memory, but the next append still costs 4.35–5.08 times as much. That append fits
inline and does not allocate. Wider inline budgets therefore cannot by
themselves close the mutation gap. Logical memory includes requested storage
and owner metadata; it is not resident memory.

`mutation-probe.py` generated isolated copies of the mutation headers to compare
complete caller fusion with scoped existing-record writes. Its controls led to
the current fused callers and partial inline multimap writes; maps retain the
fused whole-value path. No production interface is selected. Spill, absent-key admission,
damage substitution, allocation refusal and all three key tiers remain part of
the comparison. The [matched native comparison](evidence/mutation-native/README.md)
now qualifies 178,200 phase rows and 2,160 source-resolved read/update regions.
Both transformed callers remove the per-key owner/preparation boundary; only
the partial-write arm avoids the whole-record proposal for an existing map
replacement or an append that fits inline. The latter publishes the containing
bucket/block before its exact payload/tag stores. Release and ASan/UBSan checks
cover refusal, packed neighbors, all three key tiers and retained publication.

At K40/A10, W2 singleton append with the eight-byte budget falls to
5.93/7.43/8.05 ns on Zen5/Granite/V2, still 1.52/1.57/1.16 times ordinary Abseil.
W8 with two inline values in the 192-byte bucket reaches 1.41/1.41/1.15 times
Abseil. The same sixteen-byte payload in a 128-byte bucket remains substantially
slower because the reduced slot capacity sends many more keys downstream.
First spill for W2/B8 remains about 19–21 times Abseil; source inlining and
partial inline writes do not resolve allocation/transition policy. Abseil is
still inline at that particular boundary. Other boundaries give the opposite
allocation advantage, so policy selection must use whole histories and memory,
not just a favorable next-append sample.

The later `nonzero` copied-header control skips owner counter stores when the
returned delta is zero, preserving logical deltas and publication. Its local W2
checks and 10,800 complete-operation rows pass; unchanged Abseil variation makes
small timing differences inconclusive. This control is not part of the native
mutation receipt. Candidate multimap owners also maintain a table-wide value
total, unlike ordinary Abseil; that extra service needs a separately labeled
accounting control, without replacing the ordinary baseline or presuming it
explains the remaining gap. Reproducing the original generated controls after
adoption requires `--source-root` naming the pre-adoption captured spike directory.

The [coupled policy panel](bag-policies.md) expands the inline budgets and bucket
units together, compares 16/64/256-link shared blocks and grown buffers, and
adds mixed fanouts. It measures the combined live mutation implementation.
Its native receipt now qualifies 135,000 rows across all three hosts. Larger
shared blocks help writes, but memory, read and lifecycle trade-offs remain;
those findings are kept with the policy assessment rather than selecting one
inline budget from a favorable append boundary.

Remaining work includes larger/skewed fanouts and churn, small-list
lifetime/transition, full-width
hierarchy batches, solver policy across growth/platforms, retained-version cost,
erase-one and shared-block compaction. Width support does not close the broader
performance requirements tracked in the spike README.
