# Independent process and filesystem attacks

The OS adversarial subagent owns these tests. They invoke real external Python
processes and a local-only `aws` executable serving known fixture bytes. The
baseline adapter calls the existing `worker_cache.collect` entry point inside
the child; it does not mock its space checks, file writes, locking, publication
or cleanup. This is a collection-kernel test, not a full EC2/control-plane test.

Each run enters an unprivileged Linux user/mount namespace and mounts its own
**16 MiB tmpfs** beneath a new private fixture directory. This works in the
current OrbStack Ubuntu guest. It does not fill the host disk or `/dev/shm`,
mount over real data, allocate a cloud worker, read S3, or alter production
caches. The mount is unmounted on completion and temporary provider bytes are
removed. Tests fail rather than silently passing if the requested candidate is
unavailable.

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/os_failures.py \
  --backend legacy
```

The machine-readable run is ignored at
`build/reviews/storage-20261024/adversarial-os/report.json`.

## Baseline observations — October 24

All four cases ran against the current working-tree helpers, including the
provisional source-package/shared-input changes. No function fault injection
was used.

| Attack | Independent observation |
| --- | --- |
| Kill the collecting process group after 2 MiB of each of five downloads; restart normally | Five abandoned `.collection-*/.fetch-*/bundle.tar.gz` files retain exactly **10,485,760 bytes**. Normal restart fails for insufficient expansion space and leaves all five. Unique existing notes retain their original hash. This is a reproduced restart/storage failure. |
| Six concurrent ordinary collections with a 1 MiB nominal worker-cache budget | All six report success, retaining **6,316,032 allocated bytes**. The nominal budget is not enforced during admission. This reproduces a policy gap; serialization of transfers alone does not bound retained output. |
| Fill the private filesystem after the downloader has successfully begun, then resume it | The actual kernel returns `ENOSPC`. The command fails; no partial result is published, preexisting notes retain their hash, and graceful-error staging is removed. This safety case passed. It does not repair the `SIGKILL` case. |
| Supply an archive member containing `..` | Collection rejects it; an independently hashed outside witness remains unchanged. This safety case passed. |

The killed-download files are counted by current inventory but do not acquire
an immutable recovery mapping or become automatic eviction candidates. Counting
them therefore does not make the restart recover or the disk usage bounded.

## Replacement tests

`--backend candidate` invokes the spike's `store.py` as separate CLI processes.
It challenges concurrent admission, independent allocated-byte observations,
content preservation for every acknowledged object, three kill/restart points,
and actual filesystem exhaustion followed by retry. The requested stop seam is
`SIXDB_STORAGE_TEST_STOP=after-stage|after-object|after-catalog`; the test observes
the process's stopped state and sends `SIGKILL` from outside the implementation.

The first candidate passed concurrent admission (three 512 KiB unique puts
accepted and three refused under 2 MiB), all three specified kill/restart seams,
actual `ENOSPC` after staging, lease protection, corrupt-recovery refusal, local
change preservation, metadata-growth refusal and the object-directory symlink
attack. Those successes did **not** close the investigation.

An additional initial-exhaustion case found a counterexample: fill the private
filesystem **before the first command**, invoke `stats`, free the filler and
retry `stats`. Initial publication wrote directly to `catalog.json`; kernel
`ENOSPC` left it at zero bytes. Every normal retry then failed with
`JSONDecodeError` despite ample free space and no acknowledged object. This
was observed in `candidate-002.json`; the `candidate-initial-enospc` case remains
in the harness. It requires an initialization-atomicity repair and a fresh rerun.

The initialization repair now passes. The expanded suite ran on an unchanged
candidate SHA-256
`617d1b7fd6fc0cfc2e6ceac75aee2641c651addbd68effab172bc9755f7c24ba`.
The complete machine-readable record is
`build/reviews/storage-20261024/adversarial-os/candidate-003.json`.

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/os_failures.py \
  --backend candidate \
  --output build/reviews/storage-20261024/adversarial-os/candidate-rerun.json
```

All ten named cases passed on that cut:

| Case | Observed result |
| --- | --- |
| Concurrent unique writers | Three 512 KiB puts accepted, three refused under 2 MiB. Independently sampled allocated peak **1,581,056 bytes**; every acknowledged object re-read and hashed. |
| Three deliberate crash seams | `SIGKILL` after staging, object installation and catalog publication; restart/retry returned exact source bytes in every case. |
| Arbitrary timing windows | 32 attempts at eight elapsed-time windows: **19 actually killed**, 13 completed before the kill. Every restart/retry and returned content checked. These kills do not use implementation stop points. |
| Actual exhaustion after staging | Kernel `ENOSPC`, then filler release and normal restart/retry; exact source content recovered. |
| Actual exhaustion before initialization | Kernel `ENOSPC`, then filler release; normal initialization retry succeeds, fixing the retained counterexample. |
| Leases and recovery integrity | Leased data survives pressure; corrupt recovery source blocks eviction; explicit lease release plus valid recovery permits progress; unique data survives. |
| Changed managed bytes | An owner makes a cached file writable and edits it; pressure refuses and preserves the edit. Read-only file mode is not treated as protection against its owner. |
| Alias metadata growth | 441 aliases accepted; the next refuses under 256 KiB. Independently sampled peak **139,264 bytes**; the last acknowledged value remains correct. |
| Pre-operation object-directory replacement | Symlink replacement rejected; no object or witness outside the managed root changed. |
| Mid-operation directory replacement | Replace either staging or the whole root after a staged write, while the process is stopped; resumed process refuses. Outside witnesses and previously acknowledged unique data remain unchanged. |

Reports hash source files before and after the whole run; each case also refuses
to count a result while its implementation changes. No source changed during
this run. The namespace was unmounted and fixture data removed afterwards.

This establishes tested behavior of the candidate's **managed immutable
acquisition kernel**. It does not establish a deployed replacement for worker
collection/S3 recovery, captured sources, mutable incremental builds, unmanaged
external writers, the Mac backing volume or every current research runner.
Those remain integration and operating-boundary questions for this spike.

Allocated-byte sampling includes hidden staging, catalogs and lock files and
deduplicates hardlinked inodes. Sampling can find a peak violation but cannot
prove that an arbitrarily short peak never occurred. A successful run is bounded
experimental evidence, to be combined with inspection of the admission invariant;
it is not proof that arbitrary external writers obey a whole-device quota.
