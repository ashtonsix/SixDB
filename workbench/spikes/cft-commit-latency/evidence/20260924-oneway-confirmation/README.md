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

## Recover archived detail

The recipe fetches this export for `blocks.csv` and `pair-blocks.csv`, plus
the initial cohort for the az2–az4 comparison. It produces both figures. Full
clock-rate sensitivity remains in the archived `sensitivity.csv`.

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/cft-commit-latency/evidence/20260924-oneway-confirmation \
  build/reports/20260924-oneway-confirmation
```

[Shared recovery guidance](../../evidence.md#network-cohorts) explains
retained versus archived files, runtime requirements and raw reanalysis.
The executable recipe is in `provenance.json`; `artifact.json` still
identifies the original export.

## Deeper raw audit

[oneway/recover.py](../../oneway/README.md) fetches twelve raw archives and
recomputes the main timing tables; other summaries, figures and resource receipts
are separate. Raw archives retain packet/reference samples, diagnostics, actual
flow ports, binaries and captured sources.
