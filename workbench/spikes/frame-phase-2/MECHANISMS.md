# How Frame wins: mechanisms and targets

Proposal, 2026-10-23. Every target below names the mechanism that should
produce the win, the strongest baseline, why that baseline cannot use the
mechanism, and the residence tier where the mechanism pays. A target without a
mechanism is not a target. Where we expect to lose, the table says so and
gives the loss we would accept.

The numbers are hypotheses derived from the mechanisms and from hardware
priors that SixDB has not yet measured. The instruments thread
([PARTITION.md](PARTITION.md)) replaces the priors with measured bandwidths
before any target is used as a bar. Each research thread then restates its
targets numerically and keeps the losing cells.

## Residence tiers

| Tier | Working set (input, metadata, output) | Active streams | Bound |
| --- | --- | --- | --- |
| L1/L2 | ≤ half of L2 | One core | Instructions per value |
| LLC, one core | ≤ half the LLC share | One core | Mostly instructions; L2 refill for wide forms |
| LLC, shared | Same | Every core on the CCD or cluster, each on its own data | Shared L3 and CCD-link bandwidth |
| DRAM, one core | ≥ 4× LLC | One core | Per-core miss concurrency and prefetch |
| DRAM, saturated | ≥ 4× LLC per stream | Every core in the instance | Socket bandwidth divided by cores |

Every result records the instance type, the core count and the number of
concurrent streams. A flushed input on one core is the DRAM-one-core tier, not
the saturated tier; phase 1 never measured the saturated tiers. "Flushed" in every result below is a conditioning step (the
inputs were evicted before the call), not proof that loads were served from
DRAM; no guest here has qualified LLC or DRAM counters (front A's
[findings](instruments/FINDINGS.md)).

### Hardware priors (to be calibrated)

| | Zen 5 (c8a, EPYC 9R45) | Graviton 4 (c8g, Neoverse V2) |
| --- | --- | --- |
| L1D / L2 / LLC | 48 KB / 1 MB / 32 MB per 8-core CCD (guests report less) | 64 KB / 2 MB / 36 MB shared |
| Vector loads | 2 × 512-bit per cycle | 3 × 128-bit per cycle |
| Independent random-line throughput, one core (measured) | 5.0 ns per line, about 13 GB/s ([fleet](../memory-characterisation/FLEET.md)) | 7.0 ns per line, about 9 GB/s |
| Socket DRAM | 12 DDR5 channels, roughly 550–600 GB/s | 12 DDR5-5600 channels, roughly 540 GB/s |
| Per-core share at saturation | about 3 GB/s at 192 cores | about 5.6 GB/s at 96 cores |
| Shared choke below DRAM | CCD-to-IO-die link, shared by 8 cores | Mesh and SLC, shared by all cores |

Only the random-line row is a SixDB measurement. Sequential bandwidth per tier,
one core and saturated, is the first thing the instruments thread measures.

### What the tiers imply

Time per value is the larger of compute cost and bytes moved over the tier's
bandwidth. Bytes moved means input, metadata, output, RFO and writeback where
relevant, and selection and scratch; logical image bytes, the accessed
footprint and measured traffic are three different numbers. An 8× image-byte
ratio becomes an 8× speed ratio only where measured compute and traffic allow
it. In the saturated DRAM tier a Polars Int64 scan can deliver at most
about 0.4–0.7 values/ns per core (3–5.6 GB/s ÷ 8 B). A Frame body at 8 bits
per value can deliver 3–5.6 values/ns, which any SIMD decoder exceeds. So the
saturated tier converts byte ratios into speed ratios almost directly.

On one core, DRAM bandwidth is several times higher, so the same byte ratio
pays only when decode and the operation together run faster than the bytes
arrive. At L1/L2 bytes are free and only instruction counts matter: there the
wins come from narrower lanes and from removing rows altogether.

## Mechanisms

