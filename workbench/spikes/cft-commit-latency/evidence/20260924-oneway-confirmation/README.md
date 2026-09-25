# Fresh-host one-way confirmation, 24 September 2026

[Findings](../../network/studies/oneway-confirmation.md) and
[clock method](../../network/clocks.md). Twelve fresh hosts, all fifteen AZ pairs
and six same-AZ controls, four passes, 132,000 complete exchanges. All ten
supported hosts retained PHC; az3 stayed NTP-only.

`directions.csv`, `asymmetry.csv` and `host-edges.csv` retain both directions,
uncertainty and every host edge's four-pass range. Clock, timestamp-boundary,
hardware, source and cleanup context remain here. The captured source digest
is authoritative: the current sampler includes later stale-NTP hardening.
Offline conservative-bound and identity-check corrections were applied to
both retained one-way cohorts.

## Retained here and archived detail

Git keeps the comparison tables, all candidates where applicable, measured
host/clock context, campaign/source identities, checks, cost/cleanup receipts,
raw-worker references and the rendered PNGs. `provenance.json` hashes only the
members retained here. `artifact.json` still identifies the **original archived
export**, which also contains the omitted detail; its object and source identities
have not changed.

The detailed per-block tables, full `sensitivity.csv`, `pricing.json` and SVG
figures are archive-only. Keeping those in S3 avoids duplicating reconstructible
row detail and a second rendering of each figure in Git. The selected comparisons
and losing candidates remain reviewable here. Recover into ignored `build/`
space, rather than placing untracked copies beside these retained files.

This cohort's archive-only per-pass tables are `blocks.csv` and `pair-blocks.csv`.
Both are needed for the original-versus-fresh az2–az4 figure; full clock-rate
sensitivity is in the archived `sensitivity.csv`.

## Recover archived detail

From the Linux repository root (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/cft-commit-latency/evidence/20260924-oneway-confirmation/artifact.json \
  build/recovered/20260924-oneway-confirmation
```

Choose a fresh destination. This verifies and restores the small archived
export, including every omitted table and SVG, without launching workers or
sending probes. Exact-member S3 recovery was checked against the original
hashes before reducing this Git selection. The [shared fetcher](../../../../tools/artifacts.md#recover-or-reduce-retained-evidence)
also supports repeated `--file EXACT/MEMBER` selections.

With Matplotlib 3.10.8 available:

```sh
python3 workbench/spikes/cft-commit-latency/oneway/plot.py \
  build/recovered/20260924-oneway-confirmation \
  --output build/recovered/20260924-oneway-confirmation --confirmation
```

To recreate the az2–az4 comparison as well, first fetch the
[initial cohort](../20260924-oneway/README.md#recover-archived-detail), then run:

```sh
python3 workbench/spikes/cft-commit-latency/oneway/edges.py \
  build/recovered/20260924-oneway-confirmation \
  --compare-first build/recovered/20260924-oneway
```


## Deeper raw audit

```sh
python3 workbench/spikes/cft-commit-latency/oneway/recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260924-oneway-confirmation \
  --output build/recovered-az-oneway-confirmation-raw
```

This downloads all twelve raw archives and recomputes the main timing tables;
it does not automatically regenerate every summary, figure or resource receipt.
The raw archives include packet/reference samples, diagnostics, actual flow ports,
binaries and captured sources.
