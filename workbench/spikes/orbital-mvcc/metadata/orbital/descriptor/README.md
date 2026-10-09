# Compact frozen descriptor candidate

This is an experimental physical closure for the compact history candidates.
It is not an Orbital API or publication capability. `capture` accepts immutable
external bodies and copies their descriptions; catalog authority must validate
Object/version eligibility separately.

Two allocations replace the baseline's Info/Rep/individual Part records:

- A frozen metadata slab holds the shared description, Part directory and copied
  logical/physical metadata. Its 112-byte header includes one dependency owner;
  each Part record is 56 bytes.
- A 16-byte Descriptor header precedes one 16-byte backing owner per Part. It
  owns the metadata slab and all physical backings while catalog/discovery needs
  the complete representation.

`Selection` and `View` are trivial eight-byte borrows. `View::description()`
returns the frozen header by reference; body and interpretation helpers return
ordinary spans. Neither native byte use nor Part indexing acquires an owner.
Rich `describe()` reconstructs an 88-byte record for cold inspection.
The address-relative frozen header cannot be copied or moved. Borrow it with
`const auto& description = view.description()`; returned spans stay borrowed
from the same slab. `describe()` is the copy-safe cold alternative.

Part escape allocates a 40-byte Export, pins the frozen metadata/dependencies,
and copies only the selected backing owner. Releasing the complete Descriptor
then releases sibling bodies and the backing directory. The shared metadata
directory remains until the last Export dies. An umbrella supplied backing
still retains its whole payload group; the candidate cannot split that owner.
An exported raw View borrows its Export; copy the Export owner explicitly.

## Count the whole closure

Requested retained metadata is `128 + 72 × Parts + copied metadata bytes`.
There is no retained vector capacity or alignment slack between byte metadata.
For the census fixture's eight-byte logical metadata, eight-byte shared
interpretation and eight bytes of interpretation per Part:

| Parts | Baseline closure | Candidate closure | Export added on escape |
| ---: | ---: | ---: | ---: |
| 0 | 376 | 144 | — |
| 1 | 600 | 224 | 40 |
| 4 | 1,272 | 464 | 40 |
| 17 | 4,320 | 1,504 | 40 |

These exclude Store/catalog state, external body allocations/control blocks,
allocator bookkeeping and RSS. The parameter `PartSource` array is caller
scratch, not retained. More than eight Parts use a charged temporary identity
sort for duplicate validation; PartId lookup remains a linear cold discovery
path. Index-based native access needs no sorted index. The candidate still
copies Object/version/replica/format once per descriptor, rather than silently
pretending these fields disappeared.

## Bounded contracts and checks

The metadata resource outlives all descriptors and exports. This first candidate
is single-owner and uses synchronous destruction, not a queued retirement or
cross-thread ownership service. Final release invalidates its borrows before
owner destruction callbacks; callbacks cannot resurrect those borrows. It does
not charge external memory to a Store budget.
Capture's resource is not tied to a destination catalog. Accepting a descriptor
from another resource can therefore evade that catalog's metadata budget; the
candidate comparison must count both resources rather than treat this as a
runtime budget policy.

The slab and metadata offsets are limited to 32 bits; body extents are 64-bit,
readable padding is 32-bit and alignment is a power of two. Capture checks
duplicate IDs, null/overflow extents and alignment, but readable body/padding
validity and immutability remain caller obligations. Empty unpadded Parts need
no backing; padded empty Parts need readable backing. A null-pointee aliasing
shared owner is valid when its control block retains the backing.

The native and ASan/UBSan/leak checks cover 0/1/4/17 Parts, independent metadata
copies, exact IDs, narrow retention and dependency destruction, empty 4→1 Part
conversion, changed logical versions, capacity refusal after slab allocation,
refused exports, aliasing owners and padded empty Parts. Clang 21.1.8 AArch64 IR
returns `Result<View>`, `Result<Selection>` and body spans as two words, with no
attributes or special calling conventions. This does not establish latency,
throughput or a composed database advantage.

`abi.cpp` has negative compile probes selected by `COPY_FROZEN_DESCRIPTION` or
`MOVE_FROZEN_DESCRIPTION`. Both compiled before the copy guard and fail with
the deleted-constructor diagnostic after it. The runtime check keeps borrowed
header/spans live through Part export and full Descriptor release.
