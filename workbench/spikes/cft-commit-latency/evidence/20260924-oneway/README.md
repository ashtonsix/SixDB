# First one-way cohort, 24 September 2026

[Findings](../../network/studies/oneway-initial.md) and
[clock method](../../network/clocks.md). Two hosts per AZ, all fifteen cross-AZ
pairs and six same-AZ controls, four passes, 264,000 complete exchanges.
The PHC outage and broad NTP bounds remain part of the measurement.

`directions.csv` and `asymmetry.csv` retain pooled directions and paired
asymmetry with conditional clock bounds. `host-edges.csv` retains all directed
host edges and their four-pass ranges, including unfavorable candidates.
`clocks.csv`, `hardware-rx.csv`, `overhead.csv` and `counters.csv` preserve
reference coverage, drift and timestamp/ENA diagnostics. `hosts.json` and
`workers.json` preserve measured identity and exact captured-source references.
The main sampler was subsequently corrected; those fixes do not narrow this
cohort's recorded uncertainty.

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
The latter and `sensitivity.csv` preserve the paired observations and all
50/100/1000 ppm bounds used in the report.

## Recover archived detail

From the Linux repository root (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/cft-commit-latency/evidence/20260924-oneway/artifact.json \
  build/recovered/20260924-oneway
```

Choose a fresh destination. This verifies and restores the small archived
export, including every omitted table and SVG, without launching workers or
sending probes. Exact-member S3 recovery was checked against the original
hashes before reducing this Git selection. The [shared fetcher](../../../../tools/artifacts.md#recover-or-reduce-retained-evidence)
also supports repeated `--file EXACT/MEMBER` selections.

With Matplotlib 3.10.8 available, regenerate the report from recovered inputs:

```sh
python3 workbench/spikes/cft-commit-latency/oneway/plot.py \
  build/recovered/20260924-oneway --output build/recovered/20260924-oneway
```

The plot needs archived `pair-blocks.csv`; a reduced checkout alone is insufficient.


## Deeper raw audit

```sh
python3 workbench/spikes/cft-commit-latency/oneway/recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260924-oneway \
  --output build/recovered-az-oneway-raw
```

This fetches twelve raw worker archives and recomputes the main timing tables.
It does not automatically rebuild every report or resource receipt. Re-run the
analyzer with `--ppm 50` or `--ppm 1000` for sensitivity, and use `edges.py`
for host-edge summaries. The standard-library analyzer needs no plotting runtime.
`recovery-verified.json` records a narrower historical check: byte-identical
recovery of the PHC-outage host's calibration and identity members.
