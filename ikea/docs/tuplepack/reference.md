# TuplePack reference

[Using TuplePack](usage.md) introduces layout, placement and projection through
one example. This reference owns exact bounds, borrowing and execution contracts.
Code ranks carry unsigned bit patterns; application types and schema belong to
the enclosing composition.

## Layout and recovery

A `layout` owns a unit extent of 1–64 bytes and 1–128 ordered `code` descriptors.
Each descriptor is `{byte offset, shift from bit 0, width}`, with width 1–8.
Every code fits within one byte; codes have disjoint physical bits. Bit positions
are little-endian within the byte on every ISA. Spare bits have no value semantics.

`layout::encode` requires `encoded_size()` bytes and writes:

| Offset | Meaning |
| --- | --- |
| 0–1 | ASCII `TP` |
| 2 | Version 1 |
| 3 | Unit extent in bytes |
| 4–5 | Little-endian code count |
| 6 onward | Three bytes per code in rank order: offset, shift, width |

`decode` requires exactly one description. It rejects trailing data, unknown
versions, malformed widths/extents and overlapping codes. Persist the description
with the owner's placement and semantic schema. Rebuild ISA-specific controls
from the description and operation map; those controls are not a recovery format.
Rebinding does not migrate bytes or retain an earlier data version.

## Packet shape and coordinates

`reader<N, Rows=1>` and `writer<N, Rows=1>` own prepared maps and controls.
N is decoded packet bytes. Rows is original rows per packet.

| N | Supported Rows | Slots per row | Ordinary packet type |
| --- | --- | --- | --- |
| 8 | 1, 2, 4, 8 | 8/Rows | `uint64_t` |
| 64 | 1, 2, 4, 8, 16, 32, 64 | 64/Rows | `std::array<byte, 64>` |

The map has at most `N/Rows` entries and repeats for every row. Slots are in
row-major order; in a GPR, slot zero occupies bits 0–7. Entries name code ranks
or `hole`. Reads allow duplicate ranks and zero holes and trailing slots.
Writers reject duplicate destination ranks, ignore hole/trailing input slots,
and preserve every unmapped bit, including spare bits. Selected input codes
must fit their unsigned widths.

`get/set(first, ..., active)` uses mask bit r for original row `first+r`.
Bits beyond Rows or active rows beyond the view reject. An empty mask may begin
at the view's end. Inactive rows issue no payload accesses, read as zero slots
and ignore write slots, including out-of-width values.

For `read/replace`, each span element advances Rows original rows. A partial
final packet is allowed; unused read slots are zero and write slots ignored.
Surplus packets reject. `size()`, selection origins and maintenance coordinates
always count original rows. `selection::bits(origin, words)` borrows 64 bits per
word with bit zero naming origin; origin need not be a multiple of 64. Words
must cover the requested range and remain stable. Selection does not renumber rows.

`constructor` uses one `construction_input` per original row, independently of
packet shape. This is a 128-byte rank-ordered input; only defined codes are
consumed. Construction validates them all before writing, zeroes spare bits
and preserves bytes outside each unit. It needs no prior initialized contents.

## Borrowing and admission

| Resource | Required lifetime or proof |
| --- | --- |
| Layout and map supplied to preparation | Can expire after their controls are copied |
| Named view and prepared plan | Stable addresses through every bound invocation |
| Source view identified by emitted effects | Resolvable until the owner consumes those effects; the prepared plan need not survive for this alone |
| View storage | Live, with unchanged placement and the layout that encoded it |
| Input, output, selection, effects and maintenance | Valid through admission and execution; retained across any owner stop that borrows them |
| Observation projection, law and source views | Live through all observations |
| Erased operation's concrete source | Stable while the erased operation borrows it |
| CPS plan and bindings | Live through the complete chain |

A view admits `count` units at `storage + offset + row × stride`. Stride must
cover the unit. The final row requires only its occupied unit, with no padding
beyond it. Binding checks extent and address arithmetic, not existing byte
semantics. Array size is bounded by addressable storage and checked arithmetic;
Engine owns segment size and representation policy.

The owner supplies live leases, isolation and disjoint command metadata.
Input/selection/plans/output/journal storage must not alias mutable data in ways
that change the command or corrupt results. A checked call does not prove these
object lifetimes or non-aliasing obligations. Observation sources and any
persistent summary outputs require their own admitted resources.

