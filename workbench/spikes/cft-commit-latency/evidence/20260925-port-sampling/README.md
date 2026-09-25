# Consecutive versus scattered UDP ports, 25 September 2026

[Question, predeclared design and findings](../../network/studies/port-sampling.md).
Two fresh hosts in every us-east-1 AZ; all 60 cross-AZ host pairs and six same-AZ
controls, five fixed-tuple rounds, 4/16/32-port policies. **16 was primary.**

## Retained comparisons

`port-candidates.csv` keeps all **4,224 logical candidates**, including losers:
exact ports, arm/index, canonical aliases, every round's RTT median, training
scores and request holdouts with clock bounds in both directions. Budget
membership is index < budget. `port-comparisons.csv` compares policies on fixed
hosts; `port-request-comparisons.csv` uses separate outgoing-request winners.
Positive gains favor scattered ports. `port-totals.csv` is descriptive.

Host/clock context, checks, settings, capture/analysis identities, raw references,
cost and cleanup receipts and the PNG remain here. `clock-model.json` records
the post-capture switch to a reference-feasible point curve after three affine
fits failed; the primary RTT results do not depend on that curve. `port-design.json` hashes the
full plan, archived as exactly `port-plan.json` in the bundle named by
`artifact.json`. Candidate rows retain its complete logical port selection.
Each raw worker archive also contains that plan, its actual `execution.json`
and verified `port-reservation.json`.

## Report and recovery

The figure uses retained scores and runs offline with Matplotlib 3.10.8:

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/cft-commit-latency/evidence/20260925-port-sampling \
  build/reports/20260925-port-sampling
```

[Shared recovery guidance](../../evidence.md#network-cohorts) covers the executable
recipe and archive fetch. Full `blocks.csv`, `pair-blocks.csv`,
`boundary-blocks.csv`, the asymmetry `sensitivity.csv`, `pricing.json`,
`port-plan.json` and the SVG are archive-only. Recover into ignored `build/`.

For deeper raw reanalysis, [variance/recover.py](../../variance/README.md)
fetches the twelve archives and reconstructs the default 100 ppm timing tables with the recorded point-model choice,
all candidate/paired scores, design validation and boundary diagnostics using
the **recorded** plan. It does not regenerate EC2/pricing receipts or run the
50/1000 ppm sensitivity passes. Recovery launches no instances.
