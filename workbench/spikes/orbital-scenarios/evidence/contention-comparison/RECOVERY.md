# Contention study evidence

The retained CSVs include every compared series, competitor and selected sensitivity,
including failures and pending work. Repeated controls are included for paired
comparisons; these are synthetic model runs, not statistical performance samples.
The three semantic JSON files preserve all 27 authored histories.

The full archive contains `replay/`, a self-contained Python source and raw-result
snapshot. After fetching the archive, change directory into `replay/`. Study
commands and parameters are in the root `study.json` and individual raw artifacts.
The fixed-execution sensitivity runner is at
`build/workbench/orbital-fixed-execution/run_sensitivities.py`; its exact inputs
and manifest are alongside it. The snapshots of both briefs are context only.

Python standard library only. No throughput or elapsed-wall-time conclusions
follow from the modeled ticks, service units or control-message counts.
