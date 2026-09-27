# IKEA on Neoverse V3

Investigation on 2026-09-27: which IKEA operations improve on C9g, and which
compiler or source choices improve them further? The repository search found
no retained C9g IKEA comparison before this study. The source baseline is
`03f015217072aa6f31cfaf95f911adeb8a988848`.

Experiments live in this spike. Maintained IKEA kernel sources are unchanged.
The branch adds the explicit `SIXDB_TUNE=neoverse-v3` build choice, its policy
flag and build checks; it does not change the default tune.

## Identical-binary hardware comparison

Both hosts ran the same SHA-256-verified Clang 21.1.8 binaries, compiled with
`-march=armv8.2-a+simd -mtune=neoverse-v2`. This separates the hardware change
from compiler changes. The two private Spot workers were `c8g.xlarge` and
`c9g.xlarge`, four cores and 8 GiB each. CPU receipts identify Neoverse V2 and
V3 respectively; the worker AMI preset's `neoverse-v2` label is not a hardware
claim. Advertised frequencies are 2.8 and 3.3 GHz.

| Maintained suite | Cases | Median case speedup C8g/C9g | Geometric mean |
| --- | ---: | ---: | ---: |
| SeriesPack | 150 | 1.293× | 1.296× |
| TuplePack | 61 | 1.206× | 1.239× |
| TuplePack routine words | 40 | 1.240× | 1.255× |
| Bec256 | 78 | 1.205× | 1.212× |

These are descriptions of 329 microbenchmarks, not workload-weighted database
scores. The frequency ratio alone is 1.179×; the measurements do not isolate
frequency from other microarchitectural differences. Examples in CPU ns:

- SeriesPack `ordinary/k12/striped/read/bound`: 505.61 → 342.88 (1.475×).
- SeriesPack `placed/k63h16/separated/all/mutation-bound`: 17395.93 → 11632.46 (1.495×).
- TuplePack `packets/64/one_byte/half/sum`: 374.77 → 233.73 (1.603×).
- SeriesPack `ordinary/k64/local/point/bound`: 6289.70 → 6314.95 (essentially neutral).
- Bec256 random compiled exact pair: 162.95 → 134.89 (1.208×).

## Compiler scheduling is the strongest lead

In the isolated Bec256 read/intersect/popcount consumer, changing only the
compiler tune improves the unchanged maintained `native::read_pair` body by
about 1.5× on **both** generations. Trial 1 CPU ns per two-block consumer:

| Hardware | Span / data | V2 tune | V3 tune | Speedup |
| --- | --- | ---: | ---: | ---: |
| C8g | exact / random | 159.79 | 104.49 | 1.529× |
| C9g | exact / random | 132.60 | 87.75 | 1.511× |
| C8g | padded / random | 156.33 | 101.07 | 1.547× |
| C9g | padded / random | 130.33 | 85.14 | 1.531× |

The reversed-order second trial reproduces these gains. This is a compiler
scheduling opportunity that also benefits V2, not evidence of a V3-only ISA
requirement. The study-local override changes `-mtune` while retaining V2
source policy flags, so this discovery run excludes source-policy changes.

Disassembly from the C9g archive confirms 644 versus 640 instructions and
42 TBL instructions in each body. V2 tuning largely completes one dependent
population tree before starting the other. V3 tuning loads both inputs earlier
and alternates independent tree stages. The number of instructions changes
little; exposed instruction-level parallelism is the plausible mechanism.
This is an assembly-based explanation, not a PMU attribution.

The **maintained 78-case Bec256 suite confirms the gain on C9g** with the
explicit global tune option, seven repetitions and two reversed-order trials:

| Maintained C9g consumer, trial 1 | V2 tune ns | V3 tune ns | Speedup |
| --- | ---: | ---: | ---: |
| Random exact, compiled, one pair | 135.54 | 90.51 | 1.498× |
| Random padded, compiled, one pair | 133.09 | 87.22 | 1.526× |
| Runs exact, compiled, one pair | 142.83 | 98.64 | 1.448× |
| Random padded, compiled, 16 pairs | 2124.32 | 1383.40 | 1.536× |
| Random exact, CPS, one pair | 135.43 | 127.13 | 1.065× |
| Random exact, materialized, one pair | 133.23 | 124.41 | 1.071× |

Inline consumers similarly improve about 1.46–1.51×. CPS and materialized
consumers gain less, so 1.5× must not be generalized to all compositions.
The full case-matrix median is 1.072×. Random checked encoding regresses
26.04 → 26.99 ns (0.965× speedup); native checked encoding regresses about
3%. Single-block exact decoding is effectively unchanged. The second trial
repeats those results. Other modules have not had a full compiler-tune sweep,
so this supports an explicit option and targeted adoption, not a global default
change. The measured baseline and this separately linked experiment should
not be multiplied into an assumed end-to-end gain.

