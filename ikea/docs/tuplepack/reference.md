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
| 2 | Byte-law version 2 |
| 3–4 | Little-endian body length: `1 + 3 * code_count` |
| 5 | Unit extent in bytes |
| 6 onward | Three numeric bytes per code in rank order: offset, shift, width |

The shared five-byte envelope lives in the parent schema, with no per-unit
header. Code count is inferred from the body length; all 128 codes remain
supported (390 encoded bytes). `encode`/`decode` take `std::byte` spans.

`decode` requires exactly one description. It rejects trailing data, unknown
versions, malformed widths/extents and overlapping codes. Persist the description
with the owner's placement and semantic schema. Rebuild ISA-specific controls
from the description, operation map and groups; those controls are not a recovery format.
Rebinding does not migrate bytes or retain an earlier data version.

## Packet shape and coordinates

`read_plan<N, Rows=1>` and `write_plan<N, Rows=1>` own prepared maps and controls.
N is decoded packet capacity in bytes. Rows is original rows per packet.

| N | Supported Rows | Maximum map slots per row | Ordinary packet type |
| --- | --- | --- | --- |
| 8 | 1, 2, 4, 8 | 8/Rows | `uint64_t` |
| 64 | 1, 2, 4, 8, 16, 32, 64 | 64/Rows | `std::array<std::uint8_t, 64>` |

The map has at most `N/Rows` entries and repeats for every row. Entries name code
ranks or `hole`. `read_plan::make(layout, map, groups = {})` and the matching writer
call accept `std::span<const unsigned>` group lengths. Explicit lengths must be
positive and sum exactly to `map.size()`; holes count toward that size. Invalid
groups reject with `error::map`. Omitted groups mean one group of `map.size()`,
with an empty map requiring no groups.

Groups partition consecutive map slots. Each group emits its slots for all
original rows before the next group. For a group starting at map slot b with
length L, slot s of original row r occupies packet byte:

```text
Rows × b + r × L + (s − b)
```

Exactly `Rows × map.size()` byte positions are used, including holes and inactive
rows. The rest is trailing capacity: zero on reads and ignored on writes. With
one group, the used bytes are row-major, with no padding between rows. In a GPR,
byte zero occupies bits 0–7. Positions depend on map, groups and Rows, independently
of N.

Both plans own a `packet_layout<N>` and return it by const reference from
`ordering()`. Its `rows()`, `map_size()` and `used_bytes()` describe the packet;
`offset(row, map_slot)` gives
the byte position for valid original-row and map-slot indices. Authors can also
prepare this metadata with `packet_layout<N>::make(rows, map_size, groups)`.

Reads allow duplicate ranks and zero holes. Writers reject duplicate destination
ranks, consume the same grouped order, ignore hole input slots and preserve every
unmapped bit, including spare bits. Selected input codes must fit their unsigned
widths.

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

The complete-record `write_plan<128>` / `writer<128>` family uses one `packet<128>` per original row, independently of
packet shape. This is a 128-byte rank-ordered input; only defined codes are
consumed. Construction validates them all before writing, zeroes spare bits
and preserves bytes outside each unit. It needs no prior initialized contents.

## Complete-unit transfer

`bind_transfer<Bytes>(source_layout, source_view, destination_layout, destination_view)`
returns a borrowed `transfer<Bytes, Byte>`. `Bytes` is 1–64; both descriptions
must compare equal, including code rank order, offsets, shifts, widths and unit
extent. Source and destination may have different row counts; each command
admits its selected source rows and destination window. Views do not retain layout identity:
the caller supplies authoritative descriptions for the bytes. The operation
retains the named views; it does not retain the supplied layout objects.

`copy(first, source_rows, effects)` copies selected physical source rows into
consecutive original destination rows. Source rows may repeat or be reordered.
Every source unit must be initialized, including spare bits and entire spare
bytes. Copy preserves those bytes exactly; destination contents need not be
initialized. The owner must grant exclusive writes to whole destination units,
including spares. Projection replacement remains the operation for preserving
unselected destination fields. Copy performs no logical maintenance, format
migration, ownership transfer or publication.

