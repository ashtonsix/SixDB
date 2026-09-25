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

## Recover archived detail

The figure needs only retained `selected-az-directions-request.csv`, so its
recipe works offline. Fetch the export separately for `flow-blocks.csv`,
`pair-blocks.csv`, `boundary-blocks.csv` or full clock-rate sensitivity.

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-broad \
  build/reports/20260924-variance-broad
```

[Shared recovery guidance](../../evidence.md#network-cohorts) explains
retained versus archived files, runtime requirements and raw reanalysis.
The executable recipe is in `provenance.json`; `artifact.json` still
identifies the original export.

## Deeper raw audit

[variance/recover.py](../../variance/README.md) fetches twelve raw archives and
rebuilds timing tables, both rankings and boundary diagnostics; figures and
resource receipts are separate. This cohort’s export and figure recovery were
tested; full raw reconstruction was tested on the dense cohort. Independent
clock fits use the full capture, so this is an offline holdout.
