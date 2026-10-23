# Interface cohesion across the maintained packs

**Historical bounded audit.** Its broad naming/byte/schema rejections were
reopened by Ashton and are superseded by the [broader cohesion decisions](interface-proposal.md).
The evidence below applies to its recorded source cut, including the old codegen
receipt; it does not qualify the newer contracts.

Ashton's 2026-10-22 request starts from an external review of Ikea's documentation
and interfaces, asks us to challenge it seriously, and encourages broader agent
coverage. This follow-through examines actual headers and consumers rather than
adopting the review's proposed vocabulary or formats as requirements.

## Starting evidence

The [heterogeneous closing assessment](probes/ikea-heterogeneous/closing.md)
separates logical identity, native grain, owner admission and output obligations.
A selected ordinal remains its original position; decoder read windows and
preceding metadata can remain dependencies even when a body is excluded. It
explicitly establishes no universal iterator or carrier.

[Filter composition](../filter-composition/README.md) and its
[runtime findings](../filter-composition/RUNTIME.md) retain the cost of real
boundaries: ordinary hot filters favor fused bodies, while canonical shared
consumers can reduce specialization inside an already useful CPS operation.
The corrected distinct-stream results reject an earlier throughput justification
for universal shared dispatch. [SeriesPack history](seriespack-history.md)
likewise retains broad-range and scalar-normalization losses.

The [Intervals caller audit](../intervals/AUDIT.md) supplies the current
StreamPack/Directory/Frame counterexample to a generic value wrapper: fused
start+U, ordered key projections and native run-end search need different
owner interpretations around shared kernels. The
[composed metadata study](../orbital-mvcc/metadata/composed/README.md) and
[construction negative](../orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase)
show why equal byte extents need not mean equal logical counts and why smaller
metadata does not establish cheaper complete construction.

The question is which inconsistent names or hidden type ownership can be
corrected while preserving native bodies, retained state and operation checks.
These findings do not rule out a shared abstraction when a complete consumer
earns it.

## Review claims against actual consumers

| Proposed seam | Evidence and decision |
| --- | --- |
| One selection family with explicit grain | SP u16, TP u64 and Intervals byte storage all encode one bit per logical row/rank. Word width is not grain. SP needs a 16-aligned origin, TP permits arbitrary origins/cross-word windows, and Intervals preserves exact byte tails and inversion. Frame block bits are a separate coarse coordinate. Retain the different storage grants. |
| Coordinate-tagged masks | Ordinary SP and bound TP both use bit r for original row first+r; the review's claimed mismatch is false, and the consumer guide repeated it misleadingly. Native TP bits name packet bytes. A tag alone establishes neither matching origins, plan identity, field choice nor AND/OR reduction. Demonstrate chosen-slot translation before selecting a public mapper. |
| Uniform placement verbs | Attachment, unit-layout construction, compact geometry preparation, projection planning and operation binding validate/retain different things. Explain their actual phases rather than adding aliases suggesting equivalent work. No blanket rename selected. |
| One byte type | Unsigned-byte arithmetic and object-byte storage differ. Standard span byte views preserve extents at owner boundaries. No complete consumer currently justifies changing all specialized bodies. |
| Shared descriptor envelope | SP describes array/placement facts, TP a unit layout and StreamPack a shared value schema. Hashtable's descriptor is a native owning runtime binding, not serialization. Parent framing already composes portable child descriptions; a shared envelope would add an unneeded format. |
| Module errors and common diagnostics | Useful, but StreamPack imported Intervals' error enum through private compact geometry. Give StreamPack owning error/geometry types and remove retirement from Intervals. All five packs now supply cold describe(error) returning string_view; no universal error container selected. |
| Consistent effects names | Confirmed: sp::write_journal meant plane-relative writes while composition::write_journal meant source-qualified writes. Keep the shared source_write_journal name in composition and its actual callers. The underlying type stays identical. |
| Missing author surfaces | author/native.h and view::assume_valid are supported. Public scalar get/set existed in detail/point.h; give them a substantive supported point.h and explicit effects/initialization contracts. Frame's detail::each is a width-instantiation loop, not missing packed-data traversal. |

The [grouped example](../../../ikea/examples/filters/ordinary.cpp) uses map
`{1,hole,0}`, groups `{1,2}` and original window `first=7`. It supplies explicit
packet-byte activity, gathers chosen slots and intersects SeriesPack row bits.
The independent input calculation requires `0x18`.
[The filter check](../../../ikea/test/filters/tuple.cpp) separately checks literal
packet positions and input-field row masks at origins 0/7 with empty and edge
activity; its expected result does not use the tested plan mapping.