Checked copy admits every source row, the full destination range, journal state
and effect capacity before any effects or stores. Selected source units must be
disjoint from the entire destination window, including its stride gaps. This
conservative rule allows disjoint copies within one allocation but rejects
self-copy, overlapping permutations and sources inside destination gaps.
Rejection preserves destination bytes and the journal, including existing entries.
Empty selection may start at the destination end and forms no payload address.

`effect_capacity(first, count)` bounds additional journal records: zero for no rows,
one for a tight destination, otherwise one per row. The result rejects an
out-of-view command with `range`; a full journal rejects checked copying with
`effects`, while placement storage failures use `capacity`. Tight output reports the
complete contiguous span before any store; gapped output reports each exact unit
before storing it. Effects name the destination view and storage-relative offsets;
no prefix, stride gap or trailing padding is read or written.

`admit(first, source_rows, remaining)` permits preflight with other children;
`copy_unchecked` requires that exact command and proof to remain valid. Views,
backing, source bytes and source row selection stay stable throughout. View/selection metadata
is disjoint from issued writes. Journal storage is disjoint from both payloads
and command metadata; coverage hooks cannot allocate, fail, suspend or modify source bytes,
views or selection. A composed owner admits every child before beginning writes
and separately accounts for summaries and visibility.

## Borrowing and admission

| Resource | Required lifetime or proof |
| --- | --- |
| Layout, map and groups supplied to preparation | Can expire after their controls are copied |
| General reader/writer's named view and prepared plan | Unchanged values at stable addresses through every bound invocation; rebind after assigning a new plan or placement |
| Prepared predicate's layout, plan and intermediate reader | Can expire after binding; the code projection is copied |
| Prepared predicate's named view | Unchanged value at a stable address, with live backing, through every invocation |
| Source view identified by emitted effects | Resolvable until the owner consumes those effects; the prepared plan need not survive for this alone |
| View storage | Live, with unchanged placement and the layout that encoded it |
| Input, output, selection, effects and maintenance | Valid through admission and execution; retained across any owner stop that borrows them |
| Observation projection, law and source views | Live through all observations |
| Erased operation's concrete source | Stable while the erased operation borrows it |
| CPS plan and bindings | Live through the complete chain |

`predicate<Byte, Rows=1>` prepares an ordinary single-code comparison.
`bind_predicate<Rows=1>(layout, std::size_t code_index, view)` selects a code from
an existing validated layout without preparing packet controls. The layout is
authoritative for these bytes: the view retains extent, not layout identity.
The binder checks equal unit extents first (`error::description`), then the code
index (`error::map`), without payload reads. It borrows the layout only through
binding and accepts a temporary layout. Temporary views are rejected.

`bind_predicate(reader)` and `bind_predicate(plan, view)` reuse existing plans.
These forms reject empty, hole-only and multi-code maps with `error::map`,
without payload reads. Every form copies the code and borrows only the named
view under the table above. Rows is a power of two from 1 through 64; packet
capacity N has no meaning
for this operation. Binding from different packet capacities with the same Byte
and Rows returns the same predicate type. The code is prepared once; comparison
operator, right-hand side and row activity belong to each `filter<Op>` command.
The general reader borrows only plan/view; its `filter` convenience prepares a
predicate for one call. Keep a prepared predicate outside a repeated filter loop.
Its checked filter admits the original-row window even when a query can be
decided without loading payload. `_unchecked` requires that window proof.

A view admits `count` units at `storage + offset + row × stride`. Stride must
cover the unit. The final row requires only its occupied unit, with no padding
beyond it. Attachment checks extent and address arithmetic, not existing byte
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
beforeimages. `writer::effect_capacity(first, count, selection)` returns a
conservative bound on additional records for that original-row command. Invalid
coordinates return an error rather than a misleading capacity. Initialization
uses `effect_capacity_initialize(first, count)`, one record per row. Persistent summary writes
need separately reserved resources. Raw plan `set_unchecked(std::byte*, ...)` emits
no effects; bound trusted entries retain pre-write coverage.

`author::bind_group(children...)` supports recursive mutation groups.
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
`author::projection(readers...)` returns a tuple of packets. Projection
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

