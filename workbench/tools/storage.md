# Local storage

This Ubuntu machine has a **192 GiB native disk limit**, configured through
OrbStack's supported `machine.ubuntu.disk_bytes` setting. The existing Btrfs
quota enforces it. The limit includes the data already present; it is not an
extra allocation. Ordinary builds, captures, new Git worktrees, temporary files,
filesystem metadata and open-but-unlinked output in this subvolume share it.
No cleanup timer, image, overlay, mount service or periodic inventory is needed.
Existing Linux build paths and Git metadata stay in place.
The four existing Mac-hosted build paths now link into this same bounded store,
with original bytes and recovery archives verified before removing old copies.

The [replacement study](../spikes/storage-lifecycle/README.md) records the
failures, rejected approaches and independent public-workflow checks. A disk
limit prevents the tooling from consuming the rest of the Mac; sharing and
recovery below address the workflows that produced the repeated growth.

## What changed

Repeated artifact inputs previously became full copies inside every worker's
results, then were archived and downloaded again. Launch source packages also
remained locally after upload. Nominal cache budgets were retrospective scans,
not allocation limits, and interrupted transfers could leave another complete
staging tree on each retry.

Artifact inputs now use one shared immutable store. Selected-member views are
links to the same checked bytes, with process-owned pins while in use. All
members and the compressed source are verified while streaming; the compressed
archive and omitted payloads are not staged locally. Recoverable cache content
can leave only after fresh recovery verification. Unknown, changed or pinned
material is preserved.

Fetch, collection, preparation and upload use fixed, owned pending locations.
An interrupted operation is reconciled on the next operation, including a retry
with a different destination. Accepted output is separate from disposable pending
work. Launch source packages leave after their remote bytes independently verify;
`.keep-local` pins and local edits remain. Recovery receipts keep the source URI,
size and hash. No automatic expiry deletes unique builds or evidence.

## Reuse inputs and collect results

```sh
input_root=$(python3 workbench/tools/artifacts.py input REFERENCE.json --file rows.bin)
# Use "$input_root/rows.bin"; retain REFERENCE.json with the result.

python3 workbench/tools/artifacts.py input REFERENCE.json --file rows.bin \
  --run -- python3 consume.py '{input}'

python3 workbench/tools/worker.py fetch JOB --list
python3 workbench/tools/worker.py fetch JOB --file path/to/member
python3 workbench/tools/worker.py fetch JOB --full
```

Do not copy the shared input directory into `SIXDB_RESULTS`. Input views contain
read-only data links; use ordinary `fetch` for editable copies or executable
files with their original modes. A finite shell/controller process may borrow
inputs without changing its benchmark environment. `--run` owns a command's
lifetime in a private user/PID namespace and releases its pins afterward;
`{input}` and `SIXDB_INPUT_ROOT` locate its view. Persistent launchers must use
an explicit command scope rather than pinning inputs indefinitely.

Prepared dataset variants share the same cache across checkouts. Concurrent
preparation/publication callers use separate processes: Python closures run in
a single-threaded process so inherited locks cannot deadlock. Grouped remote
workers still execute concurrently; their local collection is serial.

Ordinary worker collection keeps measurements, logs and individual sources.
Root/nested source packages, large recognized compiler output and referenced
prepared inputs stay recoverable remotely. `--file` selects an exact member;
`--full` explicitly restores everything. A retry preserves differing local files
in a deterministic `replaced-results-*` tree, with later versions under
`.versions/`. Extra notes keep their existing paths.

## Inspect or reclaim

```sh
python3 workbench/tools/storage.py --status
python3 workbench/tools/local_cache.py
python3 workbench/tools/worker.py cache --prune
python3 workbench/tools/worker.py evict JOB
```

Status reads live kernel quota counters, including reserved writes; it does not
rely on an old scan or saved report. `--prune` can reclaim verified recoverable
input cache under pressure. It does not expire CMake objects. Explicit worker
`evict` streams the archive and removes only matching closed results, preserving
edits and recovery references. [Artifact eviction](artifacts.md#recover-or-reduce-retained-evidence)
does the same for other archived output. Archive and verify unique inactive
work before removing it. Pins still consume capacity; filling it produces a
clear failure rather than deleting useful work or silently raising the limit.

The immutable-store target is 10 GiB, within the shared native limit. This is
not an additional allowance per checkout. `SIXDB_MIN_FREE_GIB` controls the
foreground space reserve (default 5 GiB); it does not change the hard limit.

## Configuration and scope

On this Mac the persistent platform setting is:

```sh
orb config get machine.ubuntu.disk_bytes  # 206158430208
```

`~/.config/sixdb/storage.json` records the cache hub, existing quota scope,
filesystem UUID, level-zero qgroup and maximum accepted limit. `XDG_CONFIG_HOME`
and an explicit `SIXDB_STORAGE_CONFIG` select a different configuration. Helpers
check the live kernel identity, mode, consistency and limit; a missing explicit
config, removed/raised limit or mismatched output refuses rather than falling
back to unbounded storage. The platform owns quota setup and persistence;
SixDB does not introduce another quota installer or boot service.

The boundary is the configured Btrfs subvolume. New ordinary Git worktrees in it
are covered automatically. Separately created subvolumes, other filesystems and
arbitrary Mac output paths are not inferred to be covered. Empty explicit output
locations may link into the hub; nonempty external output needs verified
migration. Initialize a fresh Mac-hosted worktree with the current controller's
`python3 workbench/tools/storage.py WORKTREE` before its first CMake configure;
`dev.py` does this automatically. CMake checks before compiler detection and
refuses foreign output. A refused configure can leave small CMake control files;
preserve existing incremental or unique output when resolving a nonempty path.
Common Git metadata must already be within the boundary and is never relocated.

Intentional retained Git exports use one checked publication copy
outside cache storage. The limit cannot reserve space against unrelated Mac
applications or a privileged operator changing platform settings.

Ephemeral workers without host configuration keep their existing disk boundary;
local headroom reporting is explicitly diagnostic. `check.py` uses isolated
configuration for its disposable helper fixtures. Configured workflow reviews
supply their own small, real kernel quota.

The [October 24–25 recovery checkpoint](evidence/storage-20261024/README.md)
retains references for the immediate reclamation. Rejected image/overlay source
and test records are recoverable through the study; they are not installed.
