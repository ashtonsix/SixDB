# Historical measurement recipes

These recipes served the allocation/Stage 2 investigations. Both disposable
drivers are now historical; these commands are reproduction aids, not a launch
menu for the next Engine driver. Reusable probes are linked from the
[residency guide](../../tools/residency.md).

The retired dispatcher is recoverable as
`git show fc721b2b:workbench/benchmarks/orbital/stage2.py`.
It hardcoded an on-demand xlarge worker and immediate shutdown; those choices
are not defaults for future studies. For historical reproduction, use the
original captured runner and its recorded arguments/source/binary identities.

| Selection | Useful comparison |
| --- | --- |
| `scalar` | All 19 workloads, three footprints, five backends, throughput and request latency |
| `prepared-same-cpu`, `prepared-extra-cpu` | Preparation against the synchronous control, actual readiness samples, startup/waste/drain and whole-process/producer CPU |
| `lookup-widths` | Native/manual/Ring, active widths 1/16/32/64, loaded/warmed traces and genuine LMDB controls |
| `lifecycle-1`, `lifecycle-16`, `lifecycle-64` | Finite/window/mixed allocation, growth, foreign return and physical closure |
| `pmu-availability` | Event catalog, permissions, errors and scheduling coverage |
| `memory-sensitivity` | Real dependent-load and independent-chain footprint controls; ARM additionally attempts architectural refill events |
| `composed-counters` | Separate throughput diagnostic with stalls and post-interval mapping receipts |

For new diagnostics, use the independent [PMU tool](../../tools/pmu/README.md)
and [memory sensitivity runner](../../spikes/memory-characterisation/pmu.sh)
through the ordinary [worker helper](../../tools/workers.md). These are useful
without an Orbital driver. The table records the old panels, not a new workload menu.

Use `worker.py wait JOB` for verified collection, then `run.py --summarize-only`
for scalar/staged panels. The lifecycle runner writes its verified `cases.csv`
and `summary.json` during collection.
The [measurement reference](measurement.md) explains request versus batch units,
preparation sidecars, trace joins and counter scope. Keep meaningful comparisons
with an [artifact recovery reference](../../tools/artifacts.md), rather than
copying every raw run into Git.
