# Frame encoding: corpus and baseline feasibility

Owner: Claude (Frame thread). Part of the [Ikea Frame spike](../README.md); the
encoding proposal is [ENCODING.md](../ENCODING.md) and open questions are
indexed in [QUESTIONS.md](../QUESTIONS.md). This directory holds the corpus,
baseline and compression investigations. It defines no interface.

## Current digest

The [stocktake inventory](../INVENTORY.md) owns the cross-spike picture. This
directory's sections below are the evidence; read them by question, not in order.

- **Wins come from algebra, not decoding.** Run and constant blocks answer COUNT
  without rows, non-decreasing delta blocks search their checkpoints, delta SUM
  is a weighted closed form, and row-selected SUM/COUNT run at 0.17–0.32× the
  decode-then-mask cost. Plain or global-FOR native arrays win scans with no
  shortcut, and points.
- **Bytes:** real lightweight bodies; arrival dictionaries reach 43.9 bits/value
  on URLHash (ZSTD-3 41.4); predictor NULL fill costs bodies nothing, but encode
  3.6–7.6× more.
- **Seams:** borrowed blocks with exact extents and grants, and dictionary
  identity as {scope, edition}. Cached dictionary material helps equality and
  sparse COUNT, not SUM or points.
- **Open in this directory:** the run-block leaf cost behind the flushed OS SUM
  loss (prototype in progress), [fused aggregates](#fused-aggregates) (local
  ARM: SUM + COUNT in 0.58–0.74× the separate calls on packed and run columns,
  no gain where COUNT is a closed form, still 1.7–2.3× native fused; workers pending),
  [predicate masks](#predicate-masks) (emitter landed; run-heavy masks still
  trail a plain array), and every family beyond SUM/COUNT.
  Detailed questions are in [QUESTIONS.md](../QUESTIONS.md).
- **Comparison limits:** local timings are OrbStack direction only. Worker runs
  in [`worker.sh`](worker.sh) are repeated and in-memory, with cache residence
  unproven; the controlled cold panel is Ikea LEAD's [scan findings](../scan/FINDINGS.md).

## What exists

| Need | Available now | Gap |
| --- | --- | --- |
| Real integer/time columns | ClickBench `hits` slice: 3,000,000 rows, 105 columns, 77 integer/time ([provenance](../../../../../corpora/clickbench-rows/PROVENANCE.json), local at `~/corpora/clickbench-rows/`) | Not yet a registered [dataset](../../../datasets/README.md); its row order is ClickBench's (sorted by CounterID, date, UserID, time), which favours runs |
| Floats and decimals | None in SixDB | TPC-H via DuckDB's `tpch` extension (not installed in the VM; installing fetches it from DuckDB's extension repository); IMDb ratings (Codex/Workbench locating a bounded slice); Calico's N-CMAPSS/PMU corpora in S3; ALP paper's float datasets. The VM volume has about 23 GiB free, so new corpora need explicit size bounds |
| Time series and signed decimals | Jena climate: [`jena_parts.py`](jena_parts.py) (Codex) prepares three original 65,536-row windows of all 14 fields as exact ×100 int64, plus a separate targeted fault block (rows 343,552–343,807, all 18 `wv` and 20 `max.wv` −9999.00 sentinels). Under 19 MB; paths in its receipts under `build/datasets/prepared/` | The fault block is adversarial; don't blend it into population statistics |
| Real nulls, unsorted keys | IMDb JOB snapshot (May 2013), 4,096-row `title` slice at `build/reviews/frame-corpora-20261022/imdb-title-first4096/` with 205 real NULL years; Engine document `build/spikes/ikea-frame/imdb-title-year.json` (Codex) | A smoke fixture only, too small for population claims |
| Time series (synthetic) | Calico's TSBS generator recipe | Not ported; needs the Go `tsbs` tool |
| Strings | Out of scope (Ashton); lengths and prefixes only | — |
| Baselines | DuckDB 1.5.4 and Polars 1.42.1 (Python, in the Ubuntu VM); Calico Frame sources | No DuckDB/Polars C++ peers in [native peers](../../../peers/README.md); Python timings include interpreter edges |
| Logical reference | None | A small independent oracle per study, as Codex proposed; not a shared semantic contract |

## Same-work boundary

- **Compression:** bits per value at rest *including all metadata* (descriptors,
  dictionaries, exceptions, slack). DuckDB's native storage after `CHECKPOINT`
  and Parquet (lightweight encodings only, and ZSTD) are the byte baselines.
  DuckDB is the closest system peer; Parquet+ZSTD bounds what general entropy
  coding buys.
- **Decode and simple operations:** a consumed result (sum, count of a
  predicate, decode into a native array that is then read), single thread,
  same rows. DuckDB times include its execution engine, which is the bar
  Ashton set. Polars works on uncompressed in-memory Arrow arrays, so beating
  it means compression has to pay in bandwidth or algorithm, not just bytes.
- **Never compare a kernel to a whole SQL query as if they did the same work.**
  Native C++ peers would sharpen this; until then, Python-driven DuckDB timings
  need their fixed overhead measured and reported separately.

## First measurement: modelled compression on ClickBench

[`characterise.py`](characterise.py) models each 256-row block's best
lightweight encoding (plain, constant, runs, frame of reference with 0–32
exceptions, delta, part-wide sorted dictionary with FOR or runs over codes)
and charges a per-block descriptor. [`duckdb_storage.py`](duckdb_storage.py)
reports DuckDB and Parquet sizes for the same columns. Results:
`build/spikes/ikea-frame/clickbench-*.jsonl` (ignored).

**These are modelled logical bits, not SeriesPack bytes**: they ignore tile
geometry, so real Frame bytes will be somewhat higher. DuckDB sizes are
estimated from segment offsets.

Sum over the 77 integer/time columns, bits per row:

| Native types | One global FOR per column | Frame model, 16/64/128-bit descriptor | DuckDB storage (est.) | Parquet, encodings only | Parquet + ZSTD |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1,968 | 946 | 362 / 371 / 382 | 536 | 709 | 337 |

The corpus is the raw `clickbench-rows` slice (three original partitions,
original partition and row order, no physicalised Calico bytes), so columns
stay row-aligned for Engine's correlated witness. None of the 77 columns has
nulls; the model keeps positions anyway (nulls forward-filled, presence mask
uncharged), so dense nullable storage isn't conflated with compaction. Widths
are exact (checked against Python `bit_length` for 0, 2^k−1, 2^k, 2^k+1 and
UINT64_MAX), and signed columns map to offset binary before any span, so
int64-boundary spans don't overflow. Codex found both bugs in the first draft.

Block winners at a 64-bit descriptor: constant 41%, runs over values or codes
38%, dictionary + FOR 8%, FOR with exceptions 7%, plain 5%, delta under 0.1%.

What this suggests, before physical confirmation:

1. **A mixed lightweight model plausibly beats DuckDB storage by about 1.4×** on
   these columns, and sits about 10% above Parquet+ZSTD.
2. **The gap to ZSTD is concentrated in mid-cardinality skewed categoricals**
   (RefererCategoryID 5.3 vs 2.1 bits; RefererRegionID 4.5 vs 2.2;
   TraficSourceID 2.7 vs 1.5) and the two 64-bit hashes (45–50 vs 36–42). The
   model has no frequency-ranked codes; FOR with exceptions over
   frequency-ranked codes approximates entropy coding and is the obvious next
   arm.
3. **Descriptor size matters less than feared here:** 16 to 128 bits moves the
   total by 5%, because 41% of blocks are constant and take a cheap path. The
   metadata knee is about constant and near-constant blocks, not general
   descriptors.
4. **Delta essentially never wins on ClickBench**, even for EventTime, whose
   sort order restarts per user. Prediction needs time-series corpora to be
   tested fairly.
5. **Incompressible columns are a large share of bytes**: WatchID (62 bits),
   HID (26), the two hashes (45–50), EventTime and ClientEventTime (16).
   Encoding work cannot shrink them, so their decode and scan speed decides
   whole-table results.
6. **This row order is unusually run-friendly.** Results must be repeated on
   shuffled and differently clustered orders before they guide presets.

### Second pass: frequency codes, entropy and row order

Added arms: a frequency-ranked part dictionary with FOR+exceptions over its
codes, the part's zero-order entropy, and one shared random row permutation
(`--shuffle 1`; DuckDB/Parquet shuffled with `ORDER BY hash(file_row_number)`).
These are still modelled logical bits with oracle selection, so they're an
optimistic candidate estimate, not a bound or a measured Frame result.
The entropy arm charges H0 plus the dictionary (sorted values at the part's
value width) plus a quarter-bit descriptor.

| Bits/row, 77 columns | Original order | Shuffled |
| --- | ---: | ---: |
| Frame model, lightweight mixed blocks (incl. frequency codes) | 365 | 670 |
| + entropy-coded part dictionary where it's smaller | 355 | 602 |
| DuckDB storage (est.) | 536 | 906 |
| Parquet + ZSTD | 337 | 598 |

1. **Frequency-ranked codes barely help lightweight FOR** (371 → 365). Skewed
   tails still set the block width, and exceptions don't absorb them.
2. **Entropy coding matters most when order doesn't help.** In original order
   it saves only about 10 bits/row. Shuffled, it saves 68 and reaches ZSTD
   parity (602 vs 598), mostly on mid-cardinality columns (Response*Timing,
   Interests, Robotness, RegionID).
3. **The hash gap isn't entropy.** URLHash has 37k distinct values per
   65,536-row part (1.43M across 3M rows), so any dictionary costs about as
   much as the values. ZSTD's 36–42 bits come from nearby repeats (LZ
   matches), which no candidate here models. A "repeat of a recent value"
   reference is the arm that could test this.
4. **Row order changes everything else by 1.8×.** Under shuffle, runs fall from
   38% to 7% of blocks and constant blocks from 41% to 19%. UserID, FUniqID and
   the IPs revert to near full width. Clustering is Engine's choice, and it is
   probably a bigger compression lever than any codec here.
5. **The lightweight model holds against DuckDB in both orders**: about 1.47×
   fewer bits in original order, 1.35× shuffled. Real SeriesPack bytes have to
   confirm this.

### Fixed-width 256-row planes are exact SeriesPack payload

SeriesPack tiles are bit-tight (`tile_bytes = tile_rows × payload / 8`,
[format.h](../../../../ikea/include/ikea/seriespack/format.h)), and every
tile height (8 Local; 32, 64, 128 or 256 Striped) divides 256. A 256-row block
at width K therefore occupies exactly 32·K payload bytes in every preset: the
ragged-tail slack in the [residual-bounds note](../../../notebook/pfor-bounds.md#what-sixdb-composition-changes)
doesn't arise for **full fixed-width 256-row planes**. Everything else has
ragged populations: run values and lengths, exception streams, compacted
present rows and variable-size dictionaries each round to their own tiles,
plus heads, bases and placement. Those need counting in real encoded bodies,
not finer model estimates (Codex's correction).

### First peer timings (local, indicative)

[`peer_timings.py`](peer_timings.py): single thread on the Apple-silicon
OrbStack VM (noisy; not fleet evidence), 3M rows, median of 15 warm runs,
full query times (the one-row-table time is recorded only as context). DuckDB
queries a checkpointed compressed database; Polars (`POLARS_MAX_THREADS=1`)
works on uncompressed in-memory Series. **Every answer is checked against an
exact Python-integer reference.** All DuckDB answers match. Polars' native SUM
matches only where it can't overflow: on UserID, RegionID, WatchID and URLHash
it wraps (marked \*), so it answers a different question. Its exact-law
alternative widens first (Int64 or Int128), which pays a materialising cast.

| Column (native bits) | DuckDB SUM (exact) | DuckDB COUNT(x ≥ median) | Polars SUM, native | Polars SUM, widened | Polars COUNT |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS (16) | 2.11 | 2.56 | 0.100 | 0.678 | 0.073 |
| ResolutionWidth (16) | 2.19 | 2.95 | 0.065 | 0.461 | 0.061 |
| RegionID (32) | 1.48 | 2.16 | 0.105\* | 0.581 | 0.150 |
| EventTime (64) | 1.30 | 1.21 | 0.270 | 3.165 | 0.202 |
| UserID (64) | 3.85 | 2.32 | 0.326\* | 3.304 | 0.328 |
| WatchID (64) | 3.34 | 2.67 | 0.255\* | 3.271 | 0.351 |

ns per value, from one local run; run-to-run noise on this VM is easily 20%.
Hypotheses for the bar (to test, not established):

1. **For predicates and counts, Polars on plain arrays is the hard baseline**
   (0.05–0.35 ns/value, lawful), and close to memory speed. 2× over it needs
   fewer bytes or less work, not faster decoding.
2. **For exact SUM there's no fast lawful peer here.** Polars is fast only
   when its sum may wrap; widening costs a cast. DuckDB is exact at 1–4
   ns/value. The bar for exact aggregates is DuckDB or a hand-written exact
   native loop, which we should build as the control rather than quote
   Polars' wrapping number.
3. **Constant and run blocks (79% here) turn SUM and COUNT into per-block
   work**, an algorithmic win the bar can count.
4. **Incompressible columns probably can't clear 2× by scanning on this
   machine.** WatchID stays at ~62 bits under every candidate, so a predicate
   scan moves as many bytes as Polars. Block statistics and precomputed
   aggregates are the plausible levers. That's an informed hypothesis, not a
   lower bound; these columns stay in every report.
5. The filtered MAX query is a query, not a decode-to-array-and-consume
   equivalent; a real decode control is still owed.

## Real encoded bodies, first native pass

**Shared private material.** The codecs now live in
[`material.h`](material.h) / [`material.cpp`](material.cpp) (target
`frame_encoding_material`, namespace `frame_encoding`): descriptors and kinds,
`encode`, run traversal, block masks, `block_sum`, narrow SUM, COUNT and
`decode_block` (whole-block working-domain values, checked against the input
on every run). [`bodies.cpp`](bodies.cpp) keeps the timing driver and plain
controls. Codex's protected-payload check (`masked_access_check.cpp`, target
`frame_encoding_masked_access_check`) and Engine's consumers link the same
material. This is research material, not a Frame API.

[`bodies.cpp`](bodies.cpp) (target `frame_encoding_bodies`; columns exported
by [`export_columns.py`](export_columns.py)) builds each 256-row block as
constant, runs or frame-of-reference with real Ikea storage: Local SeriesPack
planes through the typed mutation path, and Boundlist compact Sizes geometry
for run lengths (M = 255, B = 256, immortal; a runs block never holds one
256-row run). It charges a 16-byte descriptor per block and every Boundlist
directory, length and control byte. Block choice is still an oracle (fewest
bytes). There are no exceptions, dictionaries or overrides yet. Exact SUM and
COUNT(u ≥ median) are checked against a plain control, the column relative to
its minimum at the narrowest byte width, under all, clustered-50% and
sparse-10% block masks.

Local VM, single thread, one run; timings are indicative and the plain
control itself moved between runs (EventTime 0.06 → 0.11 ns/row).

| Column | Real bits/value | Model (128-bit desc.) | Run geometry share | Descriptor share | SUM ns/row: u64 decode / narrow / plain | COUNT ns/row: encoded / plain |
| --- | ---: | ---: | ---: | ---: | --- | --- |
| EventTime | 17.14 | 16.82 | 6% | 3% | 0.31 / 0.23 / 0.11 | 0.27 / 0.20 |
| UserID | 16.01 | 11.81 | 20% | 3% | 0.32 / 0.28 / 0.35 | 0.11 / 0.30 |
| OS | 4.17 | 2.21 | 59% | 12% | 0.16 / 0.16 / 0.06 | 0.37 / 0.15 |
| RegionID | 6.82 | 4.14 | 43% | 7% | 0.24 / 0.23 / 0.07 | 0.73 / 0.20 |
| ResolutionWidth | 5.19 | 3.02 | 50% | 10% | 0.19 / 0.19 / 0.06 | 0.56 / 0.15 |
| IsRefresh | 1.44 | 0.85 | 0% | 35% | 0.18 / 0.06 / 0.07 | 0.003 / 0.001 |
| CounterID | 0.50 | 0.08 | 0% | 99% | 0.007 / 0.009 / 0.06 | 0.003 / 0.14 |
| WatchID | 62.50 | 62.50 | 0% | 1% | 0.68 / 0.68 / 0.29 | 0.44 / 0.29 |
| URLHash | 59.45 | 49.63 | 2% | 1% | 0.51 / 0.53 / 0.24 | 0.74 / 0.28 |

(ConnectTiming and ClientIP are in the JSON lines: 3.32 and 11.40 bits.)

Block masks are one bit per block. Groups of 16 blocks are derived from the
same bits, following Ashton's clarification: an empty group is skipped
without touching its descriptors, a full group takes the unmasked path, and
a mixed group visits only its set bits. An all-empty mask costs about 0.001
ns/row, the walk over the mask words. This is bring-up code. A poisoned-memory
fixture proving that skipped payloads are never loaded is still owed, as Codex
suggested. The
model's dictionary arms aren't built yet, which explains part of the URLHash
and RegionID gaps. Clustered and sparse masks scale both encoded and plain
time roughly with the selected fraction; no coarse-mask overhead showed up.

**Run-end projection (Codex's proposal), second run.** Runs blocks now store
exclusive end − 1 as u8 and project them into Boundlist geometry through
`from_ends` (exclusive ends 1..256, no checkpoints, control or tombstones).
The compact mutable view stays as a `--compact-runs` control. Timed u128 sums
now consume both result halves.

| Column | u8 run ends, bits/value | Boundlist compact | Model |
| --- | ---: | ---: | ---: |
| OS | 2.78 | 4.17 | 2.21 |
| ResolutionWidth | 3.91 | 5.19 | 3.02 |
| RegionID | 5.43 | 6.82 | 4.14 |
| UserID | 14.60 | 16.01 | 11.81 |

Bytes are now within about 0.6–1.3 bits of the model on run-heavy columns. The
rest is mostly the missing dictionary arm and the 16-byte descriptor. Timings
in this run were too noisy to compare the two runs bodies: the plain UserID
control alone moved from 0.25 to 0.51 ns/row between runs. Repeated runs, or a
quieter machine, are needed before any speed comparison.

Findings:

1. **Run geometry is the largest metadata cost**: 43–59% of bytes on the
   run-heavy columns. Boundlist's compact view always reserves its full
   directory (16 u16 checkpoints, 32 bytes) plus control, about 1.1
   bits/value per runs block before any lengths. Moving lengths from u16
   (M = 256) to u8 (M = 255) already took OS from 5.33 to 4.17 bits. Within a
   256-row block, a runs body could use u8 ends with no checkpoints for few
   runs. Whether Boundlist should gain a small-domain form, or Frame should
   keep its own, is a question for its owner.
2. **The 16-byte descriptor dominates near-constant columns** (CounterID 0.50
   real vs 0.08 modelled; IsRefresh 35%). Constant blocks need a much smaller
   representation, such as a run of constant blocks at group level.
3. **Decode-then-reduce loses to plain arrays by 2–4×** on packed and runs
   blocks, even with narrow lanes. SeriesPack decodes at about 0.16–0.3
   ns/row here, against about 0.06 for a plain narrow sum. Narrow lanes help
   when K is tiny (IsRefresh 0.18 → 0.06, beating plain).
4. **Wins come only from algorithmic shortcuts**: constant blocks (CounterID
   SUM 8×, COUNT 50×), runs for COUNT (UserID 2.6×), and base-versus-threshold
   whole-block decisions (ConnectTiming, IsRefresh). This matches the review:
   the gains are in less work, not faster decoding. The next arms are Ikea's
   direct packed filters for COUNT, and fused or bit-plane sums that never
   materialise lanes.

## Jena: prediction in real bodies

Codex ran the model over Jena's three 65,536-row windows (14 exact ×100
decimal fields) and the fault block: delta wins about 74% of blocks, with no
constant blocks, the opposite of ClickBench. Results:
`build/spikes/ikea-frame/jena-characterise.jsonl`. (Codex also found the
model's `monotone_blocks` diagnostic was always true for unsigned diffs; it
now compares adjacent values.)

`bodies.cpp` now has a **delta** kind: anchor in the descriptor, 256 residuals
over the minimum step as a SeriesPack plane, the step base (i64), and 15
cumulative-residual checkpoints at u32 (u64 if K + 8 > 32). SUM uses the
weighted form `256·u0 + rbase·32640 + Σ (256 − j)·s_j`, a narrow weighted
reduction with no prefix decode. COUNT prefix-decodes. Both match the plain
control exactly on all 42 window-fields.

| Real bits/row, 14 fields | Window 0 | Window 196,608 | Window 355,015 |
| --- | ---: | ---: | ---: |
| Delta with native u32/u64 checkpoints | 147.3 (delta 35% of blocks) | 146.4 (34%) | 147.8 (36%) |
| Delta without checkpoints | 128.2 (85%) | 127.6 (85%) | 129.1 (85%) |
| Model (64-bit descriptor) | ~128 | ~129 | ~131 |
| One global FOR per field | ~179 | ~177 | ~178 |

**Checkpoint format decides whether prediction wins.** Fifteen native-width
checkpoints cost about 2 bits per value, which makes delta lose most blocks to
plain FOR. Without them, real bytes match the model, and delta takes 85% of
blocks. Bit-packed checkpoints at K + 8 bits would cost about 0.06·(K + 8)
bits per value. So the decided "checkpoints every 16 values" needs a compact
checkpoint encoding, or a coarser spacing, to pay. This is now a question for
Ashton in QUESTIONS.md.

**Curated checkpoint options (Ashton: Engine decides, Ikea exposes curated
options).** `bodies.cpp --checkpoints=native|packed|every64|none`; real bits per
row over the 14 fields, with delta's block share:

| Option | Window 0 | Window 196,608 | Window 355,015 |
| --- | ---: | ---: | ---: |
| native: 15 per block at u32/u64 | 147.3 (35%) | 146.4 (34%) | 147.8 (36%) |
| packed: 15 at K + 8 bits | 139.2 (68%) | 138.5 (67%) | 140.1 (66%) |
| every64: 3 at native width | 132.7 (85%) | 132.0 (85%) | 133.5 (84%) |
| none | 128.2 (85%) | 127.6 (85%) | 129.1 (84%) |

Bit-packing still costs about 1 bit/value at Jena's widths (K + 8 ≈ 18 bits ×
15 / 256). Every-64 checkpoints get within 5 bits/row of none. Point-read cost
per option is the measurement Engine needs next to choose.

