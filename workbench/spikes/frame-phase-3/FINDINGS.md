# What the phase 3 prototypes found

24 October 2026. These test the proposal in the [README](README.md); the code
is in [proto/](proto/) and the retained summaries in
[proto/evidence/](proto/evidence/). Timings are warm, single core, medians of
three process repeats on Zen 5 (c8a.8xlarge, AVX-512 and AVX2) and Graviton 4
(c8g.8xlarge), over phase 1's ClickBench slices (11,718 blocks of 256 rows)
and two generated columns. "Native" is this thread's own u64 control, not
front A's qualified one. Builds pin `ikea/` and the bounds stage to HEAD
(`9e0cf2a8`) so other threads' uncommitted work is not measured.

## 1. Runtime-composed laws against phase 2's hand glue

[measure.cpp](proto/measure.cpp) runs `bound → pack` through the stage
contract ([frame.h](proto/frame.h), [stages.h](proto/stages.h)) against the
no-RLE path of phase 2's [pipelines.cpp](../frame-phase-2/runs/pipelines.cpp),
copied unchanged, over the same bounds-stage windows and SeriesPack arrays.
Dispatch is one virtual call per part and stage; equal-width blocks of a part
share one SeriesPack array, so a range kernel covers them in one call.

| frame ÷ glue, compact preset | Zen 5 AVX-512 | Graviton 4 |
| --- | --- | --- |
| SUM | 0.71–1.38 | 0.97–1.16 |
| COUNT(x ≥ median) | 1.03–1.88 | 1.05–1.82 |
| Points, through the tree | 0.57–1.71 | 0.88–3.14 |
| Points, fused plan | 0.53–1.35 | 0.43–0.91 |

CounterID (almost all constant blocks) is left out of the ranges: 2.0–2.6× on
SUM and COUNT and 2.8–7× on points, at 1–8 ns per block or point. [measure-v2-zen5.txt](proto/evidence/measure-v2-zen5.txt),
[measure-v2-graviton4.txt](proto/evidence/measure-v2-graviton4.txt); the
fused-plan row is from [measure-v3-zen5.txt](proto/evidence/measure-v3-zen5.txt)
and [measure-v3-graviton4.txt](proto/evidence/measure-v3-graviton4.txt), where
the plan calls the leaf's point kernel directly.

- **SUM composes for free.** Merging equal-width blocks into one range call
  makes frame faster than the glue's per-block call on ResolutionWidth,
  UserID and gen_walk (0.71–0.83× on Zen 5). The bound law is block
  arithmetic around SeriesPack's own kernel, as phase 2 predicted.
- **COUNT pays 3–8 ns per block of bookkeeping** where the operation is cheap:
  UserID and gen_ts settle most blocks from descriptors, so the separate
  classification pass and partial-set build show. Specialising the law per
  comparison took the worst case from 2.38× to 1.88×. A fused descriptor pass
  over the lo and width columns is the next lever; it is block-grain work, not
  a reason to fuse the tree.
- **Points are a critical path and need a fused plan.** Through the tree a
  point is three dependent virtual calls and several scattered descriptor
  loads; on Graviton 4 that costs up to 3.1× the glue. A plan built at bind
  time from the same tree (one flat entry per block: lo, leaf kernel, row
  base, exceptions, and the leaf's point kernel called directly;
  [trees.h](proto/trees.h) `point_plan`) is faster than the glue on every
  Graviton 4 column (15–31 ns against 20–55) and 0.53–1.35× the glue on
  Zen 5.
- **Against the u64 array**, `bound → pack` SUM is 1.9–17× faster on narrow
  columns and 2.6× slower on 64-bit URLHash, where widths above 55 bits read
  then sum. COUNT is 1.2× (OS) to 31× (CounterID) faster on narrow columns and
  0.74× on URLHash.

### The delta stage needs its laws before it lands

`intervals` (sizes form, checkpoints every 16 rows) has only a monotone COUNT
law; everything else uses the decode fallback.

- The monotone COUNT law works: 3–7 ns per block on UserID, CounterID and
  gen_ts, settled by checkpoints and one 16-row region.
