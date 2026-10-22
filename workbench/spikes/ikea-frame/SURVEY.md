# Prior art: operating on encoded data

Survey for the [Ikea Frame spike](README.md), 2026-10-22. It asks which
operations each encoding stage permits without full decoding, who has already
built that, and what their evidence says. It informs a design; it does not
choose one.

**Evidence labels.** *Source*: read in the local Calico (`ac83c82b`) or SixDB
tree. *Measured*: a number from that tree's own report, with its conditions.
*Checked*: external claim confirmed against the paper's abstract or project
documentation during this survey. *Recalled*: external claim from memory, not
re-verified here; check the primary text before relying on it. Calico numbers
are reference baselines, not SixDB results ([calico.md](../../notebook/calico.md)).

## 1. Capability matrix

Rows follow the rev 2 pipeline stages. Columns are Ashton's operation families.
Each cell names the in-place mechanism, then who already has it:
**C** Calico, **I** current Ikea, **X** external. ✔ answers without decoding
values, ◐ decides part of the work or answers some cases, ✗ needs decode or
re-encode. "Decode" means the stage's inverse, not necessarily all stages.

| Stage | Movement (read, gather, write, append) | Compare / select | Search / lookup | Transform | Reduce / scan | Reorder |
| --- | --- | --- | --- | --- | --- | --- |
| **Constant** | ✔ broadcast; write breaks it (patch or re-encode) · C X | ✔ one test decides the block · C X | ✔ trivial bounds, rank = offset · C | ✔ apply once, stays constant · X | ✔ `c·n`, min = max = c, one group · C X | ✔ no-op; partition is all-one-side |
| **Runs (RLE)** | ◐ point read = run search; write splits/fuses runs · C X | ✔ per run, emit run masks · C X | ✔ run-length prefix gives rank ↔ offset · C | ✔ per run value · X | ✔ `Σ v·len`, count = len, runs are pre-grouped; segmented scans per run · X | ◐ sort/merge runs; filter shortens runs · C |
| **Order-preserving map** (offset binary, float map) | ✔ fixed width · C I | ✔ shift constant into mapped domain · C I | ✔ bounds on mapped values · C | ◐ hashing needs canonical NaN/zero; arithmetic needs bias correction · C | ◐ min/max ✔; sum needs `−n·bias` | ✔ unsigned sort/merge · C |
| **Decimal / ALP** (integer × 10^−e, exceptions) | ✔ per-vector e/f; exceptions patched · X | ◐ constant encoded at the vector's (e,f); exceptions separately | ◐ bounds within one (e,f) | ◐ scaling by 10^k is exponent change | ◐ exact integer sum then one rounding: deterministic but **not** IEEE sequential semantics · C (kmath) | ◐ sort within one (e,f) |
| **High/low split** (heads, ALP_RD, byte planes) | ◐ point read touches both parts · I | ◐ high part decides most rows; low only for ties · C I X | ◐ high-part bounds narrow the range · I | ✗ hashing needs both | ✔ `2^s·Σhi + Σlo`; min/max via high then tied low · C | ◐ MSD radix/partition by high part |
| **Null compaction / routing** | ◐ gather needs position → index rank · C | ✔ evaluate compacted values, expand mask by presence · C | ◐ rank over presence bitset · C | ✔ only on present values | ✔ count = popcount; aggregates skip nulls by construction · C | ✔ compact is free; partition by route |
| **Dictionary, order-preserving** | ✔ codes fixed width; write may need a new code | ✔ range → code range; IN → code bitmap · C X | ✔ bounds on codes · C | ◐ f over dictionary entries (n_dict work), hash entries once | ✔ histogram of codes · values (`Σ count[c]·v[c]`); distinct/group on codes · C X | ✔ sort on codes; cross-segment merge needs translation · C |
| **Dictionary, by frequency** | as above | ◐ equality/IN only · X | ◐ point lookup only | as above | as above, no min/max on codes | ✗ order lost |
| **Prediction / delta** (Boundlist Sizes, Relative Ends) | ◐ point = checkpoint + local prefix; a value write changes two deltas; append cheap · I C | ◐ monotone data: predicate becomes a search · I; otherwise ✗ | ✔ monotone bounds via checkpoints · I | ✗ | ◐ `Σ d = last − first`; prefix scan *is* decode; monotone min/max = ends · C | ◐ sorted already when monotone; merge of sorted streams · C |
| **Frame of reference** | ✔ in-bounds point write in place; else outlier list · I C | ✔ subtract base from constant; block box decides PASS/FAIL · C | ✔ residual-domain bounds and indexof · C I | ◐ `x+c` changes base only; `x·c` widens residuals | ✔ `n·base + Σr`; min/max = base + residual extreme; second moments by base shift · C | ✔ within block; across blocks rebase · C |
| **Patches** (outliers, sideband) | ◐ every read checks the sideband · C | ◐ evaluate patches separately, merge masks · C | ◐ search must consult patches | ◐ as main stage plus per patch | ◐ correct each patched slot · C | ◐ |
| **Packing** (SeriesPack, TuplePack layouts) | ✔ point/range read, in-place write, packets · I C | ✔ direct packed compare, full-width rhs · I C X | ✔ indexof · I | ◐ narrow-lane arithmetic | ✔ narrow-lane sums; popcount counts · C | ◐ |
| **Block statistics** (min, max, count, sum) | n/a | ✔ whole-block decide; partial blocks fall through · C X | ✔ skip blocks · C X | n/a | ✔ whole-block aggregates; top-k bounds steer descent · C | ◐ block-level ordering hints |
| **Bitsets** (Bec256, Keyset kinds) | ◐ rank/select over kinds · I C | ✔ set algebra on encoded kinds · I C X | ✔ membership in place · C | n/a | ✔ count from population metadata · I C | ✔ compact = mask |

