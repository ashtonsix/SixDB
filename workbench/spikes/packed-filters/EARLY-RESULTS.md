# Early-exit results and implementation recommendation

Early exits are valuable specializations, but width and ISA alone do not make
one a good default. Keep the current general filters. Use the selected headed
path for consumers whose prefixes predictably resolve most windows before an
expensive suffix, and measure the complete consumer before selecting it.
Do not add a public “early” preset or apply a branch after every packed plane.

The useful distinctions are:

- Existing eight-bit head plus expensive suffix: substantial latency and
  throughput gains under frequent early resolution, across all five profiles.
- Ordinary native-width values: early rejection can shorten latency while
  losing throughput to unconditional SIMD. Full-ZMM controls make this especially
  clear on AVX-512.
- Narrow planes and mixed prefix distributions: extra branches frequently lose;
  zero final matches do not imply that any intermediate work can be skipped.
- TuplePack conjunctions: avoiding a second expensive projection can help, but
  compare against a specialized fused consumer, not only two prepared readers.
- NEON benefits from testing whether a vector is zero before constructing the
  scalar mask. AVX2's explicit `ptest` version did not beat mask extraction;
  AVX-512 already emits an efficient mask-register test.

The head-first specialization has since [graduated](EARLY-GRADUATION.md) as
`composition::guarded(expression)`. Ordinary defaults and hashtable remain
unchanged. This report records the original exploration, not a new format or a
claim about cold scans.

## Reading the measurements

Every paired cell below is **dependent latency / independent reciprocal
throughput**, in **ns per complete sixteen-row operation**; lower is better for
both. TuplePack operations apply two predicates. Query loads, full-mask address
mixing and loop control are included. These are warmed, small-working-set native
measurements, not isolated instruction latencies or DRAM/Loom measurements.

The main matrix covers 18 SeriesPack widths, Local and existing head8 layouts,
all five unsigned predicates, ten distributions, and tight/stride-four TuplePack
columns at widths 1/3/5/8. Each profile has 19,000 aggregate rows with every raw
repetition preserved. [Method and controls](EARLY-EXITS.md) explain the exact
coverage. All five native profiles passed the independent mask oracle and
bounded-load guard checks. Unlike the older filter cohorts, this Granite run
uses the intended Granite Rapids compiler tuning.

`current` is today's filter on the same layout. `progressive` always completes
the same split algorithm that `early` can stop. That pair isolates the stopping
branch; current/fused comparisons establish whether the algorithm is competitive.
The independent fused nonstandard-width control is a byte/plane algorithm, often
weaker than the production suffix filter; beating it alone is not sufficient.

Tables use seven 500,000-iteration focused repetitions unless explicitly marked
as the full sweep or ZMM follow-up. Samples, minima and maxima are retained.
Cases/repetitions are sequential. Predictor training on these finite fixtures is
visible: Zen AVX2 head8 K64 mixed equality's dependent early path goes from
6.70 ns on its first focused repetition to about 5.69 ns on later repetitions
(median 5.70). Its shorter-sweep median is 7.13 ns. The throughput loss persists.
Do not turn one median or a reject percentage into an automatic selection rule.

## Large gains, and the branch-cost reversal

Head8 K64 equality, comparing identical storage. In the uniform fixture the
separate serial diagnostic trace resolves 96.25% of windows at the head. The
mixed fixture resolves 48.96%. Both have zero final matches in this data, but
half the mixed windows need the suffix to establish that.

| Profile | Distribution | Current | Split, unconditional | Split, early |
| --- | --- | --- | --- | --- |
| Zen 5 AVX2 | uniform | 9.54 / 1.90 | 8.96 / 2.62 | 1.70 / 1.05 |
| Zen 5 AVX2 | mixed50 | 9.56 / 1.91 | 9.00 / 2.62 | 5.70 / 5.87 |
| Zen 5 AVX-512 | uniform | 9.88 / 1.94 | 9.38 / 2.27 | 2.01 / 1.10 |
| Zen 5 AVX-512 | mixed50 | 9.88 / 1.94 | 9.39 / 2.27 | 6.83 / 6.15 |
| Granite AVX2 | uniform | 10.87 / 3.74 | 9.96 / 3.04 | 2.42 / 1.37 |
| Granite AVX2 | mixed50 | 10.89 / 3.73 | 9.89 / 3.05 | 9.12 / 6.49 |
| Granite AVX-512 | uniform | 11.74 / 4.60 | 11.14 / 3.31 | 2.45 / 1.37 |
| Granite AVX-512 | mixed50 | 11.79 / 4.58 | 11.18 / 3.30 | 10.07 / 6.93 |
| V2 NEON | uniform | 17.88 / 7.34 | 15.85 / 7.12 | 2.82 / 2.10 |
| V2 NEON | mixed50 | 17.99 / 7.36 | 15.87 / 7.15 | 12.10 / 9.67 |

