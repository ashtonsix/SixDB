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

## Recover archived detail

The recipe fetches the export because the figure needs `flow-blocks.csv`.
Other archived detail includes paired asymmetry (`pair-blocks.csv`), timestamp
boundaries (`boundary-blocks.csv`) and full clock-rate sensitivity.

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-dense \
  build/reports/20260924-variance-dense
```

[Shared recovery guidance](../../evidence.md#network-cohorts) explains
retained versus archived files, runtime requirements and raw reanalysis.
The executable recipe is in `provenance.json`; `artifact.json` still
identifies the original export.

## Deeper raw audit

[variance/recover.py](../../variance/README.md) fetches eight raw archives and
rebuilds clock tables, selection and boundary diagnostics. `recovery-verified.json`
records byte-identical named tables and semantically equal clock checks; figures
and resource receipts are separate. Full-capture clock fitting makes this an
offline latency holdout.