- The fallback is correct but slow: SUM 130–218 ns per block on Zen 5
  against 4–165 for `bound → pack` (a first version paid an integer
  division per row and cost 450). Points cost 26–72 ns.
- Checkpoints stored as u64 every 16 rows cost 4 bits per value. Bytes only
  improve where deltas are much narrower than values (ClientIP 23 against 32,
  UserID 29 against 52, gen_ts 15 against 18 bits). Checkpoints need to be a
  framed series themselves, or sparser.

A second round ([measure-v4-zen5.txt](proto/evidence/measure-v4-zen5.txt))
tried two of the fixes:

- **Framed checkpoints pay.** Storing the checkpoints as their own
  `bound → pack` series takes OS from 13.5 to 10.1 bits per value, CounterID
  from 4.8 to 0.8, gen_ts from 14.8 to 12.1, and puts delta below
  `bound → pack` on six of eleven columns. Reading them through the child
  slows the monotone COUNT law (UserID 7.5 → 23 ns per block), so its
  checkpoint search wants a fused read.
- **A SUM law from masked sums loses.** Σx = n·c + Σ δⱼ(n − j) can be written
  as one masked sum of the deltas per bit of the in-region position, so it
  never decodes. It is exact, but 1.3–2.6× slower than decoding (166–559 ns
  per block), because SeriesPack's prefiltered SUM costs several times its
  range SUM (38–116 ns per block at 10% selection, against 4–165). Graviton 4
  agrees: 291–1,205 ns per block against 276–358 decoding
  ([measure-v4-graviton4.txt](proto/evidence/measure-v4-graviton4.txt)). The
  law is kept, switched off. SeriesPack subsequently added the one-decode
  position-weighted primitive in `0d44d527`. It returns a modulo-2^64 result;
  Frame's composed exact-wide weighted law and its admission premises are not
  integrated. The initial sizes form therefore retains its decoded SUM route.

A third round ([measure-v5-zen5.txt](proto/evidence/measure-v5-zen5.txt),
[measure-v5-graviton4.txt](proto/evidence/measure-v5-graviton4.txt)) adds
the **relative-ends form**: each row stores its offset from the region's
checkpoint instead of its delta from the previous row, so a point is one
add and SUM is n·c plus one child SUM, with no prefix pass.

| relative ends against sizes, G16 | Zen 5 | Graviton 4 |
| --- | --- | --- |
| SUM | 1.2–6.4× faster | 1.1–8.1× faster |
| point | 1.6–3.4× faster | 1.4–2.9× faster |
| COUNT ≥, read | 0.99–1.17× | 0.95–1.17× |

It costs bytes where offsets grow faster than deltas: ClientIP 21 → 32 bits
per value, UserID 29 → 48, RegionID 12 → 18, gen_ts 12 → 17, flat on the
other seven columns. Which form wins is a per-column fit choice the census
can report, not a fixed default. Even relative SUM (23–167 ns per block on Zen 5)
stays above `bound → pack` on most columns, and READ and COUNT still decode.

