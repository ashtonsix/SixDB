# Native storage rollout — October 25, 2026

The [replacement study](../../../spikes/storage-lifecycle/README.md) explains the
recurring growth and current design. [Migration](migration.json) records all nine
primary build paths within the native quota; four former Mac-hosted build trees
were copied, hashed and archived before removing their matching old copies.
Their logical paths and existing incremental output remain usable.

The four `mac-build-*.json` references each recover the complete old build tree
plus `.sixdb-migration.json`, which records original paths, modes, empty
directories and file identities/hashes. Publication included a full downloaded
verification; every file was freshly verified remotely again before switching.
The combined archived trees contain 12,570 payload files / 838,417,643 bytes.
Archive counts also include one manifest per tree.

Recover into a separate directory; never restore over a current build:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/tools/evidence/storage-20261025/mac-build-streampack.json \
  build/recovered/old-streampack
```

[Restart](restart.json) records an actual Ubuntu restart with an active input
lease. The 192 GiB limit persisted, stale process pins expired, build/Git inodes
were unchanged, and public input, CMake build and editor checks worked afterward.
No storage service or timer was installed.

Detailed source cuts, executable independent review harnesses, command logs,
cleanup proofs and migration hashes are in the study's
[qualification archive](../../../spikes/storage-lifecycle/final-qualification.json).
The [earlier recovery checkpoint](../storage-20261024/README.md) separately
accounts for the requested greater-than-50-GB immediate reclamation.