Coordinate tags could reject accidental row-mask/byte-mask arithmetic. This audit
does not establish that wrappers are slow or useless: no wrapper-performance
comparison was run. The current native call sites need plan-specific chosen-slot
translation, while ordinary filters already agree. The bounded change teaches
and verifies that translation without choosing a second public mask API or
changing native/CPS carriers. It leaves the useful type-safety proposal open.

A non-Frame parent already exists in the
[Object consumer](../orbital-mvcc/consumer/data.cpp): exact Part interpretation
spans feed module decoders, and shape construction retains parent interpretation
separately. Posting lists remain a possible future witness, not a reason to
create a consumer solely to ratify a proposed abstraction.

## Maintained consequences

[The interface guide](../../../ikea/docs/interfaces.md) connects actual phases,
coordinate words, selection laws, journals, byte bridges, errors and parent
framing. The consumer guide corrects its mask distinction; references retain the
exact contracts.

Public StreamPack types.h owns bytes, geometry, capacity and errors. Compact
internals import those from their owner and name the shared Intervals projections
explicitly. This changes source type identity without changing encoded bytes.
Error ordinals are not a shared or persisted status format.

SeriesPack point.h owns the same scalar functions previously under detail. Raw
stores emit no effects or summaries and do not initialize all tile slack. Shared
packed bytes must already be initialized and isolated. Owner mutations retain
bind_mutation with source-qualified effects. Function bodies and packed kernels
are unchanged by the header move.

Ikea LEAD confirms the active Frame seams: original-block selection plus fine
row validity, neighbor-preserving stores, journal resolution before local views
die, and coherent value/presence/validity/override versions. Construction,
borrowed native operations and publication remain separate. The Frame owner
independently updated encoding's journal spelling; the helper's approved lifecycle
adapter change is spelling-only. The Frame owner also removed its private
`detail::each` dependency in `beb76710`, using a local standard index sequence
for width instantiation. This needs no public traversal abstraction. Active
material/kernels/phase work remains with its owners.

## Verification

The [retained emitted-code receipt](evidence/interface-cohesion-20261022/codegen-summary.json)
compares four actual example translation units: SeriesPack ordinary and retained
owner integration, filter composition, and StreamPack ordinary. Baseline
`8ba3ad7362faf74f3f35d4bc4f6cec55398c29b5` supplies all 366 archived Ikea files;
the after snapshot includes the new public headers. The
[serial runner](cohesion-codegen.py) records pinned Clang21.1.8, GCC15 native/cross
headers, exact commands and dependency hashes. Source-specific automatic-vectorizer
settings match the configured targets.

| Profile | Complete emitted functions equal | Constant sections equal | Unwind/exception sections equal |
| --- | ---: | ---: | ---: |
| V2 NEON |141/141 |17/17 |8/8 |
| Zen5 AVX2 |136/136 |25/25 |4/4 |
| Zen5 AVX-512 |140/140 |27/27 |4/4 |

Function bytes, section alignment and relocation offsets, kinds, targets and
addends match. Only source prefixes and the two corresponding moved StreamPack
type names are normalized; assertion strings and other constant bytes remain
literal. Inspection includes mutation admission, the retained owner program,
source-qualified journals and native handoff. Existing NEON reductions/GPR masks
and AVX-512 mask transfers remain. The standalone aggregate-return stores also
remain: this is not a claim of universal register-only boundaries.

These are unlinked object comparisons, not runtime or linked-address results.
External compiled callees are identified call targets; their bodies and arbitrary
other consumers are not covered by this receipt. No new speedup is claimed.

All 33 selected optimized CTests pass with assertions enabled and full K1..64
scope. They include SeriesPack mutation/substitution, ownership, admission and
descriptor checks; the filter width and grouped-coordinate oracle; Intervals
search/cuts/sparse/seams; StreamPack histories, encodings and compact operations;
Bec256 ordinary/composed operations; Hashtable storage/descriptor witnesses;
actual Engine Directory; and the affected ordinary/composition/integration
examples. Seventy ordinary/author headers compile independently. The point
header also compiles on baseline x86 without an AVX2 execution profile.

The five changed benchmark callers pass syntax checks with their target macros.
A linked check verifies the five cold diagnostic signatures, distinct StreamPack
and Intervals errors, known/unknown diagnostic calls, journal type distinctions
and a writable/read-only byte-view round trip. Documentation links and Markdown
images/tables are checked; stale maintained shared-chain anchors are repaired.
Working logs live under ignored `build/ikea-cohesion/`; the retained receipt and
Git source recovery above carry the useful comparative result.
