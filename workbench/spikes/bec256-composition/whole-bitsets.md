# Whole-bitset composition with current Ikea

This restores the [original heterogeneous extension](../ikea-composition/probes/ikea-heterogeneous/operations/README.md)
alongside the renewed [directory/range-count study](metadata-findings.md).
It uses current SeriesPack, TuplePack and Bec256, rather than retaining the old
provider APIs. The operations and directory choices here remain experimental.

| Original question | Renewed experiment |
| --- | --- |
| Substitute population/length storage while preserving body identity | Nine directory layouts, checkpoints 16/64, point/buffered/native frame resolution in the range study |
| Bind the two Boolean operands independently | Plain/plain, compressed/plain and compressed/compressed inputs; independent layouts, checkpoints and lookup methods |
| Distinguish decoder inputs from output progress | One-output grain decodes two operands at one ordinal; two-output grain decodes two original ordinals from each operand |
| Preserve output postconditions under selection | Union/intersection with complete zero-filled or selected-only plain output; issued byte effects on both |
| Turn body estimates into storage decisions | Current quadrant estimator, full scan versus fixed 32-block sampling, directory extents, optional readable suffix and allocation rounding |

## Two represented inputs and one plain output

[algebra.cpp](algebra.cpp) binds metadata point/refill functions independently
for each input. The decoder/Boolean body is shared across their layout pairs.
Each input retains its own metadata frame and byte frontier. The erased frame
refill returns through memory once per refill; it is an intentional boundary,
not a claim that frame retention is free. Direct words retain inline extraction.
The experiment does not instantiate a complete decoder for every pair of layouts.

Both sources occupy the same original ordinal domain. A selection bit addresses
that ordinal, never its rank among selected blocks. Inactive bodies need no
metadata lookup or decoding, though lengths of inactive predecessors can still
be needed to locate a later active body. Compressed sources retain the study's
64-byte initialized read windows; a neighbouring body's bytes can fall within
an active window. Plain operands grant exactly 32 bytes per active block.

Admitted empty/full populations enable Boolean identities before decoding.
Plain inputs provide no population evidence. A native pair can decode A[i] and
B[i] for one result, or A[i]/A[j] and B[i]/B[j] for two results; i and j need
not be adjacent. The odd final selection uses the one-output body. Native values
remain in registers between decoding, Boolean work and plain output stores.

The caller has already admitted both sources and proved output extent,
disjointness, shared coordinates and journal capacity. The experimental command
cannot reject or suspend once started. It writes into caller-owned plain output:

- `complete` writes the selected results and establishes zero for every inactive
  ordinal. Every output byte has issued coverage.
- `selected_only` writes selected results and preserves every inactive byte.
  Coverage includes only selected spans, qualified by the named destination.

Complete output records its known contiguous footprint once. Selected-only
output budgets one event per original block; adjacent events may coalesce.
Publication, dependent summaries and owner scheduling remain outside the timed
body. Compressed output, length-change propagation and a complete larger-bitset
storage owner are not implemented here; these were also beyond the original
plain-output algebra. Bec256's exact pair-write experiment covers the separate
read/filter/re-encode boundary.

[algebra_check.cpp](algebra_check.cpp) crosses every directory independently
on both sides, plain inputs, three lookup methods, both operators and output
postconditions, two grains and five masks at 129 and 256 blocks. The 24,000 cases
check bytes against independent plain Boolean operations, issued coverage,
inactive preservation and output neighbours. The right checkpoint interval and
lookup differ from the left. All-inactive cases establish the distinct output
obligations without reading any input body.

## Estimating the enclosing representation

The full analysis uses current native pair `estimate_bytes`; fixed sampling
chooses at most 32 evenly spaced original blocks and scales their sum. Sampling
is an experimental competitor, not an Ikea analysis API. Neither result is a
bound. Reused natural windows are a regression sample, not a fresh holdout.

The decision model compares raw plain bytes with:

```text
round_up_64(predicted body bytes + directory extent + readable suffix)
```

The directory is direct4, folded TuplePack, or folded SeriesPack/striped lengths,
with checkpoints every 16. Its physical extent is independent of the predictor.
Suffix 0 models exact admitted reads; suffix 64 models a shared initialized window
at the end of densely packed bodies. Rounding describes a hypothetical single
64-byte-aligned allocation. These are modeled storage costs, not measured RSS,
allocator traffic, or the separately allocated fixture's physical residence.

Actual encoded body sizes supply the counterfactual byte choice and storage
regret. A mistaken choice counts only extra bytes relative to the cheaper actual
representation. A correct storage prediction does not establish CPU payback.
`checked_encode` measures constructing every exact body with population checks
and effects, without constructing directories; a memory barrier observes every
intermediate write. All methods exclude source acquisition and policy scheduling.

Two correlation probes deliberately put all sampled ordinals in a different
distribution from the unsampled ordinals: every eighth block is empty and the
rest dense random, or the reverse. They test fixed sampling's blind spot without
claiming these arrangements are a frequency model for application data.

## Findings

The two compressed operands can use different current Ikea metadata modules
without multiplying complete decoder kernels by the layout cross-product.
For complete intersection over 256 blocks, the direct/direct control and
TuplePack-left/SeriesPack-right alternative take the following microseconds:

