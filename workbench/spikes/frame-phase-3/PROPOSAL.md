# Ikea Frame: consolidated proposal for the first landing

25 October 2026, for Ashton. Joint proposal by the stage side (Claude) and Ikea LEAD
(Codex). Evidence is in
[FINDINGS.md](FINDINGS.md), decisions and open items in
[CHECKLIST.md](CHECKLIST.md), and the history and full argument in the
[README](README.md).

Engine research will settle much of Frame's eventual interface, and that
research needs a Frame to work on. So the first landing is a foundation:
**strong** where the evidence is good, **steerable** where we lack
confidence, and **extensible** where we know extensions are coming.

## What lands in Ikea

A column of non-null u64 values. Initial defaults use 65,536-row binding units,
16-block groups and 256-row fitting blocks. Child streams have explicit row
domains and placement; an encoding group, image section, allocation and Orbital
Part need not coincide. The supported recipes compose these optional stages:

- **`intervals`**: checkpoints every G rows (16 or 64), stored as a framed
  series of their own, and the rows between them as sizes (deltas) or
  relative ends (offsets from the checkpoint).
- **`bound`**: a per-block window [lo, lo + 2^width) with outliers in
  per-group exception areas that keep spare slots for later writes.
- **`pack`**: SeriesPack's packed fields, through its compiled-once
  operations.

Reads: get, read, select, filter, count, sum (whole and selected), and SUM,
COUNT, MIN and MAX together in one pass, with checked wrappers reconciled at
extraction. The first owner change route is a private group afterimage for
basic pack/bound recipes: ordered edits, slack and admitted group rebuild.
Interval changes, append and conversion currently use caller-driven rebuilds.
Publication replaces completed images; private apply-journal coverage and logical
contributions/final facts are distinct outputs.

### Strong (we are confident)

| Area | What is guaranteed | Evidence |
| --- | --- | --- |
| Meaning | Supported cells are exact; laws are optional fast rewrites. Unsupported images refuse | Original-value oracles, admitted trees and both interval forms |
| Composition | Runtime block-grain laws; per-part fusion and reusable selections | Fusion beats retained masks in every measured seam cell |
| Leaf | SeriesPack compiles its kernels once; Frame compiles none | Runtime leaf at timing parity, build bill halved |
| Changes | Refusal preserves source/public state; reads are granted, stores declared before apply, contributions exact | Ordered/restored writes, exhaustion, stale retry and old readers through Orbital |
| Skipping | Clustered predicates settle whole blocks from descriptors | 1.9–8.5× faster than front A's native pair |
| Interpretation and binding | Versioned meaning and explicit child domains; separate placement, contents and authority. Admit complete demand before output; retain immutable interpretation/backing | Canonical bytes, malformed/guarded images, sparse dependencies, stale-basis/teardown/conversion checks; unchanged decode can still hide a wrong SUM |

### Steerable (Engine chooses; defaults may change)

- The recipe per column: which stages, interval form and spacing, SeriesPack
  preset, bound's penalty, cap and slack. Relative ends trade bytes for SUM
  and point speed (SUM 1.1–8× faster, but UserID 29 → 48 bits per value), so
  no single default is right.
- Whether to use Frame at all. Where no recipe repays (URLHash), Engine keeps
  a native column; `pack` alone is not that choice.
- Execution plans by ISA and workload: the descriptor COUNT pass helps Zen 5;
  Graviton 4 is mixed and CounterID loses. Retaining masks can repay across
  several consumers even when one fused query is faster.
- Costs: return fit facts and capabilities; distinguish measured, estimated
  and unknown costs. Engine can probe alternatives; no complete model is required.
- Placement/preparation: Engine supplies budgets and protection, may retain
  masks/plans, and receives dependency closure. Policy/defaults stay separate
  from stored meaning. Prototype geometry/parser limits are not permanent
  whole-column restrictions.

### Extensible (known extensions)

- Stage/child domains and shared state, with described/refused RLE, head,
  TuplePack and presence/rank shapes.
- New operations with optional laws, and bind-time plans for critical paths.
- Separate change capabilities, interpretation versions and evidence. Initial
  afterimages do not impose fixed ownership or silently grant future in-place
  or asynchronous modes their authority/lifetime contracts.

