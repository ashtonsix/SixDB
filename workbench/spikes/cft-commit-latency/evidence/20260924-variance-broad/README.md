# Broad one-port variance study, 24 September 2026

[Findings](../../network/selection.md) and [method](../../network/method.md).
Twelve hosts, all fifteen AZ pairs and six same-AZ controls, four tuples,
five rounds, 105,600 complete exchanges. Only the lower-numbered endpoint's
port varies; tuples are reused across rounds and reversed roles.

All `flow-candidates-*`, `selected-host-pairs-*`, `selected-az-directions-*` and
`variance-checks-*` files remain here, with **request** and **all-legs** rankings
separate. Losing candidates and controls are kept. Training is rounds 0–2;
3–4 supply held-out requests in both directions. Host-pair winners compare
ports on fixed machines against flow 0; AZ-direction winners choose hosts and
ports jointly. `pair-variance.csv` includes every adjacent same-role comparison,
including 2→4. `tuples.csv` and `capture-config.json` retain the verified design.

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

Archive-only detail includes `flow-blocks.csv`, `pair-blocks.csv` and
`boundary-blocks.csv`: every directed host/tuple/round, paired asymmetry and
per-block timestamp-boundary diagnostics respectively.

## Recover archived detail

From the Linux repository root (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-broad/artifact.json \
  build/recovered/20260924-variance-broad
```

Choose a fresh destination. This verifies and restores the small archived
export, including every omitted table and SVG, without launching workers or
sending probes. Exact-member S3 recovery was checked against the original
hashes before reducing this Git selection. The [shared fetcher](../../../../tools/artifacts.md#recover-or-reduce-retained-evidence)
also supports repeated `--file EXACT/MEMBER` selections.

With Matplotlib 3.10.8 available, regenerate the selected-edge figure:

```sh
python3 workbench/spikes/cft-commit-latency/variance/plot.py \
  build/recovered/20260924-variance-broad --broad
```

This particular figure uses the retained `selected-az-directions-request.csv`;
the recovered-directory recipe also works for the figures requiring archived detail.


## Deeper raw audit

```sh
python3 workbench/spikes/cft-commit-latency/variance/recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-broad \
  --output build/recovered-variance-broad-raw
```

This downloads all twelve raw archives and rebuilds timing tables, both rankings
and boundary diagnostics; figures and resource receipts are separate. All raw
archives were collected and verified. Full raw reconstruction was tested on
the dense cohort; this cohort's archived export and figure recovery were tested
separately. Clock fits use all independent references, so the holdout is offline.
