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
```

[Retention](artifacts.md) reuses the full worker archive without fetching omitted
files first. Recovery from an artifact reference can use a directory on another
volume when local space is tight.

## Older local output

Worker commands reclaim verified archived compiler output after 24 hours without
collection access, toward a 10 GiB soft budget or 10 GiB free space. A fresh S3
read verifies each removed file. Measurements, datasets, edits and recent
collections stay; the budget can therefore be exceeded. `cache --prune` applies
this now; `--older-hours N` changes the age threshold.

`touch build/workers/JOB/.keep-local` protects fetched output; remove the file
to release it. Pins do not request a full fetch. The cache covers this checkout,
Git worktrees and `build/workspaces/*/build/workers`, deduplicating OrbStack aliases.
Other builds and source captures remain deliberate cleanup decisions.

`SIXDB_WORKER_CACHE_GIB` adjusts the budget; `SIXDB_MIN_FREE_GIB` adjusts the
5 GiB reserve checked before submission and transfers. OrbStack checks both
the destination and Mac backing volume. Set `SIXDB_HOST_STORAGE_PATH` if its
image lives elsewhere. These checks cannot bound other applications' disk use.

## Repairing a collection

Retry `wait JOB` or `fetch JOB`. They check sizes and hashes against
`collection.json`, restore damaged or missing output, and report write/sync
failures. Intentional omissions stay omitted. Differing or user-added files
survive repair in `replaced-results-*` beside the results; inspect before deleting.
A verified collection preserves what the script produced, including failed runs.
