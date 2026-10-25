# Final public-workflow review

This frozen pipeline cut precedes the separately reviewed final Mac-worktree CMake guard.

No product failure was observed in the executed cases. This review recorded its
independent plan (PLAN.md in the qualification archive) before reading storage review outcomes, then froze 45
current tool files into its own fixture Git repository. Every frozen tool still
matched the canonical tree at cleanup (`canonical-comparison.json` is `{}`).

The starting evidence was `workbench/spikes/storage-lifecycle/README.md`: repeated
input/source copies caused growth, and pathname accounting missed metadata and
open-unlinked data. The review reused the real public tools and their fixed
staging/shared-input paths, while independently testing native allocation.

`harness.py` is executable on Linux through `orb -m ubuntu`. Run its phases in
order, or copy this small harness and its three external-service scripts to a
fresh evidence directory and run `harness.py all`. It creates and deletes only a
new owned subvolume/qgroup; it refuses to reuse a saved review state.

| Executed workflow | Observation |
|---|---|
| Repeated artifact lifecycle | Two put/fetch/retain/report rounds; identical references; executable modes and output bytes verified; retrying identical retain passed. |
| Deleted export parent and preview | Retain recreated the missing parent; preview used initialized bounded temporary storage. |
| Shared inputs | Concurrent real `input --run` consumers in separate checkouts resolved to the same immutable object; datasets and `Run.step` reused one managed dataset hub. |
| Interrupted preparation | Actual HTTP download interrupted, then recovered; real `worker.prepare` launch snapshots interrupted inside the fixed stage, then retried with the same and a different ID; no owned pending roots remained. The controlled-ID cases use the public Python API. |
| Actual worker CLI | Two `run --detach` → `wait` → `resume --detach` → `wait` cycles. Unchanged real worker runtime saved and restored checkpoints. |
| Resolved subnet repair | Both CLI resumes read persisted `[{"id":"subnet-v12","zone":"fixture-a"}]` records and completed; the resumed application independently read `checkpoint-v12`. |
| Recovery and retention | Selected executable and full compiler/source recovery passed; prepared inputs restored as shared aliases; reupload and fetch of a recovered run passed. |
| Edits, pins and unique notes | Explicit eviction retained pins, differing samples and notes. Full repair preserved edited samples in deterministic replacement trees, and both unique notes retained their original paths. |
| Two-member Group | Actual Group CLI launch/wait completed; real runtime intervals overlapped; configured local collection remained serial. |
| Native capacity | An open-unlinked writer accepted 10,092,544 bytes, then ENOSPC at 33,538,048/33,554,432 referenced bytes. Two concurrent ordinary writers independently reached ENOSPC after 5,963,776 and 6,160,384 bytes. Accepted hash `21c0163f85e5d78ed00b219d5a5a665a2e307a9b21258228e3e0621dc92cb792` survived. Closing/removing pressure and flushing the filesystem permitted a useful artifact fetch. |
| Cache-report repair | Fresh remote verification reclaimed 1,986,560 allocated bytes while real native admission still refused a 25,835,339-byte reserve. `removed_bytes` retained that useful progress and `errors` retained the refusal. No admission/status internals were patched. |
| Ordinary native build | New ordinary Git worktree built and ran a tiny CMake/Ninja target with pinned Clang 21.1.8 before invoking a SixDB initializer for that worktree. Its executable belonged to the owned qgroup. |
| External boundary | An intentional authored export outside the owned quota passed; a nonempty external report parent refused before writing output or receipt. Native identity excluded that export and existing `/dev/shm`, and included ordinary worktrees. No separate filesystem was created. |

The own boundary was native squota qgroup **0/284**, subvolume
`/home/ashtonsix/.sixdb-fresh-final-v12-52b221d35f`, with a 32 MiB hard limit.
Live0/263, parent30000/1, other review scopes, platform settings and services were
unchanged. No images, persistent mounts or real cloud compute were created.

Only external AWS/HTTP services were replaced: each fake AWS CLI call was a
separate process, fake EC2 started the unchanged real runtime, and loopback HTTP
served pinned sources and IMDS. Unknown AWS operations failed closed. This does
not qualify real EC2/IAM/network behavior or physical cloud disks.

Operating simplicity is materially better than an image/mount/timer workflow:
ordinary builds inherit the native boundary; one checked configuration selects
the hub; repeated workflows required no inventory/age sweep or manual stage
cleanup. Full capacity still needs the owner to release useful space. Kernel
reservations can conservatively delay admission; this review explicitly flushed
after pressure release. Authored exports intentionally occupy their own checked
publication copy outside the cache boundary.

`results.jsonl`, bounded command transcripts and compact runtime/job receipts
contain the execution evidence. Independent `statvfs`, live0 kernel counters and
own-evidence `st_blocks` were read. Whole-filesystem changes include concurrent
host activity and cannot isolate this review's contribution; own retained
evidence is approximately 0.5 MiB allocated, well below 12 MiB. No bulky sources,
compiler products or filesystem images are retained outside the quota.

The review corrected fixture issues (root ownership, combined-output parsing,
polling the accepted rather than active stage, fake EC2 token parsing, optional
fixture logging at full quota). These are not counted as product failures.
The first empty setup subvolume 0/283 was independently identified and removed;
its proof is `setup-retry-cleanup.json`. Final owned-resource removal is recorded
in `cleanup.json` and must show both the final scope and qgroup absent.

Key source SHA-256s:

- `worker.py`: `f6a8526827b0c0a21d4b498f80dd30eaf5243bb16df5b18c855166cbe9b2fcf1`
- `local_cache.py`: `988057d3d9ab03fb0877e6b2da057e0fffbbda961b5ca8a8ddfcdd96869a2554`
- Full manifest: `frozen-tools-sha256.json`; manifest digest `274f4edaea4ef4fb7db750ad35a58750692daaa458574772331a684bf06e317a`.
