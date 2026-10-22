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

## Next questions

1. Convert the model's winners into real SeriesPack bytes for a sample of
   columns, checking the modelled-to-physical gap.
2. A "repeat of a recent value" arm for hash-like columns, against ZSTD.
3. Re-clustered orders (by category, by time) beside original and shuffled, so
   Engine can price clustering against encoding effort.
4. Bring in float/decimal and time-series corpora (TPC-H, TSBS, ALP's datasets).
5. Time DuckDB and Polars on sum, filter-count and decode for the same columns,
   with Python overhead separated, to set the speed bar per operation.
6. Price an entropy-coded cold form against its decode, point-read and
   conversion costs (Codex's caution: smaller bytes must repay operations).