| Input | Zen direct | Zen packed | V2 direct | V2 packed |
| --- | ---: | ---: | ---: | ---: |
| Random | 13.65 | 15.00 | 42.54 | 44.65 |
| Runs | 13.02 | 14.45 | 39.21 | 41.35 |
| census-income | 5.88 | 7.10 | 15.43 | 17.55 |
| weather_sept_85 | 10.58 | 11.90 | 30.99 | 33.12 |
| wikileaks-noquotes | 0.99 | 1.82 | 1.57 | 3.75 |

Both use two-output grain, checkpoint 16 on the left and 64 on the right.
The packed alternative exposes 1,040 directory bytes across both inputs against
2,048 direct bytes. It adds about 10–21% on the first four Zen cases and 5–14%
on V2. When population identities remove most decoding, its metadata cost is
much larger: roughly 83% and 138% on wikileaks. Reversing which side uses TuplePack
gives similar results. These findings reinforce keeping direct metadata as a
serious choice; packed directories are not automatically the appropriate parent.

Plain/plain complete output takes about 0.44–0.45 microseconds on Zen and
0.94–0.95 on V2. Those controls use the same selected traversal and effects,
not a separately optimized plain-bitset library. Compression buys space here;
it is not intrinsically faster for resident Boolean operations.

Grain two helps most when only one input needs decoding: for random
SeriesPack/Bec-left versus plain-right, grain one costs 1.66 times as much on Zen
and 1.10 times on V2. With two compressed inputs the difference is much smaller.
Terminal-rich cases can favor grain one: direct/direct wikileaks is about 28%
faster on Zen and 22% on V2. A native two-input decoder therefore should not
dictate logical output progress. Both grains remain experimental choices here.

Output obligations matter independently of the calculation. For stride-17
wikileaks with the packed inputs, complete/selected-only costs are 425/356 ns
on Zen and 738/651 ns on V2. Selected-only writes 512 bytes instead of 8,192.
It is not universally faster: complete output can record its entire known
footprint once, while a general selected-only writer tracks selected spans.
Both preserve their promised effects and byte postconditions.

Full prediction costs 0.21–0.74 microseconds over the three natural families on
Zen and 0.43–1.22 on V2, versus 0.85–2.93 / 1.60–6.59 microseconds to check and
encode every body. With folded SeriesPack metadata, a 64-byte suffix and the
stated rounding, actual modeled extents average 2,520, 2,216 and 752 bytes for
census, weather and wikileaks. Both prediction methods make the correct byte
choice in these eight-window samples; full prediction's summed body errors
average 19.1, 6.0 and 1.6 bytes, versus 77.4, 110.6 and 27.9 with sampling.
The choices are far enough from the 8,192-byte boundary that accuracy differences
need not change them.

Fixed sampling fails both correlation probes on every window. Sampling only
empty blocks predicts a small compressed representation but the actual modeled
allocation averages 9,392 bytes: 1,200 extra bytes versus raw, maximum 1,216.
Sampling only dense blocks instead misses a 1,856-byte representation and wastes
6,336 bytes by choosing raw. Full prediction chooses correctly on these probes,
though it remains an estimate with correlated errors, not a savings guarantee.

The provisional conclusion is to expose usable estimates and exact effects from
Ikea, while leaving directory choice, analysis cadence, sampling and byte/CPU
tradeoffs with the enclosing owner or Engine. The renewed experiment establishes
composition with current modules; it does not justify promoting an automatic
whole-bitset conversion policy.

## Measurement and reproduction

The runner reuses the range study's deterministic inputs. Natural Boolean pairs
use different retained windows, aligned to the same local ordinal positions for
this synthetic operation; they are not observed conjunctions over a real shared
record collection. Output and effect storage are reused. The complete/selected
comparison therefore prices output obligations, not ownership or publication.

```sh
python3 workbench/tools/worker.py run workbench/spikes/bec256-composition/run.sh \
  --machine zen5 --instance-type c8a.medium --arg avx512 \
  --env 'BEC_FILTER=^algebra/|^storage_analysis/|^pair_mutation/' \
  --env BEC_MIN_TIME=0.005 --env BEC_REPETITIONS=5
```

Use `neoverse-v2`, `c8g.medium`, and `neon` for ARM. The runner checks the module,
range composition, exact-write casing and whole-bitset algebra before timing.
The small [routine codec suite](../../benchmarks/bec256/README.md) is independent
of this broader investigation.

The [Zen](evidence/whole-zen5/cases.csv) and
[V2](evidence/whole-neoverse-v2/cases.csv) exports retain 1,353 cases with five
sequential repetitions, relevant accounting and source capture
`cc76b4ba960b2c801ef4c0d0a41abae442753ced0b3e9aae48c3d6195ea8bff4`.
Their bundles retain sources, checks, binaries, compiler settings and symbols.
The small routine suite was also run from that snapshot. Earlier whole-operation
sources, before explicit body inlining and one-span complete-output coverage,
remain recoverable in [Zen](evidence/alternatives/whole-before-explicit-inline-zen5.json)
and [V2](evidence/alternatives/whole-before-explicit-inline-neoverse-v2.json) archives.
