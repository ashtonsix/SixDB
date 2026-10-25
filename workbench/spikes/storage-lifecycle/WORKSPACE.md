# Persistent bounded command workspace prototype

October 25, 2026. `workspace.py` is an isolated ARM64/Linux prototype for this
OrbStack host. It is not installed, not connected to live builds, and not an
independently accepted replacement. Fresh reviewers still need to attack it.

The [lifecycle audit](AUDIT.md) found admission bypasses in ordinary producers;
the [immutable-store review](REVIEW.md) showed why file scans and guessed metadata
allowances cannot bound arbitrary compiler output. The preceding
[regular-image probe](QUOTA-WORKSPACE.md) established that this host can mount a
fixed-capacity ext4 image privately and retain incremental data across exhaustion
and remount. This prototype reuses that enforcing boundary rather than adding
another cache scanner.

## Interface

Run from the Linux repository, prefixed with `orb -m ubuntu` on the Mac:

```sh
python3 workbench/spikes/storage-lifecycle/workspace.py \
  --pool /home/ashtonsix/sixdb/build/reviews/storage-20261024/example-pool \
  --budget 68157440 run example --size 33554432 -- \
  /usr/bin/python3 -c 'from pathlib import Path; Path("incremental").write_text("kept")'
```

The parent directory must exist. A new pool is root-owned mode 0711; image and
manager files are root-owned mode 0600. Names accept 1–48 ASCII letters, digits,
hyphens or underscores. Images are deliberately restricted to **16–128 MiB** in
this spike, in 4 KiB multiples. Reusing the same name reopens the same image.
Its capacity and the pool's budget cannot be silently changed. There is no
automatic eviction, resizing, export, deletion, background service or timer.

The example budget admits two 32 MiB image capacities plus the charged 1 MiB
manager allowance. It refuses a third 16 MiB image. The maximum capacities are
reserved even when an image contains almost no user data. `posix_fallocate`
actually allocates each backing file; `mkfs.ext4` runs with `nodiscard`, because
its default discard otherwise undoes that allocation. Reopening also verifies
allocation without reformatting. No device path is accepted or formatted.

The command starts as the original unprivileged user in the image's `work/`
directory. Source reads outside it remain available. `$SIXDB_SOURCE` identifies
the invoking directory. HOME, TMPDIR/TMP/TEMP, XDG cache/state, Cargo target and
Python cache locations point inside the mounted image. Specify source paths
explicitly when the command needs them; a source checkout is not copied.
Stdout and stderr append to root-owned files on the same image, rather than
escaping through a parent process that accumulates unbounded output. A bounded
tail is included in the final JSON report. Logs count toward capacity and can
make a later command fail when the workspace is full.

## Enforcement and lifetime

One global pool lock covers reconciliation, capacity admission, allocation,
format, publication, command lifetime and verified teardown. This intentionally
serializes **all** images in the prototype. It favors a small inspectable
ownership rule over premature concurrency.

An image is created with O_EXCL/O_NOFOLLOW as `NAME.new`. It must be a root-owned
singly linked regular file, and its maximum size is reserved before allocating
bytes. After allocation, format and fsync, rename plus directory fsync publishes
`NAME.img`. Accepted `.img` files are never reformatted. A recognized interrupted
`.new` can be removed only while locked and after confirming it has no loop
association. Unknown entries, incompatible sizes/modes/budget, linked files,
active loop associations or failed mounts cause refusal with data retained.
There is no catalog whose loss can cause an accepted image to be mistaken for
unacknowledged staging.

The root supervisor is PID 1 in fresh PID, mount and IPC namespaces.
Mount propagation is private; ext4 uses nodev/nosuid. The ordinary-user consumer
gets a further cloned mount namespace whose inherited source/host mounts are
recursively read-only, with only the image restored writable. This matters:
Landlock alone leaves operations such as chmod and setxattr unconstrained.
Landlock ABI 3 or newer additionally denies filesystem mutation outside work/,
with `/dev/null` allowed for discarded writes; no-new-privs prevents exec
from regaining privilege. Other syscall architectures are refused. No outside
writable file descriptor is inherited by the consumer. Networking is deliberately
available for ordinary HTTPS/DNS acquisition directly into the bounded image.
This constrains owned filesystem writes; it does not claim to prevent side
effects requested from external services through sockets.
The consumer also has an inherited seccomp filter closing descriptor-import
and mount APIs, as described below; the network socket API itself is available.
The consumer's `/dev/shm` is a bind alias of image `work/shm`, so Python
semaphores/shared memory consume the same fixed capacity instead of an
unbudgeted tmpfs. The supervisor and host `/dev/shm` are unchanged.
Consumer core dumps are disabled. The prototype refuses hosts configured with a
pipe core collector: their external storage effects are outside this boundary.