SUM and COUNT on Jena are 4–8× slower than the plain int16 control (about 0.06
ns/row) on this VM: decoding through the runtime SeriesPack decoder into u64
dominates. Same caveats as before: noisy local timings, an oracle selector, no
exceptions yet (the fault block's −9999.00 sentinels are the obvious first
exception test).

### Measurement and input hygiene (Workbench and Codex review)

- **Paired, rotated arms.** Each of 21 trials times every arm once, starting
  from a different arm. The JSON keeps raw whole-call min/median/max
  nanoseconds per arm, with `physical_rows` and `selected_rows` (the `none`
  mask has no per-selected-row denominator). Medians reproduce the earlier
  ranking; maximums still show large VM outliers (spread up to 20× on some
  arms). So these remain local, indicative timings.
- **Timed wide sums** consume both u128 halves. Codex checked the ARM64
  disassembly: carry and both stores stay in the timed loop, and empty groups
  branch past the block bodies.
- **Inputs:** the native driver rejects missing, empty, non-multiple-of-8 or
  non-whole-256-row files, instead of overrunning or silently truncating.
  The exporter drops the partial tail explicitly and records
  `dropped_tail_rows` (192 for the 3M-row ClickBench slice).

## Exceptions: patched frame-of-reference blocks

A **patched** kind (separate from packed, so whole-block threshold shortcuts
stay correct) keeps residuals over a base at width K, with 0 in exception
slots. After the plane come the block minimum (u64), the exception count (u8),
the exception value width (u8, so the body needs no retained decoder to be
interpreted), ascending u8 positions and exception values relative to the
minimum as a SeriesPack plane at that width. The encoder tries the narrowest window of
256 − e sorted values for e ∈ {2, 8, 32}. SUM, COUNT and `decode_block` are
exact against the plain control on every column.

| Input | Before (bits/row or bits/value) | With patched blocks | Patched block share |
| --- | ---: | ---: | ---: |
| Jena windows, 14 fields, packed checkpoints | 139.2 / 138.5 / 140.1 | 138.9 / 138.0 / 139.7 | ~10% |
| Jena fault block, `wv` and `max.wv` (−9999.00 sentinels) | ~20.5 (sentinels set the width) | 12.3 and 13.3 | the block |
| ClickBench, 11 columns | 179.3 | 179.1 | RegionID, ConnectTiming |

**Exceptions are insurance, not a population win, on these inputs.** They
take about 10% of Jena blocks but save only 0.3% of bytes. On the fault block
they cut the sentinel fields by ~40%, because without them two sentinels set
the whole block's width. Delta blocks have no exceptions yet, so a sentinel
inside a smooth series still pushes that block from delta to patched FOR;
[delta with exceptions](#delta-with-exceptions) tests that next.

## Delta with exceptions

A **delta_patched** kind keeps the delta body unchanged (residual plane at
width K, step base `rbase` as i64, checkpoints under the image's
`delta_checkpoints` option) and appends the patched exception tail: block
minimum `lo` (u64), count n (u8), value width W (u8), n ascending u8 positions
and n values relative to `lo` as a SeriesPack plane at W (the width of the
largest exception offset, stored in the body). Delta and patched layouts are
unchanged; `encode_options::delta_exceptions` (default on) turns the kind off
for consumers that don't read it yet.

- **Prediction law: exception rows are outside the chain.** Let f be the first
  non-exception row; the descriptor anchor is row f's value. Every later
  non-exception row i stores `s_i = (x_i − x_p) − rbase`, where p is the
  previous non-exception row. Row f and every exception slot store 0 and take
  no step. With `S(i) = s_0 + … + s_i` and `m(i) = max(0, i − |{e ≤ i}|)`,
  the chain value at row i is `anchor + rbase·m(i) + S(i)`: the value of a
  non-exception row, or for an exception row the last non-exception value
  before it (the anchor before f).
- **Checkpoint law.** Checkpoints hold `S(i)` at the same rows as delta, the
  cumulative stored residuals of non-exception rows. Exception values are
  never in them. A point read adds `rbase·m(i)`, counting positions ≤ i.
- **SUM.** The N = 256 − n chain rows have m = 0 … N − 1, and summing S over
  chain rows equals summing it over all rows minus its values at exceptions:
  `Σx = N·u0 + rbase·N(N−1)/2 + Σ_j (256 − j)·s_j − Σ_{e∈E} S(e) + Σ_{e∈E} (lo + v_e)`.
  The weighted term is delta's; the correction scans only to the last
  exception. COUNT and `decode_block` reconstruct the chain and patch the
  exceptions. There is no whole-block threshold shortcut.

The encoder prices two exception sets exactly for each e ∈ {2, 8, 32}. The
value set is the rows outside the narrowest window of 256 − e sorted values,
as for patched. The step set comes from a left-to-right pass that keeps steps
inside the narrowest window of 255 − e sorted steps. An out-of-window row
becomes an exception when a later row, within the remaining budget, steps back
into the window from the last kept value. Their union was tried and saved
under 0.02 bits/row. All runs are exact against the plain control and the
input on all 42 Jena window-fields, the fault block and five ClickBench
columns under every checkpoint option, and on synthetic blocks with leading,
trailing, scattered and run exceptions at widths above 47 bits.

| Real bits/row, 14 fields, packed checkpoints | Window 0 | Window 196,608 | Window 355,015 | Fault block |
| --- | ---: | ---: | ---: | ---: |
| Before (with patched) | 138.9 | 138.1 | 139.7 | 154.9 |
| With delta_patched | 138.5 | 137.7 | 139.3 | 154.2 |
| delta_patched block share | 9.6% | 9.4% | 8.6% | 2 of 14 |

Under the other checkpoint options the saving is similar: native −0.15,
every64 −0.4 and none −0.5 bits/row per window, and 1.0 bits/row on the fault
block under every64 and none. delta_patched takes 5–13% of Jena blocks,
mostly from delta (230–270 blocks per window under packed checkpoints), plus
about 80 from packed FOR. ClickBench is unchanged apart from EventTime (65 blocks,
< 0.01 bits/value).

**The sentinel case isn't where this pays.** On the fault block the −9999.00
sentinels sit in `wv` and `max.wv`, in runs of 18–20 rows. Wind speed is noisy,
so patched FOR still beats delta with exceptions there, and those fields stay
patched. The fields that do move (`p` and `VPmax`, plus `T`, `Tpot` and `rho`
without checkpoints) have no sentinels. They gain because dropping a few
large-step rows narrows the residual width by a bit. Delta with exceptions is
another narrow insurance arm, like patched, worth about 0.3% of Jena bytes.

## Dictionary bodies (byte model on the real layout rules)

[`dictionary_model.py`](dictionary_model.py) prices a part-wide dictionary
(sorted, or ranked by frequency) per 65,536-row part. The dictionary is charged
as a bit-tight plane of values relative to the part minimum. Code blocks take
the cheapest of constant, packed, u8-run-end runs and patched, with
`material.cpp`'s byte rules and 16-byte descriptors. These aren't real
bodies yet.

| Column | Real bodies today | Sorted dictionary | Frequency dictionary | Dictionary share |
| --- | ---: | ---: | ---: | ---: |
| OS | 2.78 | 2.49 | **2.17** | 0.01 |
| ResolutionWidth | 3.91 | 3.41 | **3.02** | 0.04 |
| RegionID | 5.37 | 4.29 | **4.22** | 0.15 |
| URLHash | 59.11 | **51.86** | 51.86 | 36.57 |
| ConnectTiming | 2.55 | 2.54 | 2.53 | 0.09 |
| EventTime | **16.88** | 27.57 | 27.67 | 11.78 |
| UserID | **14.60** | 17.01 | 18.77 | 13.27 |
| ClientIP | **9.99** | 12.42 | 12.42 | 6.67 |
| WatchID | **62.50** | 78.50 | 78.50 | 62.00 |

All in bits/value; the last column is the dictionary's own share. **Dictionaries pay on low-cardinality columns (14–23% smaller) and on
URLHash (12%), and lose badly on high-cardinality ones**, where the
dictionary alone costs 7–62 bits/value. That supports Ashton's per-block (and
in practice per-part) opt-out: a part-wide dictionary must be optional. On OS
and ResolutionWidth, frequency ranking beats sorted codes by about 0.4 bits,
because frequent values' small codes narrow FOR widths and run values. Sorted
codes keep order for range predicates, so the choice is again a curated
option for Engine. [Real bodies](#dictionary-bodies-real) now confirm these bytes.

## Dictionary bodies, real

`encode_options::dictionary` (`none` by default, `sorted`, `frequency`;
driver `--dictionary=…`) builds what [`dictionary_model.py`](dictionary_model.py)
priced, with real bytes. It stays off by default because point/, filter/ and
execution/ don't interpret codes yet.

**Layout.** A part is 65,536 rows (256 blocks; the last part is shorter, in
whole blocks). Each part's dictionary is one SeriesPack plane in the column
payload, ahead of that part's block bodies. It holds the distinct values in
code order, relative to the part minimum, at W = max(1, width(max − min)).
Sorted codes rank values ascending. Frequency codes rank by descending count,
with ties broken by ascending value. The metadata sits on the column in a
24-byte `dictionary_part` record per part (part minimum, plane offset, count,
width; count 0 means no dictionary). `column::coded` holds one bit per block,
charged as 64-bit words. A coded block is an ordinary block of any existing
kind built over its codes, with descriptor, layout and body unchanged. Only
the interpretation changes: row value = part.min + D[stored]. Both records and
bits count toward `column::total_bytes()`. The JSON reports
`dictionary_bytes`, `part_record_bytes`, `coded_bit_bytes`, `coded_blocks`,
`dictionary_parts` and `coded_kinds`.

**Choice.** The encoder runs the existing per-block encoder (`emit_block`,
factored out unchanged) over each block's values and over its codes. A block
is coded only when its code body is strictly smaller, which is Ashton's
per-block opt-out. A part also drops its dictionary unless the plane plus
min(value, code) per block beats values alone. The per-block choice ignores
the plane's cost, so without this rule a high-cardinality part codes every
block and loses overall. `--dictionary-every-part` keeps every dictionary,
as the model assumed.

**Laws** (comments in [`material.h`](material.h)):

- SUM = 256·m + Σ D[code]. Constant is 256·(m + D[base]). Runs take one
  lookup per run. Other kinds decode the codes, then look up once per row.
  The mask arms (`encoded_sum`, narrow, catalogue) decode the current part's
  dictionary once into a reused array. That beat a plane point read per
  lookup everywhere tried: OS 1,248 → 642 µs, URLHash 3,930 → 2,408 µs, and
  equal or better on URLHash's sparse-10% mask.
- COUNT(u ≥ t), **sorted**: D ascends, so u ≥ t iff code ≥ ct = lower_bound(D,
  t − m). The result is 0 when t ≤ m, and no rows when ct = count. The code
  block's ordinary COUNT at ct is the answer, with every whole-block shortcut
  intact. The catalogue arm runs the plane filter at ct on coded packed
  blocks.
- COUNT(u ≥ t), **frequency**: codes lose order, so the arm builds a truth
  table of one bit per code (m + D[k] ≥ t). The count is Σ T[code]: one
  lookup per run, or the whole constant block.
- Both COUNT translations are built lazily, once per part per call, on the
  first coded block visited. An empty selection still reads no column state,
  which Codex's protected-owner check enforces: an eager first version
  faulted on it. A lone `block_count_ge` translates on each call.
- `decode_block` decodes the stored codes and maps them through D.

All of these checks are exact on every run: SUM, narrow and catalogue SUM/COUNT
against the plain control, and decode_block against the input. That covers the
11 ClickBench columns under none/sorted/frequency/every-part (sorted
every-part with `--delta-exceptions` included), four columns with
frequency, every-part, delta exceptions and compact runs, and Jena window 0
with packed checkpoints. Coded runs, packed, patched, delta and delta_patched
blocks all occur. Coded constant blocks can't: a constant block has no body,
so its codes never cost strictly less. The masked access check passes.

Bits/value on the 3M-row ClickBench slice (46 parts, 11,718 blocks; records
and bits add 0.007):

| Column | none | sorted | model | frequency | model | frequency, every part | coded blocks s / f | parts with a dictionary | dictionary share (s) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OS | 2.78 | 2.50 | 2.49 | **2.17** | 2.17 | 2.17 | 11,537 / 11,529 | 46 | 0.01 |
| ResolutionWidth | 3.91 | 3.41 | 3.41 | **3.02** | 3.02 | 3.02 | 10,705 / 10,705 | 44 | 0.04 |
| RegionID | 5.37 | 4.30 | 4.29 | **4.22** | 4.22 | 4.22 | 11,532 / 11,362 | 46 | 0.14 |
| URLHash | 59.11 | **50.89** | 51.86 | **50.89** | 51.86 | 51.86 | 7,618 / 7,618 | 30 | 18.84 |
| ConnectTiming | 2.56 | 2.54 | 2.54 | 2.53 | 2.53 | 2.53 | 2,313 / 2,492 | 18 | 0.07 |
| EventTime | 16.88 | 16.89 | 27.57 | 16.89 | 27.67 | 27.64 | 0 | 0 | 0 |
| UserID | 14.60 | 14.60 | 17.01 | 14.61 | 18.77 | 18.77 | 250 / 0 | 1 / 0 | 0.08 |
| ClientIP | 9.99 | 10.00 | 12.42 | 10.00 | 12.42 | 12.43 | 0 | 0 | 0 |
| WatchID | 62.50 | 62.51 | 78.50 | 62.51 | 78.50 | 78.51 | 0 | 0 | 0 |
| IsRefresh | 0.94 | 0.94 | 0.94 | 0.94 | 0.94 | 0.94 | 0 | 0 | 0 |
| CounterID | 0.50 | 0.51 | 0.50 | 0.51 | 0.50 | 0.51 | 11 / 2 | 3 / 1 | 0.00 |

1. **The model was right to within 0.01 bits** wherever every part keeps its
   dictionary. Real bytes add only the 24-byte records and the coded bits.
2. **Per-part opt-out makes the option safe to enable.** With it, no column
   loses more than 0.01 bits (records and bits) against none. EventTime,
   ClientIP and WatchID drop every dictionary rather than lose 2.4–16 bits.
   URLHash drops 16 of 46 parts and lands 1 bit below the model, at 50.89:
   in those 16 parts too few hashes repeat to repay the plane.
3. Frequency is never worse than sorted here, and it is 0.33–0.39 bits better
   on OS and ResolutionWidth. Sorted codes keep the code-threshold COUNT.

Jena window 0, packed checkpoints, sorted: 138.90 → 137.49 bits/row over 14
fields (−1.0%). Three fields keep a dictionary: rh 12.08 → 11.52, wd 15.97 →
15.33, and max.wv 10.57 → 10.28. The other 11 drop it and stay within 0.01
bits.

Full-scan medians in µs per 3M rows, local OrbStack VM, 21 paired rotated
trials (noisy and indicative only; the plain SUM control alone moved up to 1.7×
between runs):

| Column | SUM encoded / narrow / catalogue: none | sorted | frequency | plain |
| --- | --- | --- | --- | ---: |
| OS | 636 / 635 / 630 | 621 / 640 / 614 | 838 / 855 / 832 | 185 |
| ResolutionWidth | 761 / 757 / 780 | 708 / 719 / 701 | 869 / 896 / 876 | 178 |
| RegionID | 887 / 882 / 882 | 830 / 850 / 839 | 857 / 865 / 861 | 257 |
| URLHash | 1,588 / 1,636 / 1,579 | 2,408 / 2,408 / 2,422 | 2,386 / 2,420 / 2,404 | 1,124 |

| Column | COUNT encoded / catalogue: none | sorted | frequency | plain |
| --- | --- | --- | --- | ---: |
| OS | 1,255 / 1,261 | 1,292 / 1,299 | 1,677 / 1,718 | 462 |
| ResolutionWidth | 1,911 / 1,900 | 1,857 / 1,852 | 2,086 / 2,103 | 444 |
| RegionID | 2,299 / 2,295 | 2,358 / 2,355 | 3,346 / 3,387 | 557 |
| URLHash | 2,167 / 2,165 | 1,759 / 1,620 | 3,984 / 4,014 | 690 |

- **On low-cardinality columns, sorted codes cost no speed.** SUM takes one
  array lookup per run, and the code-threshold COUNT reuses every shortcut.
- **Frequency costs speed where it saves bytes.** On OS it turns 2,500 runs
  blocks into patched code blocks, which decode and then look up per row. Its
  COUNT reads a truth bit per row, so RegionID COUNT is 1.45× slower than
  sorted.
- **URLHash SUM is 1.5× slower coded** (a per-row gather from a ~40k-entry
  array). COUNT is faster with sorted codes (2,165 → 1,620 catalogue),
  because 16-bit codes filter faster than 64-bit values.

**Consumer obligations** before the default can change: point reads,
filters and execution must check the block's coded bit and, for a coded block,
map stored values through the part's dictionary. A selected coded block reads
its part's dictionary plane, which lies outside the block's own body: an
access-shape fact for masked placement. Sorted dictionaries make value
predicates code predicates via lower_bound. Equality becomes a binary search.
Range bounds follow as above. Frequency dictionaries need a per-part truth
table or a value lookup. Execution's dictionary editions are now real part
dictionaries, but codes from different parts are not comparable: grouping
across parts must go through values, or a cross-part code map.

## Filter heads by transform cut, and window bounds

[`head_cuts.py`](head_cuts.py) models an 8-bit head under three cuts: early
(top byte of the u64 working value), part-late (value − part minimum at the
part's width) and block-late (value − block minimum at the block's width). It
reports head entropy and the fraction of 16-row windows in which every row is
decided by its head alone for `x ≥ median`. Beside them are 16-row window
min/max bounds, exact or quantised to 8 bits against the block range. Model
only; Ikea LEAD owns the real [filter consumer](../filter/README.md). A window
counts as head-resolved when no row needs its suffix: every head differs from
the threshold's head, the head is the whole value (width ≤ 8, as on OS after
frequency of small widths), or the base alone decides (q ≤ minimum). Codex
corrected the first version, which undercounted whole-value heads; tie windows
are now reported separately in the JSON.

