# Storage recovery — October 24–25, 2026

This checkpoint preserves the recovery maps for immediate reclamation during the
[replacement storage spike](../../../spikes/storage-lifecycle/README.md). It does
not establish that the recurring growth is fixed.

The completed worker-prune log contains 308 collections. Each removal followed a
fresh full archive/member verification; pins, changed local bytes and unique
output stayed. [operations.json](operations.json) references a verified S3 bundle
containing those jobs' `artifact.json` and `collection.json` receipts, the
completion/interruption log, and the repeated-input extent-sharing plan and
kernel results. The pruner was explicitly stopped during the next archive read.
The bundle contains no benchmark payload or adversarial-test images.

Recover a worker's small recovery reference and receipt into a new directory:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/tools/evidence/storage-20261024/operations.json \
  build/recovered/storage-20261024/operations \
  --file workers/20261023T014343Z-22c72eab/artifact.json \
  --file workers/20261023T014343Z-22c72eab/collection.json
```

Then use that recovered `artifact.json` with `artifacts.py fetch` into a separate
directory, selecting the required original archive member with `--file`. The
per-job references and receipts also remain in their original `build/workers/`
directories. `workers.json` in the operational bundle indexes all 308 references.

The kernel compared and shared 35,020,546,048 bytes across 3,162 repeated input
files without changing their paths, contents, modes or independent inodes.
Eleven files disappeared concurrently; no sharing errors were reported. This
number is not added to eviction totals as exclusive physical space reclaimed.

[summary.json](summary.json) records the measured Mac Data-volume delta. It
includes concurrent owner activity, including about 2.6 GB of input copies the
Ikea owner reported releasing. Active incremental builds, the corpus pin,
datasets and unarchived evidence were retained. No timer was installed.

Commit `17093b28` reduces default source-package and per-job input duplication.
Its shared input cache is explicitly provisional and remains unbounded. The
replacement's tests and unresolved rollout work belong in the spike, separate
from this recovery receipt.
