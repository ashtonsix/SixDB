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

## Recover archived detail

Archived detail includes `blocks.csv`, `pair-blocks.csv` and the full
50/100/1000 ppm `sensitivity.csv`. The report recipe fetches the export because
the figure needs paired blocks.

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/cft-commit-latency/evidence/20260924-oneway \
  build/reports/20260924-oneway
```

[Shared recovery guidance](../../evidence.md#network-cohorts) explains
retained versus archived files, runtime requirements and raw reanalysis.
The executable recipe is in `provenance.json`; `artifact.json` still
identifies the original export.

## Deeper raw audit

[oneway/recover.py](../../oneway/README.md) fetches twelve raw worker archives
and recomputes the main timing tables. Sensitivity, host-edge summaries and
figures are separate steps. The historical `recovery-verified.json` covers only
byte-identical calibration and identity recovery from the PHC-outage host.