| | Mechanism | What it removes | Pays at | Why strong baselines lack it | Phase 1 evidence | Main risk |
| --- | --- | --- | --- | --- | --- | --- |
| M1 | **Structure replaces rows.** Closed forms on constant and run blocks, FOR base terms, monotone search, block statistics | Work and bytes proportional to rows; cost scales with blocks or runs instead | All tiers | Polars keeps no in-memory structure; DuckDB decompresses runs and constants into 2048-value vectors for most operators | UserID clustered COUNT 42 µs vs 460 µs native | Per-run leaf cost: SUM over 24+ runs per block cannot beat a native u8 sum |
| M2 | **SIMD density from shared prefixes.** Values that share high bits (FOR, split, codes) run in u8 or u16 lanes | Up to 8× fewer instructions per value than Int64 | L1/L2, LLC one core | Polars and DuckDB compute at the logical width | Calico u8 SUM 3.9–5.7× decode-then-sum; narrow run-value leaf 0.63–0.79× | Native narrow arrays have it too; Frame's edge is per-block width |
| M3 | **Fewer bytes through a bandwidth-bound tier** | Bytes per value | DRAM saturated, LLC shared, then DRAM one core | Polars is uncompressed. DuckDB, TurboPFor and FastLanes have the bytes; a small decoded scratch vector consumed at once needs no second DRAM pass, so their limit is decode plus interpretation cost, to be measured | Eight concurrent cores on a Zen 5 c8a.2xlarge, shared image against front A's qualified native SUM: Frame wins 6.2× (UserID), 2.4× (EventTime), 3.2× (OS, which loses alone) ([scaling](substrate/README.md#concurrent-streams-on-the-shared-image)); smaller ratios on Graviton 4's 8-core guest | Decode slower than the bandwidth share; metadata bytes; small guests are not sockets |
| M4 | **Capacity.** The compressed image fits a faster tier than the native one | Misses for points, gathers and repeated scans | LLC versus DRAM | Uncompressed baselines spill first | The regime exists: UserID image 5.5 MB vs native 24 MB on a 16 MiB L3; not yet exploited | Only a band of column sizes benefits |
| M5 | **Bounded random access through checkpoints** | Prefix work for a point in delta-coded data: at most 15 residuals in registers | All tiers versus block codecs on delta data | TurboPFor offers direct access for FOR and PFor (`bitgetx32`, `p4getx32`), which Frame must match rather than beat by construction; delta needs a prefix over its block. FastLanes' random-access entry is unqualified at the pinned source (Ikea LEAD) | Delta points 13–17 ns with checkpoints, 27–32 ns without (local) | Still behind a native array at L1 |
| M6 | **Memory-level parallelism through staging** (Loom rings and streams, prefetch) | Serial descriptor → body → dictionary chains | LLC and DRAM, for points, gathers and scattered selections | Native arrays have one level of indirection and need less; baselines rely on hardware prefetch | Scattered flushed selected SUM 131.6 µs vs native 51.1 µs, a serial chain | Staging overhead at high selectivity |
| M7 | **Separated fingerprints and heads.** First 1–2 bytes in their own plane, suffix read only for ties | 6–7 of 8 bytes per row for filters, plus u8 lane density | All tiers, most at DRAM | No baseline stores a head plane | Logical H16 3.4–4.8× faster on Jena filters; loses on RegionID | Tie-heavy columns; +1–2 B/row unless the head is a split's high part |
| M8 | **Pruning by coarse evidence** at 256-row grain | Blocks decided PASS or FAIL from statistics or masks | All tiers on clustered data | DuckDB zone maps work at 122,880-row row groups; Polars has none in memory | Empty and coarse grants skip bodies; clustered COUNT wins | Drift widens bounds (Calico's negative) |
| M9 | **Batched execution over descriptor planes.** Treat descriptors as columns; run every constant block in one vector loop, every FOR-K block in one width-specialised loop | Per-block indirect dispatch and call overhead | All tiers; it enables M1 and M2 | Not needed by uncompressed baselines; DuckDB pays per-vector interpretation | On phase 1's images the unit of batching moves run and packed cells by about 10%; only closed-form cells gain much (UserID COUNT 2.4 → 0.9 ns per block) ([probe](substrate/README.md)) | On phase 1's slow leaves dispatch hardly mattered; on front C's leaves a kind index plus kind-grouped placement makes OS SUM 2.5× faster ([shared image](substrate/README.md#the-shared-image-substrate-over-front-cs-leaves)). Under random cold access the loss is locality, which grouping does not recover and staging (M6) largely does |
| M10 | **Fusion without materialisation.** Predicate → mask → payload; several aggregates in one pass; decode → consume in registers | Intermediate arrays, Boolean bitmaps and second passes | All tiers, most when intermediates leave L1/L2 | Polars materialises Boolean bitmaps and filtered arrays; DuckDB builds selection and decompressed vectors | Row-selected SUM/COUNT 0.17–0.32× decode-then-mask; SUM+COUNT 0.58–0.74× separate | Code size; register pressure |
| M11 | **Code-domain execution.** Group, hash, equality and IN on dense dictionary codes; per-distinct work | Hash-table probes and per-row hashing | All tiers | DuckDB and Polars have dictionary or categorical paths, but not narrow codes fused with runs | OS grouping 73.9 µs vs 148.2 µs best native shared-code (local) | Large dictionaries; cross-part translation |
| M12 | **Algebra on transforms.** `x + c` on FOR changes the base; functions run over dictionary entries | Per-row transforms; materialised results | All tiers, when the consumer accepts an encoded result | Baselines materialise a new array | None | Needs Engine to accept structured intermediates |
| M13 | **Narrow-key reorder.** Radix passes over K-bit codes or residuals; partition by a split's high part; sort runs as runs | Passes and bytes per pass | All tiers | Baselines sort at logical width | None | Payload permutation still moves full rows |

## Baselines

| Baseline | Representation | Mechanisms it shares | Use it for | In SixDB today |
| --- | --- | --- | --- | --- |
| Native narrow and FOR arrays | Global FOR, narrowest byte lane | M2 at global width | The honest floor for hot scans and points | Yes: `frame_cells_controls` (front A, qualified) |
| Polars 1.42 | In-memory Arrow, logical width; categoricals | Full-width SIMD | Scans, filters, group-by, sort, take, cumulative ops | Yes: Python and a qualified native Rust driver (`instruments/systems/polars`) |
| DuckDB 1.5 | Compressed storage (constant, RLE, bitpacking, dictionary, ALP), zone maps, perfect-hash aggregation; 2048-value vectors | M3 at rest, M8 coarse | Bytes, filtered scans, low-cardinality group-by, updates | Yes: C API over owned typed tables (`instruments/systems`) |
| TurboPFor | Bit packing, PFor, delta in 128/256-value blocks | M3 | Encode and decode throughput; block random access | Yes: pinned, block points qualified (`instruments/codecs`) |
| FastLanes | Transposed 1024-value vectors; fused FFOR, delta, RLE, dictionary decode | M2, M3 | Decode throughput ceiling | Yes: pinned FFOR (`instruments/codecs`); its random-access entry is unqualified |
| ALP (reference implementation) | Per-vector decimal exponent and factor; ALP_RD | M3 for floats | Float encode, decode, bytes | Yes: ALP_RD audited for general IEEE; automatic decimal ALP fails at the pin (`instruments/alp-audit`) |
| BtrBlocks | Sampled cascade selection | M3 | Analysis and sampling cost (stage 0), decompression | No |
| CRoaring | Compressed bitmaps | M8 for sets | Selection masks, rank and select, presence | Yes: pinned CRoaring32 (`instruments/selection-controls`) |
| Highway vqsort, x86-simd-sort, a radix sort | Native keys | — | Reorder family | Yes: pinned vqsort (`instruments/selection-controls`) |
| Ikea Hashtable and Abseil | Native keys | — | Hash probe and grouping consumers | Yes |
| `std::lower_bound`, Eytzinger layout, Ikea Intervals | Native or encoded ordered data | — | Search family | Partly |
| Calico Frame kernels | Packed residuals with symbols | M1, M2, M7 | Parity per the brief | Yes: pinned native exact controls, functionally qualified, not timed (`bed6a1ea`) |
| Parquet + ZSTD, DuckDB storage | At rest | — | Bytes | Yes |

Front A owns this roster and its qualification ([instruments](instruments/README.md));
the table records status as of 2026-10-24. BtrBlocks is not yet acquired.
The "lacks" column is a hypothesis about each baseline until its actual entry
points and plans are inspected.

## Targets

Ratio is baseline time divided by Frame time, for the same exact result on the
same number of threads. "≥ 2×" is a hypothesis to test. "Parity" means at
least 0.9×. "Accept ≤ 2× slower" marks an expected loss that another mechanism
or another representation must repay. "1c / sat" separates one core from
every core streaming. Unless a row says otherwise, the baseline is Polars on
the logical type.

### Encoding (pack, unpack, transcode)

| Operation | Mechanisms | Baseline | L1/L2 | LLC 1c / shared | DRAM 1c / sat | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Unpack FOR K ≤ 32 to a native vector | M2, M9 | FastLanes, TurboPFor | Parity | Parity / parity | Parity / parity | The same mechanism; 256-row blocks must not lose to 1024-value vectors |
| Patched (exception) decode | M10 | TurboPFor PFor | Parity | Parity | Parity | Calico was 1.9–4.7× slower until patching fused with the reader |
| Encode: analysis, fitting and packing | Priors, M9 | TurboPFor encode; DuckDB checkpoint | ≥ 0.5× TurboPFor, ≥ 3× DuckDB | — | Same, input streamed | Bytes must also be ≤ TurboPFor's |
| Bytes at rest, integer columns | M1, M3 | DuckDB storage; Parquet + ZSTD | ≥ 1.3× smaller than DuckDB; within 10% of Parquet + ZSTD | | | Phase 1 model: 1.47× and +10% (original order) |
| ALP encode and decode | M2 | ALP reference | Decode parity, encode ≥ 0.8× | Parity | Parity | No phase 1 work |
| Transcode (width change, Local ↔ Striped, code remap) | M10 | Decode, then encode | ≥ 2× | ≥ 2× | ≥ 2× | Direct conversion, no materialised vector |

### Data movement (read, write, gather, scatter, append)

| Operation | Mechanisms | Baseline | L1/L2 | LLC 1c / shared | DRAM 1c / sat | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Independent point reads (throughput) | M5, M6, M9 | Native array | Accept ≤ 2× slower | Parity / parity | Parity / parity; ≥ 3× when only Frame fits LLC (M4) | Phase 1 was 10–20× slower; not yet attributed |
| Independent point reads, FOR and PFor | M5, M9 | TurboPFor direct access | Parity | Parity | Parity | The same mechanism; TurboPFor's prepared and unprepared costs both need pricing |
| Independent point reads, delta | M5 | TurboPFor delta block decode | ≥ 3× | ≥ 3× | ≥ 2× | Fewer residuals and fewer lines per point |
| Dependent point chain (latency) | M5, M4 | Native array | Accept ≤ 2× slower | Accept ≤ 1.5× | Accept ≤ 1.5×; ≥ 2× with M4 | The descriptor plane stays resident; one body miss per point |
| Gather by index list | M4, M6, M3 | Polars `gather`, native | Parity | Parity / ≥ 1.5× | ≥ 1.5× clustered indices; parity dispersed | |
| In-place write that fits | — | Native store | Accept ≤ 1.5× slower | Same | Same | Effects and journal are the extra cost |
| Write that becomes an override | — | Native store | Accept ≤ 3× slower | Same | Same | Above that, Engine should choose a native region |
| Append to a plain tail, then seal | M9 | Native vector append; encode as above | Parity on append | | | Seal throughput is the encode row |

### Comparison and selection

| Operation | Mechanisms | Baseline | L1/L2 | LLC 1c / shared | DRAM 1c / sat | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `COUNT(x op c)`, FOR K ≤ 8 | M2, M8, M9 | Polars Int64 | ≥ 4× | ≥ 4× / ≥ 6× | ≥ 4× / ≥ 6× | Against native narrow: parity, ≥ 1.5× when block widths are narrower than the global one |
| `COUNT(x op c)`, FOR K 9–16 | M2, M3 | Polars Int64 | ≥ 2× | ≥ 2× / ≥ 4× | ≥ 2× / ≥ 4× | |
| `COUNT(x op c)`, run and constant blocks | M1, M9 | Polars | ≥ 5× | ≥ 5× | ≥ 5× | For ≤ 16 runs per block, or when bounds decide the block (UserID: 31× native measured) |
| `COUNT(x op c)`, incompressible 64-bit with a head plane | M7 | Polars, native | ≥ 1.5× | ≥ 2× / ≥ 3× | ≥ 3× / ≥ 4× | Without a head: parity at best |
| Predicate → selection mask | M2, M10 | Polars Boolean | ≥ 2× for K ≤ 16 | ≥ 2× | ≥ 3× sat | |
| Predicate on A → SUM of B, clustered 1–10% selection | M8, M10 | Polars; native fused | ≥ 5× Polars; parity native | ≥ 5×; ≥ 1.5× native shared | ≥ 5×; ≥ 1.5× native | The cold-scan thesis |
| Same, dispersed fine selection | M10, M6 | Polars; native fused | ≥ 2× Polars | Parity–1.5× | Parity–1.5× | Every line is touched; only bytes per value help |
| Range or IN on dictionary codes | M11, M2 | Polars categorical | ≥ 2× | ≥ 2× | ≥ 3× sat | |
| Predicate on monotone delta | M1, M5 | Any scan | ≥ 10× | ≥ 10× | ≥ 10× | A search over checkpoints |
| Predicate on non-monotone prediction | — | Frame FOR | Accept ≤ 1.5× slower | | | A reason to choose map over predict for filtered columns |

### Search and lookup

| Operation | Mechanisms | Baseline | L1/L2 | LLC 1c / shared | DRAM 1c / sat | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `lower_bound` on a sorted column | M1, M5, M4 | `std::lower_bound` on native | Parity | ≥ 2× | ≥ 3× | The descriptor min plane is resident; binary search takes many dependent misses |
| Same | M1, M5 | Eytzinger layout on native | Parity | Parity | Parity | Needs an extra index on the native side |
| Find first match in an unsorted column | M8, M2 | Polars `arg_true` / native scan | ≥ 2× | ≥ 2× | ≥ 2× | Clustered data |
| Rank ↔ offset over presence | M8, M9 | CRoaring rank | Parity | Parity | ≥ 1.5× | Per-block counts stay resident |
| Hash probe with encoded keys | M3, M11 | Hashtable with native keys | Parity | Parity | ≥ 1.5× narrow keys | Coded keys: one probe per distinct, ≥ 3× when distinct ≪ rows |

### Transformation

| Operation | Mechanisms | Baseline | L1/L2 | LLC 1c / shared | DRAM 1c / sat | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| `x ± c` with an encoded result | M12 | Polars (new array) | ≥ 20× | ≥ 20× | ≥ 20× | Per block, not per row; only if the consumer takes encoded results |
| `x ± c`, `x · c`, masks with a materialised result | M2, M3 | Polars | ≥ 1.5× | ≥ 1.5× | ≥ 2× | Output stores dominate |
| Hash of values | M11 | Polars hash | ≥ 3× when distinct ≤ rows / 8 | ≥ 3× | ≥ 3× | FOR bodies: parity, the full value must be hashed |

### Reduction and scan

| Operation | Mechanisms | Baseline | L1/L2 | LLC 1c / shared | DRAM 1c / sat | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Exact SUM, FOR K ≤ 8 | M2, M9 | Polars Int64; native narrow | ≥ 4×; parity | ≥ 4× / ≥ 6× | ≥ 4× / ≥ 6× | |
| Exact SUM, run and constant blocks | M1, M9 | Polars; native narrow | ≥ 5×; ≥ 2× when runs are few | ≥ 5× | ≥ 5× | For few runs per block. At OS's median 24 runs, parity with native u8 is the ceiling; full-block SUM should come from block summaries |
| Exact SUM, delta prediction | M1 (weighted form) | Reconstruct then sum; Polars | ≥ 2×; parity–2× | ≥ 2× | ≥ 4× sat | |
| Exact wide SUM, 64-bit | M2 (split halves) | Polars Int128; native halves | ≥ 5×; parity | Same | Same | |
| MIN, MAX | M1, M8 | Polars | ≥ 20× with exact statistics | ≥ 20× | ≥ 20× | Conservative statistics fall back to bodies |
| SUM, COUNT, MIN, MAX in one pass | M10 | Separate Frame calls; Polars `select` | ≥ 1.5×; ≥ 3× | ≥ 1.5× | ≥ 1.5× | |
| Grouped SUM/COUNT, ≤ 256 groups, run-heavy | M1, M11 | DuckDB perfect-hash aggregate; Polars `group_by` | ≥ 3×; ≥ 5× | ≥ 3× | ≥ 3× | |
| Same, random codes | M11, M2 | DuckDB | ≥ 1.5× | ≥ 1.5× | ≥ 2× sat | |
| Segmented reduction by runs of a key | M1 | Polars `group_by` over run ids | ≥ 5× | ≥ 5× | ≥ 5× | |
| Prefix sum with a wide materialised result | M2 | Polars `cum_sum` | ≥ 1.5× | Parity–1.5× | Parity | Stores of 8 B/row dominate; little to win |
| Fixed-window sums | M1, M2 | Polars `rolling_sum` | ≥ 2× | ≥ 2× | ≥ 2× sat | |
| Deterministic float SUM | ALP exact integers | Superaccumulator over doubles; Polars f64 | ≥ 3×; accept parity | ≥ 3× | ≥ 3× | Polars' order-dependent sum is a weaker result |

### Reordering

| Operation | Mechanisms | Baseline | L1/L2 | LLC 1c / shared | DRAM 1c / sat | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Sort narrow keys (K ≤ 16) | M13 | Polars sort; vqsort | ≥ 2× | ≥ 2× | ≥ 3× | K/8 radix passes instead of 8 |
| Sort a run-heavy column | M1, M13 | Polars sort | ≥ 10× | ≥ 10× | ≥ 10× | Sort runs, not rows |
| Partition by code or high part (fan-out ≤ 256) | M13 | Native radix partition | ≥ 2× | ≥ 2× | ≥ 2× | |
| Filter or compact to an encoded result | M3, M10 | Polars `filter` | Parity | ≥ 1.5× | ≥ 2× | The result keeps its base and width |
| Merge two sorted columns | M5 | Native merge | Parity | ≥ 1.5× | ≥ 1.5× | |
| Apply a permutation | M6 | Native gather | Parity | Parity | Parity | Gather-bound |

## Where we expect to lose

These losses are accepted because another representation or mechanism should
repay them. Engine chooses between representations, so a loss here is a
reason to offer a native region, not to tune further.

- Independent points at L1 against a native array: up to 2× slower.
- Hot full scans of incompressible 64-bit columns without a head plane: parity
  at best.
- Update-dominated histories: phase 1 already found native winning every such
  cell.
- Prefix scans with a wide materialised output: store-bound everywhere.
- High-cardinality hash grouping: bound by the hash table, which Frame only
  feeds.

## From targets to measurements

1. The instruments thread measures sequential and random bandwidth per tier,
   one core and saturated, on Zen 5 and Graviton 4, and lands the missing
   peers.
2. Each research thread restates its rows with those numbers: predicted time
   per value from the mechanism, and the baseline's measured time.
3. A cell counts when the mechanism's prediction and the measurement agree.
   When they disagree, the explanation is the finding: either the mechanism
   was wrong or the implementation pays an overhead the mechanism did not
   account for. Phase 1's per-block cost is the example of the second.
