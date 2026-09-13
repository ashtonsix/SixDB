# Exact operations and the codec boundary

The useful reset was in the implementation boundary, not a new wire format.
Bec256 still uses the same count tree and byte-rank body. The initial ordinary
operation prepared an encoded temporary and copied it after admission. On Zen 5,
that cost about 28 ns for random blocks against 16 ns for the previous wide-write
kernel. Bounded reading similarly copied short bodies into scratch before decoding:
about 78 ns against 44 ns for the previous padded decoder.

## What changed

The encoder now shares its count/field body with a destination sink. Immediate
writes reuse the computed populations for validation, admit the actual extent,
and store that extent with one issued-span effect. Prepared bytes remain useful
when an owner must retain a candidate across a wait; they are not mandatory on
the ordinary immediate path.

That boundary change alone was insufficient. Scalar stitching repeatedly issued
overlapping stores to concatenate groups. On AVX-512, prefix widths now locate
eight groups in output words; a segmented OR combines contributions and compressed
segment ends form the complete bitstream in registers. One masked byte store
writes the exact body. The raw wide-write entry shares this assembly. Short
admitted reads use masked register loads, removing their scratch store/reload.

On ARM, exact stitching now emits complete words while they fit, then assembles
all remaining groups into one tail word and writes it once. This removes the
per-group narrow-store cascade and zero-width branches. It preserves exact bounds
and reuses the original wide-stitching prefix; it needs no new caller contract.

The same ordinary operation therefore benefits as the native body improves.
There is no separate public bound-destination API, and no weakened effects,
neighbour-preservation, population or rejection contract.

## Selected measurements

Medians of five repetitions, nanoseconds per block; two-block rows explicitly
say “pair”. Pinned Clang 21.1.8, release -O3 with assertions, one pinned CPU,
resident repeated inputs. These are operation timings, not application workload
weights or a cold-cache measurement.

| Operation / input | Zen 5 prior control | Zen 5 current | Neoverse V2 prior control | Neoverse V2 current |
| --- | ---: | ---: | ---: | ---: |
| Checked exact encode, random | 16.51 | 13.48 | 25.90 | 33.55 |
| Checked exact encode, runs | 16.38 | 12.93 | 25.92 | 29.72 |
| Checked exact encode, population 8 | 16.38 | 13.49 | 25.92 | 32.12 |
| Checked exact encode, population 128 | 16.30 | 13.48 | 25.90 | 33.40 |
| Checked exact encode, singleton | 2.24 | 5.85 | 25.90 | 6.53 |
| Admitted native exact encode, random | 16.51 | 12.55 | 25.90 | 27.44 |
| Admitted exact decode, random | 43.59 | 40.42 | 84.03 | 87.78 |
| Admitted exact pair decode to 64 bytes, random | 59.48 | 51.80 | 166.88 | 169.87 |

The prior write control has a **weaker contract**: correct population is trusted,
it has a 64-byte writable grant, and it records no effects. Current checked
writes include population/extent/effect/alias checks and reset their journal each
iteration. Admitted native writes trust those prerequisites but retain exact
stores and a coverage hook. Admission of read sources occurs outside timing.
The prior padded decoder also returns consumed bit count; the current admitted
read returns values and relies on separately retained exact framing. Pair controls
materialize both halves, so this table does not claim a consumer benefit merely
from a native return type.

Across the 15 sampled nonterminal shapes excluding singleton and singleton-hole,
Zen checked exact writes cost 0.79–0.83 times the prior wide-write control. V2
costs 1.15–1.30 times. The admitted native exact entry costs 0.97–1.06 times
its prior control. The earlier 1.24–1.50 checked / 1.12–1.32 admitted gap was
partly an avoidable per-group tail-store cost; the single-tail change removes
most of the admitted gap. Checked V2 writes still pay population, extent, alias
and journal admission while preserving live codec values. This broad residual
15–30% ordinary cost is an explicit tradeoff, not parity with the weaker control.

The V2 column uses the [single-tail repeat](evidence/tail-neoverse-v2/cases.csv),
source `1a675a63cb927f11b5e60b74de05c7d4efea751d9ab28f87b2db4eed14bb7d48`.
Zen's assembly is unchanged; its contemporaneous repeat is consistent with the
figures above. The older predictor table below retains its own measured snapshot.

