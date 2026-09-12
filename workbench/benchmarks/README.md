# Benchmarks

Recurring component and system workloads, plus shared measurement machinery.
The [SeriesPack suite](seriespack/README.md) measures packed arrays, access,
composition and caller costs. The [TuplePack suite](tuplepack/README.md) measures
ordered code maps, mutation effects, native batching and pipeline boundaries.
Individual candidate experiments and their findings
stay with the [investigation](../spikes/README.md) that asks the question.

The [results explorer](../results/README.md) turns retained evidence into a
browsable snapshot, with recorded comparisons, repetitions and source context.

## Run a suite

Select suites with `-DSIXDB_BENCHMARKS=seriespack` (semicolon-separated for several),
or `python3 workbench/tools/dev.py --add-benchmark seriespack` for editor support.
Only selected suites are configured; build the target needed for the comparison.

Google Benchmark provides microbenchmark machinery. The usual convention
is to pin CPUs and run cases/repetitions sequentially, without interleaving,
so experiments can isolate performance across cache-residence and resident-set
size tiers. The workload controls footprint and access pattern; those tiers
need to be established on the measured machine.

The [worker guide](../tools/workers.md) covers machine selection and background
activity. [Source captures](../tools/README.md#captured-experiment-runs) let several
machines use one set of bytes while editing continues.

## Replay and summarize

For another runtime setting or a paired comparison, [replay.py](../tools/replay.py)
runs retained binaries with pinned CPU affinity, sequential trials and RSS
records. The spec selects artifact/member hashes, a filter and trial order;
the [Ikea pair](../spikes/executable-placement/replay-k10.json) is an example.

```sh
python3 workbench/tools/replay.py path/to/replay.json --output build/replays/pair
python3 workbench/tools/evidence.py summarize baseline=path/to/samples.json \
  --output build/summaries/pair
```

On a worker, replay defaults to `$SIXDB_RESULTS/replay`; `--cpu` or `SIXDB_CPU`
selects affinity. Submit [replay.sh](../tools/replay.sh) through the ordinary
worker command, with the spec path after `--`.
Summaries keep repetitions and medians in one CSV row per case,
with input hashes/context. Repeat `--counter NAME` for useful workload counters,
or use `--filter` to select cases. `ns_per_item` follows the benchmark's item
count: a value, query and byte are different units. Optional `--artifact REF`
attaches an existing recovery reference. Comparison and interpretation stay in
the study; [retention](../tools/artifacts.md) handles selected evidence.

The [aggregate runner](../spikes/aggregate-maintenance/run.py) is an example of
Google Benchmark 1.9.4 with separate logical accounting. The
[regexp study](../spikes/regexp-lowering/CONCLUSIONS.md) uses counts without timing.
Calico's [Google Benchmark pilot](../../../calico/qhash/bench/GOOGLE.md) and
[affinity helper](../../../calico/tools/fleet/recipes/lib/microbench.sh) remain prior work.
