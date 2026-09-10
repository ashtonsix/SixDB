# The concrete composition

The collection maps logical ordinal `i` to one 256-position bitset and to bytes
`[32*i,32*i+32)` in a separate query. Its result is the sum of intersection
popcounts over a requested contiguous range. The implementation is bounded at
256 bodies; count is runtime data, not another TU dimension.

## Data and compute

[metadata_format.h](metadata_format.h) defines physical geometry separately
from [metadata_native.h](metadata_native.h)'s native computation. All three
directories carry population 0–256 and exact BEC length 0–47. Empty and full
bitsets consume zero BEC bytes; their population distinguishes them. BEC can
expand beyond the plain 32-byte representation. This experiment always uses
BEC bodies and does not choose a compression policy.

- `direct32` is little endian: population in bits 0–8, length in 9–14, absolute
  start in 15–28, and three reserved zero bits. Fourteen offset bits cover the
  maximum 256 × 47 = 12,032 payload bytes. Capacity rounds up to sixteen.
- `local16` rounds capacity up to sixteen. Each 32-byte packet contains a u16
  absolute checkpoint at byte 0, sixteen high population bytes at byte 2, two
  LocalPack1 bytes at byte 18, and twelve LocalPack6 length bytes at byte 20.
- `scan128` rounds capacity up to 128. There is a u16 checkpoint per sixteen
  records, followed by eighteen-byte population9 packets and ScanPack6 lengths.
  At capacity 256 these regions start at bytes 0, 32 and 320. The 32-byte length
  stripes remain intact; the record's fields therefore need not be near each
  other. Unused records have zero population/length and start at the logical
  body end, so unused checkpoints need not contain zero.

One scalar construction catalog creates the three directories over a shared
body owner. Prepared reads retain the directory, body and query owners; they
do not retain or consult that catalog. The independent wire transcription in
[source.cpp](source.cpp) is construction/admission oracle code. Native reads
reuse the integer provider directly.

## Native work and authored work

[authoring.h](authoring.h) expresses one logical operation. A cursor emits
consecutive ordinals from the requested start. The packed cursor privately
loads metadata16, computes exclusive length prefixes from a checkpoint, and
retains the reconstructed native frame. Direct reads load selected records.
Cursor advancement exposes scalar population/start accessors to the author;
the vector representation remains an inline implementation detail.

The author pairs consecutive requested bodies and submits their two addresses,
populations and consecutive query blocks to a BEC decode/AND/count region.
The final odd body uses a one-body region. A pair can cross a metadata checkpoint:
the first entry is retained while the second frame is reconstructed. Refills
may read predecessor and future metadata, but earlier logical bodies are not
emitted and unrequested query blocks are not read. Tests include `3/37`,
`15/18` and `255/1`, alongside empty and full ranges.

This is a useful nontrivial cursor case, not yet a reusable iterator protocol.
It accommodates metadata16 → BEC2 without sixteen separately invoked decoders,
descriptor-array storage or an aggregate vector return at an opaque ABI seam.
The two region choices are curated full inlining and an ordinary separately
compiled BEC decode-and-consumer call. Both return a scalar u64; split inputs
are separate scalar/pointer operands. Trusted loops contain no `std::expected`
or validation. Forced-inline markings are part of this experiment's selected
boundary, and [assembly inspection](notes/boundaries.md) checks that selection.

The split factors three metadata specializations from one consumer region.
It also couples BEC decoding to this particular consumer: another consumer
could reproduce that decoder. A decode → native consumer cut is still worth
probing. This comparison does not establish the cost of every possible cut,
sign/delta transform, ISA or source count. The
[independent granularity investigation](../../operation-granularity.md)
has broader cases; this exercise supplies concrete retained-state costs.

## Obligations and where they are established

These obligations have concrete owners/checks here; they are not yet a generic
tag or reflection interface.

| Obligation | Establishment and limit |
| --- | --- |
| Intended population/body interpretation | Construction from the source bitset establishes meaning. Bounded validation checks a valid representation; it cannot authenticate the original content against another valid interpretation. |
| Directory/body association | Preparation requires the actual same body owner referenced by the directory, not just a matching numeric source ID or equal allocation size. |
| Exact framing | Cold admission checks lengths, checkpoints, reserved bits, logical end and padding records. The hot reader trusts those facts. |
| Decoder access window | The body allocation has one 64-readable-byte suffix after its logical end. Bodies remain dense, with no per-body padding. |
| Lifetime and immutability | A prepared range retains all three owners. Factory/caller discipline supplies immutability; `shared_ptr<const T>` alone cannot exclude mutable aliases. |
| Query domain and range | Preparation checks extent and range. The caller must establish that the query uses the same coordinate domain; equal byte size does not prove that. |
| Locality | The physical parent must compose the actual resource footprints. Legal child formats alone do not prove a two-line metadata-record guarantee. |

The 64-byte decoder window may include subsequent logical bodies and the final
owner suffix. Its extent is distinct from the current body's compressed length.
The query's one/pair loads cover exactly 32/64 requested bytes. Guard checks
exercise both resource boundaries independently.

`prepare` currently validates the entire source on each range binding. That
cold cost is excluded from the timed loop. The
[operations extension](operations/README.md) implements source admission
separately from algebra binding; this original range API still revalidates.
A default `PreparedRange` must be successfully prepared before `count`;
there is no hot fallback for an unprepared object.

Conjunctive row-filter updates, aggregate maintenance and MVCC changed-byte
spans remain integration questions. This read-only count region exercises a
two-source content consumer, but it does not yet establish mutation effects,
monotonicity obligations or their propagation through enclosing blocks.

## Reuse without copied implementations

Following the scaffold task's advice, [CMakeLists.txt](CMakeLists.txt) links
`ikea_bitsets` and `ikea_integer_kernels` directly. Headers and compiled provider
algorithms stay in their owning spikes. There is no parallel copy, new provider
registry or promoted shared abstraction. The runner selects all three spikes
in one captured source configuration and builds/runs both provider checks and
the combined caller check. Thin provider README links identify this consumer.

Provider checks cover their own algorithms; combined checks cover association,
framing, native metadata reconstruction, requested ranges and owner lifetime.
Live changes can break consumers visibly at build/check time. Retained source
archives freeze exact versions for old comparisons without freezing live reuse.
Extract a smaller shared provider only if actual dependency or maintenance
pressure justifies it.