Checked range reads leave every output packet unchanged on error. Checked
mutation validates the whole call's range, selected values and effect capacity
before stores, effects or observations. Rejection leaves data, effects and
maintenance unchanged; earlier successful calls remain completed. Hooks and
observation laws must be infallible and cannot suspend after admission.
Trusted `_unchecked` entries require those proofs in advance.

## Access and mutation boundaries

A plan's `read_bytes()` and `write_bytes()` are 64-bit masks of byte positions
within one physical unit. They describe possible access envelopes. Reads can
include unselected bytes; preserving updates can read and reissue neighboring
bits. The owner must protect the issued physical bytes even when concurrent
commands would change disjoint logical bits.

Bound mutations emit source-qualified, storage-relative byte spans, including
the view's unit offset. These are issued stores, not minimal differences or
beforeimages. `writer::effect_capacity()` bounds journal records per active row
before coalescing. Sum the bound over active rows and composed children;
construction requires one record per initialized row. Persistent summary writes
need separately reserved resources. Raw plan `set_unchecked(byte*, ...)` emits
no effects; bound trusted entries retain pre-write coverage.

`composition::bind_group(children...)` supports recursive mutation groups.
All children share Rows. Preparation rejects overlapping destination bits in
actual repeating placements, including aliases across rows and strides.
Disjoint codes can share a byte and preserve each other sequentially. Read-only
observation aliases are allowed. Constructors can be leaves of one-row groups;
their destination bits cover the whole initialized unit.

The owner coordinates atomic visibility, persistence and transaction outcome.
[Integration](../integration.md) describes publication and dirty cancellation.

## Observation windows

An observation consists of a dependency projection and a semantic law. A point
projection provides `size()` and `get_unchecked(row)`; a packet projection also
provides `rows` and `get_unchecked(first, active)`. The supplied
`composition::projection(readers...)` returns a tuple of packets. Projection
sources can alias mutable leaves and include untouched dependencies.

The law independently declares `needs_before` and `needs_after`. Point laws
receive `observe(row, ...)`; packet laws receive `observe_batch(first, active, ...)`.
Requested before-values precede requested after-values in the remaining arguments;
if neither is requested, the callback still receives coordinates/activity.

For each nonempty window, every demanded before-value is read before any child
store. Each after-observation and law invocation follows all child stores.
Point observers may read their after-values and run their law one row at a time.
Packet projection Rows must match mutation Rows. Point observers also
work with packet mutations: demanded before-state for each active row survives
until the whole packet is written. A range repeats this bracket per window.
Whole-call admission does not supply a whole-range observation snapshot.

Before-observation must not read uninitialized construction storage. Use an
after-only law or caller contributions in that case. Summary correctness and
coordinated visibility remain owner responsibilities.

## Routes and native execution

`native_reader<N, Byte, Rows>` and `native_writer<N, Rows>` normally deduce
their arguments from bound ordinary operations. `native::packet_for<8>` is
`uint64_t`; `packet_for<64>` is the compiled ISA's vector carrier. Native calls
require matching carrier types and calling conventions. Controls are ISA-specific.
An x86 build without AVX2 supports native GPR packets and buffered 64-byte packets;
optimized native AVX-512 requires VBMI.

Raw multi-row native bodies take an address callback, active mask and optional
stride hint. They request only active addresses. A nonzero hint promises one
allocation with that byte stride; omit it for arbitrary addresses. Ordinary
bindings supply that proof. Controls must match Rows; one-row bodies use a
direct pointer. Access stays within active units, including with sparse masks.

`compact_word<Rows>` and `expand_word<Rows>` support Rows 1/2/4/8. They move
the first `8/Rows` slots of each wide row to/from a word. Compaction discards
later slots; expansion zeroes them. Preserving the logical operation requires
the same short map with no additional write destinations.

The restricted route algebra carries input bits or zeros. Conflicting OR,
arithmetic and predicates require authored bodies. A normalized route requires
each output byte to be a complete permutation/rotation of one input byte;
other supported wiring uses the general lowering. General plans borrow at most
16 caller-owned terms.

`erased_mutation<Input, Maintenance, Rows>` erases a whole range while keeping
concrete traversal inside it. Rows remains part of the erased contract even when
two packet shapes have the same C++ payload type. Inline and CPS share bodies;
the [source guide](../source.md#shared-execution-entry) owns private ABI and table
requirements. Explicit suspension happens after returning to the owner.