Zen singleton and singleton-hole checked writes cost about 5.9–6.1 ns against
2.2–2.3 ns. The admitted native entries cost 2.4 ns; ordinary validation and
effect/alias admission dominate such tiny bodies. V2 gains a corresponding
singleton fast path that the prior control did not have. Empty/full ordinary
writes short-circuit to zero body bytes, while the raw controls keep their
conservative access-grant contract.

The raw exact pair decoder does not exploit terminal populations. A larger
consumer can use population evidence to avoid decoding empty/full children;
that policy is measured separately in the metadata investigation.

## Size analysis and declining an encode

Two separate operations are retained. `estimate_bytes` uses the original
predictor study's frozen Q12 quadrant model. Its population, byte-enumeration,
transition and quarter-dispersion features describe the actual bitset; they do
not construct a count tree or encode ranks. Ordinary and native block/pair
entries share the model. The cheaper two-feature and transition-only candidates
remain in Workbench rather than becoming public selection knobs. Quadrant
dispersion earns its cost through the original structural holdout: full/empty
byte permutations exposed information missing from the cheap model. See the
[original accuracy study](../ikea-composition/probes/ikea-blocks/predictor/README.md)
for coefficients, training provenance, held-out errors and correlated-error limits.

`encode_if_promising` implements a different idea from archived Calico's
`calico-prototype/tools/calibrate.cpp` and `include/calico.h`: decline when the
**exact byte-enumeration cost** reaches a caller-selected threshold. The statistic
excludes tree fields. Its maximum is 224 bits, less than the 256-bit raw bitset;
it cannot by itself prove that a body will fail to compress. Calico's cutoff of
144 was a calibrated heuristic, not an allocation check. It is one comparison
here, not an Ikea default. The caller selects any alternative representation.

The shared native encoder computes byte populations once, checks the supplied
population, and evaluates this optional gate before constructing the tree,
looking up byte ranks or packing/storing fields. Accepted encoding reuses the
byte widths. Unconditional encoding has no gate. Decline returns an empty
optional without destination admission or writes/effects; an engaged zero means
a successfully encoded empty/full block. Statistical estimates do not control
this gate or provide write permission.

`analysis_bench.cpp` compares ordinary/native estimation, a no-emission exact-size
control, actual encoding, fused checked gates and a separate-statistic-then-encode
control. The exact-size control trusts its supplied population; the checked gate
validates population even on decline. Inputs include random bits, runs,
permuted full/empty bytes, three dense/run mixtures, terminals and the eight
retained windows from each of three RealRoaring families. Accuracy counters omit
empty/full blocks. These reused windows are a regression sample, not a fresh
model-selection holdout. Threshold counters report both coverage and the bytes
lost if a caller sends declined, otherwise-compressible blocks to a 32-byte raw
form. That counter is a hypothetical owner policy, not an encoded Ikea escape.

An initial raw-encode control allowed LLVM to eliminate stores while keeping
the returned length. Explicitly escaping output and journal addresses before
timing fixes this; each iteration's memory barrier now observes actual bytes and
effects. Only the corrected comparison is used for write-performance conclusions.

Medians of five repetitions on the corrected snapshot, nanoseconds per block:

| Operation / input | Zen 5 | Neoverse V2 |
| --- | ---: | ---: |
| Ordinary size estimate, random | 3.81 | 6.55 |
| Native size estimate, random | 3.39 | 6.89 |
| Native exact-size control, random | 4.79 | 10.03 |
| Ordinary checked encode, random | 13.72 | 37.54 |
| Ordinary gate at 144, random (all declined) | 2.08 | 2.52 |
| Native checked gate at 144, random (all declined) | 1.14 | 2.25 |
| Ordinary checked encode, runs | 13.33 | 32.89 |
| Ordinary gate at 144, runs (none declined) | 13.92 | 33.57 |
| Ordinary checked encode, dense50 mixture | 13.47 | 34.88 |
| Ordinary gate at 144, dense50 mixture | 7.62 | 16.26 |