| Column | Head entropy early / part / block | Windows head-resolved early / part / block | Windows settled by bounds, exact / 8-bit |
| --- | --- | --- | --- |
| OS | 0.0 / 1.9 / 1.5 | 0.00 / 1.00 / 1.00 (head is the whole value) | 0.43 / 0.43 |
| ResolutionWidth | 0.0 / 3.1 / 2.3 | 0.00 / 0.44 / 0.45 | 0.48 / 0.36 |
| RegionID | 0.0 / 1.0 / 1.3 | 0.00 / 0.03 / 0.42 | 0.43 / 0.31 |
| EventTime | 0.0 / 6.9 / 5.4 | 0.00 / 0.94 / 0.97 | 0.55 / 0.55 |
| ClientIP | 0.9 / 7.6 / 4.2 | 0.12 / 0.97 / 0.97 | 0.40 / 0.39 |
| URLHash | 7.9 / 7.7 / 6.5 | 0.96 / 0.96 / 0.96 | 0.01 / 0.01 |
| WatchID | 6.0 / 8.0 / 7.2 | 0.78 / 0.94 / 0.94 | 0.00 / 0.00 |
| Jena p | 0.0 / 6.8 / 6.5 | 0.00 / 0.95 / 0.98 | 0.96 / 0.96 |
| Jena T | 0.8 / 6.8 / 6.6 | 0.22 / 0.93 / 0.97 | 0.94 / 0.94 |
| Jena wv | 0.0 / 6.1 / 6.5 | 0.00 / 0.74 / 0.88 | 0.45 / 0.45 |

1. **Early heads are useless** except on full-width columns (hashes, WatchID,
   UserID). Most working values share their top byte.
2. **Part-late heads resolve nearly as many windows as block-late heads on
   wide columns** (EventTime 0.94 vs 0.97, ClientIP equal, Jena 0.93–0.95 vs
   0.97–0.98), with one interpretation per part. RegionID is the
   counterexample (0.03 vs 0.42): a sparse 17-bit domain spreads the part's
   range, so part-late heads are coarse there.
3. **Heads and window bounds answer different columns.** Per-row heads decide
   high-entropy columns that bounds can't touch (URLHash 0.96 vs 0.01;
   WatchID 0.94 vs 0.00). Bounds decide smooth series (Jena p, T at 0.94–0.96)
   at about 1 bit/value instead of a head's 8.
4. **8-bit quantised bounds match exact bounds** on Jena and the wide
   ClickBench columns, and lose 0.12 on the narrow ones (ResolutionWidth,
   RegionID).

## Shared plane catalogue (first cut)

