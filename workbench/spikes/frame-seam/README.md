# Frame stage seam and cross-block laws

Spike branch `spike/frame-seam`, from 7fa3ad56 (the carried-region landing).
The question: can `author::stage` carry request shape (spans, selectors,
uniform predicates, contributions in another row domain) so that laws cross
block and stream boundaries, while Intervals, SeriesPack and bitset256 own
the mechanisms? The motivating write-up is
[this artifact](https://claude.ai/artifact/Y4FeKdpkTvwXk5edC6X6sA), mirrored
locally in `build/frame-collab/seam/CLAUDE-STAGE-SEAM-WRITEUP.md`.

**Outcome.** A span request (span, coarse blocks, borrowed fine selection,
scalar-until-varied predicate) replaces the block-set author reads. The owning
stage applies contribution laws before lowering into groups and blocks, so a
partial edge need not force full interior blocks through a masked fallback.
Coarse masks still exclude groups during acquisition and execution. Some
candidates were reverted: group decode, uniform pack runs, a generalised bound
uniform pass and initial neutralised masked loops. Ordered runs (step 4) were
paused by Ashton's steer and are not started.

**Native qualification of record: [NATIVE.md](NATIVE.md)** (Codex). Candidate
38695dc4 was run against original Frame 7fa3ad56 on Zen 5 with identical leaf
archives. Span gains hold on AVX-512 and AVX2, and all checks pass. The OS
period-3 selected SUM residual on AVX2 is +6% repeated and +11% under
eviction; the native report also lists smaller remaining losses. The step
tables below are local development history on the aarch64 OrbStack VM, not
native qualification.

The maintained conformance checks now live in `ikea/test/frame/{span,demand}.cpp`
and run as `ikea_frame_span_check` and `ikea_frame_demand_check`. Development
counter hooks and their counting targets are not part of the landed library.
The historical commands/counters below describe the original spike sources;
recover those sources from the native evidence archive to reproduce them.

## Starting point from earlier work

- [Phase 3 findings](../frame-phase-3/FINDINGS.md#the-delta-stage-needs-its-laws-before-it-lands):
  framed checkpoints pay for bytes, but reading them through the child slowed
  the monotone COUNT law from 7.5 to 23 ns per block, so it "wants a fused
  read". The checkpoint SUM contributions below are that read for SUM.
- Same section, negative result: a SUM law built from masked sums lost
  1.3–2.6× to decoding, because SeriesPack's prefiltered SUM costs several times
  its range SUM. Contributions therefore hand children whole ranges where they
  can, and range calls replace masks at span edges.
- [Non-decreasing delta COUNT](../frame-phase-1/encoding/README.md#count-over-non-decreasing-delta-blocks):
  a checkpoint search plus one region scan replaced prefix decode. Step 4 would
  extend it across proven ordered runs of blocks.
- [Reuse audit](../frame-phase-3/REUSE-AUDIT.md) rows 1 and 3: checkpoint search
  and reconstruction route through `ikea::intervals`, so no Frame-side search
  is added.
- [Landing scorecard](../frame-phase-3/landing/SCORECARD.md): the cells and
  harness reused for timing (`landing/measure.cpp`).

## Historical spike method

- **Machine and flags.** Local OrbStack VM (Apple silicon, aarch64, NEON),
  Clang 21.1.8. Every arm is built with `-O3 -gline-tables-only` plus
  `-falign-functions=64 -mllvm -align-all-functions=6 -mllvm
  -align-all-nofallthru-blocks=6`. **Local VM times, not Graviton or Zen.**
  The vCPU migrates between core types, so only large, consistent differences
  count. Cells a step does not touch move by up to ±15%, mostly
  eviction-conditioned ones.
- **Arms.** Each step's binaries are saved. `compare.py` runs every arm one
  round per process, rotating arm order and pinned to one CPU. The base arm for
  Codex's fixtures is built in a separate worktree at 7fa3ad56 holding only the
  frozen harness sources.
- **`seam.cpp`** adds cells the landing harness lacks, over the public API only:
  bind, clustered selected SUM/summary (half the unit fully selected, the rest
  period 3), SUM/COUNT over alternating 8-block coarse selections, FILTER ≥
  median, and a pack-rooted OS. Results are checked outside the timed region
  (094801ee); step 0's own table predates that fix.
- **Codex's fixtures** (`span_check.cpp`, `span_compare.cpp`; Codex owns them,
  and they are committed unmodified) check spans against original values.
  They also compare a strong current caller on base (whole-block `sum(coarse)`
  plus masked `sum_selected` only on genuinely partial blocks) with the span
  overload. `span_table.py` summarises them.
- **Counters.** `counts.cpp` and `span_counts.cpp` link a counting copy of the
  Frame library (`detail/counters.h`, `SIXDB_FRAME_COUNTERS`). They report
  group setups, block steps, SeriesPack kernel calls and rows, decodes, point
  reads and scratch bytes for one checked execution per cell.
- **Sanitizers.** `sanitize.sh` runs the Frame fixtures, selection, wide
  Intervals, `span_check` and `demand_check` under ASan+UBSan, as in the landing
  receipt (instrumented Frame, uninstrumented leaf archives).

## The cut

Codex's review (`build/frame-collab/seam/CODEX-STAGE-SEAM.md`) and Ashton's
span steer replaced the first proposal, an `extent` with a central
`map(coordinate)` and more per-block virtuals. The adopted cut is
`build/frame-collab/seam/CLAUDE-SEAM-CUT.md`:

- A request is a logical span with a borrowed selector and a predicate that
  stays scalar until a stage makes it vary.
- The group is the execution grain; lowering happens where the descriptors
  live, after the owning stage has applied its law.
- Selected rows, child contributions and read dependencies stay distinct.
- Each stage lowers requests for its own children.
- Intervals owns ordered search; evidence is proven ordered runs held in
  validated state.

## Local step history (aarch64 OrbStack VM)

The steps, verdicts and ratios below were measured locally while developing,
against base 7fa3ad56. Native results are in [NATIVE.md](NATIVE.md).

| Step | Commits | Verdict |
| --- | --- | --- |
| 0 Payload-light bound proof; settle monotone blocks before reading checkpoints | d41855ab | kept |
| 1 Span requests replace block-set author reads | f712c042, 6b0c3ba1, 7af7a332 | kept |
| 2 Checkpoint SUM by contributions; interiors keep laws past partial edges | 68ce6319, 17e561cd | kept (seam-freeze-1) |
| `selected()` truthful after `within()` | c9810e8a | kept (seam-freeze-2) |
| Demand from the lowered span | 43d25440 | kept |
| `rows_of` reads a borrowed fine block in place | 3d9ccd7d | kept |
| 3 Group decode for Intervals fallbacks | 7ca131be, reverted 09e7698d | no measurable gain |
| Word-at-a-time FILTER, vector block sums in the defaults | b452bfe2 | kept |
| 5 Uniform pack COUNT runs | efc77983, reverted 1bc9d843 | within noise |
| 5 Bound uniform pass over any block set | not committed | slower |
| Neutralised masked summary and SUM in the defaults | not committed | slower |
| 4 Ordered runs, Intervals checkpoint-source adapter | — | paused by steer, not started |

### Step 0

- **Bound proof.** In `validate_group`, a width-K field is below 2^K. When
  lo + 2^K − 1 is representable, every inlier is, so only the E exception
  fields of the block are read. Otherwise the block's actual fields are read
  as one range, so a legal near-MAX block is still admitted. Group-image cases
  cover widths 1, 13, 63 and 64: a near-MAX window that fits, one field past
  the limit, and a nonzero exception field.
- **Monotone COUNT.** `last < t` settles a block before any read; `cp[0] ≥ t`
  needs one checkpoint `get`.
- **Effect.** Bind is 0.11–0.68× and gen_ts COUNT ≥ median 0.25×. A pack-rooted
  OS binds in 5.8 µs, so the old per-row proof was most of bound columns' bind
  cost. Intervals bind is now dominated by `validate_image`'s per-row
  exactness and sortedness loops (see "Next").

### Step 1: the request type

`author::request` holds:

- a span `[first, first + count)` of unit rows;
- the coarse blocks;
- a borrowed `seriespack::selection` of fine rows.

Construction lowers these once to the touched blocks and the wholly selected
ones. "Wholly" means every logical row, including a short tail.
`author::predicate` holds a scalar threshold, or a per-block plane once bound
subtracts each lo. The stage read virtuals take requests. The old block-set
forms remain as non-virtual adapters, so existing tests run unchanged. The
reader lowers fine selections without the 8 KiB row set and 2 KiB threshold
array, and gains `sum(row_range, blocks, rows)` and `count(op, t, row_range,
blocks)`.

Shared primitives that were strengthened:

- `bitset256::for_each_run`, `|`, `~` and `next`.
- `seriespack::selection::block_at`, which lends a borrowed block without
  masks.
- `author::with_op`, moved from bound, so the reference defaults compile one
  row loop per comparison.

Paired timing caught two regressions before they landed:

- EventTime COUNT ran 2.2–2.4× slower, because the request defaults branched
  on the comparison per row; with_op dispatch fixed it.
- Codex caught a fine-window over-read: span {17,14} over `bits(16, one u16
  word)`. Each 64-row word is now read over exactly its span rows. Witnesses
  for inside one word, mid-word start and end, and block and group edges are
  in the stage oracle.

On Codex's comparison, step 1's span overload was still 1.14–1.75× slower on
whole units, groups and edges:

- `make()` lowered per block even with no fine selection.
- Any request with one partial edge fell to the full decode reference
  (`all_full()` poisoning).

### Step 2: contributions, interiors and edges

Intervals SUM weighs each checkpoint by its region's selected rows:

- A run of whole blocks is G × one child SUM over its checkpoint ranks, with
  no checkpoint read.
- Any other block (span edge, fine selection, short tail, a G64 quarter
  block) reads its own checkpoints.
- Relative ends add one child SUM of the fields over the same request. Sizes
  keep the periodic weighted law over whole blocks and decode the rest.

Group size and checkpoint spacing stay independent. No stage gates on the
whole request being full: interior blocks keep the laws, and edges take
pack range calls or masks. `make()` without a fine selection is bit
arithmetic plus two edge blocks.

The stage oracle checks:

- Codex's witness: rows {0,1,3,16}, with checkpoints 10 and 100 at G16, sum to
  134.
- A seven-row last region.
- Spans with ragged edges under all, alternating and sparse coarse masks,
  with and without fine masks.
- `within()` each group against brute force, including a constant-child stage
  whose SUM law is value × `selected()`.

Counters on the worst step 1 cells (one checked span SUM, step 1 → step 2;
kernel rows unchanged unless shown):

| Cell, query | Group setups | Block steps | Kernel calls | Decodes | Scratch bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| sizes64 65536, whole unit | 16 → 17 | 1,280 → 520 | 272 → 17 | 0 | 8,192 → 0 |
| sizes64 65536, cross-group edges | 0 → 3 | 38 → 57 | 38 → 7 | 57 → 6 | 608 → 64 |
| relative64 65536, cross-group edges | 0 → 4 | 38 → 77 | 38 → 7 | 57 → 0 | 608 → 64 |
| relative64 65536, clustered fine | 0 → 17 | 512 → 1,030 | 512 → 265 | 768 → 0 | 8,192 → 4,096 |
| bound 65536, cross-group edges (rows 4,864 → 4,600) | 3 → 3 | 57 → 69 | 19 → 4 | 0 | 0 |

Block steps count stage bookkeeping items, not cost.

### After the freezes

- **Demand.** `reader::required(span, blocks)` derives the closure from the
  same lowered block set that `sum(span, blocks)` executes. A masked-out middle
  group is not acquired, and `demand_check` answers the span SUM with only that
  demand bound.
- **Borrowed fine blocks.** When a fine selection borrows consecutive blocks
  over the span, `rows_of` reads each block in place instead of re-lowering it
  at every stage of an identity chain. That closed most of the dense
  irregular-selection loss.
- **Reference defaults.** FILTER builds each 64-row word in a register. SUM and
  summary over wholly selected blocks sum 32-bit halves in 64 bits, with
  min/max alongside, so these loops vectorise. Intervals FILTER is 0.29–0.32×.

### Negative results

- **Group decode (step 3).** `decode_run` with one fields read, one checkpoint
  read and one traversal per group, also used for the image evidence check.
  Bind went to 0.92–0.97×; every other cell stayed within noise, including
  cells it does not touch. The Intervals fallbacks are dominated by per-row
  mask, summary and evidence loops, not by decode dispatch.
- **Uniform pack COUNT runs (step 5).** One range COUNT per same-width slot run
  gave OS pack-root alternating COUNT 0.94×, within noise. Bound→pack cannot
  use it, because bound hands its child per-block thresholds. Worth one native
  look.
- **Bound's branch-free uniform pass for any block set (step 5).** Slower on
  alternating selections: UserID 572 → 1,354 ns, OS 3.1 → 3.9 µs. A pass over
  all 256 blocks costs more than settling the 128 selected blocks one by one.
- **Neutral contributions in the defaults.** A branch-free masked summary for
  partial blocks was 1.3× slower than the bit walk. A neutralised masked SUM
  for partial blocks was 1.2× slower on gen_ts period 3. On NEON here, the
  selected-bit walk wins in the Frame fallback; SeriesPack's own masked
  kernels are a separate question (see "Next").

### Before and after (local VM, paired, 5 rounds)

`head` is the code at b452bfe2. Median ns per whole query; ratios are to base.

| Cell | base | head | head/base |
| --- | ---: | ---: | ---: |
| EventTime SUM | 13,851 | 8,357 | 0.60 |
| EventTime selected SUM, period 3 | 61,848 | 38,246 | 0.62 |
| EventTime clustered selected SUM | 82,495 | 23,333 | 0.28 |
| EventTime FILTER ≥ median | 175,471 | 51,924 | 0.30 |
| EventTime COUNT ≥ median | 49,672 | 48,049 | 0.97 |
| EventTime bind | 236,359 | 157,097 | 0.66 |
| gen_ts SUM | 25,631 | 16,741 | 0.65 |
| gen_ts COUNT ≥ median | 4,766 | 1,262 | 0.26 |
| gen_ts clustered selected SUM | 90,318 | 43,932 | 0.49 |
| gen_ts FILTER ≥ median | 188,659 | 59,156 | 0.31 |
| gen_ts bind | 238,641 | 141,034 | 0.59 |
| UserID bind | 102,752 | 11,063 | 0.11 |
| OS bind | 128,149 | 28,022 | 0.22 |
| URLHash bind | 73,398 | 49,710 | 0.68 |
| OS / URLHash / UserID clustered summary | 69,116 / 68,488 / 80,556 | 48,265 / 48,392 / 55,350 | 0.69–0.71 |
| OS selected SUM, period 3 | 11,852 | 12,660 | 1.07 |
| URLHash clustered selected SUM | 29,675 | 31,787 | 1.07 |

Point reads, whole-unit COUNT on bound→pack, and URLHash/UserID SUM stay
within noise. The full table is in `build/seam-bin/final.md` in the worktree.

Codex's span comparison, head span overload against base's strongest caller
(geomean over 24 cells; worst cell in brackets):

| Shape | head span / base |
| --- | ---: |
| whole unit | 0.71 (1.05) |
| whole group | 0.76 (1.04) |
| cross-group edges | 0.64 (0.87) |
| 63 rows | 0.39 (0.56) |
| tail | 0.48 (0.72) |
| coarse holes | 0.76 (1.05) |
| span with coarse holes | 0.52 (0.80) |
| span with fine selection | 0.71 (0.95) |
| clustered fine | 0.77 (0.97) |

## Mask diagnostic (separate, not applied)

Ashton's principle is that a masked-out row contributes the identity rather
than a branch. It was examined on side branch `spike/frame-seam-mask` at
d9a2ea72. That branch holds the exact rejected Frame variants (8d1955d8,
reverted in 22a2b47f; d84f4ac2, reverted in 6985390d), out-of-line kernels,
NEON and AVX2 assembly, and vectoriser remarks, explained in its
`workbench/spikes/frame-seam/asm/README.md`.

Verdict:

- The rejected neutral SUM did vectorise, and it lost only because period-3
  blocks sit below its density crossover with the bit walk (about 40% on
  NEON).
- The rejected summary did not vectorise: its guarded min/max is not
  recognised as a reduction. The identity-element form does vectorise.
- A per-block choice by popcount tracks the better form at every density.

The NEON crossovers are not Zen 5 thresholds, and the AVX2 assembly is not a
timing. None of this is applied to the spike's timed code.

## Next

- **Step 4 (paused).** Ordered runs proven during validation, plus an
  Intervals checkpoint source: `wide_sizes`/`wide_ends` parameterised on a
  checkpoint source, with range access as well as `at`, and its prepare phase
  run after the checkpoints are acquired. It needs witnesses for
  independently sorted but overlapping blocks and for duplicate boundaries.
  Prefix/suffix FILTER would follow, then upward-shaped FILTER results.
- **Intervals bind.** It is now `validate_image`'s per-row exactness and
  sortedness evidence loops. Making them vectorisable is a separate,
  evidence-policy change.
- **SeriesPack (Codex's audit).** The AVX2 arm of `exact_halves_regions` is
  scalar per selected bit. A vector low/high accumulation with neutralised
  lanes is the leaf fix. The masked SUM also lacks the unmasked packed total
  path. Neither is measurable on this aarch64 VM, and the native capture of
  this seam uses unchanged leaf archives.
- **Residual losses.** See [NATIVE.md](NATIVE.md#existing-operations-and-remaining-gaps)
  for qualified native cells. The local step history showed OS period-3 and
  URLHash clustered selected SUM at 1.07×, and some bound0 whole-unit/group
  SUMs at 1.03–1.05×.
- **Prepared handles.** Logical spans stay the entry point; writers stay
  deferred.

## Reproduce the local historical comparison

Inside ubuntu, from the worktree:

```sh
cmake --preset release "-DSIXDB_SPIKES=frame-phase-3;frame-seam" \
  "-DCMAKE_CXX_FLAGS_RELEASE=-O3 -gline-tables-only" \
  "-DCMAKE_CXX_FLAGS=-falign-functions=64 -mllvm -align-all-functions=6 -mllvm -align-all-nofallthru-blocks=6"
cmake --build build/clang/release --target frame_landing_measure frame_seam_measure \
  frame_seam_span_check frame_seam_span_compare frame_seam_demand_check frame_seam_counts frame_seam_span_counts -j3
workbench/spikes/frame-seam/sanitize.sh
python3 workbench/spikes/frame-seam/compare.py run OUT.jsonl 5 COLS base=DIR head=DIR
python3 workbench/spikes/frame-seam/compare.py table OUT.jsonl base
python3 workbench/spikes/frame-seam/span_table.py SPAN.jsonl base_current head_span
```

`COLS` is the shared input directory (`~/sixdb/build/spikes/ikea-frame/cols`).
Each `DIR` holds one arm's saved `frame_landing_measure` and `frame_seam_measure`.
