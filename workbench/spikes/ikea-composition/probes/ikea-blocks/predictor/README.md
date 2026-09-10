# Estimating Bec256 size

**Bec256 — Bisection–Enumerative Code** is the accepted name. This is an
analyser-facing scalar estimate of headless Bec256 **bytes**. It is
separate from an encoder's early-exit threshold decision. The estimate is not
a bound, a validation result, or permission to write into that many bytes.
Empty/full bitsets predict zero, matching the headless format; all other
results are rounded and clamped to 0..47. No raw escape is included in the
target. The [composition probe](../composition/) measures the cost of producing
and carrying these features; this directory records model accuracy.

## Candidates and the failure that changed the experiment

The first fit made a two-feature model look attractive: population distance
and enumerative byte cost captured most natural-corpus variation. Testing only
uniform populations, runs and repeated bytes was insufficient. Sixteen full
bytes followed by sixteen zero bytes take **1 byte** in BEC256; a permutation
of those bytes can take **18 bytes**. Both have distance 0 and enumerative cost
0, and the cheap model predicts 4 bytes for both. This is an information loss
in the features, not an implementation error.

The structure study therefore adds byte permutations, distinguishes
development from holdout, and tests a separate coefficient piece when the
enumerative cost is zero. In that regime the population tree carries all the
bits. The following candidates are frozen for the compute/composition probe:

| Candidate | Features | Real test MAE / p95 / max, bytes | Fresh structure holdout MAE / p95 / max, bytes |
| --- | --- | --- | --- |
| Cheap control | distance, enum cost | 0.185 / 1 / 5 | 1.971 / 7 / 14 |
| Transitions | distance, transitions, enum cost | 0.179 / 1 / 5 | 1.211 / 4 / 7 |
| Quadrants | previous three + quarter dispersion | 0.145 / 1 / 3 | 0.779 / 2 / 6 |

These rows cover **partial** tiles; trivial empty/full tiles do not dilute the
errors. Real test decision errors for predicted size <32 bytes are 0.157%,
0.111% and 0.127%, respectively. On the fresh structure holdout they are
2.087%, 1.918% and 0.884%. Error tables also retain thresholds 8, 16 and 24,
false compression, missed compression, bias, RMSE and error quantiles. An
accurate <32 decision alone does not establish a useful size estimate.

The natural validation MAEs are 0.087, 0.092 and 0.082 bytes. Thus transitions
earn their cost through structural coverage, not a large reduction in natural
MAE. Quadrant dispersion improves coverage further; timing must decide whether
its extraction and extra multiply are worthwhile. The header exposes all
three alternatives without an automatic selection policy.

## Features and arithmetic

For population `p`, adjacent-11 count `a`, and byte populations `q[j]`:

| Feature | Definition | Extraction consideration |
| --- | --- | --- |
| Distance | `abs(128-p)` | Reuse the population reduction. |
| One-runs, the user's intended feature | `pop(bits & ~(bits << 1)) = p-a` | Exact number of one-runs. The shift spans all 256 positions with a zero entering at position 0; it does not wrap. |
| Adjacent 11, historical typo comparator | `pop(bits & (bits << 1))` | The original expression omitted a tilde. Results for this feature do not evaluate the intended one-run count. |
| Enumerative cost | `sum(width[q[j]])`, widths `0,3,5,6,7,6,5,3,0` | Byte population plus lookup/reduction; useful algorithmic intermediate rather than stored metadata. |
| Internal transitions | Count differing neighbouring positions among the 255 internal boundaries | Complement symmetric. Equivalent to `2*(p-a)-first_bit-last_bit`. SIMD extraction can use XOR/shift/popcount. |
| Quarter dispersion | `sum(abs(4*pop(word64)-p))` over four words | Reuse word population reductions; add subtraction, absolute values and a reduction. Range 0..512. |

Raw adjacent-11 is asymmetric under complement although BEC byte size is
symmetric. This explained a result for the **missing-tilde expression**, not a
weakness of the user's intended run feature. One-run counts of complementary
bitsets differ by at most one, and internal transitions satisfy
`t = 2*one_runs - first_bit - last_bit`. Both carry almost the same structural
information. A linear mixture of existing features adds no expressive
power to linear regression. A new statistic, interaction or piece selection
does; this study uses the latter two sources of information without treating
a redundant fourth feature as a model improvement.

### Corrected one-run comparison

After the user clarified the omitted tilde, [compare_runs.py](compare_runs.py)
evaluated exact one-run count using the same prepared samples, family splits,
population pieces, zero-enum regime, Q12 arithmetic and development weights.
It loads the earlier frozen models verbatim; their coefficients were not
retuned. New one-run candidates change only the structure feature. The
historical `proposed_three` identifier in the first tables denotes the typo
expression and is retained only for reproducibility.

| Matched candidate | Validation MAE | Existing real test MAE | Existing structure comparison MAE / p95 / max |
| --- | --- | --- | --- |
| Transitions, frozen | 0.09226 B | 0.17885 B | 1.21135 / 4 / 7 B |
| One-runs | 0.09209 B | 0.17952 B | 1.21577 / 4 / 7 B |
| Transitions + quarters, frozen | 0.08206 B | 0.14476 B | 0.77862 / 2 / 6 B |
| One-runs + quarters | 0.08176 B | 0.14591 B | 0.78660 / 2 / 6 B |