The maintained suite also confirms the compiler gain on C8g: random exact
compiled pair 162.82 → 108.82 ns (1.496×), and padded 160.31 → 104.71 ns
(1.531×). The C8g case-matrix median is 1.067×. This reinforces that the
largest discovered opportunity is shared by V2 and V3.

## Source experiments

### Bec256 lookup and exact-span loading

Seven consumers share the same call boundary: unchanged maintained code,
a factored control, TBL2 decomposition, chained TBX4, explicitly interleaved
trees, SVE exact-span loading, and SVE loading with interleaved trees. Each
runs nine input shapes with exact and padded spans, plus three standalone
lookup controls, under both compiler tunes.

Splitting the 256-byte lookup into TBL2 operations or chaining TBX4 loses.
On C9g with V2 tuning the isolated lookup takes 5.50 ns for current TBL4,
6.02 ns for TBL2, and 7.01 ns for TBX4. With V3 tuning, factored and rewritten
full consumers mostly lose to the better-scheduled maintained source.

There is a narrower C9g signal for **SVE predicated loading plus interleaving**:
with V3 tuning, exact spans for runs and populations 1/16/64/248/255 improve
by 7.6–8.9% in throughput (roughly 95–96 ns → 88.6–88.7 ns). Random and
population-128 exact inputs lose about 1%; terminal inputs and padded spans
lose about 3%. It loses on C8g. This combined variant does not isolate loading
from traversal scheduling, and it does not justify replacing all exact loads.
It is a candidate for a carefully selected native path if those inputs matter.

### TuplePack lookup splitting and GPR/SIMD

A build-private overlay replaces TBL3 with TBL2 plus TBL1, and TBL4 with two
TBL2 operations. The routine 61-case suite and a 190-case word subset were
run twice with reversed stock/split order. Median ratios are effectively 1.0;
the neutral cases include unaffected GPR paths and therefore are not evidence
that changed SIMD paths are neutral.

The broad rewrite should be rejected. On C9g, map2 word hash SIMD cases regress
severely: width1/unit16/stride64 takes 1813 → 3481 ns in trial 1 and
1771 → 3126 ns in trial 2. Packet4 scans regress about 10–14% in throughput.
There are narrower winners, including width1/map0 SIMD word hash and the
packet8 half-mask sum, but some controls also shift across trials. Code layout,
register allocation and map-specific lowering need separating before promoting
a narrow winner.

The broadened stock measurements do not support switching everything to SIMD
on V3. For two-byte word updates, GPR remains faster for contiguous map0:
1594 versus 4530 ns on C9g. For map2/unit16/stride16, SIMD-word is faster:
4895 versus 16583 ns. The direction is the same on C8g (5821 versus 18995 ns
for that map2 case). Format and operation remain stronger selectors than a
blanket generation switch.

### SeriesPack mixed-local residual transposes

A build-private overlay uses the existing NEON `transpose128` for mixed-local
residuals instead of the current pair of integer-register transposes. Fifteen
widths (11/12/13/15/19/20/23/28/31/35/36/39/47/55/63), ordinary reads and
maintained writes, and bound/concrete calls give 60 cases over 8192 values.
Both variants use V2 compiler tuning and run five repetitions at 0.05 s, twice
with reversed order. The full maintained SeriesPack check passes for both.

The vector variant loses in **all 60 cases on both hosts**, with median speedup
0.731× on C8g and 0.694× on C9g.
For example, bound k28 read takes 1148 → 2186 ns, bound k36 read 1592 →
3151 ns, and bound k12 maintained-write 2823 → 4242 ns. The second trial
reproduces those losses. Keep the current integer-register residual path;
V3's additional lookup throughput does not make this vector rewrite useful.

## Why these experiments