The remaining delta optimizations are the composed weighted SUM for sizes and
a fused checkpoint-plus-prefix read. They are deferred beyond the initial
correct decoded fallback; its bytes are now competitive. The runs
thread found the same short-read cost from the other side
([runs §3](../frame-phase-2/runs/README.md#findings)).

### SUM over a selection

SUM over selected rows is now an operation: bound charges lo only for
selected non-exception rows, and pack uses SeriesPack's prefiltered SUM. On
Zen 5 a random 10% selection costs 38–116 ns per block and FILTER(x ≥
median) then SUM 20–152, against 61 for this thread's scalar fused loop over
u64. That control is weak (it does not vectorise the masked case), so these
numbers only place the operation; front A's fused pair control is the right
bar, and the prefiltered SUM is the cost to attack.

## 2. Every placeholder shape fits one contract, with seven changes

[placeholders.h](proto/placeholders.h) builds `runs` (1 → 2, fewer rows),
`split` (1 → 2, head-first filter) and a TuplePack `record` leaf shared by two
columns, through the same contract. [check.cpp](proto/check.cpp) checks all
eleven trees and the shared record against a plain array: SUM, COUNT and
FILTER under every comparison at boundary thresholds, every point, whole
reads and selections at three densities, on nine generated columns with a
partial last part and block. All are exact
([check-v3-zen5.txt](proto/evidence/check-v3-zen5.txt)), after the check found
two leaf bugs: partial-block storage, and range SUM above 55 bits over a
partial block.

The shapes forced these contract changes, all now in [frame.h](proto/frame.h):

1. **Each series has its own geometry.** Run sizes have fewer rows than their
   parent, so rows per part belong to each stage, and a part's last block may
   be partial.
2. **Geometry-changing stages address children by row range.** `runs` reads
   its children's ranges; block selections only pass down where a child keeps
   its parent's blocks.
3. **Multi-child laws combine selections.** `split` filters as
   `head > t_h | (head = t_h & suffix ≥ t_l)`, reading the suffix only where
   the head ties.
4. **A shared leaf builds by deposit and seal.** Each column's tree deposits
   its field; the record seals when every field has arrived.
5. **A leaf states what it admits.** The record admits widths up to 8 bits;
   it says so through the width price, and bound fits inside it.
6. **Priors come from descriptors, not hidden state.** Bound's prior is the
   previous block's window, so a group rebuild fits exactly as the build did
   and replay is deterministic.
7. **Capabilities are asked before anything changes.** A refused rebuild left
   bound refitted over an unchanged leaf; `rebuilds_groups()` is now asked
   first. This is Ikea's checked-call rule applied to stages.

One gap remains open: a stage with several children must compose the width
price bound asks for (`split` returns none, so bound fits it as if widths
were free). The answers are still exact; only the fit is uninformed.

## 3. Adding a stage or operation, and the compile bill

Each placeholder is 60–150 lines in one file, with no edit to another stage or
operation. `select` was added once with a reference definition and one law
(bound's). That is the extension property the proposal claims.

The compile bill is the leaf, not the composition. Stage and law TUs compile
in about 5 s. The width-specialised SeriesPack arrays (16 widths of one preset
per TU, 13 operations each) take about 150 s and 4.1 GB each locally, and peak
at 5.0–7.1 GB on the workers, where a 10-job build of all twelve TUs (three
presets) takes 2–5 minutes ([build-v2.txt](proto/evidence/build-v2.txt)). The
8 GB OrbStack VM cannot build them in parallel at all.

SeriesPack already compiles a runtime-width read once
(`src/seriespack/readers.cpp`), but its erased reader offers only `get`,
`read` and `read16`. If it also offered the whole range operations (SUM,
COUNT and filter words per comparison, the checked setter, the point
kernel), Frame would instantiate no width-specialised code at all. This is a
proposal for the SeriesPack owner, not done here.

### SeriesPack's compiled-once operations, adopted

The SeriesPack owner landed runtime-width range operations (ikea 0d44d527).
[runtime_width.h](proto/runtime_width.h) puts the pack leaf on them for every
width and preset, behind the same `width_array` seam
(`-DFRAME3_LEAF=runtime`). The full check passes on it, including the stream
round trip, on both hosts and on the 8 GB local VM, which could not build the
typed width TUs at all. Against the typed leaf in the same run
([leaf-v1-zen5.jsonl](proto/evidence/leaf-v1-zen5.jsonl),
[leaf-v1-graviton4.jsonl](proto/evidence/leaf-v1-graviton4.jsonl)), SUM,
COUNT, FILTER and points through `bound → pack`, the point plan and
`intervals → bound → pack` cost 0.79–1.17× (median 0.98–1.03) on Zen 5 and
0.94–1.04× on Graviton 4. A whole worker build takes half as long (1:34 vs
3:09 on Zen 5, 3:05 vs 6:12 on Graviton 4) at less peak memory. The runtime
leaf is the landing's leaf; the typed TUs stay only as a control.

## 4. Writes: exception slack, delta buffer or re-encode

[writes.cpp](proto/writes.cpp) applies writes through [change.h](proto/change.h)
on 524,288-row columns: 16 hot rows rewritten, one write per block, and one
out-of-window write per block. Policies: **slack32**, write-time outliers in
bound's exception areas with 32 spare slots per group; **buffer64**, a group
delta buffer of logical overrides with a bitmap of non-pristine blocks;
**reencode**, re-encode the group; **reencode_part**, the whole-part fallback.
[writes-v2-zen5.txt](proto/evidence/writes-v2-zen5.txt),
[writes-v2-graviton4.txt](proto/evidence/writes-v2-graviton4.txt).

| Zen 5 | slack32 | buffer64 | reencode (group) | reencode_part |
| --- | --- | --- | --- | --- |
| Extra bytes, OS | +0.56 bits/value | +4.0 bits/value | 0 | 0 |
| Hot-row write | 123–232 ns | 71–148 ns | 136–446 ns, some re-encodes | 0.4–8.8 µs, with part rebuilds |
| Outlier write | 194–394 ns | 230–395 ns | 7.9–19 µs | 122–301 µs |
| SUM after one outlier per block, OS | +4% | +29% | +7% | +6% |
| COUNT after, UserID | +50% | 4.1× | 1.9× | 1.8× |
| COUNT after, gen_narrow | +10% | +68% | +10% | +14% |

Graviton 4 is about twice as slow per write with the same order; its UserID
COUNT rises 3.8× under the buffer.

- **Every applied write's stores were declared before payload was read**: 0
  undeclared bytes over 72 cases per host, issued stores resolved through
  each source's `plane_storage`. SUM contributions summed to the final SUM
  exactly, block statistics never under-covered a block, and replaying the
  same writes on a second copy gave identical byte digests.
- **Slack wins the first landing.** It costs a seventh of the buffer's bytes
  and much less read tax, and outlier writes cost about the same. The buffer
  only wins hot-row rewrites (71–148 against 123–232 ns), because an override
  updates one entry while slack stores through SeriesPack's setter.
- **Group re-encode is the right fallback, the part is not.** A group fold
  costs about 8 µs; with part-wide placement each outlier rebuilds its part,
  at 122–301 µs. Group-local placement costs scans 1.2–1.6×
  (Zen 5) and 1.1–1.4× (Graviton 4), because each group calls the leaf once
  per width. That is a scan/write weight Engine sets, or a leaf call that
  spans groups.

## 5. Describing costs to Engine

[calibrate.cpp](proto/calibrate.cpp) times each preset on synthetic columns
of one width and exception count, then weights those tables by each real
column's census (blocks per width and exception bucket, a by-product of
fitting) and the share of blocks a predicate leaves partial.
[calibrate-v2-zen5.txt](proto/evidence/calibrate-v2-zen5.txt).

- **Absolute predictions are off by a median 34–83%**, mostly low. The
  calibration columns are small (cache resident) and homogeneous; real columns
  mix widths, so leaf calls are shorter, and they do not fit in cache.
  Measured times of the same operation also differed 2× between two harnesses
  on one host, depending on what ran before. Tables need residence tiers and a
  per-call term.
- **Preset ranking is right in 18–21 of 24 cells, but the test is too easy**:
  compact, bulk and grouped differ by at most about 12%. The census is the
  right shape of description; whether it decides consequential choices (delta
  or not, exception penalty, RLE) is still to be shown.

## 6. End to end through Orbital

[e2e.cpp](proto/e2e.cpp) and [orbital_frame.h](proto/orbital_frame.h) make
each column part an Orbital Object whose Representation has one Part per
group image ([frame.h](proto/frame.h) `emit`/`attach`: headless, descriptors
and exceptions in the image, SeriesPack arrays borrowed in place). Four parts
load in one packet; 168 updates (in-window values, outliers across groups,
and a burst that exhausts one group's slack and re-encodes it) each publish a
new version. [e2e-v2-zen5.jsonl](proto/evidence/e2e-v2-zen5.jsonl).

| Zen 5, four 65,536-row parts | OS | UserID | ClientIP | EventTime |
| --- | --- | --- | --- | --- |
| Load and publish | 1.3 ms | 1.7 ms | 1.2 ms | 1.1 ms |
| Attach a version | 40 µs | 37 µs | 26 µs | 26 µs |
| UPDATE: draft, apply, emit | 12.0 µs | 17.3 µs | 9.5 µs | 8.6 µs |
| UPDATE: capture and publish | 1.0 µs | 1.3 µs | 1.0 µs | 1.2 µs |
| Bytes published per write | 4.6 KB | 26 KB | 16 KB | 9 KB |

- A Scope taken before the first update still reads version 1 exactly after
  168 publications; a Scope from the middle reads its version; every version
  answers SUM, COUNT, points and FILTER exactly; SUM contributions add up.
- Each write publishes one fresh Part and reuses the other 15 by address
  (2,520 reused, 168 fresh), with no copy of untouched groups.
- Reads through attached Parts cost the same as the in-memory tree.
- The apply cost is mostly attaching all sixteen groups for one write; a
  group-local draft would remove it.

Graviton 4 passes the same checks at about 1.5–2× these times
([e2e-v2-graviton4.jsonl](proto/evidence/e2e-v2-graviton4.jsonl)). This is a
single-owner embedded run: no durable admission, concurrency, Loom
leases or Engine summaries. It shows the Block and Change ideas meet
Orbital's Object → Representation → Part model without an adapter layer.

## 7. Three seam cases on the same images

Ikea LEAD's narrowing of the seam question (CHECKLIST B1), run by
[seams.cpp](proto/seams.cpp) with front A's narrow native controls
(`frame_cells_controls`); every timed answer checked. One process, 7 rounds.
The account below follows the latest rows,
[seams-v3-zen5.jsonl](proto/evidence/seams-v3-zen5.jsonl) and
[seams-v3-graviton4.jsonl](proto/evidence/seams-v3-graviton4.jsonl), whose
controls front A has audited ([controls/SCORECARD.md](controls/SCORECARD.md)).
Ratios are throughput per block or per request, not latency.

**COUNT settled from descriptors.** A uniform-threshold law settles blocks
in one branch-free pass over bound's lo, width and count columns and scans
exception areas once per group. Against front A's count-only control
(`native_count_once`), ns per block:

| | UserID | CounterID | OS | EventTime |
| --- | --- | --- | --- | --- |
| Zen 5, per-block law | 4.03 | 1.01 | 17.31 | 13.65 |
| Zen 5, descriptor pass | 1.07 | 0.48 | 8.96 | 12.91 |
| Zen 5, native count | 26.14 | 6.08 | 5.98 | 8.49 |
| Graviton 4, per-block law | 6.29 | 1.90 | 46.57 | 36.57 |
| Graviton 4, descriptor pass | 6.02 | 2.72 | 39.66 | 36.00 |
| Graviton 4, native count | 85.56 | 57.37 | 58.78 | 54.19 |

- The descriptor pass halves the per-block law on Zen 5 where blocks settle
  from descriptors (UserID, CounterID, OS) and is level on Graviton 4
  (slower on CounterID). It is a per-ISA choice, not a free win.
- Frame wins where descriptors settle most blocks (UserID, CounterID; 6–24×
  on Graviton 4). On Zen 5 it loses to the native count on OS and EventTime
  by 1.50× and 1.52×, where most blocks straddle the threshold and need their
  payload.

**Predicate on A, then work on B** (top 10% of A; CounterID's ties select
31%), SUM of B over UserID, ClientIP and RegionID, ns per block:

| | clustered (CounterID, 31% of blocks) | partly clustered (EventTime, 74%) | dispersed (WatchID, all blocks) |
| --- | --- | --- | --- |
| Zen 5, fused per part | 10–17 | 63–114 | 130–167 |
| Zen 5, retained mask | 15–25 | 82–163 | 150–178 |
| Zen 5, front A's fused native pair | 80–141 | 75–149 | 96–177 |
| Graviton 4, fused per part | 21–51 | 113–186 | 311–434 |
| Graviton 4, retained mask | 28–59 | 120–196 | 316–446 |
| Graviton 4, front A's fused native pair | 51–114 | 62–114 | 106–129 |

- Fusing per part beats retaining a column-wide mask in every cell: by
  6–49% on Zen 5 and 0–34% on Graviton 4.
- Coarse skipping is the win: 4.9–8.5× faster than the native pair on the
  clustered predicate on Zen 5 and 1.9–2.4× on Graviton 4. Where most blocks
  are touched the native pair wins: on Zen 5 by up to 1.61× (WatchID), on
  Graviton 4 by 1.6–2.0× (EventTime) and 2.9–3.4× (WatchID). The cost is
  SeriesPack's prefiltered SUM (§1).
- SUM with MAX: see the one-pass result after the points case.

**Independent points past the LLC** (each column replicated 8×, 24 M rows,
65,536 random requests). "Swept" means after a 2 GiB eviction sweep: an
eviction attempt, not proof that each request is served from DRAM. Numbers
are ns per request (throughput), on OS, ClientIP and UserID:

| ns per request, swept | Zen 5 | Graviton 4 |
| --- | --- | --- |
| u64 array | 4.4–4.6 | 9.1–9.2 |
| front A's narrow gather (materialises every value) | 5.1–6.6 | 7.5–9.4 |
| ready plan | 38–56 | 39–72 |
| plan, batches of 32 prefetched | 23–42 | 29–50 |
| plan, software pipeline of 16 (`get_many`) | 23–42 | 23–39 |
| plan through a Loom Ring of 32 | 31–59 | 40–60 |

- The pipelined plan is the best Frame form, 3.8–6.4× behind the narrow
  gather on Zen 5 and 2.6–5.2× on Graviton 4, though Frame's images are
  2–7× smaller than u64. The gather's independent loads overlap; a Frame
  point chains entry, an indirect leaf call, the body tile and the
  exception positions.
- Staging helps (the pipeline is 1.3–1.9× the ready plan); a 32-slot Ring
  adds callback overhead and recovers less than plain batching.
- Smaller entries do not close it: packing the plan entry into 24 bytes
  left the pipelined plan where it was in the previous run
  ([seams-v2-zen5.jsonl](proto/evidence/seams-v2-zen5.jsonl): 25–41 ns on
  Zen 5). The entry is still a separate line from its body, so co-locating
  them is the next experiment. How much of the gap is irreducible is not
  established.
- Unswept ("warm") rows are not comparable: at 24 M rows the arms evict
  each other and the same arm moves 3–4× between runs.

**SUM with MAX in one pass.** `summarise_selected` returns sum, count, min
and max from one pass over the selected rows, instead of a SUM law and then
a `select` for MAX. On Graviton 4 it is 1.8–2.1× faster than two passes when
the predicate is clustered (133–166 against 279–308 ns per block, level with
the native scalar control at 156–159), and 1.0–1.2× elsewhere. On Zen 5 it
is mixed (0.89–1.15×): Zen 5's `select` is already fast. MAX still has no
law of its own; one would skip blocks whose bound (lo + 2^width − 1, or the
exception area's maximum) cannot beat the running maximum, which is B6's
remaining step.

## What changes in the proposal

- **Execution is two levels**: block-grain laws composed at runtime (no
  instantiation per tree) over leaf kernels compiled once in Ikea. Critical
  paths get plans built at bind time from the same tree: points now, the
  delta prefix and head-first filters next.
- **Writes have a fallback too**: a stage without a write law refuses, and
  the owner re-encodes the group. Every stage supports writes from day one.
- **Write-time outliers go to bound's exception areas with per-group slack**
  in the first landing; the delta buffer stays a research alternative.
- **The group image is the publication unit**: one Orbital Part per group,
  descriptors inside it.
- **Delta lands with the laws that won**: relative ends' SUM and points, the
  monotone COUNT, framed checkpoints; the sizes form keeps decode for SUM
  ([README landing plan](README.md#landing-plan)).
- **SeriesPack is asked for erased range operations**, so Frame compiles no
  width-specialised kernels.
