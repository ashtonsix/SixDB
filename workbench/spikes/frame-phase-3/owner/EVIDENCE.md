# Owner-side qualification

2026-10-24. The matching frozen runtime-leaf build passes normal and
ASan/UBSan checks on local Linux AArch64 NEON with pinned Clang21.1.8.
These are functional checks, not a native performance score. No native owner
timing is established yet.

| Check | Current completed evidence |
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
receipts are `check.log` and `check-san.log` in that directory. The manifested
owner headers/sources match the worktree at this documentation review.
`qualification.json` seals the final source checks and qualification result.
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

## What the current checks establish

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

These checks do not establish relative-ends execution, optimized interval
mutation, hot/cold owner throughput, fitting/encoding speed or shared bandwidth.
The [control scorecard](../controls/SCORECARD.md) owns performance evidence;
functional checks do not fill unmeasured cells.

## Earlier receipts

`build/frame-owner-core-v1/` retains the earlier 804-check basic history;
`build/frame-description-review/` and `build/frame-owner-material/` retain
the separate early description/material/validator checks.
`build/frame-owner-stream-v1/` retains the earlier multi-stream capture
(251 description, 142 manifest, 2,120 binding and 87,628 history checks), with
uninstrumented reused width kernels. Those snapshots predate the latest
facade/basis changes and do not replace the current matching runtime-leaf
receipt. Directory-wide commit `88cb8ee1` included unfinished owner files;
its seam measurements alone never qualified their implementation.