`author::native_reader<N, Byte, Rows>` and `author::native_writer<N, Rows>` normally deduce
their arguments from bound ordinary operations. `native::packet_for<8>` is
`uint64_t`; `packet_for<64>` is the compiled ISA's vector carrier. Native calls
require matching carrier types and calling conventions. Controls are ISA-specific.
Current execution profiles are x86-64-v3 or ARMv8 SIMD; unsupported profiles
fail at the shared configuration boundary. Native AVX-512 requires VBMI.
Ordinary view/storage signatures remain independent of the ISA.

For `author::native_reader<8, Byte, Rows>`, `admit_transfer<Bytes>()` accepts
`1 <= Bytes <= 8 / Rows` at compile time and returns `description` unless the
prepared projection has exactly that contiguous transfer width in one group.
It changes neither the plan nor the binding. Offsets, equal shifts and masks
remain those qualified by ordinary reader preparation; sparse/reordered or
otherwise unsupported shapes keep the ordinary/native fallback.
`get_transfer_unchecked<Bytes>(first, active)` requires that shape admission
and a separately admitted current original-row window, stable named plan/view
and readable backing. It invokes the same bounded transfer body. Inactive rows
issue no access; an entirely empty mask returns zero without forming an address.

Raw multi-row native bodies take an address callback, active mask and optional
stride hint. They request only active addresses. A nonzero hint promises one
allocation with that byte stride; omit it for arbitrary addresses. Ordinary
bindings supply that proof. Controls must match Rows; one-row bodies use a
direct pointer. Access stays within active units, including with sparse masks.

`author::read_access::common_window` explicitly grants wider reads to a native
adapter. The default is `active_units`:

```cpp
using Access = tp::author::read_access;
tp::author::native_reader<64, std::byte, 64, Access::common_window> read_window(read);
tp::author::native_writer<64, 64, Access::common_window> write_window(write);
```

This is an owner promise, not another extent check: retain initialized, readable,
read-isolated bytes throughout the command. When contiguous physical transfers
fill their stride, the grant covers the hull from the first selected transfer's
first byte through the last selected transfer's end, including inactive interior
units. It grants no preceding/following bytes or stride padding. A named view's
allocation extent and a nonzero stride hint alone do not supply initialization
or isolation. Row admission still applies; an empty mask forms no address.
The grant adds no stored state to either adapter.

The same compile-time choice follows `Rows` in
`native::read_body<Rows, Access>` and `native::write_body<Rows, Access>`.
A zero stride hint retains active-only gathers, including with `common_window`.
Ordinary and compiled/buffered operations keep `active_units`. GPR and point
transfers are unchanged. Preserving writes can use the grant for old-data reads;
issued stores and journal effects still cover active units only. Protecting those
stores does not by itself isolate inactive old-data reads.

`native::row_mask(plan, active)` expands original-row activity using the prepared
packet order. Each map slot of an active row, including a hole, gets byte `0xff`;
inactive slots and trailing capacity get zero. `row_mask_for<N, Rows>` describes
full-capacity row-major blocks and does not account for a short map or grouping.

`compact_word(wide)` extracts packet bytes 0–7 into a GPR;
`expand_word(word)` preserves those positions and zeroes bytes 8–63.
Equivalent operations on the two widths use the same
map, groups and Rows, with at most `8/Rows` map slots (Rows 1/2/4/8). The entire
used sequence then fits in the first eight bytes. Compaction of a larger map
discards data; expansion does not supply values for additional write destinations.

The restricted route algebra carries input bits or zeros. Conflicting OR,
arithmetic and composite predicates require authored bodies;
[unsigned code comparisons](../filters.md) have shared filter bodies. A normalized route requires
each output byte to be a complete permutation/rotation of one input byte;
other supported wiring uses the general lowering. General plans borrow at most
16 caller-owned terms.

`erased_writer<Input, Maintenance, Rows>` erases a whole range while keeping
concrete traversal inside it. Rows remains part of the erased contract even when
two packet shapes have the same C++ payload type. Inline and CPS share bodies;
the [source guide](../source.md#shared-facilities) locates their shared supported
`ikea::author::chain` implementation. Explicit suspension happens after returning to the owner.
