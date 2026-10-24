# Owner-side qualification

## RLE and leading-plane placeholder extension

25 October 2026. The latest source extension passes all seven owner checks
both normally and with owner/runtime/Orbital ASan/UBSan:

| Check | Placeholder-extension result |
| --- | --- |
| Description / manifest | 559 / 284 checks |
| Material / group images | Material checks pass; 1,953 valid images, 991 guarded prefixes and 26 malformed cases |
| Basic history | 785 total checks, including teardown |
| Stream binding / history | 7,495 / 350,630 checks |

Receipts are `build/frame-owner-relative-v1/placeholders-finish.log` and
`placeholders-san.log`. Production and check sources were rebuilt against the
renamed node member; the normal run completed its final two test TUs after
compiling the unchanged runtime-width bridge and core, and the sanitizer run
rebuilt the owner set. The linked SeriesPack archive remains uninstrumented;
hardware-guard and sanitizer coverage retain the limits below. No native
placeholder or relative-owner qualification is claimed.

The description now names block-local `runs` with ordered sizes-minus-one
and values children in one data-dependent `run_ordinal` domain. Independent
wire bytes and malformed coordinate/arity/type/global-mode cases are checked.
The shared in-memory run stage demonstrates 257 equal rows becoming two runs
(`[255,0]` sizes-minus-one), versus 257 alternating singleton runs. Its child
cardinalities, reads, points and exact SUM are checked against original rows.
This is a shape/reference witness, not a new RLE encoder or imported-image law.

Pack nodes separately carry H=0/8/16 in the former reserved u16; other nodes
require zero. H0 wire bytes stay unchanged. Independent H8/H16 golden inputs
and refusals cover all packing families and bound/plain leaves. The physical
witness uses existing SeriesPack K/H=12/8, 20/8, 20/16, 32/16 layouts at 257 rows:
actual body/head planes reconstruct the input, leading bytes use each layout's
K, and family geometry is selected from K−H. The description does not establish
per-block K or H≤K. Checked owner factories refuse RLE and headed leaves
before touching an inaccessible payload; Frame image placement and execution
for those placeholders remain unsupported.

## Relative-ends admission and form conversion

Owner baseline `d4ad21a4` uses shared stage `a74e1c44`, including its relative-ends
mode. All seven owner checks pass in a small local Linux AArch64 runtime-leaf
overlay, both normally and with owner/runtime/Orbital ASan/UBSan. The unchanged
SeriesPack headers and archive are reused from the qualified runtime capture;
no provider rebuild, corpus copy, worker or performance run is involved.

| Check | Qualified d4 baseline result |
| --- | --- |
| Description / manifest | 443 / 284 checks; both interval kinds retain their original node IDs and the same checkpoint cardinality/placement law. |
| Material / group images / basic history | Pass; 1,953 valid, 991 guarded prefixes, 26 malformed images; 785 basic history checks. |
| Stream binding | 5,885 checks: both forms, G16/G64, all predicates/results, sparse grants, constants and form-specific signed-offset evidence. |
| Stream history | 350,630 checks: both forms at G16/G64, logical updates, old readers, and pure same-version conversion in both directions with exact original-row equivalence and full teardown. |

