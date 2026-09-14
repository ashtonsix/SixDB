# Evidence and recovery

The [results](RESULTS.md) select comparisons that explain an operating choice.
These retained tables preserve the surrounding cases, repetitions and source
context. The original worker archives hold raw samples and captured experiment
sources; compact exports have separate immutable recovery references.

## Completed comparisons

| Study | Cases and repetitions | Context |
| --- | --- | --- |
| Network: all 15 pairs and 20 triples | [AZ evidence guide](evidence/20260914/README.md) | Six i4i.xlarge workers, two MTUs, three passes; network-only quorum models |
| Persistence | [All paths](evidence/20260914-durable/persistence.csv), [passes](evidence/20260914-durable/persistence-passes.csv) | [Devices and settings](evidence/20260914-durable/devices.json); screen and longer tails are labeled separately |
| D3 completion diagnostic | [Timings](evidence/20260914-durable/d3-diagnostic.csv), [passes](evidence/20260914-durable/d3-diagnostic-passes.csv), [issued requests](evidence/20260914-durable/d3-request-traces.csv) | Timing runs are separate from traced runs |
| Joint durable commit | [Configurations](evidence/20260914-durable/commits.csv), [passes](evidence/20260914-durable/commit-passes.csv) | [Nodes and storage](evidence/20260914-durable/commit-nodes.json); healthy and known-follower-absent cases are separate |

The selected [persistence figure](evidence/20260914-durable/persistence.png) and
[commit figure](evidence/20260914-durable/commits.png) use these tables. Numerical
columns ending `_us` are **microseconds**; the main report converts commit times
to milliseconds. Raw timestamps ending `_ns` are nanoseconds. Network echo sizes
describe both the request and the full-size reply; durable replication uses a
record and a small acknowledgment.

The durable [analysis record](evidence/20260914-durable/analysis.json) identifies
analyzers, units, sample counts and selected CSV hashes. Pooled percentiles and
per-pass ranges describe different aspects of the observations. The
[worker references](evidence/20260914-durable/workers.json) identify eleven raw
archives: six persistence workers, one D3 diagnostic worker and four commit
workers. All were fetched and reanalyzed independently; selected CSVs reproduced
byte for byte. The compact bundle was also uploaded and download-verified.

## Throughput cohorts

The [throughput findings](commit/throughput.md) distinguish short screens from
three-pass candidates. Each completed cohort has its own immutable directory,
with raw worker references, captured settings, analysis and recovery reference:

| Cohort | Cases and passes | Selected choices | Recovery |
| --- | --- | --- | --- |
| Small, standard ENA | [Configurations](evidence/20260914-durable/throughput-small/throughput.csv), [passes](evidence/20260914-durable/throughput-small/throughput-passes.csv) | [Percentile selections](evidence/20260914-durable/throughput-small/knees.csv) | [Worker archives](evidence/20260914-durable/throughput-small/workers.json), [compact bundle](evidence/20260914-durable/throughput-small/artifact.json) |
| Larger, standard ENA | [Configurations](evidence/20260914-durable/throughput-scale/throughput.csv), [passes](evidence/20260914-durable/throughput-scale/throughput-passes.csv) | [Percentile selections](evidence/20260914-durable/throughput-scale/knees.csv) | [Worker archives](evidence/20260914-durable/throughput-scale/workers.json), [compact bundle](evidence/20260914-durable/throughput-scale/artifact.json) |
| Larger, ENA Express | [Configurations](evidence/20260914-durable/throughput-express/throughput.csv), [passes](evidence/20260914-durable/throughput-express/throughput-passes.csv) | [Percentile selections](evidence/20260914-durable/throughput-express/knees.csv) | [Worker archives](evidence/20260914-durable/throughput-express/workers.json), [compact bundle](evidence/20260914-durable/throughput-express/artifact.json) |

All three cohorts independently reproduced all seven reconstructed numeric/context
outputs byte for byte from recovered raw archives. Six are retained in each
compact export; the seventh is the equivalent pooled JSON representation in
ignored recovery output.
The
[completed resource receipt](evidence/20260914-durable/resources.json) is a
snapshot for its listed cohorts, not a claim that later workers are closed.

For throughput, the pooled `mean_batch_records` is total native records divided
by total native batches across whole passes, including warmup. Per-pass
`average_batch_records` and pooled `record_weighted_batch_records` describe batch
size as experienced by measured records, giving larger batches more weight.
These quantities differ from each other and from the configured batch-size cap.

`quorum_drain_seconds` ends at the final quorum commit; the older `drain_seconds`
field has this same boundary. `all_followers_drain_seconds` includes both
acknowledgments, completed sends and native ring teardown. Recomputed
`stable_observed` checks both drains; `recorded_stable_observed` preserves the
earlier classification. The analyzer validates archived summaries against their
original definitions before recomputing and ranking from the recorded timestamps.

## Recover a comparison

From the repository root on Linux, prefixing with `orb -m ubuntu` on the Mac:

```sh
python3 workbench/spikes/cft-commit-latency/recover-durable.py \
  workbench/spikes/cft-commit-latency/evidence/20260914-durable \
  --study commit --output build/cft-commit-latency/recovered-commit
```

Choose a fresh output directory. This fetches and hash-verifies the selected
workers' raw archives and reruns their analysis without launching machines.
Use `--study persistence` or `--study diagnostic` for those studies; omitting
`--study` recovers all available studies, including nested throughput cohorts.
Use `--study throughput` for the small cohort and `--study throughput-scale`
for the larger standard-ENA cohort; `--study throughput-express` recovers Express.
AWS read access to the existing artifact bucket is required.

For only the compact bundle, use [the shared artifact fetcher](../../tools/artifacts.md)
with its [recovery reference](evidence/20260914-durable/artifact.json). The
[AZ evidence guide](evidence/20260914/README.md) provides the separate command
for recovering the original network cohort. Reproduction uses the captured
sources and recorded toolchain/settings; a rerun on new hosts is a new observation.