The improvement from current to unconditional splitting is separate from early
stopping. For example, head8 K12 equality on Zen AVX2 falls from 10.04 / 2.98 to
4.81 / 2.29 ns just by splitting head and suffix; adding the guard reaches
1.62 / 1.36 ns. Attribute only the latter difference to the branch.

Whole-process hardware counters support the branch-cost explanation for head8
K64 mixed equality. The guarded path executes fewer instructions yet more
cycles, with many more branch misses. These are diagnostic counts over setup,
checks and both timing modes, not separate latency/throughput loop counters.

| Profile | Instructions, unconditional → early | Cycles, unconditional → early | Branch misses, unconditional → early |
| --- | --- | --- | --- |
| Zen 5 AVX2 | 4.081B → 3.291B | 1.214B → 2.130B | 38,335 → 36,504,825 |
| Zen 5 AVX-512 | 3.820B → 3.133B | 1.118B → 2.260B | 37,607 → 36,885,087 |
| Granite AVX2 | 4.135B → 3.291B | 1.197B → 2.121B | 37,059 → 38,796,266 |
| Granite AVX-512 | 3.910B → 3.133B | 1.309B → 2.276B | 37,000 → 38,859,455 |
| V2 NEON | 8.021B → 5.205B | 1.835B → 2.252B | 34,679 → 34,174,323 |

x86 counters ran at 100%; V2 events were multiplexed (roughly 50–75% running).
They establish a mechanism, not an exact misprediction penalty. The eight-stream
branch trace differs from the dependent trace, so the same code can improve
latency and lose throughput.

## Width, predicate and selectivity change the answer

For Local K1/K8 there is no remaining component to skip: differences from current
are alternate circuit/code-generation effects, not an early-exit gain. At K3,
uniform data usually leaves a live candidate until the end; the branchless
progressive circuit is at least as good as its guarded equivalent. K5/K7 insert
several less predictable intermediate stopping decisions, with substantial
throughput losses. Byte-containing formats can cheaply resolve many equality
windows at their leading byte, but extracting that byte and branching can still
cost more throughput than comparing the entire native word.

Selected width contrasts, equality. The K5/K63 rows use the five-repeat full
sweep; K3 and head8 K12 use the longer recheck.

| Profile | Format | Distribution | Current | Progressive | Early |
| --- | --- | --- | --- | --- | --- |
| Zen 5 AVX2 | local K3 | uniform | 4.07 / 1.73 | 3.89 / 1.65 | 3.90 / 1.76 |
| Zen 5 AVX2 | local K3 | reject_first | 4.09 / 1.72 | 3.87 / 1.64 | 1.36 / 0.87 |
| Zen 5 AVX2 | local K5 | uniform | 4.84 / 2.39 | 4.31 / 2.36 | 4.17 / 4.98 |
| Zen 5 AVX2 | head8 K12 | uniform | 10.04 / 2.98 | 4.81 / 2.29 | 1.62 / 1.36 |
| Zen 5 AVX2 | local K63 | uniform | 15.37 / 4.80 | 15.27 / 8.38 | 1.97 / 2.06 |
| Zen 5 AVX-512 | local K3 | uniform | 6.47 / 0.96 | 3.88 / 1.64 | 3.87 / 1.75 |
| Zen 5 AVX-512 | local K3 | reject_first | 6.45 / 0.96 | 3.87 / 1.64 | 1.36 / 0.79 |
| Zen 5 AVX-512 | local K5 | uniform | 5.87 / 0.93 | 4.28 / 2.35 | 4.33 / 4.97 |
| Zen 5 AVX-512 | head8 K12 | uniform | 7.16 / 1.03 | 6.23 / 1.02 | 1.72 / 1.34 |
| Zen 5 AVX-512 | local K63 | uniform | 11.32 / 2.69 | 14.01 / 7.89 | 1.92 / 1.92 |
| V2 NEON | local K3 | uniform | 6.70 / 2.42 | 5.80 / 2.11 | 6.09 / 2.28 |
| V2 NEON | local K3 | reject_first | 6.70 / 2.42 | 5.82 / 2.11 | 2.05 / 1.22 |
| V2 NEON | local K5 | uniform | 7.70 / 3.58 | 6.96 / 3.00 | 7.45 / 6.18 |
| V2 NEON | head8 K12 | uniform | 25.00 / 7.08 | 10.99 / 3.56 | 2.87 / 2.48 |
| V2 NEON | local K63 | uniform | 24.03 / 10.96 | 32.65 / 22.79 | 4.75 / 4.78 |