### Readings across the matrix

- **The sum family is the richest seam.** FOR, high/low split, constant, runs,
  dictionary histograms and block statistics all reduce SUM to narrow work plus
  a correction term. Ashton's prefix example is the high/low row with the high
  part constant. Calico's measured win came from keeping lanes narrow, not from
  the algebra alone (§2).
- **Comparison survives order-preserving stages and dies at delta and frequency
  rerank.** That fixes which presets suit filter-heavy columns.
- **Patches appear in every column as ◐.** The sideband cost is uniform across
  families, so its layout (aligned with blocks, groups or neither) matters more
  than any single kernel.
- **Delta is search-friendly but compare-hostile.** Boundlist already serves
  the search column; prediction residuals would serve only decode and sums.
- **Reorder is mostly ✗ beyond order-preserving codes.** Sort and partition will
  usually run on decoded or code values; the useful encoded cases are
  partition-by-high-part and run merging.

## 2. Calico

Calico's [Frame SPEC](../../../../calico/frame/SPEC.md) centred on one idea:
a per-frame **symbol** naming `(offset, pow2 range)` per field from a learned
codebook, so the symbol stream is a scannable table of contents for the
residuals ("knowing without looking"). Engine owned operators; Frame owned
kernels; Gym fitted codebooks offline.

