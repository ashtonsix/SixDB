# September 14, 2026 AZ-latency evidence

This selection retains the full comparison space: every AZ pair/direction,
MTU/message size, repetition and three-voter CFT network scenario. Raw RTTs,
ping output, host diagnostics and captured measurement sources remain in six
verified immutable worker archives referenced by `workers.json`. Their small
common summaries have their own `artifact.json` recovery reference.

| File | Contents |
|---|---|
| `directed.csv` | 540 protocol/MTU/size/direction summaries, with tail and repetition statistics |
| `pairs.csv` | 270 summaries pooling the two initiating directions |
| `repetitions.csv` | 1620 successful-size per-pass cases, including transport metadata |
| `mtu-boundaries.csv` | 360 expected oversize rejection cases, distinct from packet loss |
| `triples.csv` | All 20 sets × two MTUs × six TCP sizes, ranked by worst directional p99 |
| `consensus-scenarios.csv` | All 720 leader/set/MTU/size network models; joint-distribution bounds and separately labeled independence scenario |
| `diagnostics.csv` | 36 epochs of relevant NIC counter deltas and coarse CPU time |
| `hosts.json`, `workers.json` | Host context and six original worker archive references |
| `campaign.json` | Launch identities, successful collection outcomes and confirmed resource cleanup |
| `analysis.json` | Analysis hashes, quantile conventions and validation totals |
| `pricing.json` | AWS's contemporaneous on-demand instance price, not a bill |
| `az-rtt.png`, `mtu-effect.png` | Selected figures supporting the findings |

Units in machine-readable statistics are **microseconds**, unless a field says
otherwise (`started`/`ended`/`utc` are Unix seconds). TCP byte counts describe
application bytes **both requested and echoed**; ICMP byte counts exclude its
28-byte IPv4/ICMP overhead. TCP retransmissions are segment counters, not failed
request counts. Oversize ICMP rejections are excluded from fitting-packet loss.

From the repository root on Linux, prefixing with `orb -m ubuntu` on the Mac:

```sh
python3 workbench/spikes/cft-commit-latency/recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260914 \
  --output build/az-latency/recovered-20260914
```

This fetches and hash-verifies all six raw archives into a new directory and
regenerates descriptive statistics, diagnostics and consensus scenarios. AWS
read access to the existing artifact bucket is required. It does not launch
workers. For only the compact evidence, use the ordinary `artifacts.py fetch`
command on this directory instead.

The [figure guide](../../evidence.md#regenerate-the-report-figures) regenerates
the current presentation from these CSVs, outside this original export.
The [findings](../../az-findings.md) distinguish the measured link distributions from
network-only consensus models and discuss the cohort's sampling limits.

Recovery was exercised against all six S3 archives after retention. All eight
regenerated statistics/context outputs matched every retained value; seven were
byte-identical, while diagnostics differed only in CSV column ordering.