Even the frequently resolving head8 K12 case loses throughput to the fast
AVX-512 current filter. The narrow AVX2 and NEON suffixes are more expensive,
so there is more work for the same guard to avoid. K63 has an expensive residual
plane reconstruction and therefore a different balance from K64.

Ordered comparisons must retain the mask of lanes already known to be smaller;
a zero unresolved mask can return a mixed result. They still benefit from
skipping suffixes, but do not inherit equality's especially cheap all-zero
return. Head8 K64 `<` illustrates both the gain and the mixed-window reversal:

| Profile | Distribution | Current | Early |
| --- | --- | --- | --- |
| Zen 5 AVX2 | uniform | 9.94 / 2.27 | 5.74 / 1.32 |
| Zen 5 AVX2 | mixed50 | 9.92 / 2.27 | 7.32 / 6.13 |
| Zen 5 AVX-512 | uniform | 9.93 / 1.92 | 5.51 / 1.28 |
| Zen 5 AVX-512 | mixed50 | 9.92 / 1.94 | 7.65 / 6.29 |
| Granite AVX2 | uniform | 11.70 / 4.25 | 5.70 / 1.70 |
| Granite AVX2 | mixed50 | 11.63 / 4.25 | 9.90 / 7.09 |
| Granite AVX-512 | uniform | 11.81 / 4.58 | 6.15 / 1.63 |
| Granite AVX-512 | mixed50 | 11.79 / 4.59 | 10.90 / 7.33 |
| V2 NEON | uniform | 18.00 / 7.36 | 11.23 / 2.99 |
| V2 NEON | mixed50 | 18.02 / 7.36 | 14.59 / 10.65 |

All five predicates, including both inclusive endpoints, are retained in the
full matrix. `ordered1`/`ordered99` vary ordered result selectivity; `one_match`,
`half_match` and `all_equal` vary equality selectivity. Head8 K64 shared-prefix,
one-match, half-match and all-equal fixtures all have zero head-resolution rate:
there is no head work to skip, despite very different final masks. For example,
V2 all-equal is 17.85 / 7.35 ns current versus 15.94 / 7.39 ns early: the latency
change is chiefly the split circuit, while throughput does not improve.

## Strong fused controls matter

Full-ZMM independent controls, seven 300,000-iteration repetitions, Local
uniform equality. These rerun current and early in the same executable.

| Profile | Width | Current | Fused ZMM | Early |
| --- | --- | --- | --- | --- |
| Zen 5 AVX-512 | K32 | 5.50 / 0.79 | 5.16 / 0.79 | 1.54 / 1.38 |
| Zen 5 AVX-512 | K64 | 5.60 / 1.13 | 5.38 / 0.72 | 1.77 / 1.79 |
| Granite AVX-512 | K32 | 6.61 / 1.06 | 5.21 / 0.89 | 2.51 / 1.79 |
| Granite AVX-512 | K64 | 6.37 / 1.60 | 6.13 / 0.87 | 3.10 / 2.37 |

The guarded Local K64 latency win is real, but the ZMM path processes independent
windows faster. For head8 K40, unconditional fused suffix comparison also wins
throughput: Zen AVX-512 is 6.44 / 0.96 ns fused-ZMM versus 1.84 / 1.47 guarded;
Granite is 6.23 / 1.19 versus 2.52 / 1.82. Do not choose a guard because it only
beats a weaker progressive baseline.

TuplePack K8, first predicate equality followed by a second column `< 128`,
stride four. These are complete two-predicate consumers.

| Profile | Distribution | Current pair | Guarded pair | Fused pair | Guarded fused pair |
| --- | --- | --- | --- | --- | --- |
| Zen 5 AVX2 | uniform | 15.93 / 8.15 | 5.89 / 5.60 | 6.66 / 1.18 | 1.69 / 1.51 |
| Zen 5 AVX2 | mixed50 | 15.93 / 8.13 | 16.99 / 14.78 | 6.65 / 1.18 | 4.49 / 6.34 |
| Zen 5 AVX-512 | uniform | 16.20 / 8.50 | 5.85 / 5.42 | 6.66 / 0.94 | 1.71 / 1.33 |
| Zen 5 AVX-512 | mixed50 | 16.27 / 8.48 | 16.93 / 14.71 | 6.69 / 0.94 | 4.61 / 6.13 |
| V2 NEON | uniform | 26.60 / 19.97 | 11.00 / 11.88 | 14.52 / 4.17 | 3.40 / 3.33 |
| V2 NEON | mixed50 | 26.60 / 19.97 | 26.18 / 25.19 | 14.50 / 4.17 | 11.49 / 9.81 |

