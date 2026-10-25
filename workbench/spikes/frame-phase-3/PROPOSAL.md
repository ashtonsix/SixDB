# Ikea Frame: consolidated proposal for the first landing

25 October 2026, for Ashton. Written by the stage side (Claude) and Ikea LEAD
(Codex); sections marked **OWNER** are Ikea LEAD's. Evidence is in
[FINDINGS.md](FINDINGS.md), decisions and open items in
[CHECKLIST.md](CHECKLIST.md), and the history and full argument in the
[README](README.md).

Engine research will settle much of Frame's eventual interface, and that
research needs a Frame to work on. So the first landing is a foundation:
**strong** where the evidence is good, **steerable** where we lack
confidence, and **extensible** where we know extensions are coming.

## What lands in Ikea

A column of non-null u64 values, stored as parts (65,536 rows) of groups
(16 blocks) of blocks (256 rows), each group one headless image. A column's
recipe is a tree of three stages:

- **`intervals`**: checkpoints every G rows (16 or 64), stored as a framed
  series of their own, and the rows between them as sizes (deltas) or
  relative ends (offsets from the checkpoint).
- **`bound`**: a per-block window [lo, lo + 2^width) with outliers in
  per-group exception areas that keep spare slots for later writes.
- **`pack`**: SeriesPack's packed fields, through its compiled-once
  operations.

Reads: get, read, select, filter, count, sum (whole and selected), and SUM,
COUNT, MIN and MAX together in one pass. Writes: set through bound's slack,
with group re-encode when a stage refuses.

### Strong (we are confident)

| Area | What is guaranteed | Evidence |
| --- | --- | --- |
| Meaning | Every stage × operation cell answers exactly; a fast law is an optional rewrite of the decode fallback | Oracle checks on nine generated columns and ClickBench slices, every tree, both interval forms |
| Composition | Laws compose at block grain at runtime, fused per part; no retained column-wide mask | Runtime SUM within 0.7–1.4× of phase 2's hand glue; per-part fusion beats the retained mask in every seam cell |
| Leaf | SeriesPack compiles its kernels once; Frame compiles none | Runtime leaf at timing parity, build bill halved |
| Writes | Out-of-window point writes take reserved slack; every store declared before payload; contributions exact | Writes study; Orbital end to end, 15 of 16 Parts reused per write |
| Skipping | Clustered predicates settle whole blocks from descriptors | 1.9–8.5× faster than front A's native pair |
| Bytes, images, validation, authority, lifetime | **OWNER** | **OWNER** |

### Steerable (Engine chooses; defaults may change)

- The recipe per column: which stages, interval form and spacing, SeriesPack
  preset, bound's penalty, cap and slack. Relative ends trade bytes for SUM
  and point speed (SUM 1.1–8× faster, but UserID 29 → 48 bits per value), so
  no single default is right.
- Whether to use Frame at all. Where no recipe repays (URLHash), Engine keeps
  a native column; `pack` alone is not that choice.
- Execution choices that are per ISA: the descriptor COUNT pass wins on
  Zen 5 and not on Graviton 4.
- Costs: the census reports fit statistics and capabilities; costs that are
  unknown stay unknown rather than zero. Calibration missed by 34–83%, so it
  steers rather than decides.
- **OWNER**: placement, preparation and binding choices.

### Extensible (known extensions)

- New stages: a stage declares its children, their row domains and any
  shared state. RLE, H8/H16 heads, shared TuplePack records and presence or
  rank maps have described shapes and refuse execution until they are built.
- New operations: a law is optional, so adding one never breaks a stage
  without it.
- New bind-time plans for critical paths (points have one).
- **OWNER**: change capabilities, interpretation versions, evidence.

## Weaknesses we land with

- Points are 2.6–6.4× behind front A's narrow gather: a Frame point chains
  two or three random lines against one.
- Dispersed predicates lose to the native pair: up to 1.6× on Zen 5 and
  2.9–3.4× on Graviton 4 when every block is touched.
- Descriptor COUNT loses 1.5× to the native count on OS and EventTime on Zen 5.
- The sizes form's SUM decodes until SeriesPack's exact weighted sum lands.
- The u64 domain only: no NULL, signed logical types or other domains yet.
- **OWNER**: no lock manager, durability, fine-row read authority or
  asynchronous lease protocol.

## Resolve before landing

1. **Submodule reuse** ([REUSE-AUDIT.md](REUSE-AUDIT.md)):
   - Intervals' searches are routed through `ikea::intervals`.
   - Intervals gains a source over decoded wide fields, so delta
     reconstruction goes through it too.
   - SeriesPack gains an exact weighted SUM (periods 16 and 64) and an
     exact wide SUM, replacing Frame's masked law and its decode above
     55-bit widths.
   - `bitset256` gains its iteration and intersection helpers.
   - The `split` placeholder is retired in favour of SeriesPack heads.
2. **Extraction**: one implementation behind Ikea's lifecycle, with the
   prototype's virtual driver kept internal. **OWNER**: the public
   description, validation and checked binding, taking Ikea's selection types.
3. **Explicit capabilities and defaults**: which cells have laws, which
   operations each stage supports, what refuses.
4. **Qualification** of the combined implementation on the workers, against
   front A's controls.

## Stays in research

- Engine's choice of recipe judged by regret (B11), with a real native
  candidate and a workload reversal: the first Engine-side work on the core.
- Points: entries stored with their bodies.
- RLE, head separation, TuplePack-shared records, map, routing and domain
  stages as executable stages.
- Scans across groups through one prepared operation (B10).
- Bound's MAX law is built and checked but not timed, so it lands only as an
  internal law.
- **OWNER**: native writers, scratch sizing, source-proof reuse.
