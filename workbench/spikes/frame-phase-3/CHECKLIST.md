# Before the initial implementation: what to explore or decide

Draft, 2026-10-24, for Ashton. Each item says why it matters, what would
settle it, and where it stands. "Decided" means Ashton decided; "evidence"
means a prototype answered it, with the caveat noted; "open" means nothing
settles it yet. Ikea LEAD's reply will be folded in when it arrives.

## Composition and execution (ideas 1 and 2)

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| C1 | Seam comparison on one stage stack: virtual calls per part, whole-tree template inlining, CPS (`ikea::author::chain`), bind-time plans, Loom staging/prefetch and rings | The seam decides speed, compile cost and how research slots in; only two seams were measured on Frame stages | Points, SUM WHERE and COUNT through each seam, warm and flushed, Zen 5 and Graviton 4, plus compile time | Open; earlier spikes measured each in isolation |
| C2 | When a cell earns a bind-time plan | Plans are the "critical paths hand-fused"; too many re-create a fixed pipeline | A rule from C1: which operations and shapes lose more than a threshold through laws | Points: evidence (plan beats hand glue on Graviton 4) |
| C3 | A standard pipeline description | Engine must name a pipeline, the schema must store it, Frame must rebuild the same tree from it | A headless encoded description (Ikea's envelope) round-tripping every prototype tree, placeholders included | Open (only C++ constructors today) |
| C4 | Operation vocabulary for the first landing | Fixes what laws exist and what Engine can ask | List with reference semantics: get, read, select, filter, count, sum, sum over a selection, min, max; which comparisons; NULL behaviour | Partial: all but min/max prototyped |
| C5 | COUNT and FILTER bookkeeping | 3–8 ns per block over the hand glue where blocks settle from descriptors | A fused descriptor pass over lo and width columns, timed | Open |
| C6 | Leaf kernels compiled once in Ikea | Each 16-width TU costs 4–7 GB to compile in Frame | SeriesPack erased range operations | Decided: yes; request routed to the SeriesPack owner |
| C7 | Multi-consumer scans (one decode, several results) | Phase 2's `scan_regions` gave 1.4–3.1×; laws currently run one operation per pass | A law form for several aggregates in one pass, timed against separate calls | Open |
| C8 | Predicate on A then work on B across columns | The cold-scan thesis and SUM WHERE; selection is not read authority | Pair through two trees with coarse skipping and fine masks, against front A's fused pair control | Open (single-column only) |

## Stages (idea 1)

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| S1 | Delta (intervals) laws: fused weighted SUM, fused checkpoint-plus-prefix point read, monotone filter | Delta on the fallback is 5–6× slower than bound → pack | SeriesPack weighted consumer in `scan_regions`; a point plan for delta; timings | Partial: framed checkpoints pay; masked-sum SUM law loses |
| S2 | Relative-ends form and G16 vs G64 for value series | Ashton named both forms; phase 2 found relative ends widen at G64 | Bytes and laws for both forms on time-series and key columns | Open |
| S3 | Signed values and offset binary | Delta needs it; bound must keep windows contiguous | Domain stage or fused into intervals; checks on signed columns | Partial: offset binary inside intervals |
| S4 | Bound's admitted set and Engine's weights (penalty, cap, slack) | These are the choices Engine tunes | Defaults per preset with evidence; exposure in the description | Partial: slack decided; penalty/cap open |
| S5 | Width 0 and constant blocks | 41% of ClickBench blocks; CounterID shows 2–7× overhead per block | A constant fast path in laws and plans | Open |
| S6 | Placeholder shapes the contract must admit beyond runs, split and record: map (dictionary with part state), presence (rank map), route (n children), domain (ALP with exceptions) | Shapes not tried may force more contract changes | Fit/emit/decode for each through the contract | Partial: three of seven tried |

## Geometry and publication (idea 3)

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| G1 | Part-wide versus group-local width arrays | Group-local costs scans 1.1–1.6×; part-wide makes every outlier rebuild a part | A leaf call spanning groups, or a scan/write weight Engine sets | Open |
| G2 | Group image layout as an Ikea byte law | Headless images are what Orbital stores and replicas exchange | An encoded, versioned law per stage section; validate() over contents | Prototyped informally |
| G3 | Attach cost and group-local drafts | 26–40 µs per version attach; most of an UPDATE's 9–17 µs | Attach only touched groups; lazy attach | Open |
| G4 | Group and block sizes (16 × 256) | Fixed early, expensive to change | Sweep 8/16/32 blocks per group for scans, writes and publication | Open |
| G5 | Row protection and access masks as coarse selections | Ashton listed row-level protection as Frame's | Owner-supplied masks applied before address formation, checked | Open |

## Change and MVCC (idea 4)

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| M1 | Reserved capacity for point writes outside bounds | Writes must not force re-encode | Per-group exception slack, size per preset | Decided |
| M2 | Range writes, appends to a plain tail, seal | Bulk ingest and streaming | Writer commands with envelopes and contributions | Open |
| M3 | Envelope before payload, metadata-only classification | Locks project onto physical envelopes; payload-dependent steps need admitted reads first | Contract split: metadata classify, admitted reads, then store envelope (Ikea LEAD's point) | Partial: metadata classify done |
| M4 | Summaries published with data: SUM, COUNT, MIN/MAX precision | Engine summaries and indexes need exact contributions | Contribution law per stage, checked over all workloads | Partial: SUM exact, stats precision |
| M5 | Re-encode as conversion (Orbital `adopt`) versus a new version | Compaction must not change logical time | An adopt path for compaction-only re-encodes | Open |
| M6 | Concurrency: two writers, one group | Real workloads | Admission over overlapping envelopes, conflict behaviour | Open |
| M7 | Replay from admitted input | Recovery basis | Deterministic fit and write paths; replay checks | Evidence (digest-equal replay) |

## Description and Engine (idea 5)

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| D1 | Cost tables per residence tier with a per-call term | Absolute predictions were off by a median 34–83% | Tier-calibrated tables predicting within a set error | Open |
| D2 | A consequential decision Engine can make from the description | Ashton's bar: analysis that makes impactful decisions | Predict delta-or-not, penalty and placement per column and workload, check against measurement | Open (the tested choice was too easy) |
| D3 | Census contents and cost | The cheapest planning input | Which statistics, how they update under writes | Partial |

## Baselines and the brief's bar

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| B1 | Polars and DuckDB timed on the landing operations | The brief's ≥ 2× bar is still untested | Front A's controls timed at each tier | Open (functional controls exist) |
| B2 | Front A's native controls replace this thread's | Own controls are weak (scalar masked sum) | Rerun with `frame_cells_controls` | Open |
| B3 | Cold and shared-bandwidth tiers | Wins on bytes show only there | Flushed and concurrent-stream runs | Open |

## End to end

| # | Item | Why it matters | What settles it | Status |
| --- | --- | --- | --- | --- |
| E1 | Orbital material and update path | Basic DB operations end to end | Prototype passes with older Scopes live | Evidence (embedded, single owner) |
| E2 | Loom leases and staging around reads | Real readers go through Loom | A Loom-driven read and update | Open |
| E3 | An Engine-side table driver over several columns | "Basic database operations through the whole database" | Load, SELECT SUM(b) WHERE a ≥ t, point lookup, UPDATE, across columns | Open |