Guarding two prepared readers avoids work, but the independently fused layout
kernel is a much stronger throughput baseline. For tight Zen AVX2 columns on
uniform data, current / guarded current / fused / guarded fused throughput is
1.87 / 1.69 / 0.63 / 1.01 ns. The guard's latency benefit does not justify
replacing that branchless fused throughput path.

Forwarding a sparse first mask is also not automatically better than guarding
an otherwise full-width second filter. On Zen AVX2 stride-four mixed data, mask
forwarding gives 17.76 / 14.20 ns versus current's 15.93 / 8.13. Sparse-reader
work and branch behavior both belong in the consumer comparison.

## Cheaper zero detection

The follow-up tests vector zero detection before extracting the scalar equality
mask: NEON `umaxv`, AVX2 `vptest`, and AVX-512 mask-register testing. Head8 K64
uniform equality below uses seven 500,000-iteration repetitions, with controls
rerun in the same executable.

| Profile | Current | Mask then guard | Vector zero guard |
| --- | --- | --- | --- |
| Zen 5 AVX2 | 9.55 / 1.90 | 1.71 / 1.05 | 1.73 / 1.10 |
| Zen 5 AVX-512 | 9.88 / 1.92 | 1.99 / 1.10 | 2.01 / 1.11 |
| Granite AVX2 | 10.84 / 3.74 | 2.42 / 1.35 | 2.52 / 1.48 |
| Granite AVX-512 | 11.63 / 4.57 | 2.42 / 1.36 | 2.42 / 1.36 |
| V2 NEON | 17.98 / 7.36 | 2.82 / 2.11 | 2.87 / 1.74 |

NEON throughput improves, although dependent latency is essentially unchanged
on this case. NEON head8 K12 uniform equality improves from 2.93 / 2.47 to
2.80 / 1.99 ns. The mixed K64 case remains worse than current on throughput:
current 7.35, mask guard 9.66, vector-zero guard 8.85 ns. This is a useful NEON
implementation choice inside a selected guard, not a cure for unpredictable
branches. AVX2 `ptest` adds no benefit here; AVX-512's existing guard already
uses `kortest` in the generated code.

## Correctness, generated code and retained evidence

Independent scalar truth checks complete masks for all measured variants.
SeriesPack additionally checks sparse/empty activity, domain endpoints and
out-of-domain queries. Guard-page tests check bounded reads and a rejected head
with an unreadable suffix. Local ASan/UBSan runs passed for the guard-page and
vector-zero tests. Native workers passed their complete checks before timing.

[Code-generation notes](codegen/early/README.md) identify branches and loads in
the actual timed functions, including the complete result-to-next-address
recurrence. The streaming audit checks 950 measurement functions per main
profile, plus the focused follow-up functions, for unexpected outlined calls.
There were none. This does not imply no spills or a cost-free eight-stream
consumer; the timings include generated scheduling and stack traffic.

Selected exports preserve the entire qualifying matrix, longer rechecks, repeat
samples, counters, host/compiler/source identity and check receipts. This keeps
negative cases and width/predicate coverage independently inspectable. Raw CSV,
binaries, full disassembly and captured source remain in verified S3 bundles;
no superseded unsalted throughput cohort selects behavior.

| Native host | Main matrix + focus + ZMM where applicable | SIMD-zero follow-up |
| --- | --- | --- |
| Zen 5 | [early-zen5](evidence/early-zen5/artifact.json), job `20261015T155501Z-32878aaf` | [early-zero-zen5](evidence/early-zero-zen5/artifact.json), job `20261015T162055Z-bfd0bd4c` |
| Granite Rapids | [early-granite](evidence/early-granite/artifact.json), job `20261015T155508Z-9bbb43d0` | [early-zero-granite](evidence/early-zero-granite/artifact.json), job `20261015T162055Z-fb3150a6` |
| Neoverse V2 | [early-neon](evidence/early-neon/artifact.json), job `20261015T155514Z-50f3a8a0` | [early-zero-neon](evidence/early-zero-neon/artifact.json), job `20261015T162019Z-c87f1057` |

Each export's `provenance.json` and `artifact.json` list exact selected hashes
and recovery information. Example recovery from the repository root:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/packed-filters/evidence/early-zen5 \
  build/recovered/filter-early-zen5 --file avx2-disassembly.txt
orb -m ubuntu python3 workbench/spikes/packed-filters/audit.py \
  build/recovered/filter-early-zen5/avx2-disassembly.txt \
  build/recovered/filter-early-zen5/audit.json
```

Remaining limits: no Striped/head16 timing panel, larger filter-window panel,
cold-cache/Loom consumer, or caller's real prefix distribution is covered here.
Those are limits on deployment claims, not missing qualifications for the warm
Local/head8 and two-column experiment above. The evidence supports selective
consumer specialization and keeping the current general defaults.
