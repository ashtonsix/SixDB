# Local storage

Ordinary collection keeps measurements, logs, sources and metadata locally.
Recognized compiler output of at least 1 MiB stays in S3, as do referenced
prepared inputs. The complete compressed archive still downloads temporarily
for verification; omitted files are hashed without being expanded.

```sh
python3 workbench/tools/worker.py fetch JOB --list             # paths, sizes, local presence
python3 workbench/tools/worker.py fetch JOB --file study/probe # add one member; repeat --file
python3 workbench/tools/worker.py fetch JOB --full             # all output and prepared inputs
python3 workbench/tools/worker.py cache                       # disk use and headroom
python3 workbench/tools/worker.py cache --prune               # explicit verified reclamation
python3 workbench/tools/local_cache.py                        # whole local-build accounting
python3 workbench/tools/worker.py evict JOB                   # release a finished local collection
```

[Retention](artifacts.md) reuses the full worker archive without fetching omitted
files first. Recovery from an artifact reference can use a directory on another
volume when local space is tight.

## Older local output

Explicit pruning reclaims byte-identical archived results from closed collections
after 24 hours without collection access, toward a 10 GiB cache budget or 10 GiB
free space. This includes sources, logs and measurements as well as compiler
output. A fresh full S3 read verifies each removed file, and the durable receipt
keeps its recovery identity. Edited/unarchived files, active jobs, recent access
and `.keep-local` pins stay; these can exceed the budget. `cache --prune` applies
this now; `--older-hours N` changes the age threshold. Pin an ongoing local audit
before relying on an older collection. Ordinary fetching renews its access age.
The job's duplicate launch `source.tar.gz` is included in the budget and can leave
only when it matches the freshly verified archive member. `fetch JOB --file
source.tar.gz` recovers the source under `results/`; checkpoint resume already
downloads the original source from S3.
Partial downloads, replacement/conflict trees and job metadata also count in
worker storage. They are preserved unless covered by explicit verified recovery;
metadata-only jobs do not trigger repeated archive reads just because the total
is over budget. A complete local collection receipt also lets maintenance skip
an archive read when only differing local edits remain. Restoring matching bytes
makes them eligible again; every actual removal still requires fresh remote
verification.

Scheduled cache sweeps and the hourly storage heartbeat were removed from this
device at Ashton's request. Use `worker.py cache --prune` for an explicit sweep.
Healthy foreground launches and fetches only check space reserves; they do no
online cache pruning after results become available. If the reserve check fails, preflight can try
one emergency collection before checking the reserve again. A cache backlog must
not hold a submission behind archive reads. A shared nonblocking maintenance lock
prevents simultaneous pruners. Space reserves still apply.

For a finished collection you no longer need locally, `evict JOB` also removes
archived sources, logs and measurements. It streams and verifies the entire S3
bundle without staging a download, then removes only byte-identical members.
The recovery reference, collection receipt, local edits and unarchived files stay.
`fetch JOB --file PATH` recovers selected files; ordinary `fetch JOB` restores the
measurement/source set, and `--full` restores compiler output too. Eviction is
explicit, independent of age and free-space reserves, and respects `.keep-local`.

`touch build/workers/JOB/.keep-local` protects fetched output; remove the file
to release it. Pins do not request a full fetch. The cache covers this checkout,
Git worktrees and `build/workspaces/*/build/workers`, deduplicating OrbStack aliases.
`cache` also reports the separate local build budget below.
For finished local studies with an existing artifact reference,
[`artifacts.py evict`](artifacts.md#recover-or-reduce-retained-evidence) verifies
and releases unchanged archived copies, including renamed selected logs.

`SIXDB_WORKER_CACHE_GIB` adjusts the budget; `SIXDB_MIN_FREE_GIB` adjusts the
5 GiB reserve checked before submission and transfers. OrbStack checks both
the destination and Mac backing volume. Set `SIXDB_HOST_STORAGE_PATH` if its
image lives elsewhere. These checks cannot bound other applications' disk use.

## Local builds, captures and other ignored output

The separate **60 GiB local build budget** counts output files under every
checkout/worktree's `build/`, including captures, datasets, experiments, reviews,
frozen workspaces and pinned/unique files. Worker subtrees are counted by the
10 GiB worker budget instead. OrbStack aliases, nested worktree roots and
hardlinked files are deduplicated; sparse files count allocated bytes.
`SIXDB_LOCAL_BUILD_GIB` changes this budget.

`worker.py cache --prune` accounts for local output before pruning workers;
`local_cache.py --prune` handles local output alone. Under budget pressure, or
below a 20 GiB free-space target, it can remove
recognised CMake object/PCH products from build trees idle for **seven days**
(`SIXDB_LOCAL_BUILD_DAYS`). Any recent activity in that build tree keeps its old
objects. A running compiler, linker or build controller defers automatic removal.
Ancestor `.keep-local` pins protect incremental work; pins still count toward
the budget. Tracked files, links, executables, libraries, sources, measurements
and arbitrary files with compiler-looking names are preserved.

These are retention budgets, not filesystem quotas. Recent, pinned and unique
material can exceed them. A durable report at
`~/.cache/sixdb/local-build-cache-CHECKOUT_HASH.json` records over-budget output,
scan errors and the largest directories; they are not silently excluded from
accounting. The report reflects its most recent invocation; no scheduled scanner
refreshes it on this device. The
[artifact helper](artifacts.md#recover-or-reduce-retained-evidence) preserves
recovery references for explicit curation.

Frozen captures and new local runner workspaces use verified copy-on-write source
clones where supported. This avoids another full physical copy of unchanged
sources without coupling later edits. Existing snapshots can have identical
data ranges shared by the kernel without changing their bytes, paths or modes.
Ordinary `du` still counts shared extents at each file; budget accounting is
conservative after reflinking, rather than a measure of exclusive physical use.

## Repairing a collection

Retry `wait JOB` or `fetch JOB`. They check sizes and hashes against
`collection.json`, restore damaged or missing output, and report write/sync
failures. Compiler omissions stay omitted unless requested. Differing or user-added
files survive repair in `replaced-results-*` beside the results; inspect before deleting.
A verified collection preserves what the script produced, including failed runs.

Closed local study recovery checkpoints: [October 16](evidence/storage-20261016/README.md)
and [October 23](evidence/storage-20261023/README.md).
