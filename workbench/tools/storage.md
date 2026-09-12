# Local worker storage

Worker results are recoverable local copies. Ordinary `worker.py run`, `wait`
and `fetch` reclaim older archived compiler output when results exceed a
10 GiB soft budget or free space falls below 10 GiB. This covers the checkout,
its Git worktrees and `build/workspaces/*/build/workers`; OrbStack aliases count once.

```sh
python3 workbench/tools/worker.py cache          # largest collections and headroom
python3 workbench/tools/worker.py cache --prune  # apply the same reclamation now
touch build/workers/JOB/.keep-local             # keep a working set available
python3 workbench/tools/worker.py fetch JOB --full  # restore reclaimed outputs too
```

Only large binaries, archives, LLVM bitcode and assembly are candidates, after
24 hours without collection access. A fresh S3 read verifies the full archive
hash and every member before deleting matching local files. Measurements,
logs, datasets, local edits, recent jobs and pinned collections stay. Removing
`.keep-local` releases a pin; `cache --older-hours N` adjusts the age for an
explicit cleanup. Recovery references and member hashes remain beside results.
Reclamation is local: it never deletes S3 objects or changes Git evidence.

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

Differing and user-added files survive in `replaced-results-*` beside the repaired
tree; byte-identical duplicates are removed. Inspect these conflicts before
discarding them. Reclaimed compiler files are reported as omitted, and `--full`
restores them. This does not make an interrupted experiment scientifically valid:
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
