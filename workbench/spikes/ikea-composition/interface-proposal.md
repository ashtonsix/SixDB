# Ikea interface cohesion

**Status (2026-10-23):** the broader contract is implemented in commit `876a8af0`.
Cross-profile checks and native comparisons are recorded in the
[qualification account](evidence/interface-cohesion-v2-20261023/README.md), including
remaining losses and the final complete-writer API correction. This document
records the decisions and their reasons. The [interface guide](../../../ikea/docs/interfaces.md)
owns current usage; completion requires the evidence below, not this status line.

Ashton's follow-up reopened the handle/control design. The
[reusable execution survey](reusable-execution.md) connects the original
TuplePack ABI work to cheap block updates across all consumers. The implemented
TuplePack endpoint restores the two-pointer general reader and prepares repeated
filters with a separate `predicate`. The earlier unconditional 24-byte reader
cache is retained as candidate history; cross-profile and complete-caller
qualification of the new endpoint is recorded separately from the ABI improvement.

Ashton explicitly allowed disruption before these modules acquire substantial
consumer weight. The initial [cohesion audit](interface-cohesion.md) identified
real coordinate, ownership and execution differences, but its rejection of broad
vocabulary, byte and schema changes was insufficiently justified. Having no
current consumer requesting a correction does not make an accidental distinction
a good long-term contract.

## Starting evidence and the test applied

