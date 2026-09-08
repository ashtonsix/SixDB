# Raw-string efficacy evidence

Two complete runs of identical source and prepared public inputs, 2026-09-08:

| Run | Branch budget | Checks | Tables |
| --- | ---: | --- | --- |
| `20260908T111713.048979Z` | 8 | Differential, corpus, bounds, controller, ASan/UBSan | [Summary](budget8/summary.md) |
| `20260908T111850.309580Z` | 64 | Differential, corpus, bounds, controller | [Summary](budget64/summary.md) |

Both receipts report complete and unchanged source. Source digest:
`52ba0c95553c8faef27b839573144a4f3389ecf89946b51b94f403923c33b445`.
The source snapshot includes uncommitted spike code. Source/input hashes,
compiler information, full expressions, per-container and per-query policy
traces, raw outcomes, executables, and command logs remain in the full bundles.
These are efficacy runs, not performance benchmarks; receipt durations are
operational logs only.

Each subdirectory retains every query's compact counters, all policy aggregates,
selected full expressions, input hashes, compact correctness counters, and provenance.
Both full bundles were uploaded to the existing SixDB artifact prefix and
downloaded again with SHA-256 verification. [Budget 8 reference](budget8/artifact.json)
and [budget 64 reference](budget64/artifact.json) contain S3 locations and verified
sizes. The source CSV remains externally referenced; exact prepared strings and
patterns are included in the bundles.

Regenerate compact tables on Linux, from the repository root:

```sh
python3 workbench/spikes/regexp-lowering/analyze.py workbench/spikes/regexp-lowering/evidence/raw-20260908/budget64
```

The analyzer uses `pattern_summary.csv` and `policy_summary.csv`, without
downloading data or rerunning RE2. It also emits `summary.json` for convenience;
that duplicate is not retained in Git. To recover a full bundle:

```sh
python3 workbench/tools/artifacts.py fetch workbench/spikes/regexp-lowering/evidence/raw-20260908/budget64/artifact.json build/recovered/regexp-lowering-budget64
```

Interpretation belongs in [the findings](../../FINDINGS.md). The
[runner instructions](../../experiments.md) reproduce the study; recovered
`source.tar.gz` reconstructs precisely the measured source.
