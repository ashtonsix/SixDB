# Fixed-tuple capture and selection tools

The findings are now organized under [network latency](../network/README.md):
[host/port selection across all six AZs](../network/selection.md),
[controlled method](../network/method.md), and the supporting
[dense az2–az4 control](../network/studies/tuple-dense.md).

| Source | Responsibility |
| --- | --- |
| [launch.py](launch.py) | Broad six-AZ, one-port capture by default; `--focused` selects the dense control; `--port-sampling` compares consecutive and scattered ports |
| [analyze.py](analyze.py) | Verify physical tuples and complete grids, compare same-role revisits, train and assess host/flow choices |
| [diagnose.py](diagnose.py) | Per-block application/kernel and hardware/software receive-boundary diagnostics |
| [plot.py](plot.py) | Dense tuple repeatability and broad held-out edge figures |
| [port_plan.py](port_plan.py), [check_ports.py](check_ports.py) | Reproducible nested port policies, balanced schedule, overlap and holdout-selection checks |
| [port_sampling.py](port_sampling.py), [port_plot.py](port_plot.py) | Equal-budget paired comparisons and a figure reproducible from compact scores |
| [recover.py](recover.py) | Fetch raw archives and reproduce clock analysis, rankings and boundary diagnostics without new probes |

The runner reuses [oneway/](../oneway/README.md) for packet capture and independent
clock references. It holds endpoint ports fixed across role reversals. Broad
analysis retains request-only and all-leg rankings separately. Source paths
remain stable for archived sources and recovery.

For ordinary report recovery, first fetch the small archived export using the
[evidence guide](../evidence.md#network-cohorts). Full block/sensitivity tables
and SVGs are archive-only; the guide gives tested plotting commands on recovered
inputs. The following is the deeper raw-worker reanalysis path.

From the repository root on Linux (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/spikes/cft-commit-latency/variance/recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260924-variance-broad \
  --output build/recovered-variance-broad
```

Choose a fresh output directory. This downloads existing data, not a new fleet.
The [broad evidence](../evidence/20260924-variance-broad/README.md) and
[dense evidence](../evidence/20260924-variance-dense/README.md) retain all candidates,
source identities, cost scopes and verified resource cleanup. New captures via
`launch.py` are new observations with their own budget and worker lifetimes.
