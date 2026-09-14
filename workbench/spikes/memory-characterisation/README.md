# Automatic memory characterisation

Can initialization recognise a CPU, reuse applicable knowledge, and cheaply
infer missing memory-system characteristics from short performance sweeps?
The consumers are hash bucket / TuplePack geometry, ring sizes, placement and
scheduling in Loom or Engine. The scope also includes cache sharing, NUMA,
filesystem I/O and transport diagnostics. This spike does not select a module
boundary or a production API. [Findings](FINDINGS.md) cover 13 instance types;
[fleet comparison](FLEET.md) and [lookup](lookup.json) retain the results.

Start with reported cache geometry, CPU identity, topology and OS page options.
Measure behaviour separately: effective independent-miss concurrency, spatial
fetching, training length, lookahead, page-boundary effects, stream retention,
translation reach, and interference. Unknown is an output: a throughput knee
is not by itself a count of MSHRs, and a stream knee is not by itself a table size.

The probe is implemented afresh. [Calico's AMAC report](../../../../calico/loom/report/AMAC.md)
supplies prior questions and numbers to recalibrate, not static SixDB defaults.
[Method and reading](METHOD.md) explains controls and interpretation.

The [CFT commit study](../cft-commit-latency/README.md) complements these host
measurements with controlled persistence paths, paired cross-AZ links and an
actual replicated log under arrival-driven load. Use it when the question moves
from local memory and diagnostic I/O costs to a durable distributed operation.
Both studies distinguish measured behaviour from reported hardware properties
and keep tuning choices conditional on the workload and observation window.

Build/run on Linux (prefix commands with `orb -m ubuntu` from this workspace):

```sh
python3 workbench/spikes/memory-characterisation/run.py --profile screen
python3 workbench/spikes/memory-characterisation/run.py --profile quick
python3 workbench/spikes/memory-characterisation/initialise.py
python3 workbench/tools/worker.py run workbench/spikes/memory-characterisation/cloud.sh --machine zen5
python3 workbench/tools/worker.py --config workbench/spikes/memory-characterisation/smt-worker.json run workbench/spikes/memory-characterisation/cloud.sh --machine granite-rapids --instance-type c8i.xlarge
```

The runner captures sources through the existing experiment helper, builds only
this target with pinned Clang, pins each measurement, retains repetitions, and
emits `hardware.json`, `samples.csv`, `analysis.json` and `summary.md`. The quick
profile bounds work and memory; it is an experiment in initialization cost,
not a hard real-time deadline. `--smt` adds sibling and separate-core interference
when the visible topology permits it. Cloud screen runs enable it by default.

`initialise.py` uses the qualified lookup first and otherwise runs the quick
profile; `--force-probe` bypasses the lookup. `--mib 256` gives a quick repeat
the same largest footprint as the fleet screen. `--system` adds temporary-file
I/O and loopback/regional HTTPS diagnostics. Ordinary probes never format a
device. The optional `instance-store.sh` preparation is restricted to an
explicitly requested fresh disposable EC2 worker; [the method](METHOD.md)
and [findings](FINDINGS.md) explain that experiment.

[Retained evidence](evidence/README.md) owns offline regeneration and recovery.
`check.py` checks inference refusal and lookup invalidation; every generated
pointer cycle is validated by the measurement binary before timing.
