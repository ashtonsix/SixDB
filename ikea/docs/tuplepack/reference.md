# TuplePack contracts and scope

TuplePack operates on caller-supplied layouts and prepared code maps.
This reference specifies their bounds, recovery format and execution requirements.

| Surface | Contract |
| --- | --- |
| `layout` | Owned 1–64-byte unit, 1–128 ordered code descriptors; widths 1–8, byte-contained, disjoint physical bits |
| `reader<8/64>` | Owned prepared map/control; read duplicates allowed; holes and unused slots zero |
| `writer<8/64>` | Owned prepared map/control; duplicate destinations rejected; selected unsigned widths checked; all other bits preserved |
| `constructor` | Complete rank-ordered input across up to two packets; zero spare bits, no old-data read |
| `view` / `const_view` | Borrowed storage with original row count, byte stride and unit offset; exact last-row extent |
| Bound read/write/construction | Borrow named plans/views; checked ordinary calls and explicit trusted calls; no allocation/suspension/publication |
| Mutation groups | Recursive static composition; all children preflight before mutation; reject overlapping destination bits across actual placements |
| Observation | Separate dependency projection and semantic law; independent before/after demand; explicit row identity and complete-group after image |
| Native execution | Scalar uint64 packet; NEON four-vector HVA, AVX2 regcall pair, AVX-512/VBMI vector; masks outside the payload |
| Native batching | 16 bytes × 4 rows and 32 bytes × 2 rows; sparse active masks and exact tails; compact-window specialization plus general legal-map fallback |
| Route preparation | Checked restricted bit wiring; byte permutation/rotation normalization or bounded general lowering into caller-supplied controls |
| Erasure/CPS | Whole-range mutation erasure retains traversal; shared straight-through native chain supports early completion |

## Recovery description

`layout::encode` requires `encoded_size()` bytes and writes:

| Offset | Meaning |
| --- | --- |
| 0–1 | ASCII `TP` |
| 2 | Version 1 |
| 3 | Unit extent in bytes, 1–64 |
| 4–5 | Little-endian code count, 1–128 |
| 6 onward | Three bytes per code in rank order: byte offset, shift from least-significant bit, width |

`decode` requires exactly that description, rejecting trailing data, unknown
versions, malformed widths, extents and overlap. Persist the description together
with the owner's placement and semantic schema. Rebuild ISA-specific controls
from the description and operation map when binding.

## Access and mutation boundaries

`read_bytes()` and `write_bytes()` are 64-bit masks of byte positions within one
physical unit. They describe possible native/scalar access envelopes, not semantic
bit masks. Chunk reads and bounded word updates can include unselected bytes;
full native read-modify-write can read whole units. View binding admits the full
unit. Journals translate issued stores to the view's storage-relative offsets.

Mutation groups allow disjoint codes to share a physical byte and preserve each
other sequentially. Binding checks actual repeating placements for overlapping
destination bits, including aliases across rows/strides. It does not reject
legitimate read-only observation aliases. Concurrent callers must exclude each
other for the issued physical bytes even if their semantic bits differ.

The owner coordinates atomic visibility, persistence and transaction outcome.
A returned error leaves that call's data and effects unchanged. Construction
clears full units; replacement preserves spare bits. Hooks and observation laws
must be infallible and cannot suspend after admission.

## Execution limits

Array size is bounded by addressable storage and checked extent arithmetic.
Engine defines the size and representation policy of its record segments.

Prepared controls and native calling conventions are specific to the compiled
ISA. An x86 build without AVX2 uses the scalar/buffered surface; optimized native
AVX-512 requires VBMI. Native calls must use matching carrier types and calling
conventions.

General sparse 64-byte native writes bridge to byte-coalesced stores. Scalar
eight-byte maps suit small transactional projections. Batch maps spanning too
many source chunks use the general reader. Mutation ranges traverse rows within
one bound operation using the point kernels.

The restricted route algebra only carries input bits or zeros. Conflicting OR,
arithmetic and predicates need caller-authored bodies. A normalized route requires
every output byte to be a complete permutation/rotation of one input byte. Its
failure is an applicability decision, not a rejection of the underlying layout.
General lowering uses at most 16 caller-owned terms; the plan borrows them.

Observation laws independently request old and new values and can include
untouched dependencies across multiple children. See
[maintenance composition](extending.md) for callback signatures and timing.