The correction preserves the ranking: quarter dispersion provides the larger
accuracy gain, and one-runs closely match transitions. Differences of a few
thousandths of a byte do not establish an accuracy winner. The intended
one-run feature is a valid structural signal; the earlier conclusion about
raw adjacent-11 cannot be attributed to it. Natural-only fits are also
retained and show the same need to represent the structural regime.

These previously inspected holdouts are now explicitly called **comparison
sets**, not fresh untouched tests. There was no tuning after this comparison,
and no new native feature path or model was selected. A direct one-run
extraction might be an equal-accuracy compute alternative; that requires a
timing comparison before changing the frozen native candidates. The original
frozen model identities and APIs remain intact.

[model.h](model.h) contains the exact Q12 coefficients and evaluation. Each
candidate selects from minority-population bins 1..8, 9..32, 33..96, 97..128.
The structure candidates add an `enum_cost==0` piece. Integer coefficients
are rounded ties-to-even once when exported; runtime evaluates an integer dot
product, adds 2048, shifts right 12, then clamps. All intermediate values fit
signed 32 bits over the feature domain. The coefficients return byte sizes,
not coded bit counts.

The scalar handoff packs distance into bits 0..7, internal transitions into
8..15, enum cost into 16..23, and quarter dispersion into 24..33. The cheap
control ignores the structure fields. This packing is an execution carrier;
it is not stored in a bitset. [reference.h](reference.h) is a plain byte-wise
feature oracle, not a claimed fast scalar implementation.

## Data, splits and model fitting

The shared [MS MARCO](../../../../../datasets/msmarco-keyset/README.md) and
[Real Roaring](../../../../../datasets/real-roaring/README.md) adapters provide all
nonempty 65536-position windows, retaining source-list lineage. Every such
window contributes all 256 of its tiles, including empty tiles. Unspecified
all-zero windows beyond/between observed windows are not synthesized. The
target is computed from the definition of the bisection alternatives and
binomial byte ranks, independently of a codec implementation.

There are 22,481 source-list records and 11,305,472 window tiles. A seeded
priority reservoir retains 201,724 tiles, capped separately per corpus,
partition and population stratum. Sparse and dense tails have their own
budgets; inverse inclusion weights recover the observed tile mixture. This
mixture weights large supplied datasets more heavily and does not represent
a measured SixDB workload.

Partitions keep **entire source families** together. This is stronger than
splitting parent lists: sorted/unsorted column identity is not recovered, and
the dimension projections may be related. No adjacent tiles from a parent
list can cross a partition:

- Train: dimension projections, MS MARCO, US census, weather, WikiLeaks,
  including available sorted companions: 10,025,216 tiles, 141,393 sampled.
- Validation: census1881 and its sorted companion: 1,024,000 tiles,
  21,246 sampled.
- Test: census-income and its sorted companion: 256,256 tiles, 39,085 sampled.

The initial twelve candidate fits compare nested 1..4-feature models, with
and without four population pieces. Weighted least squares targets exact
bytes. Floating, rounded-floating, Q8, Q12 and Q16 evaluations are separate,
so coefficient quantization is not confused with rounding the final estimate.

The structure study tests an extra low-enum piece with threshold none/0/16/32
and either no augmentation or a fixed structural-development augmentation.
The selected structure candidates use threshold zero. Augmentation has 8,928
development cases weighted at **1% of all natural training tile weight**.
Because terminals are excluded from fitting, this is 10.76% of natural partial
training weight, or 9.72% of combined fitted weight. It is not a claim that
synthetic cases represent 1% of a real analyser workload.

Candidates were chosen from validation and development results before
examining a separately seeded structure holdout. That holdout includes fresh
permutations and previously absent heterogeneous-byte and independent-quarter
families (21,949 partial cases). The original real test had already been seen
for the cheap baseline; it was scored for the three frozen candidates only
after refinement. Neither holdout caused further tuning. This sequence and
the development/test seeds are retained in the provenance.

The original synthetic stress remains separately reported: fixed-population
random sets, rotated runs, repeated bytes and Markov runs. These are no longer
the sole evidence for structural coverage. In particular, errors on adjacent
tiles can be correlated; this study establishes no concentration guarantee
for the summed estimate of a larger bitset, and no guaranteed error bound.

## Reproduction and checks

From the repository root on this host:

```sh
orb -m ubuntu python3 workbench/spikes/ikea-composition/probes/ikea-blocks/predictor/run.py
```

The runner uses the shared prepared data and captured-source machinery.
Python 3 and NumPy are required (recorded run: NumPy 2.2.4); the C++ check uses
pinned Clang 21.1.8. It writes raw output under ignored `build/experiments/`
or `SIXDB_RESULTS`. No workers or kernel timing are launched by this runner.

An independent scalar integer/range oracle checks training labels and feature
symmetry. C++ evaluation matches Python fixed-point arithmetic on 6,156
basic and 6,156 structural cases, including piece boundaries, byte-boundary
transitions, complemented inputs and full/zero-byte permutations. The
[initial retained evidence](evidence/20260909/) and
[corrected-feature evidence](evidence/20260909-one-runs/) contain coefficients, error summaries,
checks and provenance; its artifact reference recovers raw samples, all
candidate tables, fixtures and captured source through the shared retention
tool. These are accuracy measurements and correctness checks, not timings.

The correction export keeps its new comparisons and coefficients, and references
identical initial outputs through `reused-evidence.json` instead of copying them.
That reference names the initial export and hashes its provenance and reused
files. The runner applies this only to byte-identical outputs; changed results
remain in the new export. Full original outputs remain in each S3 bundle.
These CSVs/JSONs are direct outputs; running training again is a new experiment,
not offline report regeneration.
