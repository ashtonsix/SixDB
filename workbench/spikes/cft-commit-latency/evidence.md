# Evidence and recovery

The [results](RESULTS.md) select comparisons that explain an operating choice.
These retained tables preserve the surrounding cases, repetitions and source
context. The original worker archives hold raw samples and captured experiment
sources; compact exports have separate immutable recovery references.

## Completed comparisons

| Study | Cases and repetitions | Context |
| --- | --- | --- |
| Original TCP network cohort and modeled triples | [AZ evidence guide](evidence/20260914/README.md) | Six i4i.xlarge workers, two MTUs, three passes; network-only quorum models |
| One-way clocks and host/port selection | [Network cohorts below](#network-cohorts) | Four September 24 UDP cohorts; complete candidates, uncertainty and held-out selections |
| Persistence | [All paths](evidence/20260914-durable/persistence.csv), [passes](evidence/20260914-durable/persistence-passes.csv) | [Devices and settings](evidence/20260914-durable/devices.json); screen and longer tails are labeled separately |
| D3 completion diagnostic | [Timings](evidence/20260914-durable/d3-diagnostic.csv), [passes](evidence/20260914-durable/d3-diagnostic-passes.csv), [issued requests](evidence/20260914-durable/d3-request-traces.csv) | Timing runs are separate from traced runs |
| Joint durable commit | [Configurations](evidence/20260914-durable/commits.csv), [passes](evidence/20260914-durable/commit-passes.csv) | [Nodes and storage](evidence/20260914-durable/commit-nodes.json); healthy and known-follower-absent cases are separate |
| Cliff and live preparation | [Follow-up evidence guide](evidence/20260914-cliff/README.md) | Longer replication runs, local-only storage controls and real background file preparation |

## Network cohorts

The [network guide](network/README.md) is the current interpretation; cohort
reports preserve distinct populations and capture conditions. Original TCP RTT,
calibrated one-way delay and joint durable commit use different timer boundaries.
A new-host rerun is a new observation, not a recovery of an earlier cohort.

| September 24 cohort | Completed exchanges, including same-AZ controls | Cross-AZ IPv4+UDP probe bytes | Findings / immutable evidence |
| --- | ---: | ---: | --- |
| Initial one-way; 12 hosts | 264,000 | 44.1600 MB | [Report](network/studies/oneway-initial.md) / [export and recovery](evidence/20260924-oneway/README.md) |
| Fresh one-way confirmation; 12 hosts | 132,000 | 22.0800 MB | [Report](network/studies/oneway-confirmation.md) / [export and recovery](evidence/20260924-oneway-confirmation/README.md) |
| Dense fixed tuples; 8 hosts in az2/az4 | 102,400 | 18.8416 MB | [Report](network/studies/tuple-dense.md) / [export and recovery](evidence/20260924-variance-dense/README.md) |
| Broad one-port tuples; 12 hosts across all six AZs | 105,600 | 17.6640 MB | [Selection findings](network/selection.md) / [export and recovery](evidence/20260924-variance-broad/README.md) |
| Total | **604,000** | **102.7456 MB** | All **44 distinct instances terminated** |

MB here means 1,000,000 bytes. A 64-byte payload plus IPv4/UDP headers is 92
bytes per datagram, or 184 per complete echo. Probe-byte accounting includes
warmup but excludes same-AZ controls from the cross-AZ total. No missing or
duplicate exchanges were observed. This was a paced latency study, not a
throughput screen.

The four `campaign.json` files model approximately **$1.22 EC2 compute through
archive completion**, including reused preflight-host lifetimes, and
**$0.0021 cross-AZ probe transfer** at the captured aggregate two-sided rate of
$0.02/GB. These are scoped models, not observed billing. Shutdown lag, EBS,
public IPv4, S3, control traffic, encapsulation and tax are excluded; the user's
reported ~$40 for earlier spike traffic is outside this follow-up's accounting.
The corresponding `resources.json` files retain cleanup receipts.

Git keeps candidate scores (including losing comparisons), selected summaries,
measured host/clock context, campaign/source identities, cost/cleanup receipts,
raw-worker references and PNGs. `provenance.json` hashes that selection;
`artifact.json` identifies the original export. Detailed block/round and boundary
tables, full 50/100/1000 ppm sensitivity, pricing dumps and SVGs live in the export's
S3 bundle. Historical capture gaps remain gaps.

`selected-host-pairs-*` evaluates ports on fixed machines against flow 0;
`selected-az-directions-*` chooses hosts and ports jointly. Broad `request` and
`all-legs` rankings stay separate. Clock intervals are conditional measurement
bounds, not confidence intervals; independent clock fits use the full capture
while ranking excludes held-out packets. These are offline holdouts.

Run the cohort's recorded recipe with Matplotlib 3.10.8 available (Linux repository
root; prefix with `orb -m ubuntu` from the Mac):

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-dense \
  build/reports/20260924-variance-dense