The [heterogeneous closing assessment](probes/ikea-heterogeneous/closing.md),
[filter-composition runtime findings](../filter-composition/RUNTIME.md),
[Intervals caller audit](../intervals/AUDIT.md), and
[composed metadata study](../orbital-mvcc/metadata/composed/README.md) constrain
value meaning, traversal, exact storage grants and coherent retained state.
They do not establish that coherent names or common boundary types are slow.
We reuse their native bodies and complete consumers rather than making a new
universal iterator. The [construction counterexample](../orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
requires charging complete owner work even when a child description gets smaller.

Every difference must communicate a fact the caller needs. Preserve a difference
when it represents distinct semantics or measured useful execution. Remove it
when it merely reflects separate implementation history. Qualification compares
actual callers; a spelling change is not proof of identical generated code.

## One vocabulary

| Fact or action | Chosen interface |
| --- | --- |
| Byte law, independent of addresses | `layout`; runtime construction uses `make` |
| Physical placement | `view::attach` / `const_view::attach` |
| Recovered content proof | `validate`; Bec256 binds that proof into a reader |
| Storage-independent maps and routes | `read_plan::make`, `write_plan::make` or an author plan |
| Reusable operation | `bind_reader`, `bind_writer`, `bind_transfer` |
| Runtime whole-operation erasure | `erased_reader`, `erased_writer`, or `erased_table` |
| Command | `get`, `read`, `filter`, `set`, `replace`, `initialize`, ... |
| Trusted command | Same name plus `_unchecked`, beside its checked peer |
| Error text | `describe(error)` |

`view` is writable and `const_view` is read-only. Attachment proves placement,
not contents, lifetime, synchronization or initialization. Infallible binders
return an operation directly; fallible binders return `expected`.

SeriesPack's compile-time `format` becomes `layout`, recovery `representation`
becomes `layout_value`, and expression construction becomes `author::expression`.
Its typed reader owns expression nodes and borrows leaves, with point/range,
filter, first-match and sum commands. General expression erasure borrows that
reader and rejects temporaries. Dense-source erasure can copy its pointer/count
proof. A direct erased physical-view binding retains the original named view;
copying metadata must not change journal identity.

TuplePack's former `reader` and `writer` were plans. They become `read_plan`
and `write_plan`; the bound operations take the simpler nouns. Filtering is a
reader command with the existing encoded one-code shortcut. Predicate-only
callers use `bind_predicate<Rows>(layout, code_index, view)` without preparing
packet controls. Existing plans support `bind_predicate(reader)` or
`bind_predicate(plan, view)`. Every form copies the code projection once.
The predicate borrows only its named view; its layout, preparation
plan and intermediate reader may expire. Its type is `predicate<Byte, Rows>`,
independent of unused packet capacity. A checked filter
rejects a hole-only, empty or multi-code map. The unchecked peer requires that
map proof as well as its activity/window proof. Full-record initialization is a
writer command over `write_plan<128>`, retaining the two 64-slot bodies, zeroed
slack and absence of old-destination reads. It is not implemented as repeated
preserving replacement, and that writer exposes no preserving `set`/`replace`
commands. Its existing author construction leaf remains available to admitted
compound commands.

StreamPack uses `layout::options`, `layout::make`, `attach` and
`attach_unchecked`, and keeps checked/trusted free commands together.
`author::visit` selects typed whole-consumer execution. Bec256 separates a placed
`const_view` from its validated reader and exposes `encoding::make` for prepared
replacement. Intervals remains a borrowed ordered-search/geometry interface;
adding unnecessary layout/reader objects would obscure its actual contract.
Hashtable remains an owner, with `erased_table`, `reader`, `native_reader` and
`staged_reader` replacing names that conflicted with layout descriptions/views.

The supported extension namespace is `author`; `native` holds ISA-shaped
carriers and bodies; `detail` is private. Shared CPS is `ikea::author::chain`,
with its own `chain_error`. Public declarations no longer hide in private point
or mutation headers. Raw physical SeriesPack stores live in `author/store.h` and
explicitly omit effects and summary maintenance; ordinary mutations use writers.

## Storage, values and selections

Borrowed storage, encoded descriptions and opaque Hashtable payloads use
`std::byte`. Decoded values and numeric code maps remain unsigned integer types.
An ambiguous public `byte` alias for numeric TuplePack values is removed.
Const-preserving `to_u8` bridges serve private arithmetic without changing the
storage grant. This adopts one storage-byte contract without pretending an
unsigned decoded value is opaque storage.

All bitmap selectors are called `selection` and expose `all`, `none`, `bits`,
`is_all`, `is_none`, `covers`, `contains` and `mask`. Default construction means
none; command defaults explicitly say all. They borrow and retain original
coordinates. The physical backing remains useful:

| Selector | Backing and origin |
| --- | --- |
| SeriesPack | Numeric u16 words; origin divisible by 16 |
| TuplePack | Numeric u64 words; arbitrary origin and bounded cross-word windows |
| Intervals | Exact bytes, arbitrary origin; `complement` accepts external exclusions |

No selector grants wider padding. Native TuplePack masks still name packet
bytes, while ordinary filters name rows relative to the requested window.
Plan-specific chosen-slot translation remains explicit. This is a real semantic
difference, not an excuse to retain unrelated names.

`ikea::bitset256` is a shared 32-byte numeric position value, with count, rank,
window extraction and portable little-endian byte conversion. The name identifies
a fixed-size set rather than an unspecified block. It has natural u64 alignment;
the proposal's mandatory 32-byte alignment would impose a native-carrier
constraint without a need. Selectors borrow a named value and reject temporaries.
Bec256 accepts it directly. Little-endian bridges use existing bounded extraction
paths; big-endian conversion is explicit. This value supplies no Frame-specific
row meaning or coarse-block semantics. No future posting-list consumer is claimed
as an implemented witness.

## One effects protocol, with owner admission intact

`ikea::journal` records `{source, plane, offset, size}` into preallocated storage.
`effects<E, Source>` requires an infallible `before(source,plane,offset,size)`;
`no_effects` states the intentional private-write case. The plane-only journal and
competing write record types are removed. Source means the actual named view or
owner, including substituted leaves, not a copied placement wrapper.

Checked mutations use the journal; trusted mutations accept the effects concept.
Capacity queries count conservative **additional records per command**. Invalid
coordinates can return an error; a capacity query does not admit the command.
Journal shortage is `error::effects`, storage shortage is `error::capacity`.
Shared condition names have shared meanings; module-specific errors and owner
outcome statuses retain their meaning.

Hashtable keeps `enter`/`prepare`, generation checking and stable-owner semantics.
Its records resolve planes through `plane_storage`. Retirement identities and
extents remain owner metadata, separate from issued writes. Sharing the hook
cannot remove a prepare-before-store guarantee, authorize an unknown source, or
turn journal entries into allocation/retention records.

Issued bytes still differ from logical destinations and old-value/summary
dependencies. Packed preserving stores require protection of the complete
physical grant. Ikea effects provide neither beforeimages nor publication.

## Headless data and shared parent-schema framing

Data blocks acquire no descriptor header. The parent owns count, placement,
strides, semantic identity and coherent publication. It may share one layout
schema across many blocks and store a short schema id per group. Per-block
mutable controls are data in caller-placed planes.

A shared five-byte envelope frames descriptions inside that parent schema:

| Bytes | Meaning |
| --- | --- |
| 0–1 | Module tag |
| 2 | Byte-law version |
| 3–4 | Little-endian body length |
| 5 onward | Canonical module-owned body |

The proposal's one-byte length cannot encode TuplePack's 128 code triples; the
two-byte length preserves its supported scope. Exact-length decoding rejects
unknown tags/versions and noncanonical bodies. SeriesPack's schema is eight
bytes containing K/H/geometry only; parent counts remain distinct even where
the same physical tile extent fits several logical counts. TuplePack v2 retains
all code triples. StreamPack's current schema is framed and its unused historical
geometry is removed after owner/caller search; old captures remain historical
sources. Bec256 has one fixed law, and Hashtable's erased owner has no wire form.
No native C++ object or function table becomes a persisted description.

## Targets and verification

The current execution profiles are x86-64-v3 and ARMv8 SIMD. This is an explicit
simplifying assumption, not a claim that the old build already required it.
Fresh CMake configurations choose these minima; explicit cache values remain
owned by the caller. One configuration check rejects missing backends clearly.
Ordinary signatures and portable wire laws remain independent of ISA types,
leaving room for future targets including PowerPC.

Qualification must cover:

- Full-scope optimized Ikea checks, independent wire/selection fixtures, source
  identity, journal refusal, temporary borrowing and standalone public headers.
- Maintained Engine Directory, Loom and Orbital callers; retained Orbital metadata
  and multimap owner checks where the changed boundary is actually consumed.
- Actual SeriesPack/TuplePack/filter consumers on NEON, Zen 5 AVX2 and AVX-512,
  with source-qualified disassembly and native before/after timings. Inspect
  register pressure, stores, helper calls, addressing and branches. Byte equality
  is useful evidence where it holds, not an acceptance condition for changed
  contracts. Investigate meaningful losses.
- Guide/example/source-map consistency, image/link rendering and removal of
  obsolete public names. Historical research retains its source vocabulary.

The old 417-function cohesion receipt proves its old cut only. Broader effect
records, initialization and integrated filtering require a new receipt. The isolated
[settled Frame port](evidence/interface-cohesion-v2-20261023/frame/README.md)
keeps the same parent algorithms and controls in both arms. The later
[owner-qualified live port](../frame-phase-1/evidence/api-migration-20261023/qualification.json)
passed 462 commands and 9,600 complete owner records on its recorded source cut
in commits `feb56ce1` and `772efec7`. Neither proof qualifies subsequent Frame
algorithm changes or supplies a throughput result for them.