## Measured weaknesses and initial limits

These are the qualified spike results, before the queued reuse changes:

| Work | Current evidence | Implication |
| --- | --- | --- |
| Independent points | 2.6–6.4× slower than native gather after an eviction sweep | Extra dependent acquisition remains exposed; no exact DRAM-line count or irreducible floor is proved |
| Dispersed predicate → SUM | Up to 1.6× slower on Zen 5, 2.9–3.4× on Graviton 4 | Selected fraction is insufficient; touched-block topology matters |
| COUNT | OS/EventTime lose 1.5× on Zen 5 | Descriptor pruning is valuable, not universal |
| Wide SUM/dispersed work | Roughly 2–4× slower than typed native in the local six-cell panel | Exact-wide leaf improvements target part of the gap; recheck changed cells. Wins over DuckDB/Polars include larger query/result boundaries |
| Checked private preparation | 21–26 µs for one edit, 57–71 µs for 64 edits locally; group-only saves 9–12 µs/batch versus eager attachment | Identical images, but independent drafts excluding publication/disposal; competitive complete native-owner writes are unmeasured |

Memory is conditional too: URLHash's G16 image is 528,864 B against 524,288 B
native payload, OS 74,721 B against 65,536 B. Neither account is full retained heap.
The sizes form currently decodes SUM; the module work below targets that route.

The initial domain is non-null u64. Fine-row read authority, retained asynchronous
preparation and general in-place mutation are not established. Transaction/lock
ownership and recovery remain with their existing modules.

[Scorecard](controls/SCORECARD.md) and [owner evidence](owner/EVIDENCE.md)
record source/operation boundaries. Native comparisons cover Zen 5 AVX-512 and
Graviton 4 NEON; newer owner conformance/sanitizers and actual-system cells are
local AArch64. Repeated execution and a 2 GiB eviction sweep prove no cache tier.
Module changes inherit no old timing or qualification automatically.

## Resolve before landing

1. **Submodule reuse** ([REUSE-AUDIT.md](REUSE-AUDIT.md)):
   - Intervals' searches are routed through `ikea::intervals`.
   - Intervals gains a source over decoded wide fields, so delta
     reconstruction goes through it too.
   - SeriesPack gains an exact weighted SUM (periods 16 and 64) and an
     exact wide SUM, replacing Frame's masked law and its decode above
     55-bit widths.
   - `bitset256` gains its iteration and intersection helpers.
   - Physical H8/H16 planes use SeriesPack heads. A fixed logical split is a
     different child mapping and must not be aliased to that packing option.
   - SeriesPack supplies runtime geometry/required-plane extents, replacing the
     owner validator's duplicate tile-size formula.
2. **Extraction**: one implementation behind Ikea's lifecycle; the prototype
   driver stays internal. Frame owns canonical composition/image validation and
   checked operations. Attachment, validation and binding keep Ikea's existing
   meanings; selections reuse its types with explicit block/row coordinates.
   Orbital material/history stays an integration example and conformance test,
   keeping owner retention/publication outside Ikea. Preserve old-reader meaning,
   child domains and conversion equivalence; no final Engine interface is required.
3. **Explicit capabilities and defaults**: which cells have laws, which
   operations each stage supports, what refuses.
4. **Qualification** of the combined implementation on the workers, against
   front A's controls.

## Stays in research

- Engine's choice of recipe judged by regret (B11), with a real native
  candidate and a workload reversal: the first Engine-side work on the core.
- Points: entries stored with their bodies.
- Executable RLE, TuplePack-shared records, logical split, map, routing and
  domain stages; Frame image placement/filtering for SeriesPack head planes.
- Scans across groups through one prepared operation (B10).
- Bound's MAX law is built and checked but not timed, so it lands only as an
  internal law.
- A strong adaptive-native owner comparison, reusable/right-sized preparation
  scratch and reuse of an immutable source's checked proof. Both current
  preparation paths request 128 KiB of journal scratch; its cost has not been
  isolated. These are follow-ups, not another pre-landing performance campaign.