| Exploited opportunity | Where (Source) | Evidence and limits |
| --- | --- | --- |
| Split-arithmetic SUM/MIN/MAX: `offset·n + Σ residual`, narrow lanes, patch correction | [`frame/aggregate.h`](../../../../calico/frame/include/frame/aggregate.h), [`engine/raw_farray_ops.h`](../../../../calico/engine/include/engine/raw_farray_ops.h) | *Measured*: u8 SUM 0.030 vs 0.113 ns/value decode-then-sum, 4096 L1-hot values, Graviton4; spike reported 3.9× laptop / 5.7× G4. x86 path had not landed in the reported rows. |
| Byte-plane SUM as paired radix-256 reduction | `raw_farray_ops.h` | *Source note*: joining planes into u16 then summing was slower than a wide scalar scan at K=16 on NEON. |
| Symbol bounding-box prepass: PASS / FAIL / MAYBE per frame by LUT over symbols | [`frame/prepass.h`](../../../../calico/frame/include/frame/prepass.h) | SPEC §5.2 negative: selectivity collapses under drift (wide symbols or patches defeat boxes), so "no plan may depend on prepass selectivity". |
| MSB-first byte-plane range refinement | `frame/byteplane.h`, `raw.h` `range_mask` | Refines MAYBE frames without materialising u16 residuals. |
| Direct packed predicate scans | `frame/scan.h` (shuffle-align), `bitweave.h`, `scancmp.h` | *Measured*: BitWeaving/H COUNT 30–290× faster than decode-and-compare at a `1/W` storage tax. Rebuilding shuffle tables per call was a 2–3× trap. |
| Set membership on codes (IN) | [`frame/inset.h`](../../../../calico/frame/include/frame/inset.h) | Bitmap sized by dictionary cardinality, not code width. |
| Order-preserving per-segment string dictionaries; code = rank | [`engine/dict.h`](../../../../calico/engine/include/engine/dict.h) | Range predicates become code ranges. Codes are segment-local, so grouping across segments needed persisted `qhash64` fingerprints ([`dict_fingerprint.h`](../../../../calico/engine/include/engine/dict_fingerprint.h)). |
| In-place point write over word-bounded packing | `frame/pointrw.h`, `record.h` | *Measured*: K=13 read/write 1.5–2.2 ns L1, near memory latency at DRAM; PEXT/PDEP never won. |
| Fused (layout ∘ patch semantics) decode tier with a total generic fallback | [`frame/fused.h`](../../../../calico/frame/include/frame/fused.h), SPEC §5.5 | First member: bit-plane-8 ∘ offset-restart (Keyset POST), 1.5–4.2× over re-expressing POST on the generic roster, x86. |
| Two patch-section encodings (flat directory for random access, tight per-frame for size); MDL width selection with OR cap and histogram fallback | [`frame/patch.h`](../../../../calico/frame/include/frame/patch.h), `widthsel.h` | *Measured*: tight patches reached TurboPFor parity on 1% jumps (+7.4% bits) and beat it on bursty gaps (−6%), but scalar patch decode was 1.9–4.7× slower. Width-selection fallbacks were correctness mechanisms, not tuning. |
| Cross-field predictive transform | `frame/transform/predictive.h` | Paid on N-CMAPSS telemetry only; internal shapes stayed below the bar. Matvec dominated decode (31.6 vs 11.5 ns/record on G4). |
| Exact moments with base shift; exact float superaccumulators; monotone float maps | [`kmath/moments.h`](../../../../calico/kmath/include/kmath/moments.h), `superacc.h`, `floatmap.h` | `Σ(B+s)² = N·B² + 2B·Σs + Σs²`. Gives order-preserving float codes and order-independent float sums. |
| Keyset: seven set kinds (empty, full, bitset, array, runs, enum, post) with masked scan, membership, rank and splice writes in place | [`keyset/SPEC.md`](../../../../calico/keyset/SPEC.md) | Rank lets column data stay put when kinds change. POST patches carry absolute positions as prefix-sum restart points, so a mask can cut the chain; POST had no in-place write path. |
| Fine summaries (4096 × 16-position ranges) with explicit presence and DIRTY | `frame/fine_summary.h` | Missing or dirty data always falls back to the exact body. |
| Top-k seeding from cell maxima, then fine ranges, then exact scores | `engine/topk_seed.h` | Bound planes steer descent only when they carry sound bounds. |
| Plane-grain Boolean verdicts `[lo, hi]` closed under the circuit | `engine/verdict.h`, `gates.h` | Claims (empty/full per cell) decide gates without touching bodies. |

Not exploited by Calico: RLE in Frame (listed as future), decimal/ALP-style
float conversion, high/low splits beyond byte planes, frequency rerank, sort or
partition on encoded data, and string predicates over compressed bodies. The
[R6 string study](../../../../calico/workbench/science/prior-art/r6-string-codecs/FINDINGS.md)
is the main negative: LIKE over FSST or front-coded bodies ran 5–43× slower than
the raw memchr sweep, widening with selectivity.