The [Arm V2 optimization guide](https://documentation-service.arm.com/static/668bc0a369e89f01e39c4668)
and [V3 optimization guide](https://documentation-service.arm.com/static/6734eb2627eda361ad4da4f4)
report doubled NEON TBL throughput: one/two-register tables 2 → 4
instructions/cycle, three-register tables 1 → 2, and four-register tables
2/3 → 4/3. Lookup latency is unchanged. Both have 128-bit vector units;
SVE2 BDEP/BEXT remain six-cycle latency and 0.5 instructions/cycle. This points
toward independent lookup chains and batching rather than a wider-vector
rewrite or a new bit-deposit windfall. [AWS's guide](https://github.com/aws/aws-graviton-getting-started#building-for-graviton)
identifies Graviton4 as V2 and Graviton5 as V3.

Scaffolding LEAD confirmed the [existing tuning contract](../../design/tuning.md):
`SIXDB_MARCH` and compiler feature macros govern ISA legality;
`SIXDB_TUNE` independently supplies `-mtune` and 0/1 policy flags. Use `#if`,
not `#ifdef`, for those flags. No runtime CPU autodetection or environment
dispatch is established. Any source specialization should stay near the native
body/preparation and be shared by ordinary and composed consumers.

## Methods, checks and limits

Benchmarks are pinned to one core, run sequentially, with no concurrent build
on the worker. Five repetitions use at least 0.05 s per case for the hardware
baseline, 0.04 s for Bec256 exploration, and 0.03 s for TuplePack exploration.
Candidate trials reverse variant order. This does not randomize function
placement or estimate between-instance variance. One instance of each type
was used, with resident benchmark fixtures and each suite's existing access
patterns. No cold-memory, whole-database, NUMA, power or cost claim follows.

Before Bec256 exploration, every variant was checked bit-by-bit against
uncompressed data for all 257 populations at both edges of a writable page
between inaccessible guard pages. Those fixtures cover encoded lengths 0–41;
they do not exhaust every possible encoded bit pattern or length. Timed
fixtures independently check the result against bytewise AND and popcount.
Both AWS hosts ran the SVE checks. The current runner also requires SVE2 and
a 128-bit runtime vector length; the recorded exploration used only an SVE
availability check on these known 128-bit hosts. Only `sve.cpp` enables SVE2.

Maintained Bec256 and composition checks and TuplePack packet/GPR checks ran
on both baseline hosts. TuplePack overlay runs additionally passed wire,
operations and execution checks. The build check passes with the new tune
choice, including fixed-ISA macro equivalence and rejected cross-architecture
tunes. Local guards pass without SVE. Full worker archives retain binaries,
disassembly, compiler commands, captured sources and checks.

The useful next production step is the Bec256 scheduling choice, with explicit
attention to encoder-heavy workloads and CPS/materialized consumers. The SVE
exact-span signal is narrower and needs an isolated loading comparison before
promotion. Broad lookup splitting and vectorizing the mixed-local residuals
are negative results worth preserving.

## Reproduction and evidence

Run from the Linux checkout with the pinned toolchain; from macOS prefix
Linux commands with `orb -m ubuntu`. Worker-group specs are:

- `baseline-group.json`: build once on C8g, transfer and hash-check on C9g.
- `explore-group.json`: Bec256 source/compiler comparison.
- `tuple-group.json`: TuplePack stock/split comparison.
- `followup-group.json`: full maintained Bec256 compiler comparison followed
  by SeriesPack mixed-local residual-transpose exploration.

The worker tool can reuse a compatible idle worker. Set `fresh: true` in the
spec's config for a fresh pair, or coordinate reuse of existing resources.
Never reuse a worker that another task is measuring on.

```sh
python3 workbench/tools/worker_group.py run workbench/spikes/ikea-neoverse-v3/baseline-group.json --output build/ikea-neoverse-v3/baseline-group --detach
python3 workbench/tools/worker_group.py wait build/ikea-neoverse-v3/baseline-group/group.json
python3 workbench/spikes/ikea-neoverse-v3/analyse.py baseline build/ikea-neoverse-v3/baseline-group/group.json --output build/ikea-neoverse-v3/analysis
```

`analyse.py` accepts `baseline|explore|tuple|followup`. It keeps every selected
case, all raw repetitions in ns, per-case medians, variability, input hashes
and benchmark context. Trials remain separate. The complete small comparison
matrices are selected so that regressions and negative alternatives remain
visible. Named-member references point to each worker archive and source hash;
bulky output remains in S3. No retained binary depends on the final branch
commit being identical to its captured source: the source archive is authoritative.

Selected evidence is retained in [evidence/2026-09-27](evidence/2026-09-27/):
[hardware baseline](evidence/2026-09-27/baseline-cases.csv),
[Bec256 candidates](evidence/2026-09-27/explore-cases.csv),
[maintained compiler comparison](evidence/2026-09-27/tune-cases.csv),
[TuplePack routine](evidence/2026-09-27/tuple-cases.csv),
[TuplePack words](evidence/2026-09-27/tuple-words-cases.csv), and
[SeriesPack transpose](evidence/2026-09-27/series-cases.csv).
The adjacent `*-inputs.json` records input hashes and contexts;
`*-references.json` carries the original worker recovery references.
The selected comparison bundle has its own verified `artifact.json` and
provenance. Recover it with the shared artifact tool, or recover an original
worker archive by passing a named member's `artifact` object as the reference.