```

The broad figure uses retained inputs offline. The other recipes fetch their small
exports; confirmation also fetches the initial cohort and produces both figures.
Use a fresh output directory. `--dry-run` shows the steps; the adjacent report
receipt records current entrypoints and command completion, without rewriting
captured-source identities. For archived tables alone, use `artifacts.py fetch`
with the same two paths. [Artifact tools](../../tools/artifacts.md#run-a-report-recipe)
own the command details.

All 98 originally selected members were verified through exact-member S3 recovery
before reducing Git retention; all five figures were regenerated. The recipe
commands reproduce those figures using current plotting sources.

Deeper raw reanalysis uses `oneway/recover.py` for the first two cohorts and
`variance/recover.py` for fixed tuples, with the evidence directory and
`--output build/recovered/NAME-raw`. These fetch existing worker archives and
recompute timing tables; variance also rebuilds rankings and boundary diagnostics.
Figures and resource receipts remain separate. Dense raw reconstruction was
verified; initial verification covered only outage-host calibration/identity,
and full broad raw reconstruction is not claimed. The cohort guides retain
these specific limits. Recovery and these report recipes launch no workers.

## Read the fields and distributions

Numerical columns ending `_us` are **microseconds**; `_ms` columns and the durable-commit
tables use **milliseconds**. The network selection reports use microseconds. Raw timestamps ending `_ns` are nanoseconds. Network echo sizes
describe both the request and the full-size reply; durable replication uses a
record and a small acknowledgment. In the one-record CSVs, `remote1_*` and
`remote2_*` are follower write durations, not leader-observed acknowledgment
timestamps; they cannot be substituted into the quorum-readiness equation.

The durable [analysis record](evidence/20260914-durable/analysis.json) identifies
analyzers, units, sample counts and selected CSV hashes. Pooled percentiles and
per-pass ranges describe different aspects of the observations. The
[worker references](evidence/20260914-durable/workers.json) identify eleven raw
archives: six persistence workers, one D3 diagnostic worker and four commit
workers. All were fetched and reanalyzed independently; selected CSVs reproduced
byte for byte. The compact bundle was also uploaded and download-verified.

## Throughput cohorts

The [original throughput findings](commit/throughput.md) distinguish short screens
from three-pass candidates. Each original cohort has its own immutable directory,
with raw worker references, captured settings, analysis and recovery reference:

| Cohort | Cases and passes | Selected choices | Recovery |
| --- | --- | --- | --- |
| Small, standard ENA | [Configurations](evidence/20260914-durable/throughput-small/throughput.csv), [passes](evidence/20260914-durable/throughput-small/throughput-passes.csv) | [Percentile selections](evidence/20260914-durable/throughput-small/knees.csv) | [Worker archives](evidence/20260914-durable/throughput-small/workers.json), [compact bundle](evidence/20260914-durable/throughput-small/artifact.json) |
| Larger, standard ENA | [Configurations](evidence/20260914-durable/throughput-scale/throughput.csv), [passes](evidence/20260914-durable/throughput-scale/throughput-passes.csv) | [Percentile selections](evidence/20260914-durable/throughput-scale/knees.csv) | [Worker archives](evidence/20260914-durable/throughput-scale/workers.json), [compact bundle](evidence/20260914-durable/throughput-scale/artifact.json) |
| Larger, ENA Express | [Configurations](evidence/20260914-durable/throughput-express/throughput.csv), [passes](evidence/20260914-durable/throughput-express/throughput-passes.csv) | [Percentile selections](evidence/20260914-durable/throughput-express/knees.csv) | [Worker archives](evidence/20260914-durable/throughput-express/workers.json), [compact bundle](evidence/20260914-durable/throughput-express/artifact.json) |

All three cohorts independently reproduced all seven reconstructed numeric/context
outputs byte for byte from recovered raw archives. Six are retained in each
compact export; the seventh is the equivalent pooled JSON representation in
ignored recovery output. Cleanup is verified by separate receipts for the
[original durable studies](evidence/20260914-durable/resources.json),
[small](evidence/20260914-durable/throughput-small/resources.json),
[larger standard](evidence/20260914-durable/throughput-scale/resources.json) and
[Express](evidence/20260914-durable/throughput-express/resources.json) cohorts.
The network cohort records cleanup in its [campaign receipt](evidence/20260914/campaign.json).

For throughput, the pooled `mean_batch_records` is total native records divided
by total native batches across whole passes, including warmup. Per-pass
`average_batch_records` and pooled `record_weighted_batch_records` describe batch
size as experienced by measured records, giving larger batches more weight.
These quantities differ from each other and from the configured batch-size cap.

`quorum_drain_seconds` ends at the final quorum commit; the older `drain_seconds`
field has this same boundary. `all_followers_drain_seconds` includes both
acknowledgments, completed sends and native ring teardown. Recomputed
`stable_observed` checks both drains; `recorded_stable_observed` preserves the
earlier classification. The corrected drain rule changed no cohort classification
or selected landmark. The analyzer validates archived summaries against their
original definitions before recomputing and ranking from the recorded timestamps.

## Cliff follow-up

The [longer-run findings](commit/cliff.md) have a separate
[retained guide](evidence/20260914-cliff/README.md) covering cases, time-aligned
diagnostics, preparation receipts and captured sources. Its
[recovery verification](evidence/20260914-cliff/recovery-verified.json) records
byte-identical reconstruction of the selected numerical tables and all four
figures; the [cleanup receipt](evidence/20260914-cliff/resources.json) covers
all new instances, including the failed setup attempt. Follow that guide's
[`cliff_recover.py`](commit/cliff_recover.py) recipe to recover this campaign.

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
`--study` recovers the original durable studies, including nested throughput cohorts.
Use `--study throughput` for the small cohort and `--study throughput-scale`
for the larger standard-ENA cohort; `--study throughput-express` recovers Express.
AWS read access to the existing artifact bucket is required.

For only the compact bundle, use [the shared artifact fetcher](../../tools/artifacts.md)
with its [recovery reference](evidence/20260914-durable/artifact.json). The
[AZ evidence guide](evidence/20260914/README.md) provides the separate command
for recovering the original network cohort. Reproduction uses the captured
sources and recorded toolchain/settings; a rerun on new hosts is a new observation.

## Regenerate the report figures

The figures in `images/` are reader-facing views of the original studies' CSVs. Original
figures and source hashes in `evidence/` remain part of the immutable exports.
With matplotlib 3.10.8 available, run from the repository root on Linux:

```sh
python3 workbench/spikes/cft-commit-latency/plot-durable.py \
  workbench/spikes/cft-commit-latency/evidence/20260914-durable \
  --output build/cft-commit-latency/figures
python3 workbench/spikes/cft-commit-latency/plot.py \
  workbench/spikes/cft-commit-latency/evidence/20260914 \
  --output build/cft-commit-latency/figures
```

The operating-choice chart selects the same `knees.csv` rows as the report;
the screen/repeat chart selects one unchanged policy from the per-pass CSV.
Repeated-candidate plots show pooled values and observed pass ranges. Their
budget view has a fixed 0.3–1.25 ms scale and flags any range extending above it;
the companion view retains the full tail. MTU intervals describe variation
across directions. None of these intervals is a confidence interval.
The cliff figures live with their retained evidence; their
[separate reproduction guide](evidence/20260914-cliff/README.md#recover-and-verify)
specifies the analyzer and plotting sources used for that follow-up.
