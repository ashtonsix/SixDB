# Frame encoding: corpus and baseline feasibility

Owner: Claude (Frame thread). Part of the [Ikea Frame spike](../README.md); the
encoding proposal is [ENCODING.md](../ENCODING.md) and open questions are
indexed in [QUESTIONS.md](../QUESTIONS.md). This directory holds the corpus,
baseline and compression investigations. It defines no interface.

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
an edition id. At attach time it resolves its own spans into `body`/`grant` and
`dictionary.plane`/`grant`, calls `admits` once, then calls the operations. The
view owns nothing. A coded block must be read with the dictionary edition it was
coded against. `code_sum` and `code_count` cache a decoded dictionary or
translated threshold per edition, which `borrow` sets to the part index. Coarse
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
   URLHash, WatchID and Jena (packed and native checkpoints) shows a SUM and
   COUNT geomean within 1.006 of the old code, apart from EventTime, which
   has few monotone blocks in original order.

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
2. Real recent-value reference bodies for hash-like columns (byte model
   above), with gather costs for point, equality filter and SUM.
3. Re-clustered orders (by category, by time) beside original and shuffled, so
   Engine can price clustering against encoding effort.
4. Bring in float/decimal and time-series corpora (TPC-H, TSBS, ALP's datasets).
5. Time DuckDB and Polars on sum, filter-count and decode for the same columns,
   with Python overhead separated, to set the speed bar per operation.
6. Price an entropy-coded cold form against its decode, point-read and
   conversion costs (Codex's caution: smaller bytes must repay operations).
