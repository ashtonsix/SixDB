# SeriesPack benchmarks

Recurring measurements of Ikea's packed arrays: bulk encode/decode, resident
point/range access, authored composition, and caller/validation/effect costs.
[Workloads and comparisons](measurements.md) define the cases, units and controls.
The [SeriesPack introduction](../../../ikea/seriespack.md) explains the component and its use.

## Local iteration

From the repository root on Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
cmake --preset dev -DSIXDB_BENCHMARKS=seriespack
cmake --build --preset dev --target ikea_seriespack_bench -j1
./build/clang/dev/workbench/benchmarks/seriespack/ikea_seriespack_bench \
  --benchmark_filter='^casing/.*/attach$' --benchmark_min_time=0.03s \
  --benchmark_repetitions=3 --benchmark_enable_random_interleaving=false
```

The shared main pins one allowed CPU; `SIXDB_CPU` selects another. Allocation
and independent value checks precede timing. This small example exercises the
workload; choose the build/ISA/tuning and footprint for the performance question.
No spike selection or Calico download is needed. The immediate-predecessor
adapters reference the retained probe code directly.

For editor support, `python3 workbench/tools/dev.py --add-benchmark seriespack`
adds this suite while preserving active spikes. Use `--remove-benchmark seriespack`
to remove it. Only selected suites are configured and their targets are opt-in.

## Hardware profiles and retained results

Use [workers](../../tools/workers.md) with the ordinary script runner. This
uses default Spot capacity; the worker guide explains explicit fallback:

```sh
python3 workbench/tools/worker.py run workbench/benchmarks/seriespack/cloud.sh \
  --machine zen5 --instance-type c8a.large --idle-seconds 600 -- \
  --tune zen5 --profile avx512 --measure --benchmark-filter '^bulk/'
```

`run.py --tune zen5 --plan` prints profiles without launching anything. X86
profiles separate AVX2, AVX-512BW and the fuller VBMI/VBMI2/GFNI feature set.
For ARM use a Neoverse V2 worker and `--tune neoverse-v2 --profile neon`.
Tuning and ISA remain independent. Four GiB and one build job are useful for
full-width checks. `--check-target` narrows the correctness check for iteration;
omitting `--measure` runs checks only. The receipt records expected unavailable
checks: grouped composition and deferred sums currently need the full AVX-512
profile. Aggregate validation still requires executed public-wire, range and
native-composition checks for the selected profile. The suite's
`python3 workbench/benchmarks/seriespack/check_runner.py` checks this interpretation
offline when the runner or native check set changes.

Add `--calico` to restore the pinned BytePack capsule and include its controls.
For a direct CMake build, `SERIESPACK_PRIOR_DIR` supplies that include directory;
[the existing preparation command](../../spikes/ikea-composition/probes/ikea-integers/prepare_prior.py)
prints it. Source hashes and adapter/compiler settings travel with the result.

Measurements default to Release, 30 ms minimum per repetition and three
sequential repetitions. `--build-type`, `--min-time` and `--repetitions` override
them. `SERIESPACK_BULK_VALUES` and `SERIESPACK_RESIDENT_BYTES` select footprints;
allocated bytes alone do not establish cache residency.

The runner uses `$SIXDB_RESULTS/seriespack` and reuses builds under
`build/validation/seriespack`. Results include case JSON, commands, feature and
cache context, logs and binary identities. Passing measurements omit check
executables unless `--retain-check-binaries` is requested. Use the shared
[summary/replay tools](../README.md) and [retention helpers](../../tools/artifacts.md)
for selected results.

## Findings and experiments

[Current baseline findings](findings/baseline-20260911.md) reconcile the measured
implementation. Earlier observations and their selected evidence live in
`findings/` and `evidence/`. Candidate mechanisms belong to their
[owning studies](../../spikes/README.md), including the experimental carrier
implementation used by composition comparisons.

The suite also builds the former overlay's runtime-range, boundary, head-placement
and checked-point fixtures. Select their families with `--benchmark-filter`;
[workload definitions](measurements.md) describe their contracts. Independent
oracles live in `fixtures/`, with [historical source identities](fixtures/origins.json).
The suite owns its [bulk](analysis/bulk.py) and [access](analysis/access.py) analyzers.

The old `validation/run.py` and spike CMake selector remain compatibility entries
for existing recipes, including their Calico default and binary paths. Recorded
runs retain their original paths and hashes.