The outer wrapper retains the sole writer of an EOF lifeline. Losing the wrapper
terminates the operation; a waiter also watches the lifeline while acquiring the
lock. At command exit, all remaining namespace descendants are killed and reaped,
including forked children that called setsid. Death of the namespace PID 1 itself
causes the kernel to kill its descendants. Normal completion unmounts and checks
that no loop association remains before reporting success. Private-namespace
destruction and loop autoclear handle abrupt supervisor death; a subsequent run
still refuses a lingering association rather than mounting it again.

The filesystem supplies the real data/inode/open-unlinked capacity boundary.
No scanner has to find an in-flight file before its allocation counts. ENOSPC
does not delete an accepted incremental object to force the budget number down.
An impossible working set simply cannot allocate more.

## First-round observations

Tiny fixtures used two 32 MiB images under the ignored
`build/reviews/storage-20261024/workspace-prototype/` directory. Both backing
files had exactly **33,554,432 logical and allocated bytes** after testing.
The reports are `first-round.json` and `final-round.json` in that directory; the large fixture images
were removed after confirming teardown. These are implementer's checks,
not independent acceptance.

| Check | Observed result |
| --- | --- |
| Source reads; original uid | Reads worked; consumer uid was 501 |
| Outside writes, source chmod and xattr | Refused; source bytes, mode and xattrs unchanged |
| Subprocess inheritance; TMPDIR | Child shell writes refused; temporary output stayed inside |
| Legitimate local/network socket APIs | Socketpair round-trip and AF_INET socket creation worked, with no actual network connection |
| Python multiprocessing | Queue/process and shared-memory exchange worked through bounded `/dev/shm` alias |
| Native `dd` beyond available capacity | Actual ENOSPC; previously written marker survived |
| Empty-file inode exhaustion | Actual inode ENOSPC; marker survived |
| Unbounded native `yes` stdout | Failed at real filesystem capacity; marker survived a later remount |
| Two simultaneous same-image counter updates | Serialized; final counter was 2 |
| Aggregate capacity, size and budget changes | Third image, changed size and changed budget all refused |
| SIGKILL of outer wrapper with fork+setsid children | Descendants gone; loop released; fsynced marker survived reopening |
| SIGKILL of root namespace supervisor | Descendants gone; loop released; fsynced marker survived reopening |
| Final host loop listing | Empty |

## Independent FD-import finding and targeted closure

The [fresh v3 review](WORKSPACE-V3.md) found a real direct-write escape in source
`5ed5a1c768964e832389c0e48f942d9e1498105748f0dc21d7948ade16d08ccf`.
An outside fixture helper sent a writable regular descriptor over a Unix socket
with SCM_RIGHTS. The consumer itself wrote and fsynced that descriptor outside
the image. Read-only path mounts and Landlock did not revoke the imported writable
mount reference. The independent harness's `helper_fd` case was rerun unchanged:
before the fix the consumer returned 0 and an independent outside read recovered
`consumer wrote via received fd`; after the fix recvmsg returned EPERM, the
consumer failed, and no consumer bytes reached the helper-created outside file.
The helper's own outside file creation is outside the consumer boundary.

The small ARM64 seccomp policy now refuses recvmsg/recvmmsg, including the
time64 entry point, pidfd_getfd, open_by_handle_at and all io_uring entry points.
io_uring could otherwise receive descriptors without a recvmsg syscall. It also
refuses the seccomp syscall: a new notification listener could be sent to an
outside helper, which could inject a descriptor with NOTIF_ADDFD. Ordinary
prctl-installed filters cannot create such a listener. Legacy/new mount APIs,
pivot_root, unshare and setns are refused; source/system mounts stay read-only.
Cloned namespace capabilities therefore cannot enable a new writable filesystem
through an unfiltered mount interface. Another syscall architecture is refused.
The policy is installed only after the trusted namespace/mount setup completes.

This deliberately restricts **whole syscall families**, rather than claiming to
inspect ancillary pointers with classic seccomp. Arbitrary descriptor-passing
IPC, credential-receiving recvmsg users, io_uring workloads and consumers that
create namespaces/mounts are unsupported. That can affect performance or tool
semantics; timings of such altered workloads must not be presented as ordinary
production timings. No user-space descriptor broker or syscall-notification
supervisor was added to disguise the tradeoff.

On fixed source `fb2cc7522d873bdb90273e639c15212a65ecc2839870c59aeb4d7cc3252b542f`,
all 22 blocked API probes returned EPERM, while AF_INET socket construction,
socketpair send/recv, SSL context creation, fork **and spawn** Queue/process
exchange, and SharedMemory succeeded. A saved marker survived another run.
Actual HTTPS/S3/DNS traffic was not exercised. Descriptor-passing IPC such as
runtime multiprocessing resource sharing/forkserver has no compatibility claim.
Normal root and run-subcommand `--help` now return 0 without a command separator.
The compact before/after evidence remains in ignored
`build/reviews/storage-20261024/workspace-fd-fix/{before,after}.json`.