Two Calico constraints shaped all of this: byte-identical state across ISAs
and replicas, and **no in-band learning** (all fitting offline, artifacts
version-bound). SixDB does not inherit them. Following Orbital's data model
([embedded Objects](../../../orbital/README.md#embedded-objects)), Frame formats
are Representations of one Object: all Objects of the same identity must be
logically identical, their representations convertible, and writes applied
deterministically. Ashton confirmed this on 2026-10-22. In-band priors may
therefore choose different bytes on different replicas. The constraint moves to
logical results: an operation's answer must not depend on which representation
served it (§5, lesson 5).

## 3. Current Ikea

Ikea already supplies the packing row and parts of several others.

| Capability | Where | Relevance to Frame |
| --- | --- | --- |
| Packed unsigned arrays, 1–64 bits, Local and Striped tiles, optional 8/16-bit head planes | [SeriesPack representation](../../../ikea/docs/seriespack/representation.md) | The packing stage. Heads are already a high/low split with independent placement. |
| Logical bit-window expressions with child substitution (`value_expression`, `with_tail`, `with_payload`) | [SeriesPack extending](../../../ikea/docs/seriespack/extending.md) | A split value whose parts live in different representations already has a logical node. A natural home for high/low and residual pipelines. |
| Direct packed filters for five unsigned comparisons with full-width rhs; `indexof`; head-guarded evaluation | [Filters](../../../ikea/docs/filters.md) | The compare column for packed and FOR-residual data, once the base subtraction is folded into the constant. Head guards are the split row's ◐. |
| In-place point and range replacement with `sum_change` (modulo-u64 replacement delta) and issued-byte journals | [SeriesPack usage](../../../ikea/docs/seriespack/usage.md) | Ashton's in-bounds write path. Summary maintenance for sum exists; extrema repair does not. |
| Fixed-layout byte-contained codes, packet projections, row selections | [TuplePack usage](../../../ikea/docs/tuplepack/usage.md) | Block metadata (bases, widths, patch counts) and row-major consumers. |
| Ordered-boundary geometry: Absolute Ends, Sizes and Relative Ends, 16-rank checkpoint groups, tombstones, bounds/reverse/overlap | [Boundlist usage](../../../ikea/docs/boundlist/usage.md), [portfolio](../boundlist/PORTFOLIO.md) | The delta stage for monotone data and the rank ↔ offset primitive for runs, null compaction and variable-length values. Its sources already accept SeriesPack/TuplePack projections. |
| Variable-length values over Boundlist geometry with slack (R vs U), retirement and packing transfer | [StreamPack usage](../../../ikea/docs/streampack/usage.md) | Prior art for slack-plus-compaction inside one block. |
| Headless 256-position bitset code with population retained by the caller; native pair composition; size estimates | [Bec256 usage](../../../ikea/docs/bec256/usage.md) | Null/presence masks and patch-position sets; population answers counts. |
| Integer-key sets/maps with mutable accumulation | [Hashtable usage](../../../ikea/docs/hashtable/usage.md) | Grouped reductions on codes feed into it. |
| Native composition: inline bodies and CPS chains carrying native values, coordinates and activity | [Consumers guide](../../../ikea/docs/consumers.md) | The execution substrate for fused presets. The guide's rule: no universal execution style; measure the complete consumer. |

Gaps relative to the matrix: no frame of reference, patches, constant or run
stages; no dictionary or rerank; no decimal or float conversion; no signed
domain (signed meaning belongs to the enclosing representation); no block
statistics container; no capability or cost description an Engine planner could
query. The [residual-bounds note](../../notebook/pfor-bounds.md) retains the
unsettled estimator work and warns that narrower is not always smaller under
SeriesPack's tile geometry.

## 4. External work

| Work | What it contributes here | Label |
| --- | --- | --- |
| Abadi, Madden, Ferreira, *Integrating compression and execution in column-oriented database systems*, SIGMOD 2006 | The original form of this matrix: blocks expose properties (one value, sorted, position-contiguous) and operators specialise on them, over RLE, dictionary and bit-vector encodings. | Recalled |
| [FastLanes compression layout](https://ir.cwi.nl/pub/33289/33289.pdf), PVLDB 16(9) 2023 | 1024-value vectors in a unified transposed order so DICT, FOR, DELTA and RLE decode data-parallel at any register width. Calico adopted the layout. | Checked |
| [FastLanes file format](https://vldb.org/pvldb/volumes/18/paper/The%20FastLanes%20File%20Format), PVLDB 18(11) 2025 | Expression encodings that cascade lightweight schemes, multi-column compression, a two-phase expression search, and a partial-decompression API so engines execute on compressed data at small-batch grain. Closest external analogue to "Ikea vocabulary, Engine decides". | Checked |
| ALP, Afroozeh, Kuffó, Boncz, SIGMOD 2024 | Per-vector exponent and factor chosen by two-level sampling; fused FOR + bit-packing; exceptions patched; ALP_RD splits high-precision doubles into a small dictionary of front bits plus raw low bits. | Recalled |
| BtrBlocks, Kuschewski, Sauerwein, Alhomssi, Leis, SIGMOD 2023 | Sample-driven cascade selection (shallow cascades over RLE, one-value, dictionary, frequency, FOR/bit-packing, pseudodecimal floats, FSST, Roaring nulls). Optimised for decompression speed, not encoded execution. | Recalled |
| Vortex ([docs](https://docs.vortex.dev/)) | Nested encodings with per-encoding compute kernels over encoded arrays; lazily computed statistics shared by kernels and compressor; dictionary filtering by filtering the dictionary first; uses FastLanes, ALP and FSST. Function-level kernel coverage not verified. | Checked (partly) |
| [MorphStore](https://www.vldb.org/pvldb/vol13/p2396-damme.pdf), Damme et al., PVLDB 13(11) 2020 | Operators process compressed base data *and intermediates*; "direct morphing" converts between compressed formats without full decompression; cost-based format choice. Damme et al.'s EDBT 2017 survey supplies per-scheme cost models. | Checked |
| Data Blocks, Lang et al., SIGMOD 2016 | Per-block choice among single value, truncation (FOR), dictionary; SMAs plus PSMA lookup tables that narrow the scan range inside a block; SIMD predicates on compressed blocks. | Recalled |
| SIMD-Scan (Willhalm et al., VLDB 2009), BitWeaving (Li & Patel, SIGMOD 2013), ByteSlice (Feng et al., SIGMOD 2015) | Predicate scans on packed codes; vertical layouts allow early termination on high bits. Calico measured ByteSlice out (dominated by byte planes). | Recalled |
| Order-preserving dictionaries: Binnig et al., SIGMOD 2009; IBM Blink/BLU (Raman et al.) | Range predicates, sort and grouping on codes; BLU's frequency partitioning gives frequent values short codes while keeping order within partitions, a middle ground between the two dictionary rows. | Recalled |
| Compressed Linear Algebra, Elgohary et al., VLDB 2016 | Matrix-vector work on column groups via per-distinct-value aggregation: the `Σ count·value` reduction at scale. | Recalled |
| LeCo, SIGMOD 2024 | Piecewise learned (linear) predictors with bit-packed residuals and random access; an existence proof for prediction without serial chains. | Recalled |
| FSST, Boncz, Neumann, Leis, VLDB 2020 | Random-access string compression; equality on compressed strings by encoding the constant with the same table. Calico's R6 measured LIKE losses (§2). | Recalled |
| GPU-FOR/DFOR/RFOR, Shanbhag et al., SIGMOD 2022 | Tile-based FOR, delta and RLE decoded in one pass inside query kernels. | Recalled |
| DuckDB constant/dictionary vectors; Velox dictionary peeling; Arrow run-end encoding | Engines carrying constant, dictionary and run representations through operators rather than flattening at scan. | Recalled |
| Gorilla, Chimp, Elf | XOR float streams; serial decode, no random access. Calico held them as comparands only. | Recalled |

## 5. Lessons to carry forward

1. **Narrow lanes are where the encoded sum pays.** The algebra is cheap;
   Calico's gain came from summing u8/u16 lanes and correcting once.
2. **Block boxes prune well on clustered, stationary data and collapse under
   drift.** Rev 2's per-group slack and forced compaction may restore box
   quality where Calico's frozen codebooks could not; this is untested.
3. **Patches dominated decode cost.** Calico reached size parity but not speed
   parity until patch application was fused with the layout reader.
4. **A fused tier with a total generic fallback** made specialisation a speed
   question, never a correctness one. That maps onto Ashton's presets: a preset
   is a registered fusion over a vocabulary that always composes.
5. **Encoded comparison does not license encoded arithmetic.** SixDB's
   [semantics note](../../notebook/representations-and-planning.md#plans-that-keep-improving)
   already says so; floating sums over ALP integers are exact and deterministic
   but differ from sequential IEEE addition. Because equivalent representations
   must give logically identical results, an encoded kernel cannot return a
   representation-dependent answer: floating aggregates need exact or
   order-defined semantics, such as kmath's superaccumulators.
6. **Segment-local codes complicate cross-segment work.** Grouping, merging and
   joins on codes need a translation or a stable fingerprint.
7. **Possibility is not profit.** R6's compressed string predicates and
   Calico's predictive transform were correct and lost. The matrix needs costs
   per cell, measured in complete consumers, before Engine plans against it.
8. **External systems mostly optimise decompression** (BtrBlocks, ALP, FastLanes
   2023). Encoded execution with mutation (in-place writes, slack, compaction)
   has much less prior art; MorphStore and Vortex cover execution, not
   maintained writes.

## 6. Open questions

Collected with the spike's other questions in [QUESTIONS.md](QUESTIONS.md).
