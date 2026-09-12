# Local worker storage

Ordinary worker collection keeps measurements, logs, source snapshots and
metadata locally. Recognized compiler outputs of at least 1 MiB stay in S3;
they are hashed while reading the archive but never expanded onto disk.
The complete compressed archive is still downloaded and verified temporarily.
Prepared inputs stay referenced until explicitly restored.

```sh
python3 workbench/tools/worker.py fetch JOB --list  # discover member paths and sizes
python3 workbench/tools/worker.py fetch JOB --file study/probe  # add an exact member
python3 workbench/tools/worker.py fetch JOB --full  # all output and prepared inputs
python3 workbench/tools/worker.py cache          # largest collections and headroom
touch build/workers/JOB/.keep-local             # protect already fetched output
```

Repeat `--file` for several members. Existing local files are preserved when
adding output. A pin prevents later reclamation; it does not request full fetch.
The console reports omitted files and bytes. [Retention](artifacts.md) can reuse
the complete worker archive, so making a Git export does not require full fetch.

For older/full collections, worker commands also reclaim archived compiler
output after 24 hours without collection access, toward a 10 GiB soft budget
or 10 GiB free space. A fresh S3 read verifies the archive and each removed file.
Measurements, logs, datasets, edits, recent and pinned collections stay.
`cache --prune` applies this now; `--older-hours N` adjusts its age threshold.
Removing `.keep-local` releases a pin. Recovery references remain beside results.
The cache covers Git worktrees and conventional `build/workspaces/*/build/workers`
as well as this checkout; OrbStack aliases count once. S3 objects are never deleted.

`SIXDB_WORKER_CACHE_GIB` adjusts the soft budget. Preserved output can exceed
it; the console explains that instead of silently deleting research data.
`SIXDB_MIN_FREE_GIB` adjusts the 5 GiB reserve checked before submission and
archive download/expansion. Both the destination and the Mac backing volume
are checked in OrbStack. If its disk image lives elsewhere, set
`SIXDB_HOST_STORAGE_PATH` to a directory on that volume.

These checks bound this tooling's transfers, not unrelated applications or
arbitrary local builds. Concurrent transfers share a lock; another application
can still consume disk space afterward. `build/` remains working space, not
an indiscriminate cache. A raw output tree outside the worker locations above
needs its own deliberate retention decision.

## Collection and recovery

An existing `results/` directory is not proof of successful collection. Fetch
checks member sizes and hashes against `collection.json`, which records the
verified archive and intentional local omissions. Missing, damaged or older
unverified collections are restored. Data is synced before publication; the
collection receipt is synced afterward. A write or sync failure is an error,
even when the remote job is complete. Retry `wait JOB` or `fetch JOB`.

During repair, differing and user-added files survive in `replaced-results-*`
beside the repaired tree; byte-identical duplicates are removed. Inspect these
conflicts before discarding them. This does not make an interrupted experiment scientifically valid:
script status and the study's own validation still apply.

## Why this exists: 2026-09-12

At 17:31:20 UTC, OrbStack's host log recorded `StorageFull` / `No space left on
device`, followed by guest I/O errors and a VM stop. Job
`20260912T171942Z-c5df00e9` had zero-length local metadata and all 75 collected
files empty. Its verified S3 archive was intact. The old fetch path accepted
the existing directory and printed success without checking any contents.

The September 10 cleanup had freed about 17.8 GB, mostly inactive Calico output
and download caches. It preserved SixDB's active work but left no control on
subsequent local growth. Git-retention guidance addressed a different problem.
On September 12, 63 freshly verified archived files were manually removed
(3.11 GB); the subsequent cross-workspace inventory still found 27.09 GiB of
worker results, including 21.47 GiB of older compiler output to verify.
The new reclamation path then removed 18,483,611,515 bytes in 547 files across
69 older jobs, leaving 9.88 GiB of results. All September 12 jobs were preserved.

The remedies above replace repeated manual deletion and directory-existence
checks with an ordinary recovery/cache path. They do not establish why each
guest file became empty, or promise that filesystem sync defeats every host failure.

Validation: `python3 workbench/tools/check.py worker_storage` injects corrupt
files, failed writes/syncs, low host space, insufficient expansion space and
failed archive verification. An isolated 75-empty-file replica also recovered
from the actual 15,856,832-byte incident bundle, SHA-256
`da40bf9ab1700e630906042c61c9204878fc3ad2601cb314b4048d9ec3877d29`.
No EC2 instance or deliberate disk exhaustion was needed for these checks.