### Acquisition composition is still a separate requirement

Actual frozen `acquire.py run` was tested, not merely inferred from its code.
Before the new filter, its nested `unshare --user --map-root-user --mount --pid
--mount-proc` already failed because `/proc/self/uid_map` is read-only. The fixed
workspace explicitly refuses unshare. Landlock also forbids filesystem topology
changes, including the nested proc mount ([kernel documentation](https://docs.kernel.org/userspace-api/landlock.html#filesystem-topology-modification)).
No writable-proc exception or environment-variable escape was retained to make
this redundant guardian appear to work.

The tested acquisition pair was store
`4f0169e68785aa1dc5cbef1e7ce8ce269312b5f8585e897acb18dce5f7fa47d1`
and acquire `023bc2f88f0474bc41b9bea3f3d0281ea2157137bc13aeab11394dac1cbd511a`;
neither was edited here. A composed driver needs an explicit internal API that
uses the **existing** workspace PID 1, pool lock and descendant teardown instead
of a second namespace guardian. Standalone acquisition must retain its safe
default guard. Cache, temporary data and consumer output must share the same
enforced image, with no next pool writer admitted before descendants are reaped.
That integration is owned separately and has not been validated by this fix.

## Remaining limits and replacement boundary

### Btrfs inventory correction after the composed v5 review

The independent composed capacity review found two simultaneous fresh names both
admitted as 16 MiB under a one-image 17 MiB pool budget. A cloned instrumented
replay proved that the same-inode flock intervals did not overlap. The second
holder's first `os.listdir(pool_fd)` returned only `lock,budget`, while a newly
opened directory handle at that instant also saw the preceding holder's image.
The lock worked; admission consumed a stale inventory.

Btrfs records a directory-index ceiling in the open-file description at open
time. This pool FD had been opened before waiting for the lock. Duplicating it
for `os.listdir(fd)` preserves that ceiling; its trailing rewind makes a later
scan current, too late for the already-computed capacity total. The original
reproduction and a 32 MiB maximum cloned replay remain under ignored
`build/reviews/storage-20261024/workspace-lock-audit/`.

All inventory calls now open `.` relative to the held directory FD and enumerate
that fresh open-file description while the lock is held. The existence check
uses the same helper; fixing only the capacity loop would leave stale existence
decisions. This keeps inode anchoring without switching to a replaceable pathname
or relying on a second scan. The corrected native replay admits exactly one
fresh name and refuses the other, and concurrent same-name reuse retains its
previously accepted marker. The implementation still requires independent
workflow testing; a passing replay does not promote it into the public tools.

The budget is an enforceable aggregate **image-capacity admission rule**, with a
charged manager allowance. It is not a byte-exact whole-device physical quota.
The 1 MiB allowance does not prove a bound on arbitrary outer-filesystem Btrfs
tree allocation, retained snapshots, journal/allocator overhead or other apps.
Backing extents are actually allocated and image capacities cannot grow through
the consumer, but outer filesystem metadata and operational headroom still need
a separately justified reserve. Image/inode geometry can constrain a workload
before its nominal capacity; resource failures remain visible command failures.

The private mount is invisible after the command ends. Generated headers,
compile_commands.json, retained binaries and absolute build paths consequently
cannot yet be consumed by ordinary VSCode sessions or later unwrapped commands.
The bounded log tail is not an export interface. Actual rollout must preserve
IntelliSense and source/editor usability, probably with a stable visible
kernel-managed mount or a deliberately small read-only inspect/export operation.
Copying whole build trees back onto the ordinary filesystem would undo the bound.
No visible mount has been deployed here.

Other obligations remain: independent killed-writer and concurrent-admission
attacks; creation/publication interruption; metadata/image corruption; full
ENOSPC behavior in real tools; descendants retaining mounts; quotas on manager
metadata under the chosen outer filesystem; and power-loss evidence beyond the
tested SIGKILL/replay cases. The wrapper provides no interactive stdin. The original operator's passwordless
sudo can deliberately bypass it; this is not protection against that operator.

Production integration should preserve useful worker/artifact/dataset command
surfaces and give their acquisition/build/temp/log lifetimes one owner. The
immutable store and this filesystem experiment are complementary primitives,
not two new permanent frameworks alongside the old caches. Local builds across
checkouts/worktrees need explicit output-path migration into an enforced pool;
legacy images/results require owner-coordinated retention or recovery. Unique,
pinned and active bytes still count. No producer has been migrated, and this
prototype does not claim that the recurring real workflow is fixed.
