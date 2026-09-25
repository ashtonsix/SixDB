# UDP timestamp and clock tools

The [network guide](../network/README.md) owns the findings. The original
one-way report moved to [the initial cohort](../network/studies/oneway-initial.md),
with a separate [fresh-host confirmation](../network/studies/oneway-confirmation.md).
[Clock methodology](../network/clocks.md) applies to both those cohorts and the
later fixed-tuple controls.

| Source | Responsibility |
| --- | --- |
| [probe.cpp](probe.cpp) | 64-byte UDP echo, kernel TX/RX timestamps, local clock brackets and explicit endpoint ports |
| [calibrate.py](calibrate.py), [phc-setup.sh](phc-setup.sh) | Independent PHC/NTP references and pinned ENA driver setup on study workers |
| [node.py](node.py), [cloud.sh](cloud.sh) | Paced blocks, disjoint peer matchings, roles and optional fixed-tuple layouts |
| [analyze.py](analyze.py), [check.py](check.py) | Independent clock fit/bounds, packet validation and synthetic checks of the uncertainty model |
| [assemble.py](assemble.py), [edges.py](edges.py), [plot.py](plot.py) | Cohort context/accounting, edge summaries and figures |
| [recover.py](recover.py) | Fetch immutable raw archives and reanalyze without new probes |
| [launch.py](launch.py), [preflight.sh](preflight.sh) | Original one-way capture and optional clock preflight; these launch cloud work |

For ordinary report recovery, first fetch the small archived export using the
[evidence guide](../evidence.md#network-cohorts). Full block/sensitivity tables
and SVGs are archive-only; the guide gives tested plotting commands on recovered
inputs. The following is the deeper raw-worker reanalysis path.

From the repository root on Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/spikes/cft-commit-latency/oneway/check.py
python3 workbench/spikes/cft-commit-latency/oneway/recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260924-oneway \
  --output build/recovered-az-oneway
```

Choose a fresh recovery output directory. Exact capture sources and toolchain
identities remain in worker archives; current source includes later corrections.
The [fixed-tuple tools](../variance/README.md) reuse this probe and clock model.
[Evidence and resource accounting](../evidence.md#network-cohorts) covers all
four completed UDP cohorts. Source paths remain stable for recorded commands
and capture/recovery machinery.
