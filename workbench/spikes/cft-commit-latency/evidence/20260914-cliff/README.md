# CFT cliff follow-up: retained evidence

The [findings](../../commit/cliff.md) interpret this selection. It preserves every
successful intervention in the bounded follow-up, including unsuccessful tuning
choices, plus a new reduction of twelve original high-rate traces.

- `small/`: ten three-AZ i8g.large replication passes, including the fresh
  reproduction, 60-second load controls, zero-fill omission and batch policies.
- `express/`: thirteen i8g.8xlarge replication passes, including actual preparation
  of future ext4 ranges, repeated controllers and a changing arrival rate.
- `local/`: four i8g.large controls without replication, with NVMe vendor counters.
- `original/`: twelve old repeats reduced in arrival order and by cumulative service.
- `workers.json`: immutable raw archives, captured-source identities and hardware.
- `verification.json`: all 73 successful new voter runs, totaling 464,635,200
  exact records checked by direct readback across voters.
- `resources.json`: all eight new instances terminated, and no owned volumes or
  private security groups left. One instance belonged to the failed local setup.

Primary quantiles and deadline fractions exclude the first scheduled second.
Every offered record remains accounted for, including late completion during
drain. Per-second tables include warmup. Resource rows group counter intervals by
their ending second; boundary intervals can overlap setup or drain. Leader stages
share its steady clock. Cross-host telemetry uses wall-clock anchors.

`resource-seconds.csv` contains ENA allowance/SRD counters and block-device rates;
mode and utilization are gauges. `nvme.csv` retains individual vendor-counter
intervals: the `instance_*` fields identify instance-store throughput/IOPS
exceedance. The EBS-volume fields are not independent evidence for instance store.
Local-only ACK fields alias local completion for trace compatibility; that control
performs no network replication. Preparation receipts include full-range
completion and sampled pause events, which are not exact pause durations.

The first local-only launch failed the native count guard before measurement
because the wrapper did not forward probe environment flags. Its diagnostic
archive is retained by reference under `failed-local`; the corrected fresh
cohort supplies the local performance results. Subsequent preparation-error
propagation improvements affect failure reporting and do not invalidate successful
captured runs. Each worker's archive contains the exact sources it executed.

## Recover and verify

From the repository root on Linux (prefix commands with `orb -m ubuntu` on Mac):

```sh
python3 workbench/spikes/cft-commit-latency/commit/cliff_recover.py \
  workbench/spikes/cft-commit-latency/evidence/20260914-cliff \
  --output build/cft-cliff-recovered
```

This fetches and verifies eight immutable input archives: seven successful new
workers and the original leader. It reconstructs every selected numeric CSV and
compares the bytes. `--cohort small|local|express|original` limits the reconstruction.
The failed setup archive remains independently fetchable through `workers.json`.

The compact artifact's full archive also contains `analysis-source.tar.gz`, with
the exact offline analyzer and plotting sources; it is omitted from Git's selected
files. If these scripts later change, fetch the compact artifact with
[artifacts.py](../../../../tools/artifacts.md), extract that snapshot in ignored
storage, and pass its directory as `--analysis-sources` to the recovery command.

Figures are generated from the reconstructed tables by
[`cliff_plot.py`](../../commit/cliff_plot.py). NumPy 2.2.4 produced the
numeric reconstruction; Matplotlib 3.10.8 with NumPy 2.5.3 produced the figures.
The archive records the verified recovery result. Native correctness checks
exercise TCP/UDP, actual prepared-prefix replenishment, changing batches,
three-voter readback and an injected file-size failure with partial accounting.
