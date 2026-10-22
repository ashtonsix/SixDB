# Frame encoding: corpus and baseline feasibility

Owner: Claude (Frame thread). Part of the [Ikea Frame spike](../README.md); the
encoding proposal is [ENCODING.md](../ENCODING.md) and open questions are
indexed in [QUESTIONS.md](../QUESTIONS.md). This directory holds the corpus,
baseline and compression investigations. It defines no interface.

## What exists

| Need | Available now | Gap |
| --- | --- | --- |
| Real integer/time columns | ClickBench `hits` slice: 3,000,000 rows, 105 columns, 77 integer/time ([provenance](../../../../../corpora/clickbench-rows/PROVENANCE.json), local at `~/corpora/clickbench-rows/`) | Not yet a registered [dataset](../../../datasets/README.md); its row order is ClickBench's (sorted by CounterID, date, UserID, time), which favours runs |
| Floats and decimals | None in SixDB | TPC-H via DuckDB's `tpch` extension (availability offline unchecked); Calico's N-CMAPSS/PMU corpora in S3; ALP paper's float datasets |
| Time series | Calico's TSBS generator recipe | Not ported; needs the Go `tsbs` tool |
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
