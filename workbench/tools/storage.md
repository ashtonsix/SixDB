# Local worker storage

Ordinary collection keeps measurements, logs, sources and metadata locally.
Recognized compiler output of at least 1 MiB stays in S3, as do referenced
prepared inputs. The complete compressed archive still downloads temporarily
for verification; omitted files are hashed without being expanded.

```sh
python3 workbench/tools/worker.py fetch JOB --list             # paths, sizes, local presence
python3 workbench/tools/worker.py fetch JOB --file study/probe # add one member; repeat --file
python3 workbench/tools/worker.py fetch JOB --full             # all output and prepared inputs
python3 workbench/tools/worker.py cache                       # disk use and headroom
python3 workbench/tools/worker.py cache --install-maintenance # once on another Linux checkout
python3 workbench/tools/worker.py evict JOB                   # release a finished local collection
```

[Retention](artifacts.md) reuses the full worker archive without fetching omitted
files first. Recovery from an artifact reference can use a directory on another
volume when local space is tight.

## Older local output

Worker commands reclaim byte-identical archived results from closed collections
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

This device has a per-user background timer for full cache sweeps, roughly every
30 minutes after the preceding sweep finishes. Foreground worker preflights
reclaim from at most one collection; a cache backlog must not hold a submission behind
hundreds of archive reads. A shared nonblocking maintenance lock prevents
controllers from duplicating a background sweep. Space reserves still apply.

The former compiler-only pruning left old raw collections permanently over
budget even after their binaries were omitted. Closed archived results are now
treated as a recoverable local cache, rather than accumulating without a limit.

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
`cache` also shows the checkout's largest build directories: these can dwarf
worker collections and may contain unique prototypes or checkpoints. They remain
deliberate cleanup decisions, not automatically disposable caches.
For finished local studies with an existing artifact reference,
[`artifacts.py evict`](artifacts.md#recover-or-reduce-retained-evidence) verifies
and releases unchanged archived copies, including renamed selected logs.

`SIXDB_WORKER_CACHE_GIB` adjusts the budget; `SIXDB_MIN_FREE_GIB` adjusts the
5 GiB reserve checked before submission and transfers. OrbStack checks both
the destination and Mac backing volume. Set `SIXDB_HOST_STORAGE_PATH` if its
image lives elsewhere. These checks cannot bound other applications' disk use.

## Repairing a collection

Retry `wait JOB` or `fetch JOB`. They check sizes and hashes against
`collection.json`, restore damaged or missing output, and report write/sync
failures. Compiler omissions stay omitted unless requested. Differing or user-added
files survive repair in `replaced-results-*` beside the results; inspect before deleting.
A verified collection preserves what the script produced, including failed runs.