The estimate saves work even against an exact-size body with no bit emission.
The optional shortcut saves substantially when it declines dense inputs; on the
accepted run inputs its ordinary overhead is about 4.4% on Zen and 2.1% on V2.
The separate gate control can be slightly faster, especially when declining,
but trusts the supplied population on that path. The fused checked API retains
ordinary population validation and avoids making the caller reproduce the
decision/write protocol. These microbenchmarks use resident inputs and a single
output slot; decline time excludes the caller's eventual alternative encode.

In the reused natural sample the quadrant estimate's partial-block mean absolute
errors are 0.165 bytes for census, 0.111 for weather and 0.141 for wikileaks;
all three have p95 and maximum error one byte. The new synthetic byte permutations
have MAE 0.702, p95 two and maximum three bytes. These small regression samples
do not replace the larger original holdout or establish a bound.

Cutoff 144 declines only 0.53% of partial census blocks, 0.93% of weather blocks
and none of wikileaks; none of those declines misses a sub-32-byte encoding in
this sample. Accordingly it saves little on these natural inputs and can add
overhead. Lowering the cutoff to 100 illustrates the risk: 90.3% of its census
declines and 55.6% of its weather declines would have compressed below 32 bytes.
Routing them to raw would add 0.471 and 0.059 bytes per partial block, respectively.
The retained counters distinguish coverage, conditional mistakes and extra bytes;
no single cutoff is selected for general use.

The [Zen](evidence/prediction-zen5/cases.csv) and
[V2](evidence/prediction-neoverse-v2/cases.csv) bundles also repeat the broad codec
screen and pair-mutation operation with observable stores. The ordinary codec
results remain consistent with the earlier boundary-reset findings. Use
`BEC_FILTER=^analysis/` with the runner below to reproduce prediction and gating.

## Alternatives retained as evidence

- [Initial casing](evidence/alternatives/initial-casing.json): temporary encoded
  body and short-input scratch. Its early metadata update measurements rebound
  SeriesPack writers inside each call; they are not steady-state bound costs.
- [Direct scalar stitching](evidence/alternatives/branchy-direct.json): exposing
  the sink helped some cases but did not remove the serial store dependency.
- [Masked per-group stores](evidence/alternatives/masked-stores.json): marginal
  random-block improvement and a substantial run-pattern regression; rejected.
- [Register assembly repeat](evidence/alternatives/register-assembly-repeat.json):
  the structural improvement reduced checked random writes to 13.66 ns while
  the then-current staged path still cost 28.07 ns.
- [NEON before admission inlining](evidence/alternatives/neon-before-admission-inline.json):
  further scalar tail-store rearrangement barely helped. Keeping immediate
  admission visible to the native body recovered about 1 ns, not the remaining gap.

An experimental prebound alias envelope remains only in this study. It saved too
little to justify another caller-facing state/lifetime contract. Incomplete Spot
runs are not selected timing evidence. Early runtime-switch benchmark callbacks
also distorted specialization; the selected codec cases use separate statically
typed callbacks for each method.

## Evidence and reproduction

The selected [Zen cases](evidence/prediction-zen5/cases.csv) and
[V2 cases](evidence/prediction-neoverse-v2/cases.csv) retain all five repetitions,
medians, populations and body sizes. Their adjacent provenance and artifact
references recover the exact sources, worker configuration, compiler output,
checks, executable and raw timing JSON. Both use source capture
`6eee4e9662f94bdca0e6f826b4feaa8965fab67b61fe0b415b9421a5971c5b05`;
subsequent documentation and test-report wording changes do not replace
that recorded snapshot.

From the Linux checkout, a focused current-source rerun is:

```sh
python3 workbench/tools/worker.py run workbench/spikes/bec256-composition/run.sh \
  --machine zen5 --instance-type c8a.medium --arg avx512 \
  --env 'BEC_FILTER=^codec/' --env BEC_MIN_TIME=0.01 --env BEC_REPETITIONS=5
```

Use `--machine neoverse-v2 --instance-type c8g.medium --arg neon` for V2.
Worker execution follows the [measurement conventions](../../benchmarks/README.md).
The script checks wire, bounds, composition and bitstream assembly before timing.
`codec_bench.cpp` names the old opaque same-wire controls and each current entry;
`casing.cpp` contains the deliberately unpromoted bound-envelope experiment.
