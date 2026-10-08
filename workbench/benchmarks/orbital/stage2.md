# Carried-forward measurement recipes

`stage2.py` prints one command using the existing worker and workload runners.
Add `--run` to dispatch it. Nothing is automatically queued. Supply the source
capture for the composition being tested; the retained allocation-stage panels
provide a baseline, with their original source and binary identities.

```sh
orb -m ubuntu python3 workbench/benchmarks/orbital/stage2.py lookup-widths \
  --source build/captures/SELECTED-STAGE2-CUT
```

| Selection | Useful comparison |
| --- | --- |
| `scalar` | All 19 workloads, three footprints, five backends, throughput and request latency |
| `prepared-same-cpu`, `prepared-extra-cpu` | Preparation against the synchronous control, actual readiness samples, startup/waste/drain and whole-process/producer CPU |
| `lookup-widths` | Native/manual/Ring, active widths 1/16/32/64, loaded/warmed traces and genuine LMDB controls |
| `lifecycle-1`, `lifecycle-16`, `lifecycle-64` | Finite/window/mixed allocation, growth, foreign return and physical closure |
| `pmu-availability` | Event catalog, permissions, errors and scheduling coverage |
| `memory-sensitivity` | Real dependent-load and independent-chain footprint controls; ARM additionally attempts architectural refill events |
| `composed-counters` | Separate throughput diagnostic with stalls and post-interval mapping receipts |

Each job settles its worker by default. `--idle-seconds 900` permits compatible
reuse for a paired experiment; check actual instance and binary identities after
collection. `--machine neoverse-v2` selects the ARM build, and `--instance-type`
allows an explicitly chosen PMU-capable size. Availability and sensitivity
controls precede selecting raw events; [residency tools](../../tools/residency.md)
own the method. These recipes do not select a new research priority.

Use `worker.py wait JOB` for verified collection, then `run.py --summarize-only`
for scalar/staged panels. The lifecycle runner writes its verified `cases.csv`
and `summary.json` during collection.
The [measurement reference](measurement.md) explains request versus batch units,
preparation sidecars, trace joins and counter scope. Keep meaningful comparisons
with an [artifact recovery reference](../../tools/artifacts.md), rather than
copying every raw run into Git.
