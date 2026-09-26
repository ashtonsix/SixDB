# Position-pipeline evidence

This selection supports [the allocation-lifetime comparison](../../PIPELINING.md)
and the final [contention recommendation](../../CONVERGENCE.md).

It retains every competitor and cohort, including deadline failures, unfinished
work and expensive broad-envelope controls:

- `sweep.json`: 132 runs, comparing both lifetimes over 19 existing workloads and
  three envelope controls at three seeds. The controls repeat identical inputs
  across seeds; those repetitions are not independent statistical samples.
- `contrasts.json`: 16 runs separating single-attempt deadline outcomes, complete
  drains and explicitly unconstrained blind replacements.
- `semantic.json`: six successful program histories and one deliberately invalid
  provisional-position counterexample from the independent core.
- `release-audit.json`: 14 valid and 3 intentionally invalid application histories,
  plus the separate capacity-cycle/preflight dependency-graph check.
- `study.json`: exact source/raw identities and self-contained replay commands.

Only repeated timing inputs are omitted from the compact files. Full inputs and
raw outputs remain in `replay/build/orbital-pipeline/` in the source bundle.
All figures are synthetic costs or logical counts, not measured performance.

The full spike's 119 checks pass. The self-contained snapshot passes its 10 checks,
including agreement between the two pending-version cores; all 16 contrast runs
reproduce the original raw output bytes exactly. Every held-lifetime sweep
control matches the frozen backend's cohort outcomes and final serial state.
These checks do not establish distributed confluence or recovery.

`artifact.json` identifies the source/raw bundle uploaded and download-verified
by the retention tool. Recover into a new ignored directory:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/orbital-scenarios/evidence/position-pipeline \
  build/recovered/position-pipeline
```

Run `study.json`'s `commands_from_replay_root` from the recovered `replay/`
directory with Linux Python. Quote `check*.py` when passing the unittest pattern
through a shell. Prefix Linux commands with `orb -m ubuntu` on macOS. Recreate
the selected outcomes from their raw inputs using:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/pipeline_export.py \
  --input build/orbital-pipeline --output build/orbital-pipeline/evidence
```

The exporter verifies all source hashes and refuses mismatched source/results.
The [preceding envelope evidence](../contention-convergence/RECOVERY.md) and
[original comparison](../contention-comparison/RECOVERY.md) retain their own
source identities; they have not been overwritten by this follow-on.