This local checked-owner qualification is distinct from the native shared
captures: their owner files match `e90542d9` and qualify sizes only (142
manifest, 2,700 binding and 87,718 stream-history checks). The shared stage in
those captures exercises relative ends, but that does not qualify the later
checked-owner admission/history or the new placeholder extension. See the
[source-qualified comparison account](../controls/SCORECARD.md#source-and-qualification-scope).

One independent malformed-image witness changes a checkpoint child field from
2^63 to 2^63+1, using the shared child's real builder/emitter. The child remains
structurally valid and all decoded logical values stay unchanged, but relative
SUM increases by one. Checked attachment refuses it. A negative control removing
only the zero-checkpoint-field proof fails on that witness, rather than merely
passing another round-trip assertion.

The exactness tests separately cover -2^63 (accepted), +2^63 (false exact flag
required), adjacent differences that fit while a relative offset does not, and
large jumps between checkpoints whose relative offsets remain exact. Partial
blocks retain the decoded fallback. Conservative false evidence remains legal.

`build/frame-owner-relative-v1/` retains the small source overlay, dependency
reference, normal/SAN logs, conversion recheck and checkpoint-negative receipt.
The final conversion-only test-TU recheck is in `conversion.log`; its source
change does not replace or qualify a different production implementation.
The instrumentation limits below still apply: the SeriesPack archive is
uninstrumented, and hardware guards cover its image boundaries. No native
performance or optimized interval mutation claim follows from these checks.

## Earlier runtime-leaf capture

2026-10-24. The matching frozen runtime-leaf build passes normal and
ASan/UBSan checks on local Linux AArch64 NEON with pinned Clang21.1.8.
These are functional checks, not a native performance score. No native owner
timing is established yet.

| Check | Completed in the earlier runtime capture |
| --- | --- |
| Canonical descriptions | 443 checks: independent little-endian bytes, exact lengths, malformed topology/coordinates, sharing, fixed Fr/v1 packing geometry and unchanged output on refusal. |
| Stream manifests | 142 metadata-only checks: node IDs, child row/group domains, disjoint co-packing, bounded Part locations, closure and availability refusal; no payload access. |
| Public Orbital material | Two-TU lifetime/identity/resource checks pass, including selected-Part interpretation after builder/Scope/Store destruction, retained backing without sibling payloads and co-packed stream metadata. |
| Group image admission | 1,953 valid images, 991 guarded truncated prefixes and 26 malformed cases; all widths 0–64 and all packing presets. |
| Basic owner history | 785 total checks: ordered/repeated/restored writes, slack exhaustion, exact summaries/contribution, rollback, sparse group 1 attachment, stale basis and explicit retry, untouched Part reuse, append, pack-only conversion and old readers. The earlier 751-check line in the log precedes teardown checks. |
| Stream binding | 2,700 checks at G16/G64: real emitted images, K0/zero bodies and empty exception areas, partial tails, evidence validation, sparse/co-packed dependency refusal, FILTER/SELECT guarantees and payload-free domain predicates. |
| Stream owner history | 87,718 checks: actual Orbital publication of root/checkpoint replacements together, retained old readers, exact original-row answers and a valid metadata-only remap that defeats shallow basis comparison. Interval changes use full rebuilds. |

## Source and instrumentation scope

The capture uses landed SeriesPack `0d44d527` and shared Frame prototype
`d1a439b4`, with the owner source hashes in
`build/frame-owner-runtime-v1/sources.json`. Normal and sanitizer compile/run
receipts are `check.log` and `check-san.log` in that directory.
`qualification.json` seals that capture's source checks and result; its source
identity predates the relative-owner and placeholder extensions above.
All owner check TUs and the runtime-width bridge were rebuilt together; this
receipt does not reuse the earlier template width objects against a new
interpretation layout.

The owner code, compiled runtime-width bridge and Orbital runtime are
ASan/UBSan-instrumented. The matching linked SeriesPack archive remains
**uninstrumented**. Guarded source/image tests check hardware boundaries,
including native leaf accesses; sanitizer success is not a claim that the
whole Ikea library was instrumented. This is local NEON correctness evidence,
not a native Zen5/Graviton4 throughput ranking.
The basic/stream histories and stream-binding fixtures use compact packing;
group-image admission separately covers all three packing families.

## What the runtime-leaf baseline establishes

- Fr/v1 packing families retain their fixed Local/striped geometry by width,
  independent of future preset recommendations and execution ISA. K0 has no
  leaf body; real constant streams with slack 0 exercise empty exception areas.
- Explicit manifests separate physical `part_extents` from stream row domains
  and section locations. `same_material` includes that complete immutable
  meaning and selected Part geometry/addresses. A valid group remap can change
  logical answers while pipeline bytes, row extent and addresses stay fixed.
  This is a material-basis test, not a Store-cut or cross-participant proof.
- FILTER preserves excluded coarse slots and clears partial-tail high bits.
  SELECT preserves original-row order, output sentinels and unused capacity;
  domain, fine-mask, availability and capacity refusal precedes writes.
- COUNT/FILTER settle `>=0`, `<=UINT64_MAX`, `<0` and `>UINT64_MAX` from the
  non-null UInt64 logical contract with every Part inaccessible and no section
  keys available. Invalid comparisons/coarse coordinates still refuse.
  Empty fine SUM/SELECT also need no payload. Other predicates remain subject
  to payload closure; point/read `required()` stays conservative.

## Payload accounting and limits

The basic-history run records 3,680 successful-private-source-copy bytes and
7,602 emitted bytes across its explicit preparation attempts, including the
successfully prepared candidate later rejected as stale. These are not
per-publication costs. It publishes 12,638 fresh image bytes and reuses 4,073
image bytes. After conversion, the first old reader alone retains an extra
2,134 actual payload-capacity bytes relative to the selected alternative.
These are fixture observations, not general compression or memory-bus traffic
claims.

Append and conversion deliberately use full rebuilds. Refused `prepare_group`
attempts, private preparation allocations, material metadata and allocator/
control-block overhead are excluded from the payload counters. PMR live bytes
count only resource allocations; interpretation vector capacities are
accounted once per unique retained `&control.meaning()`, separately from
payload backing.

This earlier capture predates relative-ends admission. None of these captures
establishes optimized interval mutation, hot/cold owner throughput,
fitting/encoding speed or shared bandwidth.
The [control scorecard](../controls/SCORECARD.md) owns performance evidence;
functional checks do not fill unmeasured cells.

## Earlier receipts

`build/frame-owner-core-v1/` retains the earlier 804-check basic history;
`build/frame-description-review/` and `build/frame-owner-material/` retain
the separate early description/material/validator checks.
`build/frame-owner-stream-v1/` retains the earlier multi-stream capture
(251 description, 142 manifest, 2,120 binding and 87,628 history checks), with
uninstrumented reused width kernels. Those snapshots predate the latest
facade/basis changes and do not replace the later matching runtime-leaf
receipt. Directory-wide commit `88cb8ee1` included unfinished owner files;
its seam measurements alone never qualified their implementation.