[`kernels.h`](kernels.h) / [`kernels.cpp`](kernels.cpp) hold one entry per width
1–64: point, range, a native packet SUM (SeriesPack composition read plus native
sum, with no widened scratch) and COUNT(≥ rhs) through SeriesPack filters over
64-row windows. An operation derives each plane from the column payload and the
descriptor alone, which is what removes per-block decoder bindings (Ikea LEAD's
point work found those bindings cost ~240 B per block). The owner operations
now read through it too (see
[Owner operations on the shared catalogue](#owner-operations-on-the-shared-catalogue)).
`catalogue_sum` and `catalogue_count_ge` remain separate arms: they differ only
in taking native packet SUM and plane filters on packed blocks, and are exact
everywhere.

Same image, local VM, medians in µs per 3M-row (ClickBench) or 65,536-row
(Jena) full scan:

| Column (packed share) | SUM: decoder / narrow / catalogue / plain | COUNT: decoder / catalogue / plain |
| --- | --- | --- |
| EventTime (80%) | 992 / 721 / 828 / 373 | 894 / 665 / 617 |
| WatchID (100%) | 1,981 / 2,090 / 2,012 / 1,181 | 1,246 / 1,609 / 788 |
| URLHash (75%) | 1,589 / 1,664 / 1,621 / 1,210 | 2,279 / 2,222 / 771 |
| Jena wv (63%) | 18 / 12 / 13 / 4 | 19 / 14 / 9 |
| Jena p (1%; 98% delta) | 32 / 32 / 33 / 4 | 52 / 52 / 9 |

1. **Catalogue SUM matches narrow decoding but doesn't beat it.** Native packet
   reduction isn't faster than decoding into narrow lanes and summing on this
   VM. Its value is memory: no per-block binding objects.
2. **Catalogue COUNT helps where packed blocks dominate** (EventTime 894 → 665,
   close to plain's 617; Jena wv 19 → 14), and loses on WatchID (1,246 →
   1,609). That suggests the 64-row filter windows cost more than decode +
   compare at K ≈ 62.
3. **The big remaining gap is prediction and runs, not packed blocks.** Jena
   pressure is 98% delta and stays 7–8× slower than plain for both SUM and
   COUNT. ClickBench's run-heavy columns stay 3× slower. The next speed arms
   are a native weighted delta SUM and run-weighted COUNT (which Codex already
   fused in filter/) inside the owner operations.

### Narrow weighted delta SUM

The owner delta SUM now decodes residuals into their narrowest lane (u8, u16
or u32) and weights them there (`weighted_residuals`), instead of decoding into
u64. On Jena window 0 (packed checkpoints, local VM medians per 65,536-row
scan), delta-heavy fields fell from 28–32 µs to 11–15 µs, a 2.2–2.7× gain.
Fields that are mostly FOR are unchanged at 17–20 µs. The plain int16
control is about 4 µs, so prediction SUM is still about 3× behind plain.
Exact on every field.

## Owner operations on the shared catalogue

Every owner operation now derives its planes from `c.payload.data() + offset`
plus descriptor or body metadata through `plane_catalogue`. That covers block
and mask SUM, narrow SUM, COUNT, `decode_block` and their dictionary variants
over every kind and coded blocks. Algorithms and exactness are unchanged: only
where each plane comes from moved. Codex's access fixture now places bodies
inside `column::payload`, which made this possible. Code at 4fc19831.

**Catalogue entries and read grants** (documented in [`kernels.h`](kernels.h)):

| Entry | Lanes | Grant | Owner uses |
| --- | --- | --- | --- |
| `point` | u64 | exact tail | dictionary lookups (block SUM, sorted COUNT's lower_bound, decode) |
| `range` | u64 | exact tail, arbitrary edges | full residual planes; runs' `d.count` values; exception values (n at the stored W); dictionary planes |
| `range8/16/32` (new) | u8/u16/u32, null when K exceeds the lane | exact tail | packed narrow SUM; delta weighted SUM |
| `sum` | native packets | full window: ceil(take/16)·16 occupied rows | `catalogue_sum` on packed blocks only |
| `count_ge` | filters, 64-row windows | exact tail | `catalogue_count_ge` on packed blocks only |

Exact tail reads only the occupied 8-row tiles of the requested rows (SeriesPack
dense range: 16-row native windows only inside the requested count, then scalar
points within a tile). So it is the path for short planes: run values,
exception planes, dictionaries and packed checkpoints. No owner operation uses
the full-window `sum` on a short plane. Owner scans don't read checkpoints;
the point read `stored_at` does (see [Borrowed blocks](#borrowed-blocks)).

**`encode_options::bind_decoders`** (default true; driver `--no-bindings`) keeps
`column::decoders`, `d8`, `d16`, `d32`, `exceptions` and `dictionaries` for the
point/, filter/ and execution/ consumers that still read them. When false,
encode builds none of them. The protected-payload check passes, and every driver
run passes with and without bindings. `sizeof(column)` is 256 B either way. The
bindings take 232 B per block by sizeof (40 B decoder plus four 48 B optionals);
encode grows four of the vectors without reserving, so the capacity is
308 B per block on 11,718 blocks:

| 3M-row column | Payload | Retained, bindings | Retained, `--no-bindings` | Binding share |
| --- | ---: | ---: | ---: | ---: |
| OS | 0.86 MB | 4.85 MB | 1.24 MB | 75% |
| OS, sorted dictionary | 0.75 MB | 5.30 MB | 1.68 MB | 68% |
| RegionID | 1.83 MB | 6.12 MB | 2.50 MB | 59% |
| EventTime | 6.14 MB | 10.82 MB | 7.21 MB | 33% |
| URLHash | 21.98 MB | 29.69 MB | 26.08 MB | 12% |
| WatchID | 23.25 MB | 36.38 MB | 32.77 MB | 10% |

Retained bytes are the column object plus every vector's capacity. Payload
capacity also carries growth slack (WatchID 32.8 MB against 23.2 MB used),
which is a separate trim. Image bytes and bits/value don't change.

**Timings.** The binaries were built from f760ca39 (before) and 4fc19831
(after). Every configuration ran before, after, then after with
`--no-bindings`, back to back, all-blocks mask, local VM medians in µs:

| Column, options | encoded SUM | narrow SUM | encoded COUNT |
| --- | --- | --- | --- |
| OS | 531 / 521 / 554 | 535 / 532 / 571 | 421 / 418 / 451 |
| EventTime | 943 / 908 / 894 | 687 / 656 / 641 | 682 / 681 / 685 |
| URLHash | 1,416 / 1,442 / 1,453 | 1,464 / 1,478 / 1,434 | 810 / 836 / 812 |
| WatchID | 1,954 / 1,898 / 1,914 | 1,970 / 1,936 / 1,940 | 1,153 / 1,139 / 1,142 |
| URLHash, sorted | 2,164 / 2,080 / 2,085 | 2,082 / 2,078 / 2,058 | 877 / 854 / 857 |
| OS, frequency | 761 / 815 / 804 | 771 / 831 / 815 | 900 / 972 / 963 |
| Jena window 0, 14 fields, packed + delta exceptions (sum of medians) | 330 / 329 / 332 | 304 / 301 / 304 | 621 / 617 / 623 |
| Jena window 0, none | 182 / 186 / 187 | 170 / 173 / 174 | 679 / 675 / 673 |

Across all 117 configurations (11 ClickBench columns × 3 dictionary modes,
plus 14 Jena fields × 2 inputs × 3 option sets), the geometric-mean
after/before ratio is 0.997–1.007 for every owner and catalogue arm. With
`--no-bindings` it is 1.000–1.012. Single cells move up to ±10% both ways
(OS frequency +7%, UserID COUNT −30% from one slow before-run). That is
within this VM's noise, since local timings are noisy (see
[Measurement and input hygiene](#measurement-and-input-hygiene-workbench-and-codex-review)).
**Reading through the catalogue costs no measurable speed and drops the
bindings.** A per-call function-pointer load from a shared table replaces the
per-block decoder's.

**Caveats.**

- Owner operations now trust the body metadata as written: the exception value
  width W in patch tails and each dictionary part's width and count. A
  hand-built column whose payload and descriptors disagree used to be read
  through whatever decoder it bound. Now it is read at the descriptor's width.
- The fixtures in point/ still bind decoders and are unaffected. The defaults
  keep the vectors, so no consumer changes behaviour.
- To stop needing bindings, consumers replace `c.decoders[b]` with
  `plane_kernels(d.width)` over `body_of(c, b)` (or `read_plane`). They replace
  `c.exceptions[b]` with `read_exceptions(patches/delta_patches(c, b))` or a
  `point` at `p.value_width`, and `c.dictionaries[part]` with
  `dictionary_at`/`read_dictionary`. Point/'s `compact_image` already
  releases the vectors. filter/image.cpp, filter/measure.cpp and
  execution/encoded.cpp still read `decoders` directly.

## Borrowed blocks

Engine's [material witness](../execution/MATERIAL.md#the-smallest-useful-encoding-owner-capability)
needs to read a block whose bytes live in another allocation, such as an Orbital
Part. Every owner operation in [`material.h`](material.h) now has one body over
`borrowed_block`: a descriptor (its offset is ignored), a body pointer and grant,
the image's checkpoint option, the original-coordinate row count (256 for every
kind) and, when coded, a `dictionary_view`. Column operations are thin wrappers
through `borrow(c, b)`, so the code path is shared and codecs aren't cloned. The
operations are point (`stored_at`, `value_at`), `decode_stored`/`decode_block`,
exact u128 stored and logical SUM, narrow SUM, COUNT(≥ t) and the catalogue arms.
Stored-domain operations read values, or codes if coded, and never the
dictionary. Logical operations map codes through it. Code at 39810ad0.

**Extents** are reported by the owner (`extent_of`), never inferred from the next
offset. K is the descriptor width, n and W the patch tail's count and value
width, and CP is `checkpoint_cost(mode, K)`:

| Kind | Body bytes | Scan grant (decode, SUM, COUNT) |
| --- | --- | --- |
| constant | 0 | 0 |
| runs | `plane_bytes(K, count)` + count u8 ends | body |
| packed | 32K | body |
| delta | 32K + 8 (rbase) + CP | 32K + 8: checkpoints serve points only |
| patched | 32K + 10 (lo, n, W) + n + `plane_bytes(W, n)` | body |
| delta_patched | 32K + 8 + CP + 10 + n + `plane_bytes(W, n)` | body |
| coded block | its kind's body over codes; dictionary `plane_bytes(width, count)` separately | same |

CP is 15 × 4 or 15 × 8 (native), `plane_bytes(K + 8, 15)` (packed, else 15 × 8),
3 marks at native width (every64) or 0. `delta_exceptions` doesn't enter: it only
decides whether delta_patched occurs. The variable tails depend on bytes in the
body. A bounded staged parse proves the fixed prefix fits the span, reads n and W
from its last two bytes (n ≥ 1, 1 ≤ W ≤ 64), and then proves the positions and
tile-rounded exception plane fit. `admits` also checks ascending positions, and
the dictionary's width, count, order and plane against its own grant. On every
block of every input below, the extent equals the gap to the next body, or to the
next part's dictionary plane. Each dictionary ends exactly where its part's first
body begins, so all payload bytes are accounted for. Nothing in a body depends on
side material elsewhere in the payload.

**Grant finding.** `read_grant_bytes` equals the body extent for every kind and
operation. Nothing reads past it or before the body start, including every
point, and no padding is needed. Short planes (run values, exception values,
packed checkpoints, dictionaries) are read exact tail. Full-window reads only
touch full 256-row planes. The extent is sufficient but not always necessary.
Local tiles store a row's body bytes row by row, so a partially occupied final
tile leaves its unoccupied rows' bytes unread. With the last byte withheld, 7–22%
of sampled patched, delta_patched and packed-checkpoint delta bodies still read
correctly, as did 204 of 527 dictionaries. Delta checkpoints are read only by
point reads, hence the separate scan grant. No alignment is required: current
bodies start at any byte, and every start residue mod 8 was exercised (aarch64).

**What a consumer constructs.** Engine or a point consumer retains, pointer-free,
the 16-byte descriptor, the checkpoint option and the row count. For a coded block
it also retains the part's `min`, `count`, `width`, order (sorted or frequency) and
a `dictionary_identity {scope, edition}`. At attach time it resolves its own spans
into `body`/`grant` and `dictionary.plane`/`grant`, calls `admits` once, then
calls the operations. The view owns nothing. A coded block must be read with the
dictionary it was coded against. `code_sum`, `code_count` and `code_count_eq`
cache a decoded dictionary, translated threshold or code per identity within
a call; a [`dictionary_cache`](#dictionary-caches-across-calls) keeps them
across calls under the same identity premise. A part
index alone was not enough: one cache shared across `borrow(old)`, `borrow(new)`,
`borrow(old)` returned stale answers when both images had part 0 (Ikea LEAD's
case). `borrow` now uses `{column::image, part}`, where `image` is unique per
`encode()` and never reused, so an unchanged copy keeps its identity. Other
owners supply their own scope, so a retained unchanged dictionary can keep its
identity and a changed one takes a new edition. The caller guarantees that one
identity names one content while any cache keyed on it lives; no global
registry is needed. `borrowed_check` shares each cache across old, new (values
mirrored within the column's range), old, copy and new images: 270 checks
across 417 runs. A negative control that aliases the two image ids goes stale
in every run that has a coded block, so the check can see a collision. The
column object grows by 8 bytes; nothing else in the driver changes. Coarse
masks skip a block before any body or header parse. A selected coded block reads
only its own part's dictionary; others can stay unreadable.

**Verification.** `frame_encoding_borrowed_check NAME FILE.u64 [options]` encodes
a column and copies every block's body into an mmap region. It runs four
placements: the grant ending at a `PROT_NONE` page, starting right after one,
and starting 1 or 3 bytes after one. The coded block's dictionary plane alone is
copied the same way. Every borrowed operation is compared with the input
values: every point (sampled in the unaligned placements), decode, SUM, narrow
SUM, catalogue SUM and COUNT at block and column thresholds. For coded blocks,
codes are derived independently from the input by the sorted and frequency
laws. Column wrappers must equal the borrowed results, and mask aggregates must
equal the input.

Injected faults run in forked children, eight blocks per class and
configuration. A body starting one byte inside a guard must fault. So must a
body ending one byte short when it ends in a full plane, run ends or fixed delta
fields. Per-operation fault outcomes are recorded.

Inputs were OS, RegionID, URLHash, EventTime, WatchID, UserID and all 28 Jena
window-0 and fault files. The nine option sets were default, sorted, frequency,
packed + delta exceptions, packed + delta exceptions + frequency, none,
none + delta exceptions, every64, and every64 + delta exceptions + sorted.
There were 306 runs and 665,154 blocks (125,905 coded), giving 2.66M borrowed
block checks, 14,241 scan-grant checks and 7,996 injected faults; all pass. The
script is `build/spikes/ikea-frame/borrowed/check.sh`. The rebuilt
`frame_encoding_bodies` matches the frozen `catalogue-owner/bodies_after` on
all 11,232 non-timing values. That covers all 117 run.sh configurations, with and
without bindings (`borrowed/compare.sh`). `frame_encoding_masked_access_check`
passes.

**Timings.** Paired rotated runs compared the frozen `bodies_after` with the new
driver, `--no-bindings`, over 15 rounds. Local VM medians are in µs, all-block
mask, old / new:

| Column | encoded SUM | narrow SUM | encoded COUNT |
| --- | --- | --- | --- |
| OS | 535 / 530 | 543 / 544 | 429 / 435 |
| EventTime | 880 / 883 | 637 / 631 | 654 / 657 |
| URLHash | 1,412 / 1,432 | 1,426 / 1,422 | 804 / 825 |
| WatchID | 1,957 / 1,963 | 1,994 / 1,981 | 1,164 / 1,165 |
| Jena window 0, packed + delta exceptions (14 fields, sum) | 319 / 319 | 292 / 292 | 599 / 600 |

Geometric-mean new/old ratios over all rounds and inputs were 1.001 for SUM,
0.997 for narrow SUM, 1.011 for COUNT, 1.009 for catalogue SUM and 1.002 for
catalogue COUNT. On the sparse-10% mask they were 1.008, 1.002, 1.026, 1.011 and
1.021. SUM is unchanged. COUNT is 1–3% slower, consistently but within this VM's
single-cell noise. The out-of-line `stored_count_ge` now takes a materialized
view instead of `(c, b)`; `borrow` no longer zero-fills an uncoded view's
dictionary. A cleaner measurement belongs on a quiet worker.

**Remaining.** The column wrappers still go through `column`. A borrowed mask
aggregate needs only a block-visit adapter over Engine's own selection, because
`code_sum`/`code_count` already key on the edition. The consumers in point/,
filter/ and execution/ are unchanged and still compile. They can move to
`borrowed_block` as their owners choose. Engine's witness can use `admits` plus
these operations for any kind, rather than its direct packed-FOR catalogue reads.

## COUNT over non-decreasing delta blocks

Residuals are non-negative, so a delta block with rbase >= 0 is
non-decreasing. Its COUNT(x >= t) is 256 minus the first row that reaches t.
`stored_count_ge` finds that row by binary search over the checkpoint rows,
each probe being one checkpoint plus one residual, then scans one checkpoint
interval. With no checkpoints it keeps the full scan. Runs gain the bound
shortcut packed already had: a threshold above every representable run value
returns 0 without decoding. As a result, COUNT on such a delta block reads its
checkpoints, so its scan grant now covers only decode and SUM;
`borrowed_check` exercises scan-grant COUNT only for rbase < 0. It also gained
thresholds either side of each checkpoint spacing.

EventTime, COUNT(x >= median), local VM, medians in µs over 3M rows. The
"before" column is the frozen pre-change driver. Polars is single-threaded
`(s >= t).sum()` from Python on the same order, so this is indicative only.

| Order (dominant kinds) | Before | After | Polars |
| --- | ---: | ---: | ---: |
| Original (80% packed) | 639 | 638 | 995 |
| By region, OS, time (73% delta) | 2,456 | 559 | 856 |
| Sorted (all runs) | 205 | 39 | 704 |

1. **Clustered time columns were COUNT's worst case, and are now its best
   delta case.** Decoding the prefix of every delta block made the
   time-clustered order 3.8× slower than the original and slower than Polars.
   Now it is 4.4× faster than before and 1.5× faster than Polars.
2. **Original-order EventTime is mostly packed, so it is unchanged.** Its
   COUNT stays near the cost of scanning the plane. A 2× lead there needs
   block evidence, such as Ikea LEAD's window bounds, not a codec change.
3. **Equivalence holds.** The full guard-page check passes (306 runs). Across
   all 117 driver configurations with and without bindings, the 10,296
   non-timing values match the frozen driver. Paired timing on OS, EventTime,
   URLHash, WatchID and Jena (packed and native checkpoints) puts the SUM and
   COUNT geomeans within 1.006 of the old code. Those inputs have few
   non-decreasing delta blocks in original order, so the search rarely runs.

## Arrival-order dictionaries

`encode_options::dictionary = arrival` (driver `--dictionary=arrival`) makes the
[recent-value byte model](#recent-value-references-byte-model) real, while
keeping the part-wide dictionary plane. The option is off by default, and
`bind_decoders` defaults are unchanged. Code at 012e2ffd.

**Layout.** The part dictionary is the same min-relative SeriesPack plane, with
the same record, edition and drop rules. Its entries are in order of first
occurrence within the part, so a borrowed block still depends on nothing outside
its body except its part's dictionary edition. The new `kind::arrival` exists
only in blocks coded against such a dictionary. The descriptor's base is the
block's frontier F, the number of entries introduced before the block, and its
count is the number of reference rows. The body is a 256-bit flag plane, with
1 marking a first occurrence, then one exact-tail plane of the references at the
descriptor's width. With F_i the frontier at row i, a flagged row's code is F_i,
and any other row stores F_i − 1 − code. The extent is 32 +
`plane_bytes(K, count)`, from the descriptor alone, so no staged parse is
needed. `admits` also requires an arrival-order coded view, popcount(flags) +
count = 256, and F + popcount ≤ the dictionary count.

**Choice and operations.** Each block's codes still take the cheapest existing
kind, and the arrival body replaces it only when strictly smaller. Per-block and
per-part opt-outs are unchanged.

- A point read takes the rank over the flags, at most one reference point read,
  then one dictionary point read.
- Decode enumerates flag bits: literal rows take F, F + 1, …, and references are
  visited in row order with a sequential counter. A per-row prefix loop through
  the plane measured about 8 cycles per row on aarch64: a load through a
  data-dependent pointer.
- SUM reads literal rows' entries F … F + L − 1 in sequence and gathers only
  references.
- COUNT(x ≥ t) uses the frequency path's per-edition truth table: a popcount
  over truth bits [F, F + L), plus one bit per reference. The table is now built
  a word at a time. A per-entry read-modify-write of the vector had cost about
  twice the table's decode, and one run's URLHash frequency COUNT fell from
  2,697 to 1,902 µs.
- New equality COUNT(x = v) arms (`stored_count_eq`, `block_count_eq`,
  `code_count_eq`, `encoded_count_eq`) cover every kind and order. They find
  v's code once per edition: binary search for sorted, a plane scan otherwise.
  Constant, packed and runs reject out-of-range values without decoding. An
  arrival block rejects any code at or past F + L.

Bits/row on the 3M-row ClickBench slice. "Model" is `repeat_model.py`
(lightweight kinds + references, best window per part). The ZSTD columns
compress each part's raw u64 bytes (`build/spikes/ikea-frame/repeat/`):

| Column | none | sorted | frequency | arrival | model | ZSTD-3 | ZSTD-19 | arrival blocks | parts with a dictionary |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| URLHash | 59.11 | 50.89 | 50.89 | **43.90** | 45.3 | 41.4 | 40.6 | 9,943 | 46 |
| URLHash, shuffled | 64.50 | 64.51 | 64.51 | **56.43** | 57.5 | 56.9 | 56.0 | 11,718 | 46 |
| UserID, shuffled | 64.50 | 64.51 | 64.51 | **54.43** | 55.5 | 58.7 | 54.6 | 11,718 | 46 |
| UserID | 14.60 | 14.60 | 14.61 | 14.60 | 14.6 | 13.3 | 13.1 | 210 | 3 |
| ClientIP | 9.99 | 10.00 | 10.00 | 9.99 | 9.95 | 9.8 | 9.1 | 79 | 7 |
| RegionID | 5.37 | 4.30 | 4.22 | **4.20** | 5.2 | 3.4 | 2.7 | 5 | 46 |
| OS | 2.78 | 2.50 | **2.17** | 2.24 | | | | 0 | 46 |
| ResolutionWidth | 3.91 | 3.41 | **3.02** | 3.12 | | | | 0 | 44 |
| WatchID | 62.50 | 62.51 | 62.51 | 62.51 | 62.5 | 64.0 | 64.0 | 0 | 0 |

Jena window 0 with packed checkpoints (14 fields) goes from 138.90 (none) and
137.49 (sorted) to 135.34. The gains are max.wv 10.57 → 8.58 and wd 15.97 →
14.36; rh is 12.03, against 11.52 sorted.

Paired timings use median µs over 3M rows on the local VM, 5 rounds of none /
sorted / arrival back to back in rotated order. Within each run there are 21
paired trials, and `--no-bindings` is set. "Points" is 65,536 random `value_at`
reads. Plain native-array controls: points 173–183, SUM 1,056–1,083, COUNT
567–662, equality 558–671.

| Input | points | SUM | COUNT ≥ median | COUNT = v | sparse-10% SUM | sparse COUNT ≥ | sparse COUNT = |
| --- | --- | --- | --- | --- | --- | --- | --- |
| URLHash | 1,215 / 1,641 / 2,054 | 1,408 / 2,162 / 2,814 | 809 / 891 / 2,661 | 825 / 438 / 1,015 | 202 / 515 / 717 | 96 / 129 / 984 | 126 / 66 / 635 |
| URLHash, shuffled | 877 / 897 / 1,971 | 1,354 / 1,338 / 2,765 | 767 / 769 / 2,387 | 779 / 781 / 1,180 | 202 / 196 / 781 | 82 / 86 / 1,149 | 126 / 128 / 885 |
| UserID, shuffled | 874 / 894 / 2,107 | 1,351 / 1,368 / 2,776 | 764 / 771 / 2,448 | 784 / 786 / 1,152 | 203 / 199 / 812 | 85 / 82 / 1,139 | 127 / 124 / 817 |
| UserID | 2,355 / 2,172 / 2,199 | 850 / 847 / 885 | 40 / 43 / 119 | 57 / 58 / 66 | 92 / 96 / 106 | 5 / 6 / 26 | 6 / 6 / 16 |

1. **Real arrival bodies beat the byte model.** URLHash is 43.90 bits/row,
   against 45.3 modelled and 50.89 for sorted. That is 26% below no dictionary
   and 2.5 bits above ZSTD-3. Shuffled, both hash-like columns beat ZSTD-3,
   and UserID also edges past ZSTD-19. The model's window turned far repeats
   into duplicate literals; a part dictionary stores each value once, and
   every repeat becomes a reference.
2. **Unwindowed distances aren't the problem.** URLHash's references are
   13–16 bits wide in most blocks: targets really are spread across the part.
   Narrow references plus wide exceptions (patched-style, e ∈ {2, 8, 32})
   would save 0.28 bits/row. The rest is the dictionary: 36.6 of 43.9 bits/row
   on URLHash are its literals at about 63 bits, then 6.1 for references, 1.0
   for flags and 0.5 for descriptors (`arrival/breakdown.py`). Closing the gap
   to ZSTD needs multi-row matches or entropy over the literals, not a
   different reference shape.
3. **The bytes cost scan speed, mostly in gathers and per-edition work.** On
   URLHash, SUM is 2.0× none and 1.3× sorted. In the reference loop, about 60%
   of samples sit on the dictionary load (a gather from a ~40k-entry array),
   and each part's dictionary is decoded once per call. COUNT ≥ is 3×: sorted
   keeps a code threshold, while arrival builds a truth table over every
   part's 40–50k entries and reads a bit per reference. Equality is 1.2× none,
   but 2.3× sorted, whose binary search finds the code without scanning the
   plane. Point reads are 1.3× sorted and 1.7× none (about 31 ns against
   25 and 19), and 2.3× where shuffling leaves none and sorted as plain FOR.
4. **Sparse selections pay for the whole edition.** Under a 10% mask, arrival
   COUNT ≥ is 10–14× none, equality 5–7× and SUM 3.5–4×. The decoded
   dictionary, truth table and equality code are rebuilt per call for every
   part touched. They are immutable per edition, so a consumer that caches
   them across calls (or keeps a sorted index for equality) removes most of
   this; the material here doesn't yet.
5. **Low-cardinality columns need no new kind.** They get 0–5 arrival blocks
   and land between sorted and frequency, because first occurrence gives
   frequent values small codes. RegionID's 4.20 is the best of the three.
   Where dictionaries don't pay (WatchID, EventTime, IsRefresh), every part
   drops its dictionary, and UserID and ClientIP stay within 0.01 bits.

So the byte savings survive, but at 1.3–3× the scan cost of sorted codes on
URLHash. For SUM the gather dominates. For range COUNT the cost is unordered
codes, which need a truth table. Arrival suits hash-like columns where bytes
matter more than scan speed. Sorted remains better for range predicates, and
equality-heavy hash workloads would want both an arrival body and a sorted
lookup per edition.

**Verification.** `build/spikes/ikea-frame/arrival/check.sh` runs
`borrowed/check.sh`'s nine option sets plus three arrival sets: default,
packed + delta exceptions, and every64. The arrival sets cover OS, RegionID,
URLHash, EventTime, WatchID, UserID, ClientIP, shuffled URLHash and UserID,
and Jena window 0 and fault. The oracle derives arrival codes from the input
by first occurrence, and equality is checked at present and absent values per
block and over masks. That makes 417 runs, 992,334 blocks (313,754 coded,
101,496 arrival bodies), 3.97M borrowed-block checks and 11,380 injected
faults, all passing. An arrival body's last byte was read in 169 of 191
sampled blocks; the rest end in a partially occupied reference tile.
The driver's exactness checks (decode, SUM, COUNT, equality and every random
point) pass under arrival on all 11 ClickBench columns, the two shuffled
inputs and Jena window 0, with and without bindings. Its non-arrival output
matches the frozen `catalogue-owner/bodies_after` on all 11,232 non-timing
values of the 117 configurations, with and without bindings
(`arrival/compare.sh`, `compare.py`; new keys are additions).
`frame_encoding_masked_access_check` passes.

**Consumer obligations.** point/, filter/ and execution/ don't handle
`kind::arrival`. Their switches over kinds now warn (`-Wswitch`), and they
reject dictionaries in the values domain. An arrival-coded block reads only its
own body and its part's dictionary edition, as other coded blocks do.

## Dictionary caches across calls

Most of arrival's scan cost, and nearly all of its sparse-mask cost, came from
work repeated per call: decoding each part's ~40k-entry dictionary, building
the COUNT truth table and finding the equality code. A caller-held
`dictionary_cache` in [`material.h`](material.h) keeps that material across
calls, keyed by `dictionary_identity`. Code at ed281d89. It has no public API.

**Contract and structures.** The premise is the one from
[borrowed blocks](#borrowed-blocks): one identity names one content while the
cache lives, so nothing is revalidated. The cache owns derived copies and never
points into borrowed bodies or planes. Each piece is built lazily per identity:

- **Values:** the decoded min-relative entries in code order, 8 B/entry. They
  serve SUM, point reads, truth tables and the equality index.
- **Thresholds:** COUNT(u ≥ t) keeps a truth table per (identity, t) for
  frequency and arrival, at one bit per entry. Sorted keeps only its code
  threshold. Each identity holds at most four thresholds, replaced round-robin.
- **Equality index:** open addressing over u32 codes, probed by value and
  confirmed against `values[code]`. Slots are a power of two at load ≤ 2/3
  and 4 B each, about 10 B/entry here. Sorted (value, code) pairs cost
  12 B/entry, built 6–8× slower and took ~8× longer per cache-cold lookup
  (50 against 6 µs for one lookup in each of URLHash's 46 parts;
  `build/spikes/ikea-frame/dict-cache/index_bench.cpp`). A node-based hash
  map cost ~40 B/entry.

`bytes()` counts the tables plus a fixed charge per identity. An insertion that
would pass `capacity_bytes` clears the whole cache first. Holders share
ownership of the tables they read, so a clear never invalidates a call in
progress. `code_sum`, `code_count`, `code_count_eq`, the `encoded_*` and
`catalogue_*` mask aggregates and `value_at` take an optional
`dictionary_cache *`. With null they take the per-call path.

Paired timings, local VM, in median µs over 3M rows. Each of 5 rounds ran every
dictionary back to back in rotated order, with 21 trials per run. Cold is the
per-call path; warm uses a cache built once before the timed loop and reused
across trials and masks. Under shuffling, sorted and frequency keep no
dictionary, so those rows equal none.

| Input, dictionary | bits/row | cache MB | SUM | COUNT ≥ median | COUNT = v | sparse SUM | sparse COUNT ≥ | sparse COUNT = | points |
| --- | ---: | ---: | --- | --- | --- | --- | --- | --- | --- |
| URLHash, none | 59.11 | 0 | 1,407 | 824 | 821 | 166 | 95 | 99 | 1,268 |
| URLHash, sorted | 50.89 | 15.6 | 2,161 → 2,403 | 920 → 870 | 439 → 413 | 512 → 578 | 138 → 118 | 68 → 62 | 2,215 → 3,277 |
| URLHash, frequency | 50.89 | 15.7 | 2,139 → 2,389 | 1,903 → 1,530 | 697 → 412 | 536 → 614 | 653 → 208 | 369 → 65 | 2,104 → 3,128 |
| URLHash, arrival | **43.90** | 30.9 | 2,795 → 2,415 | 2,694 → 1,937 | 873 → **288** | 677 → 736 | 960 → **252** | 631 → **57** | 2,803 → 4,850 |
| URLHash shuffled, none | 64.50 | 0 | 1,353 | 780 | 777 | 164 | 87 | 84 | 943 |
| URLHash shuffled, arrival | **56.43** | 43.6 | 2,647 → 2,174 | 2,323 → 1,313 | 960 → **71** | 794 → 819 | 1,161 → **180** | 893 → **36** | 2,984 → 4,770 |
| UserID shuffled, none | 64.50 | 0 | 1,362 | 777 | 778 | 168 | 89 | 87 | 931 |
| UserID shuffled, arrival | **54.43** | 42.6 | 2,751 → 2,389 | 2,443 → 1,465 | 958 → **177** | 817 → 913 | 1,127 → **192** | 816 → **46** | 3,132 → 4,912 |

1. **The cache pays for equality and sparse range COUNT.** Arrival's equality
   falls 3–14× on all blocks and 11–25× on the sparse mask, and it then beats
   sorted: 288 against 439 µs on URLHash, 57 against 68 sparse. A part without
   v is skipped without reading its bodies. The cold path scanned each part's
   plane to learn that, and none must scan every block (777 µs shuffled
   against 71). Sparse COUNT ≥ falls 4–6×, to 1.8× sorted on URLHash (252
   against 138 µs) from 7×. Frequency gains the same way: sparse COUNT ≥
   653 → 208, and equality 697 → 412.
2. **Full-scan range COUNT stays at 1.7–2.2× sorted.** Cached truth tables
   remove 28–43% of arrival's COUNT ≥ (URLHash 2,694 → 1,937 µs against
   sorted's 870). The rest is decoding references and reading a bit per row.
   It is the body's cost, which caching can't remove. Sorted needs no table,
   so it gains only 5–14%.
3. **SUM barely gains, and sparse SUM loses.** Arrival's all-block SUM falls
   13–18%, to 1.1× sorted's cold path on URLHash. Sparse SUM is 3–12% slower,
   and sorted and frequency are 10–15% slower warm. A cache of 7–19 MB of
   decoded entries doesn't stay resident through a scan, so warm gathers hit
   cold memory. The per-call path streams the plane into one reused 320 KB
   buffer that the gathers then hit. A 60-bit plane is nearly as large as its
   decoded entries, so that decode costs about a copy.
4. **Point reads get slower through the cache** (1.4–1.7× wherever most
   blocks are coded, including OS and RegionID). Random rows change identity on almost
   every read, so each read pays a hash lookup, which costs more than a plane
   point read. `none` controls read 0.75–1.21 on points, so point timings are
   the noisiest cells here. A consumer that resolves its part's entries once
   could index them directly (untested).
5. **Low-cardinality columns see no change.** On OS and RegionID (0.04–0.46 MB
   of cache), cached scans are within 3% of cold, except frequency and arrival
   sparse COUNT ≥ (0.87). UserID in original order keeps 3 arrival parts:
   sparse COUNT ≥ is 25 → 11 µs, and equality 16 → 6.
6. **The memory is about twice the column.** Arrival URLHash holds 18 B/entry
   (8 values, 9.9 index and 0.125 per threshold): 30.9 MB for a 16.5 MB
   column. Shuffled URLHash holds 43.6 MB for 21.2 MB. Values alone are 13.7 MB
   and 19.5 MB. The first build on URLHash arrival took 1.7 ms for values,
   0.5 ms for the median truth tables and 10 ms for the index, including first
   touch. At ~0.6 ms saved per equality call, the index repays itself after
   about 17 calls. So arrival keeps its bytes on disk and in the image, but a
   full warm cache doesn't keep them in memory. Truth tables alone cost
   0.125 B/entry per threshold, needing the values only while they are built,
   and they carry finding 2 and sparse COUNT ≥. The equality index confirms
   against the values, so equality costs the full 18 B/entry. SUM and points
   gain little from the values (findings 3 and 4).

So a warm cache brings arrival's equality below sorted's and its sparse range
COUNT to within 2×. Full-scan COUNT ≥ stays at about 2× sorted and SUM at about
1.1×, because those costs live in the arrival body itself.

**Measurement.** Ratios are per-round paired medians. The first run used the
driver's rotation, which keeps the cyclic arm order, so each arm always
followed the same predecessor. Its `none` controls, where cold and cached arms
do identical work, read 0.62–1.52. The driver now shuffles the arm order per
trial with a fixed seed, and the controls read 0.95–1.01 on every scan. The
null-cache path was checked against the frozen pre-cache driver (d2a81e80)
with `--cold-only`, which uses the old arm set and rotation. Over 5 rounds on
URLHash, RegionID and OS under all four dictionaries, the geometric-mean
new/old ratio was 0.990, and every arm fell between 0.978 and 0.998
(`dict-cache/regress.sh`, `summarise.py --regress`). Scripts and results are in
`build/spikes/ikea-frame/dict-cache/`. Local VM numbers are indicative only.

**Verification.** `borrowed_check` now shares one cache across the
old, new, old, copy, new sequence and across every block coded in both images.
Each step checks SUM, COUNT at the median, the catalogue COUNT, equality at a
present and an absent value, and cached point reads. Every eighth block also
checks six thresholds, twice over, so slots are replaced. A capacity-1 cache,
which clears on every insertion, runs on every 64th block. Mask aggregates go
through the shared cache over all five images on the all and sparse masks. All
answers are exact against the input. Aliasing the two image ids makes the
cached path go stale too, in every one of the 54 runs with a block coded in
both images. `identity/check.sh` passes 417 runs (1.59M cached checks). The
driver's non-timing output matches the frozen `catalogue-owner/bodies_after`
on all 11,232 values. The only differences are the known +8 B in
`column_object_bytes` and `retained_bytes`; the cache's keys are additions.
`frame_encoding_masked_access_check` passes. The point/, filter/ and
execution/ targets compile unchanged.

## Wide exact SUM by 32-bit halves

Ikea LEAD's control law: x = lo32(x) + 2^32·hi32(x), with each half summed in
u64. That keeps the exact u128 SUM without a u128 add per row. `split_sum`
replaces the per-row u128 loop for packed blocks wider than 56 bits, and the
prefix sums that delta_patched subtracts for its exceptions. Weighted delta
SUM keeps its u64 × u64 → u128 multiply-accumulate. On 48–56-bit delta blocks
the split form was 1.21× slower and a prefix-sum form 1.37× slower (paired
A/B, aarch64), so the law pays only for unweighted sums. Synthetic inputs in
`build/spikes/ikea-frame/wide/` cover the wide paths that the real columns
rarely reach: uniform 64-bit values, and 48–56-bit random walks with injected
outliers (delta and delta_patched).

Paired against ed281d89, 7 rotated rounds, local VM, medians in µs, encoded
SUM old / new (ratio):

| Input | All blocks | Sparse 10% |
| --- | ---: | ---: |
| WatchID | 2,114 / 1,473 (0.71) | 340 / 304 (0.81) |
| URLHash | 1,509 / 1,277 (0.79) | 262 / 253 (0.94) |
| UserID, shuffled | 1,477 / 1,117 (0.72) | 324 / 248 (0.72) |
| Random 64-bit | 258 / 185 (0.73) | 29 / 18 (0.61) |
| Walk with exceptions | 745 / 560 (0.75) | 79 / 60 (0.74) |
| Walk, no exceptions | 370 / 325 (0.88) | 40 / 37 (0.91) |

Narrow and catalogue SUM follow, because their wide fallback is the same
path. The plain-array control ratio is 0.96–1.02 for all blocks. WatchID's
encoded SUM still trails the exact native array (1,141 µs) because it moves
62 bits/row through a decode. Checks: the 417-run borrowed check, both
synthetic inputs under four option sets, masked_access_check, and 11,232
non-timing driver values, which are unchanged apart from the +8-byte column
object.

## Row-selected aggregates and NULL fill

Owner operations used to take only coarse masks, one bit per block. A
nullable consumer such as Ikea LEAD's lifecycle work then had to decode
every block with mixed validity, which gives up the encoded-aggregate
advantage. [`selected.h`](selected.h) adds exact operations over
`borrowed_block` under a 256-bit row selection S, which is the caller's
selection AND validity: stored and logical SUM (u128), COUNT(x ≥ t),
COUNT(x = v) and |S|. Each has column wrappers and a decode-then-mask control.
The NULL fill is an opt-in encoder option. Code: operations at 2c3bdf87, a
standalone-member guard at e83f22b1, speed at 5b286591, fill at 3c3b5824.

**Dispatch.** Column forms take a coarse block mask plus a row plane of four
u64 per block. The coarse bit is tested before any descriptor. An empty S
returns before the descriptor, body or dictionary, in the column forms and
in every block form, including the direct `code_sum`, `code_count` and
`code_count_eq` members. A full S takes the existing full-256 operation,
unchanged. Only mixed blocks take the per-kind algebra:

| Kind | SUM over S | COUNT over S | Reads |
| --- | --- | --- | --- |
| constant | base·\|S\| | \|S\| or 0 | descriptor |
| runs | Σ value·popcount(S ∩ run), each popcount a difference of two per-word prefix ranks | per-run test × popcount, branch-free; bound shortcuts as unmasked | run values, ends |
| packed | \|S\|·base + catalogue `masked_sum` (native packets under `mask16` of S; split halves above 56 bits) | catalogue `masked_count_ge`/`_eq` (64-row filters, S as active mask) | only 16-row packets or 64-row words with a selected row |
| patched | inliers S′ = S ∖ E as packed (exception slots hold 0); exceptions by position test | the same split | plane for S′, all exceptions |
| delta | \|S\|·u0 + rbase·Σ_{i∈S} i + Σ_j c_j s_j, c_j = \|{i ∈ S : i ≥ j}\| | monotone (rbase ≥ 0): popcount(S ∧ rows ≥ first); else prefix reconstruction through the last selected row | residuals through the last selected row; monotone COUNT reads checkpoints |
| delta_patched | the delta law over S′ with m(i) steps; selected exceptions add lo + v_e | decode, then mask | body |
| arrival | selected literals F + rank; references filtered in row order | per-edition truth bits for selected rows | all flag words and references |
| coded | \|S\|·min + Σ D[code] through the edition's entries | sorted: stored COUNT at the code threshold; otherwise truth bits | as the kind, plus the dictionary |

The delta law generalises the weighted SUM. Weights 256 − j become suffix
counts c_j ≤ 256, so the same narrow-lane bounds hold, and Σ_{i∈S} i = Σ_{j≥1} c_j.
Two equivalent forms are cheaper at the ends: for at most 64 selected rows,
Σ_{i∈S} S(i) as a walk of segment sums in 32-bit halves; for at most 64
unselected rows U, the full SUM minus Σ_{i∈U} x_i. A paired sweep on Jena `p`
and `T` over thresholds 0, 32, 64, 96 and 128 chose 64. Segments lose 2× at
50% NULL, where the suffix counts stay.

**Laws and grants.**

- Selected values aren't independent of unselected rows. A selected delta
  row's value depends on unselected rows' residuals, so the plane is read
  through the last selected row; c_j counts those rows. An arrival literal's
  code is F plus the flagged rows before it, and a reference's index counts
  every earlier reference. So masked arrival reads all four flag words and
  enumerates every reference, filtering by S.
- The monotone delta COUNT reads checkpoints, as the full one does. Every
  other mixed SUM and COUNT stays inside the scan grant. borrowed_check runs
  every selection's SUM on delta bodies granted only plane + rbase, and
  COUNT where rbase < 0. The complement form reads the whole residual plane,
  still without checkpoints.
- Packed and patched SUM and COUNT skip 16-row packets and 64-row words with
  no selected row. So a sparse S reads less than the extent, never more.
- The first cut's direct coded members began dictionary preparation before
  testing S. Ikea LEAD found this; e83f22b1 guards them.

**Verification.** In borrowed_check, every block of every input takes
adversarial selections: empty, full, row 0 alone, row 255 alone, rows 1 and
254, every 16th row, rows either side of each checkpoint spacing,
alternating both phases, rows [0, 100), rows [200, 256), all but row 0, all
but row 255, only and all but the exception positions. It also takes random
selections at 0.1, 1, 10, 50, 90 and 99% density, which are subsampled at
unaligned placements. Each runs every block form, the coded members and the
decode-then-mask control against the input oracle restricted to S. Column
wrappers run over a mixed row plane (empty, full, sparse, dense and
adversarial blocks), with and without the shared cache, across the
old/new/old/copy/new image sequence. For empty S, the guard places the body
and dictionary inside a `PROT_NONE` page and calls every operation and
member, per call and cached. `identity/check.sh` passes all 417 runs: 47.9M
masked checks, 396k scan-grant masked checks and 5,690 empty-selection
guards. Seeded mutations were caught: an off-by-one Σ i, a dropped literal
rank, and a member without its empty guard. The driver's 11,232 non-timing
values match the frozen `bodies_after`, apart from the known +8 B
`column_object_bytes` and `retained_bytes`. `masked_access_check` passes.

### Predictor-consistent NULL fill

`encode_nullable(values, validity, options, fill)` keeps validity beside the
column as its own plane of four u64 per block, charged at 32 B per block
(1 bit/row). Validity never enters a body, and original coordinates are
unchanged. Logical results over valid rows pass validity (AND any selection)
as S. NULL inputs are never read. For each block with NULLs, the predictor
fill encodes four candidates and keeps the smallest body:

- the valid minimum (FOR residual 0);
- the previous valid value, so the run extends;
- previous + r, with r the smallest step between adjacent valid rows (delta s = 0);
- linear interpolation across each gap, copying the nearest valid value at the edges.

Every filler lies within the block's valid [min, max], so no fill changes
the span or wraps. A previous + r candidate that would leave the span is
refused, never wrapped. That keeps the i128 delta law exact and the
rbase ≥ 0 monotone premise intact. A block with no valid row is constant 0,
and one with a single valid value is constant. A block with no NULL encodes
exactly as `encode()` would. Zero fill (NULL → 0) is the naive control.
Fills are chosen in the value domain, so with a dictionary an interpolated
filler can add an entry. The tables below use default options.

`frame_encoding_null_check` pins the unsigned boundaries:

- walks rising from 0, falling to 0, rising to and falling from UINT64_MAX,
  noisy, and in steps of 2^62 that exceed i64;
- each with 0, 1 or 9 leading and 0, 1 or 13 trailing holes plus an
  interior gap;
- all-NULL blocks, one valid row (at 0, 77 or 255), two valid values at the
  top of the range, alternating and run NULLs, sentinels, and no NULLs;
- five option sets, including sorted and arrival dictionaries.

Real columns add uniform and clustered NULLs at 0.1, 10 and 50%. Every image
must:

- keep its valid rows;
- keep fillers inside the valid span;
- give the same bytes whether NULL inputs are poisoned or zeroed;
- equal `encode()` for blocks without NULL;
- decode non-decreasing for every delta block with rbase ≥ 0;
- give exact SUM, COUNT ≥ median and COUNT = v over the valid rows (and over
  the valid rows AND a random selection) against the input.

670 synthetic images and 730 per real column pass, on OS, RegionID,
EventTime, URLHash, UserID, ClientIP and Jena `p`, `wv` and `T`. Removing
the span refusal fails at once.

Bits/row with synthetic NULLs, as predictor / zero / compacted. Every arm
includes the 1.00 bit/row validity plane. Uniform NULLs are independent per
row. Clustered NULLs form runs with a mean length of 64 rows. ClickBench uses
3M rows; Jena uses window 0. The no-NULL column has no validity plane.
**Compacted** is a byte model only: each block stores its NN valid rows,
priced with `material.cpp`'s rules for the kind chosen on those values,
plus its descriptor and the validity plane. It was calibrated exactly
against real bodies on every no-NULL block. Output is in
`build/spikes/ikea-frame/masked/bits.jsonl`.

| Uniform | No NULL | 0.1% | 1% | 10% | 50% |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS | 2.78 | 3.78 / 4.90 / 3.78 | 3.77 / 8.87 / 3.77 | 3.67 / 17.72 / 3.67 | 3.12 / 40.94 / 3.12 |
| RegionID | 5.37 | 6.37 / 7.72 / 6.37 | 6.35 / 12.48 / 6.35 | 6.14 / 21.99 / 6.12 | 4.93 / 42.09 / 4.79 |
| EventTime | 16.88 | 17.88 / 18.52 / 17.88 | 17.87 / 20.60 / 17.87 | 17.82 / 30.07 / **16.60** | 13.66 / 54.91 / **10.19** |
| URLHash | 59.11 | 60.11 / 60.13 / 60.11 | 60.06 / 60.27 / 60.05 | 58.04 / 61.37 / **55.45** | 34.62 / 53.87 / 33.01 |
| UserID | 14.60 | 15.59 / 16.27 / 15.59 | 15.53 / 19.02 / 15.53 | 14.95 / 28.04 / 14.95 | 11.64 / 42.35 / 11.63 |
| ClientIP | 9.99 | 10.99 / 12.75 / 10.99 | 10.95 / 18.81 / 10.95 | 10.54 / 28.25 / 10.54 | 8.27 / 42.52 / 8.26 |
| Jena p | 8.88 | 9.88 / 10.79 / 9.88 | 9.88 / 13.71 / 9.92 | 9.83 / 25.30 / 9.61 | 9.45 / 54.88 / **6.66** |
| Jena wv | 9.97 | 10.97 / 11.46 / 10.97 | 10.97 / 13.08 / 10.97 | 10.96 / 24.47 / **10.16** | 10.21 / 56.06 / **6.37** |

| Clustered | No NULL | 0.1% | 1% | 10% | 50% |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS | 2.78 | 3.78 / 3.81 / 3.78 | 3.76 / 4.09 / 3.76 | 3.58 / 6.30 / 3.58 | 2.73 / 8.38 / 2.73 |
| RegionID | 5.37 | 6.37 / 6.41 / 6.37 | 6.32 / 6.72 / 6.32 | 5.94 / 9.15 / 5.93 | 4.09 / 10.60 / 4.06 |
| EventTime | 16.88 | 17.87 / 17.94 / 17.86 | 17.81 / 18.86 / 17.71 | 17.23 / 26.41 / 16.35 | 12.49 / 33.65 / **9.91** |
| URLHash | 59.11 | 60.07 / 60.07 / 60.06 | 59.62 / 59.63 / 59.52 | 55.52 / 55.63 / 54.65 | 33.33 / 34.01 / 31.69 |
| UserID | 14.60 | 15.59 / 15.60 / 15.59 | 15.46 / 15.58 / 15.46 | 14.36 / 15.34 / 14.36 | 9.18 / 11.39 / 9.18 |
| ClientIP | 9.99 | 10.98 / 11.02 / 10.98 | 10.89 / 11.23 / 10.89 | 10.16 / 12.80 / 10.16 | 6.67 / 11.61 / 6.66 |
| Jena p | 8.88 | 9.87 / 10.03 / 9.87 | 9.87 / 10.40 / 9.85 | 9.72 / 19.18 / 9.46 | 8.27 / 34.75 / **6.60** |
| Jena wv | 9.97 | 10.97 / 11.15 / 10.96 | 10.96 / 11.50 / 10.92 | 10.78 / 20.45 / 10.19 | 8.94 / 36.63 / **6.48** |

1. **Predictor fill costs the bodies nothing, and the validity plane is the
   overhead.** At 0.1–1% every column grows by 0.93–1.00 bits/row, which is
   the plane. NULL rows then lower body bytes. At 50%, previous-value fills
   turn half the rows into run extensions, so five of the eight columns
   become smaller than without NULLs. URLHash drops from 59.1 to 34.6, close to its modelled
   compaction at 33.0.
2. **Zero fill is the failure the predictor avoids.** Against predictor fill
   it costs 0.2–7.9 bits/row more at 1% uniform (least on URLHash, already
   59 bits wide) and 3.3–17.7 at 10%, because a 0 widens every FOR block and
   breaks runs. At 50% it reaches 41–56 bits/row. Clustered NULLs touch few
   blocks, so zero fill stays close at 0.1–1%.
3. **Compaction would pay in packed- and delta-heavy blocks with many NULLs.**
   This is an observation from the model, not a contract. At 50%, it saves
   3.5 bits/row on EventTime, 2.8 on `p` and 3.8 on `wv` over predictor
   fill. At 10% uniform it saves 1.2 on EventTime, 2.6 on URLHash and 0.8 on
   `wv`. On the run-heavy OS, RegionID, UserID and ClientIP it saves at most
   0.14 at any density. At ≤ 1% it saves at most 0.10 on any column, and
   Jena `p` is 0.04 worse, because checkpoints aren't scaled to NN. The gain is where a
   filler occupies a residual slot that no predictor makes free: a packed
   plane slot at width K, or a delta row. NN bodies would need rank-based
   point reads and would change every kind, so they stay modelled.
4. **At low NULL density the plane is the remaining lever.** At 0.1% uniform,
   2,628 of 11,718 blocks are mixed, yet every block pays 32 B. An all-valid
   flag per block, or a position list, would remove most of that 1 bit/row
   (untested).

Timings are local VM medians in µs, as masked / decode-then-mask. Each run
times 21 trials of all ten arms in a fixed-seed shuffled order. Arms are
masked SUM and COUNT ≥ median, a duplicate of each, the control, the no-NULL
image's full-mask aggregate and the filled image unmasked. Three rounds
rotate the column order, and cells are medians over rounds. The filled image
uses predictor fill under validity S. At 1% uniform, 92% of blocks are mixed
(10,834 of 11,718); at 50% all are.

| SUM | No NULL | 1% | 10% | 50% | 10% clustered |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS | 528 | 1,034 / 5,517 | 1,043 / 5,932 | 868 / 5,444 | 731 / 2,470 |
| RegionID | 733 | 1,419 / 5,783 | 1,436 / 6,343 | 1,170 / 6,326 | 996 / 2,761 |
| EventTime | 884 | 1,101 / 2,465 | 1,104 / 2,722 | 3,200 / 10,232 | 1,219 / 1,757 |
| URLHash | 1,096 | 2,107 / 2,835 | 4,244 / 6,302 | 2,974 / 10,206 | 2,135 / 2,508 |
| UserID | 837 | 1,626 / 6,171 | 1,663 / 6,929 | 1,304 / 6,836 | 1,116 / 2,905 |
| ClientIP | 664 | 1,522 / 6,225 | 1,527 / 6,872 | 1,170 / 6,698 | 966 / 2,817 |
| Jena p | 13 | 28 / 68 | 63 / 72 | 82 / 81 | 26 / 36 |
| Jena wv | 18 | 12 / 44 | 12 / 46 | 64 / 187 | 18 / 30 |

| COUNT ≥ median | No NULL | 1% | 10% | 50% | 10% clustered |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS | 441 | 985 / 5,365 | 999 / 5,795 | 817 / 5,296 | 669 / 2,353 |
| RegionID | 594 | 1,332 / 5,650 | 1,349 / 6,221 | 1,110 / 6,189 | 895 / 2,598 |
| EventTime | 654 | 820 / 2,317 | 812 / 2,583 | 2,242 / 10,088 | 891 / 1,567 |
| URLHash | 806 | 1,477 / 2,662 | 3,789 / 6,110 | 2,727 / 10,062 | 1,820 / 2,268 |
| UserID | 43 | 79 / 6,033 | 72 / 6,789 | 71 / 6,626 | 109 / 2,379 |
| ClientIP | 488 | 1,412 / 6,109 | 1,422 / 6,721 | 1,107 / 6,567 | 833 / 2,646 |
| Jena p | 50 | 59 / 68 | 60 / 69 | 56 / 78 | 52 / 57 |
| Jena wv | 19 | 10 / 41 | 10 / 43 | 60 / 185 | 18 / 29 |

5. **Per-kind algebra beats decode-then-mask by 3–6× on the run- and
   packed-heavy columns.** Geometric means of paired masked/control ratios
   are 0.32 (SUM) and 0.22 (COUNT) at 1% uniform, 0.27 and 0.17 at 50%, and
   0.50 and 0.37 at 10% clustered. UserID's COUNT keeps its bound shortcuts
   and needs about 1% of the control's time.
6. **Masked aggregates cost 1.2–2.9× the no-NULL aggregate at 1% uniform**,
   Jena `wv` aside (geometric means 1.67 SUM and 1.59 COUNT). Almost every block is mixed,
   so almost none takes the full path, and a run-heavy block pays two ranks
   per run. Clustered NULLs leave more blocks full, giving 0.9–2.6×
   (geometric means 1.44 and 1.52). Jena `wv` is faster masked than
   unmasked (12 against 18 µs): its packed blocks reach the catalogue's
   native packet SUM, which beats the full path's range decode here, as in
   the [catalogue table](#shared-plane-catalogue-first-cut).
7. **Delta SUM gains most at the ends, with no gain in the middle.** Segment
   sums bring Jena `p` at 1% to 28 µs against 68 for the control (13 with
   no NULLs). At 10–50%, the suffix-count weights take about as long as the
   control (63–82 against 72–81 µs), because computing c_j and the
   reconstruction prefix are both one pass over 256 rows. Delta COUNT on `p`
   reconstructs a prefix (most blocks are not non-decreasing) and runs at
   0.7–0.9× the control.
8. **Speed changes were paired.** Per-run ranks beat decoding at every
   run-count crossover tried (16–256 runs). Branch-free run counts cut
   URLHash COUNT at 10% from 7,899 to 4,018 µs, and OS SUM at 1% fell from
   1,603 to 1,042 µs. The duplicate controls read 0.99–1.01 in every cell.

**Encode cost.** The candidate encodings are paid at construction.
`nulls --encode-cost` reports the median of 5 runs and one run's
allocations (bytes requested from `operator new`) for `encode()` on the
no-NULL column and for `encode_nullable` (fill, encode and validity copy) on
the same column. Cells are ms (MB allocated), local VM:

| Column | Plain encode | Predictor, 1% | 10% | 50% | 1% clustered | Zero fill, 50% |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OS | 40.7 (72) | 159.2 (432) | 194.3 (467) | 241.7 (496) | 48.5 (114) | 87.0 (200) |
| RegionID | 53.4 (86) | 201.8 (476) | 242.3 (517) | 302.2 (547) | 65.0 (130) | 101.7 (202) |
| EventTime | 71.2 (169) | 262.6 (766) | 307.3 (815) | 349.1 (740) | 88.6 (223) | 102.6 (228) |
| URLHash | 94.3 (195) | 336.5 (759) | 394.7 (807) | 389.2 (720) | 112.7 (249) | 104.9 (226) |
| UserID | 41.4 (101) | 195.2 (576) | 236.8 (631) | 313.0 (661) | 51.8 (149) | 106.1 (201) |
| ClientIP | 57.1 (90) | 219.2 (483) | 265.0 (521) | 298.6 (558) | 67.5 (134) | 99.9 (203) |
| Jena p | 1.7 (3) | 6.7 (18) | 7.9 (17) | 7.5 (15) | 1.7 (4) | 2.1 (5) |
| Jena wv | 2.0 (4) | 7.4 (17) | 8.1 (17) | 9.0 (16) | 2.1 (4) | 2.1 (5) |

9. **Predictor fill makes construction 3.6–7.6× slower once most blocks
   hold a NULL.** It allocates 3.7–6.9× the bytes. Each mixed block encodes up
   to four candidate bodies, then `encode()` runs again on the filled
   values: that second pass costs the plain encode, and the rest is the
   fill. Clustered NULLs leave most blocks without NULL, so the cost is
   1.0–1.25×. Zero fill costs 1.0–2.6×, all of it in the re-encode of wider
   blocks. The allocations are per-candidate scratch vectors and payloads,
   and nothing is reused yet. Pricing a candidate without emitting it, or
   choosing the fill from the block's kind on valid rows (one emit), would
   remove most of this (untested). These are encoder costs only: no write
   or lifecycle gain is inferred from body bytes or scan timing.
   `fill_nulls` returns plain filled values, which Engine's EncodedRegion
   accepts as they stand.

Scripts and results are in `build/spikes/ikea-frame/masked/` (`timing.sh`,
`summarise.py`, `bits.jsonl`, `timing.jsonl`, `encode_cost.jsonl`). Local VM timings are
indicative only. **Remaining:**

- NN bodies are unbuilt, and so is a sparse validity form;
- the fill's encode cost needs a cheaper chooser;
- coded-block masked timing is unmeasured (the control's dictionary lookups
  are point reads);
- fill bytes under dictionaries are unmeasured;
- delta SUM at middle densities has no faster form;
- point/, filter/ and execution/ consumers and Engine's witness can now take
  these operations in place of their decode fallbacks.

## Delta point reads through the catalogue SUM

Ikea LEAD's discriminator: a delta `stored_at` used to decode the residuals
between its checkpoint and the row into a u64 array, then sum them. The row's
checkpoint interval starts at a 16-aligned row, so first + ceil(take/16)·16 ≤
256, and the catalogue's native-packet `sum` can read the residual plane
advanced by `plane_bytes(K, first)`. That gives the same modulo-2^64 prefix,
read inside the scan grant, with nothing materialised. The checkpoint, rbase
and exception logic is unchanged. The A/B ran in an isolated snapshot build
(`build/spikes/ikea-frame/dp/`): 65,536 random points, 5 paired rotated
rounds, local VM, ns per point, old → new.

| Input | native | packed | every64 | none |
| --- | ---: | ---: | ---: | ---: |
| EventTime, clustered by region/OS/time | 45.9 → 25.4 | 38.9 → 25.3 | 48.6 → 30.8 | 75.2 → 47.9 |
| Jena p | 24.1 → 13.2 | 26.2 → 14.6 | 35.7 → 21.5 | 60.4 → 31.2 |
| Jena T | 21.7 → 14.5 | 24.6 → 14.1 | 34.1 → 20.7 | 56.4 → 26.6 |
| Synthetic 48–56-bit walk | 37.4 → 26.4 | 41.2 → 27.9 | 55.8 → 46.6 | 80.7 → 70.3 |

1. **Real delta columns read points in 0.48–0.73× the time.** The longest
   intervals gain most (no checkpoints: about 0.5×).
2. **Wide residuals gain less.** The synthetic walk is 0.68–0.94×, and 57–63-bit
   walks are 0.86×. Inputs with no delta blocks read 0.98–1.06×, which is the
   noise floor. No input lost, so the old path was removed rather than kept
   as an option.
3. **Checks:** the point checks (2.83M exact reads, four checkpoint modes) and
   the delta-exception check (residual widths 1–64, wrapping checkpoints) pass,
   as does the 417-run borrowed check. A 57–63-bit walk under every64 and none
   adds wide-prefix wrap coverage.

## Fused aggregates

Ikea LEAD's lifecycle runs found the gap: native array controls fuse SUM and
COUNT in one pass, while Frame ran each aggregate as its own full pass. On
all-valid EventTime that left Frame at 4.65 ms against 2.39 ms native
([REPAIR.md](../lifecycle/REPAIR.md#null-aware-reads-change-the-comparison)).
[`fused.h`](fused.h) adds one owner program per request. A
`fused_request` names the wanted aggregates: exact u128 SUM, COUNT(x ≥ t) for
up to four thresholds, COUNT(x = v), MIN and MAX. Aggregates it leaves out
cost no pass and no dictionary preparation. Code: 6321ff6d.

There are full-256 and row-selected forms, over borrowed blocks
(`fused_block`, `masked_fused_block`, `fused_stored`) and columns
(`encoded_fused`, `masked_encoded_fused`), each with an optional
`dictionary_cache`. The dispatch is the same as in [selected.h](selected.h).
An empty S returns before any body or dictionary access, a full S takes the
full program, and a SUM-only request runs the existing SUM unchanged. The
per-kind algebra is reused:

- **Packed and patched inliers:** one decode into the narrowest lane feeds
  every reduction, with lane-width accumulators.
- **Mixed S:** at most 64 unselected rows take the complement (all rows,
  minus the unselected ones); otherwise only the 64-row words that hold a
  selected row are decoded.
- **Constant and runs:** closed forms. One Intervals pass writes per-run
  weights, which are run sizes or rank popcounts. Branch-free loops over the
  value and weight arrays follow; a select had compiled into a mispredicting
  branch.
- **Non-decreasing delta:** keeps its weighted SUM, and answers both COUNTs by
  the checkpoint search. Rows [first_ge(v), first_ge(v + 1)) hold v.
- **Other delta, delta_patched and arrival:** reconstruct once, then reduce.
- **Coded blocks:** SUM, MIN or MAX gathers each entry once. A COUNT-only
  request takes translated code thresholds under sorted dictionaries, and
  truth bits from one code buffer otherwise.

**Checks.** borrowed_check runs every block of every input against the input
oracle restricted to S, under 16 requests (each of its thresholds four at a
time with every aggregate, SUM + COUNT, counts only, MIN/MAX, EQ at
UINT64_MAX, empty) and under every adversarial and random selection, in all
four guard placements. Each request runs per call and through a shared cache.
The stored-domain form is checked against stored codes, and SUM + COUNT
(+ EQ) are also checked against the separate operations. Further coverage:

- inside the scan grant, the unsearched delta blocks;
- under the empty-S `PROT_NONE` guard;
- the column forms over the old/new/old/copy/new image sequence;
- a `fused` fault probe.

The full `identity/check.sh` passes 417/417 runs (1.10G fused checks). The
driver's 11,232 non-timing values match, apart from the known +8 B.
`masked_access_check` passes. Eight seeded mutants were each caught:

- a shifted equality search;
- a dropped complement row;
- an unweighted run MIN;
- a code threshold off by one;
- an absent equality code;
- a lost lane keep mask;
- an extra exception;
- a missing empty guard.

The mutants are in `build/spikes/ikea-frame/fused/mutants.sh`. Because the
live `ikea/` tree held another task's edits, qualification ran from a snapshot
of HEAD plus these files (`build/spikes/ikea-frame/fused-snap/`).

**Timings.** `frame_encoding_fused` compares the existing owner calls (one
per aggregate), the fastest separate arms (narrow SUM plus catalogue COUNT
filters) and one fused program. Its native controls are arrays of the column
minus its minimum at the narrowest byte width, with SUM over u64 in 32-bit
halves and a per-row keep mask under validity. Each aggregate is native
either in its own pass or in one fused pass. The column-wide threshold is
the median, and v is the middle row's value.

Arms run in bodies.cpp's fixed-seed shuffled order, 21 trials. An exact
duplicate of the separate and fused arms is the noise control. Three rounds
rotate the input order, and cells are medians over rounds. Ratios are paired
per trial. All timings are from the local aarch64 OrbStack VM, under a load
average of 5–6 from other tasks, so they are indicative only. Duplicates read
0.98–1.02 on all-block and NULL shapes, but up to 1.14 on sparse10. Results
are in `build/spikes/ikea-frame/fused/` (`timing.sh`, `summarise.py`,
`timing.jsonl`).

All blocks, SUM + COUNT(≥ median), µs, and paired ratios (the last two
columns are SUM + COUNT + COUNT(=) and fused against native fused):

| Input | Separate | Best separate | Fused | Native separate | Native fused | Fused / separate | Fused / best | 3 aggregates fused / separate | Fused / native fused |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| OS | 1,008 | 1,032 | 717 | 432 | 427 | 0.71 | 0.70 | 0.55 | 1.67 |
| EventTime | 1,677 | 1,151 | 958 | 723 | 421 | 0.58 | 0.83 | 0.46 | 2.19 |
| URLHash | 2,079 | 2,093 | 1,551 | 1,537 | 883 | 0.74 | 0.74 | 0.62 | 1.77 |
| WatchID | 2,682 | 3,081 | 1,910 | 1,538 | 836 | 0.71 | 0.63 | 0.57 | 2.27 |
| UserID | 952 | 966 | 983 | 1,623 | 924 | 1.03 | 1.02 | 1.00 | 1.10 |
| OS, frequency dictionary | 1,841 | 1,886 | 1,369 | 440 | 434 | 0.74 | 0.74 | 0.59 | 3.15 |
| URLHash, arrival dictionary | 5,977 | 6,035 | 5,573 | 1,607 | 890 | 0.92 | 0.92 | 0.85 | 6.28 |
| Jena window 0, 14 fields (sum; geomean ratios) | 629 | 522 | 483 | 122 | 121 | 0.73 | 0.89 | 0.49 | 3.70 |

Every 10th block, and every block under a 1% uniform NULL validity plane
(`encode_nullable`, predictor fill; 92% of blocks are mixed). Paired fused /
separate for two and three aggregates, then fused / native fused under NULLs:

| Input | Sparse10 | Sparse10, 3 | 1% NULL | 1% NULL, 3 | 1% NULL, fused / native fused |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS | 0.70 | 0.54 | 0.65 | 0.47 | 1.05 |
| EventTime | 0.55 | 0.45 | 0.78 | 0.62 | 1.35 |
| URLHash | 0.63 | 0.60 | 0.65 | 0.52 | 1.41 |
| WatchID | 0.65 | 0.53 | 0.62 | 0.54 | 1.28 |
| UserID | 1.02 | 1.01 | 1.00 | 0.97 | 1.05 |
| OS, frequency dictionary | 0.71 | 0.55 | 0.53 | 0.44 | 1.37 |
| URLHash, arrival dictionary | 0.58 | 0.43 | 0.59 | 0.56 | 3.55 |
| Jena window 0 (geomean) | 0.73 | 0.49 | 0.81 | 0.59 | 1.36 |

1. **Fusion pays where both aggregates would scan the same plane.** On the
   packed- and run-heavy columns, SUM + COUNT takes 0.58–0.74× the separate
   calls, and adding COUNT(=) takes 0.46–0.62×. A third aggregate costs 12–24%
   more fused, but a whole pass separately. EventTime drops from 1.68 to
   0.96 ms. Against the fastest separate arms it is 0.63–0.83×, since the
   catalogue COUNT filter is already cheap on packed planes (EventTime:
   0.83×).
2. **It doesn't pay where one aggregate is a closed form.** UserID's
   COUNT(≥ median) is decided by run bounds, so the separate COUNT costs
   almost nothing and the fused program is 1.00–1.03×. Jena's delta fields
   are 0.89× against the best arms (geomean; delta-heavy fields 0.98–1.07).
   Non-monotone delta keeps the weighted SUM separate, while the fused
   program reconstructs once; monotone delta was already closed form both
   ways.
3. **Coded blocks gain under frequency dictionaries, barely under arrival.**
   OS frequency reaches 0.74× (0.74 cached). URLHash arrival reaches only
   0.92×, and through a warm cache the fused program is 1.08× slower. The
   separate arrival SUM reads literal entries in sequence and its COUNT reads
   L1 truth bits, while the fused gather fetches an 8 B entry per row for every
   comparison. Under sparse masks and NULLs, arrival still gains (0.58–0.59×).
4. **Masked fusion holds up under 1% NULL** (0.53–0.81×), with the complement
   form taking the dense blocks. Fused masked SUM + COUNT is within 1.05–1.41×
   of the native masked fused loop on ClickBench.
5. **The native gap narrows but stays.** All-valid fused programs are
   1.67–2.27× native fused on ClickBench, 3.7× on Jena and 6.3× under arrival
   dictionaries. Native fusion gains 0.55–0.58× on 4- and 8-byte arrays, but
   nothing on OS's u8 array. EventTime's SUM + COUNT drops from 4.0× native
   fused to 2.2×. What is left is the plane decode, which SUM-only already
   paid.

**Remaining:**

- worker timings on Graviton and Zen5 (local ARM only so far, and the AVX-512
  gap is wider);
- a native-packet fused leaf, one packet read per SUM and comparison (kernels
  are Ikea LEAD's; the catalogue `sum` was slower than the narrow decode on
  EventTime, 443 against 376 µs);
- arrival-coded fusion that keeps the literal sequence and truth bits;
- MIN/MAX timings (checked, not timed);
- consumers: the lifecycle programs can call `masked_encoded_fused` in place of
  two calls.

## Predicate masks

filter(A) → SUM/COUNT(B) needs A's predicate as an original-row mask that B's
[row-selected aggregates](#row-selected-aggregates-and-null-fill) can consume.
[`predicate.h`](predicate.h) emits one 256-bit `row_selection` per block for
x ≥ t, x < t, t1 ≤ x < t2, x = v and x ≠ v. Code at 3f31a329.

**API.** `predicate_masks(column, coarse, predicate, out, {selection, validity},
cache, counts)` returns the selected row total, and `counts` takes |mask| per
block. `emit_predicate_masks` does the same over any block source, such as
Engine's borrowed Parts. The `predicate_mask` evaluator answers one
`borrowed_block` under an active selection. `reference_predicate_*` is the
decode-then-compare control. Four inputs stay distinct:

- The coarse mask: a skipped block is never visited, so its descriptor, body and
  dictionary are not read, and its `out` words and counts stay as the caller
  supplied them.
- The fine input selection.
- A's validity.
- The predicate.

The mask is predicate ∧ selection ∧ validity(A). B's validity belongs to the
consumer, which ANDs it into the plane before aggregating. A visited block whose
active rows are empty is written 0 before any read.

**Laws.** Every predicate is x ∈ [lo, last], its complement (≠), or empty:

| Kind | Law | Reads |
| --- | --- | --- |
| constant | one compare: all active rows or none | descriptor |
| runs | one compare per run value; passing runs filled as word-level ranges | run values, ends |
| packed | residual interval clipped to [0, 2^K); bounds decide whole blocks; else SeriesPack `filter_unchecked` over 64-row windows with the active word as their active mask (≥ and = one filter, < the complement of ≥ under active, a range two) | 64-row words with an active row |
| patched | the packed law over inliers (active ∖ E), plus each active exception compared | plane, exceptions only if one is active |
| delta, rbase ≥ 0, checkpoints | non-decreasing: rows [first_ge(lo), first_ge(last + 1)), one checkpoint search per finite bound | checkpoints, one interval of residuals |
| delta, otherwise | prefix reconstructed through the last active row and compared; a non-decreasing block stops past the interval | residuals through the last active row |
| delta_patched, arrival | decode, then compare | body |
| coded, sorted | code range [lb(lo), lb(last + 1)) through the stored laws above; equality is a code range too | dictionary bounds, then as the kind |
| coded, other orders | one truth bit per code, T[k] = pred(min + D[k]): constant one bit, runs one per run, arrival literals by rank and references in row order, others decode codes | whole dictionary once per identity, then as the kind |

Translation happens once per dictionary identity, lazily. An identity whose
translation admits every code, or none, answers its blocks without reading
them. With a `dictionary_cache`, sorted bounds and unordered threshold tables
(T_lo ∧ ¬T_{last+1}) persist across calls. Sorted needs only a non-decreasing
plane, so a projected view with equal neighbours stays correct. Unordered
truth tables give each duplicated value its own bit. Neither assumes one code
per value, unlike `code_count_eq`'s first-match search: a projected
frequency-ordered view with duplicates would be wrong there (fused.h inherits
this). Grants: every predicate stays within `scan_grant_bytes` except the
monotone delta search, which reads checkpoints, as monotone COUNT does.
Packed filters live in [`predicate.cpp`](predicate.cpp), the catalogue's
`filter_unchecked` call returning bits.

**Verification** (`frame_encoding_predicate_check`,
`build/spikes/ikea-frame/pred-snap/check.sh`). The check builds the expected
mask from the input values by the predicate's own comparison, never through
the owner's laws, and compares it complete. Runs cover the 417 configurations
of `identity/check.sh` and 30 end-to-end pairs (447 runs, 1.34M blocks, 386k
of them coded, every kind and coded kind):

- **Blocks:** each body ends at a `PROT_NONE` page, as does each dictionary
  plane. Delta bodies outside the monotone law are also read with only their
  scan grant. Thresholds cover 0, UINT64_MAX, the block min − 1 and max + 1,
  row values and their successors around each checkpoint spacing, the base and
  its first unrepresentable residual, and random values. Every predicate runs,
  including empty and reversed ranges, under full, adversarial and random active
  selections, in per-call, cached and control forms: 599M masks each, plus 12.9M
  scan-grant masks. An empty active selection on a `PROT_NONE` body and
  dictionary reads nothing (1.18M guards).
- **Columns:** coarse masks all, sparse and none, crossed with fine inputs none,
  a mixed row plane, a validity plane with all-NULL blocks, and both. Output
  words are pre-filled with a sentinel. The block source fails if asked for a
  skipped or empty block, and skipped blocks must keep their sentinel (1.03M
  column masks, 1.13G skipped blocks checked).
- **Editions and projections:** old, mirrored new, old, copy and new images run
  through one cache. Duplicate-valued projected dictionaries are built by hand:
  D ≫ 3 labelled sorted, and D mod 7 labelled frequency or arrival (174M
  masks).
- **Pairs:** RegionID → UserID, OS → URLHash and EventTime (quantile windows)
  → WatchID, in original and region/OS/time order, under five option sets. A's
  mask, with A's validity, ANDed with B's validity feeds `masked_encoded_sum`,
  `_count_ge`, `_count_eq` and `masked_selected_rows` (6,600 aggregates against
  the input).

Seeded mutations were caught: a range end off by one in the plane filter, a
sorted code range one too long, the monotone bound at last instead of
last + 1, and arrival references set without their truth bit. All pass.
`masked_access_check` passes. The qualification ran from a HEAD snapshot build
(`build/spikes/ikea-frame/pred-snap`), not the live ikea/ tree, which is
mid-migration.

**Timings.** Local VM, all blocks selected, no fine input, medians in µs per
3M rows over 21 trials in a fixed-seed shuffled arm order. "Emit" is the per-call
emitter. "Control" is decode-then-compare. "Plain" compares a native u64 array
into the same plane. The exact duplicate emit arm's paired median ratio was
0.87–1.26, and 0.95–1.05 in 108 of 126 cells, so single cells move about 10%.
`pred-snap/timing.jsonl`; `summarise.py`.

| Column (dominant kinds) | ≥ median: emit / control / plain | [p25, p75) | = mode | = rare point |
| --- | --- | --- | --- | --- |
| OS (98% runs) | 4,218 / 11,385 / 2,112 | 2,738 / 7,691 / 1,154 | 2,618 / 7,889 / 1,248 | 773 / 6,175 / 908 |
| OS, sorted (97% coded runs) | 2,810 / 12,568 / 1,145 | 2,397 / 11,361 / 971 | 2,084 / 9,177 / 867 | 791 / 10,251 / 979 |
| RegionID (95% runs) | 5,131 / 8,233 / 1,111 | 3,231 / 6,155 / 871 | 1,805 / 6,063 / 903 | (the mode) |
| RegionID, frequency | 5,528 / 10,205 / 904 | 7,015 / 13,041 / 1,603 | 4,709 / 15,051 / 1,277 | |
| EventTime (80% packed) | 1,175 / 2,137 / 842 | 1,391 / 2,169 / 830 | 562 / 2,114 / 874 | 552 / 2,079 / 820 |
| EventTime, by region/OS/time (73% delta) | 789 / 3,995 / 951 | 1,185 / 3,944 / 1,164 | 681 / 3,503 / 864 | 810 / 4,075 / 908 |
| URLHash (75% packed) | 4,007 / 2,631 / 923 | 4,039 / 2,811 / 917 | 1,514 / 2,801 / 1,052 | 1,351 / 2,685 / 841 |
| URLHash, sorted | 3,365 / 7,756 / 962 | 2,712 / 6,344 / 896 | 653 / 6,130 / 920 | 493 / 5,824 / 917 |
| URLHash, frequency: cold / cached | 5,058 / 4,387 | 4,849 / 4,366 | 1,601 / 1,154 | 976 / 589 |
| UserID (99% runs) | 133 / 6,822 / 847 | 149 / 7,241 / 882 | 128 / 7,583 / 912 | 121 / 6,592 / 828 |

1. **The laws beat decode-then-compare in 123 of 126 cells, by 1.2–59×.** The
   exception is unsorted URLHash ≥, < and range (0.7–0.8×). Its 59–64-bit packed
   planes filter more slowly than they decode, which matches catalogue COUNT on
   WatchID (finding 2 of the [shared catalogue](#shared-plane-catalogue-first-cut)).
   Equality on the same blocks is 2× faster than the control. Mixed blocks
   there call for a width-dependent choice between filter and decode.
2. **Masks beat a plain native array where blocks decide whole.** UserID's long
   runs emit in 121–150 µs, 6–7× below plain. Rare equality is at or below
   plain wherever bounds or code ranges reject blocks: EventTime 552–681,
   URLHash sorted 493, OS 773–791. On time-clustered EventTime, ≥ and <
   (non-decreasing delta, two checkpoint searches) run at 0.8–0.9× plain.
3. **Run-heavy categorical masks lose to plain by 2–6×** (OS, RegionID,
   RegionID frequency up to 7 ms against about 1 ms). They pay per run for a
   value compare, an Intervals visit and a four-word range fill, and OS and
   RegionID blocks carry tens of runs. That is the same run-leaf cost that
   loses flushed OS SUM (digest). A mask built from run-end boundaries (a bit
   per passing run start and end, then a prefix XOR) is the next arm. Sorted
   codes help OS (4.2 → 2.8 ms) through narrower run values. Frequency truth
   bits make RegionID slower.
4. **Dictionary translation is cheap except for unordered high-cardinality
   parts.** Sorted URLHash needs two lower bounds per part, and equality
   becomes a code range that bounds reject (493 µs). Frequency builds a
   ~40k-entry truth table per part per call. A warm cache reuses its threshold
   tables and cuts rare equality 976 → 589 µs and ≥ 5,058 → 4,387. Low-
   cardinality columns see no cache effect.
5. **< costs no more than ≥** (complement under active), and the empty and
   always-true identities (e.g. ≠ an absent value) answer without bodies.

Remaining: the run-boundary mask arm; a filter-or-decode choice for wide packed
planes; worker timings (local VM only here); a fused predicate-then-aggregate
over the same block when A = B; and Engine's block source adapting
`emit_predicate_masks` to its Parts.

## Row order in real bodies

The same 11 ClickBench columns were encoded as real bodies (default options,
binary built at 6aee1935), with one joint row permutation applied to every
column: the original order (CounterID, date, UserID, time), a random shuffle,
and a re-clustering by (RegionID, OS, EventTime).

| Bits/value | Original | Shuffled | By RegionID, OS, EventTime |
| --- | ---: | ---: | ---: |
| OS | 2.78 | 8.24 | **0.57** |
| RegionID | 5.37 | 11.96 | **0.52** |
| EventTime | 16.88 | 21.50 | **10.24** |
| ResolutionWidth | **3.91** | 11.88 | 9.88 |
| CounterID | **0.50** | 11.50 | 8.37 |
| UserID | **14.60** | 64.50 | 51.66 |
| ClientIP | **9.99** | 32.50 | 27.77 |
| URLHash | **59.11** | 64.50 | 62.61 |
| WatchID | 62.50 | 62.50 | 62.50 |
| **Total (11 columns)** | **179.1** | 293.6 | 238.1 |

Block kinds go from 73% constant or runs (original) to 6% (shuffled) and 56%
(re-clustered).

1. **Real bytes confirm the model's row-order effect.** Shuffling costs 1.64×
   in real bodies (the model said 1.8× over 77 columns).
2. **Clustering trades columns, it doesn't simply help.** Re-clustering by
   region and OS makes those columns nearly free (about 0.5 bits/value) and
   halves EventTime. It wrecks the columns that rode the original sort key:
   UserID 14.6 → 51.7, CounterID 0.5 → 8.4, ClientIP 10.0 → 27.8.
3. So sort order is a table-level choice that Engine has to price against its
   workload. It moves bytes more than any per-block codec choice measured
   here, and it interacts with Ikea LEAD's lifecycle work: inserts that
   disturb clustering erode exactly the run and constant blocks that make
   these columns small.

## Images to hand Engine's witness early

Engine's witness is a selected grouped reduction over parts with different
dictionary editions. From this corpus, each 65,536-row part naturally has its
own dictionary edition:

| Role | Candidate columns | Why |
| --- | --- | --- |
| Category C | `OS`, `TraficSourceID`, `RegionID` | Dictionary + runs, dictionary + FOR, and a 17-bit sparse domain |
| Nullable/value X | `ResolutionWidth`, `ConnectTiming`, `ResponseStartTiming` | Runs-heavy; half-constant; FOR with exceptions |
| Predicate column | `IsRefresh`, `AdvEngineID`, `CounterID` | Dense bits, mostly constant, sorted runs |
| Incompressible control | `WatchID` | Plain at full width |

ClickBench has no real nulls in these columns, so nullability will need
injected or derived presence (for example ConnectTiming = 0 as absent),
labelled as synthetic.

[`clickbench_parts.py`](clickbench_parts.py) derives Engine's
`frame-execution-logical-v1` documents (`kind: corpus-derived`, with source
provenance) from consecutive 65,536-row slices in original order. Each part's
dictionary is in first-occurrence order, so editions differ per part. Two
documents validate and pass the oracle (three parts each, in
`build/spikes/ikea-frame/`): `OS × ResolutionWidth` with no nulls, and
`RegionID × ConnectTiming` with synthetic nulls where ConnectTiming = 0.

## Recent-value references (byte model)

[`repeat_model.py`](repeat_model.py) tests the hash gap from the second pass
with one new block kind. A row whose value was a literal within the last 2^R
literals of the same part becomes an R-bit distance. Other rows are literals,
held in a FOR plane, with a 256-bit flag plane. References point only at
literals, so a point read is flag, then distance, then one literal point read.
Parts stay independent. Each block takes the cheaper of this kind (best R in
8/12/16 per part) and the lightweight kinds of `dictionary_model.block_cost`,
which excludes delta. ZSTD compresses each part's raw u64 bytes, so it is a
byte bound with no random access. Output is in `build/spikes/ikea-frame/repeat/`.

| Bits/row | Lightweight | + references | ZSTD-3 | ZSTD-19 | Reference rows |
| --- | ---: | ---: | ---: | ---: | ---: |
| URLHash | 59.1 | 45.3 | 41.4 | 40.6 | 41% |
| URLHash, shuffled | 64.5 | 57.5 | 56.9 | 56.0 | 19% |
| UserID, shuffled | 64.5 | 55.5 | 58.7 | 54.6 | 23% |
| UserID | 14.6 | 14.6 | 13.3 | 13.1 | 1% |
| RegionID | 5.4 | 5.2 | 3.4 | 2.7 | 13% |
| WatchID, ClientIP, EventTime | unchanged | unchanged | | | ≤1% |

1. **Recent-value references explain most of the hash gap.** URLHash closes
   78% of its distance to ZSTD-3, and every block takes the kind. Most parts
   choose R = 16, so useful repeats sit thousands of rows back, beyond any
   block-local dictionary.
2. **They work when order doesn't help.** Shuffled UserID beats ZSTD-3 by
   3 bits/row. Under shuffle, clustering no longer narrows FOR, but a value
   still recurs within its part.
3. **They don't replace the other levers.** Clustered UserID, ClientIP and
   EventTime already sit at or below ZSTD-3 through FOR, runs and delta.
   RegionID's remaining gap is entropy (second pass), not repeats. WatchID
   is unique.
4. **Costs to price before building the kind.** This is a byte model. The
   remaining 4 bits on URLHash are probably multi-row matches and entropy over
   distances. Reads become gathers. Equality filters can test the literals,
   then the references whose targets matched. SUM and range COUNT need the
   gather. A point read crosses at most one block boundary, using a per-block
   prefix of literal counts.

## Next questions

1. Convert the model's winners into real SeriesPack bytes for a sample of
   columns, checking the modelled-to-physical gap.
2. Recent-value references are real as [arrival-order dictionaries](#arrival-order-dictionaries):
   43.9 bits/row on URLHash, at 1.3–3× sorted's scan cost. A
   [cross-call cache](#dictionary-caches-across-calls) brings equality below
   sorted and sparse COUNT ≥ to 1.8× sorted, but full-scan COUNT ≥ stays at
   ~2×, and the cache takes about twice the column's bytes. Open: a bounded
   policy (truth tables only, or a smaller equality structure than values
   plus index) priced on Engine's workloads; exact prefix sums over literal runs for SUM;
   per-part resolved entries for point reads; multi-row matches or entropy
   over the literal plane for the last 2.5 bits to ZSTD-3.
3. Re-clustered orders (by category, by time) beside original and shuffled, so
   Engine can price clustering against encoding effort.
4. Bring in float/decimal and time-series corpora (TPC-H, TSBS, ALP's datasets).
5. Time DuckDB and Polars on sum, filter-count and decode for the same columns,
   with Python overhead separated, to set the speed bar per operation.
6. Price an entropy-coded cold form against its decode, point-read and
   conversion costs (Codex's caution: smaller bytes must repay operations).
