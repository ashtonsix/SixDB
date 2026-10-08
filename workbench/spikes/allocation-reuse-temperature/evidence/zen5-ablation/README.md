# Selector ablation on Zen5

The complete run `20261008T101428Z-6bf8d702` compares the ranked baseline,
Window first-ready, finite two-pass and both together on one Spot c8a.large.
[Findings](../../FINDINGS.md) explain the decision and limits.

The selection keeps all 4,608 matching blocks: four policies, two touch widths,
nine distinct cases, eight trials and eight blocks per case. `touch_bytes=0`
means the whole variable-sized requested payload. Samples carry VM call counts,
allocation/reuse counts and mapped/idle byte snapshots. These are block means,
not individual-operation latency percentiles. `comparisons.csv` is the runner's
pooled summary; the report recipe computes same-trial ratios and observed ranges.

`context.json` records balanced order, commands and frozen source/binary hashes.
`host.json` records the actual machine. `paths.json` is a separate untimed
instrumented comparison, not a performance counter in measured binaries.
Worker completion succeeded. The interrupted first attempt contributes no rows.

Run the offline recipe shown in the [study README](../../README.md).
[artifact.json](artifact.json) recovers complete sources, receipts, binaries,
checks and logs. Those compiler outputs and full manifests stay in S3.
