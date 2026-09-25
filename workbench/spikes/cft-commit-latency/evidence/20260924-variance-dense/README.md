# Dense fixed-tuple control, 24 September 2026

[Findings](../../network/studies/tuple-dense.md) and
[controlled method](../../network/method.md). Eight hosts, sixteen az2/az4 host
pairs, sixteen port tuples per pair, four shuffled rounds, 102,400 exchanges.
Both ports vary across tuples; each tuple is reused across rounds and roles.

`flow-candidates-all-legs.csv` keeps every training/holdout candidate, including
losers. `selected-host-pairs-all-legs.csv` compares port selection on fixed hosts
against flow 0; `selected-az-directions-all-legs.csv` selects hosts and ports
jointly. Lower→higher's sole held-out block is a reply; empty request cells
preserve that limitation. `pair-variance.csv` retains tuple spreads, same-role
changes and correlations; `tuples.csv`, `capture-config.json` and
`variance-checks-all-legs.json` retain the verified design and selection rule.

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

This cohort's archive-only detail is `flow-blocks.csv` (all directed
host/flow/round observations), `pair-blocks.csv` (paired asymmetry) and
`boundary-blocks.csv` (per-block timestamp-boundary diagnostics).

## Recover archived detail

From the Linux repository root (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-dense/artifact.json \
  build/recovered/20260924-variance-dense
```

Choose a fresh destination. This verifies and restores the small archived
export, including every omitted table and SVG, without launching workers or
sending probes. Exact-member S3 recovery was checked against the original
hashes before reducing this Git selection. The [shared fetcher](../../../../tools/artifacts.md#recover-or-reduce-retained-evidence)
also supports repeated `--file EXACT/MEMBER` selections.

With Matplotlib 3.10.8 available, regenerate the repeatability figure:

```sh
python3 workbench/spikes/cft-commit-latency/variance/plot.py \
  build/recovered/20260924-variance-dense
```

This requires archived `flow-blocks.csv`; the reduced checkout alone is insufficient.


## Deeper raw audit

```sh
python3 workbench/spikes/cft-commit-latency/variance/recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-dense \
  --output build/recovered-variance-dense-raw
```

This downloads eight raw archives and rebuilds clock tables, selection and
boundary diagnostics. `recovery-verified.json` records byte-identical
reconstruction of its named tables, with semantically equal clock checks.
Figures and resource receipts are separate. Full-capture clock fitting makes
this an offline latency holdout.
