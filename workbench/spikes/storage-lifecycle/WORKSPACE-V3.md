# Fresh independent workspace review, 25 October 2026

Reviewed `workspace.py` SHA-256
`5ed5a1c768964e832389c0e48f942d9e1498105748f0dc21d7948ade16d08ccf`.
No author's or previous reviewers' workspace reports were read before testing.
The prior-work starting point was the [storage guide](../../tools/storage.md):
retention targets are not admission limits, and recent, pinned and unique output
can exceed them. This review therefore checks actual file allocation and kernel
behavior rather than treating reported budget totals as proof.

The replacement establishes a useful bounded subprocess workspace in the tested
Linux composition. It is not yet an operational replacement for all SixDB output.
In particular, reachable outside helpers can perform uncapped writes or transfer
outside writable file descriptors into the sandbox.

## Findings

**Outside helpers can bypass the file boundary.** A fresh fixture AF_UNIX server
outside the namespace accepted a connection from the sandboxed consumer and wrote
the received bytes to `source/helper-output`, outside the image. More strongly,
another fixture server sent a writable regular-file descriptor using `SCM_RIGHTS`.
The sandboxed consumer used `os.write` and `os.fsync` on the received descriptor,
returned zero, and an independent host read found
`consumer wrote via received fd` in `source/fd-output`. No real daemon, network
connection, source data or outside cache was involved.

Recursive read-only mounts and Landlock restrict pathname operations but do not
revoke the writable mount reference received through this socket. This is a
concrete limitation for an IDE, persistent compiler helper, Docker-like service,
or other outside process that accepts work or passes files. It does not imply
that merely opening a socket grows disk. Keeping ordinary IPC usable while
bounding helper work requires the actual helper composition to be included in
the boundary. See `external-helper-socket`, `external-helper-observed`, and
`external-helper-fd` in the evidence.

**Normal CLI help fails.** Both `workspace.py --help` and
`workspace.py --pool POOL --budget BYTES run --help` exit 2 with
`error: separate the command with --`. Appending `--` permits help. The explicit
separator lookup at lines 450–453 precedes argparse's help handling. This is a
small usability defect, independently reproduced before any image was created.

**Logical capacity is not exact outer allocation.** On the bounded outer ext4
fixture, a 16,777,216-byte accepted image used 16,781,312 bytes according to its
outer inode's `st_blocks * 512`. Admission sums file lengths and reserves 1 MiB
for management. That reserve covered this experiment; no configured-budget
overrun was observed. A claim about exact physical allocation across arbitrary
outer filesystems or fragmentation needs more evidence than the logical sum.

## Independently observed behavior

| Case | Observation |
| --- | --- |
| Aggregate admission | Three 16 MiB images accepted under a 49 MiB pool budget; fourth refused. Independently statted accepted images were exactly 16 MiB in size and allocated bytes on the host fixture. Size and budget changes were refused. |
| Repeated/restarted work | `incremental` retained `one\ntwo\n`; four independent runs left counter `4`; two concurrent runs of the same name left `xx`, read using an independent read-only mount. |
| Concurrent names | Four wrappers using three names completed successfully. The global pool lock serializes complete consumers, including different names; no simultaneous same-pool workspace use is provided. |
| Source and outside paths | Direct writes to the source sentinel, a fresh hard-coded `/tmp` path and an escaped HOME path failed with EROFS. The original source sentinel stayed byte-identical. |
| Output/temp/cache/shm | HOME, TMPDIR/TMP/TEMP, XDG cache/state, Cargo target, Python cache prefix and `/dev/shm` fixture files were found inside the image by an independent read-only mount. |
| Ordinary local IPC | Unconnected `socket.socket()`, `socketpair()` traffic, and a forked `multiprocessing.Queue` exchange succeeded. No actual network traffic was attempted. |
| Consumer error and signal | Explicit exit 7 retained acknowledged files; consumer SIGTERM reported -15 and outer exit 125, while an independent mount recovered its pre-signal file. |
| Wrapper loss and descendants | Killed outer wrapper with a forked, daemonized descendant; all recorded host PID/start-time identities disappeared, images had no loop association, and restart retained state. |
| Disk exhaustion | A data writer reached ENOSPC in a 16 MiB image. An acknowledged earlier file survived; deleting only its fixture filler allowed resumed writes. |
| Inode exhaustion | Empty-file creation reached ENOSPC with zero free inodes. Deleting that fixture directory inside a later run restored operation. |
| Log exhaustion | Large stdout reached ENOSPC and consumer exit 1, with image length/allocation unchanged. An earlier acknowledged data file survived independent inspection. |
| Interrupted initialization | Deterministic pause after allocation, before mkfs; SIGKILL of the verified private namespace PID 1 left one fully allocated `.new`. Next run reconciled it and succeeded. |
| Interrupted mount setup | Deterministic pause immediately after actual mount; SIGKILL of private namespace PID 1 released the loop, retained the accepted image and allowed reuse. |
| Outer allocation failure | Fresh 32 MiB ext4 regular-file fixture with about 8 MiB free forced fallocate ENOSPC. A 16 MiB `.new` had only 8,376,320 allocated bytes. Freeing only fixture filler allowed reconciliation and successful publication. |
| Corrupt image | Zeroed only the fixture image's primary superblock; reuse refused with mount failure, preserved the image, and left no loop association. Other accepted image contents remained intact. |
| Corrupt budget | Damaged only fixture budget metadata; run refused without deleting the accepted image. Restoring the exact budget permitted reuse. |

Image inspection used a separate private mount namespace and explicit read-only,
`noload` ext4 mounts. Allocation checks used independently statted outer inodes;
loop checks used `losetup --associated`, not wrapper report fields. Process checks
recorded host PIDs, start times, namespace identities and ancestry before teardown.
Namespace PID 1 was only signaled after checking that it differed from the host
PID namespace and matched the unique fixture invocation.

The setup injections change invocation timing by wrapping the imported module's
`tool` function in the review harness; the reviewed implementation file is not
edited. These are interruption checks before formatting and immediately after
mounting, not a power-loss test or an interruption inside the mkfs/mount syscall.

## Real integration limits

The command's cwd becomes `pool/mount/work`; source paths are available through
`SIXDB_SOURCE` and the source mount is read-only. Helpers that assume a writable
source tree, an existing absolute build path, a real HOME cache, or an outside
daemon need deliberate integration. No real build or benchmark was run here.

The mounted workspace exists only in private namespaces. After completion the
host sees an empty `pool/mount`, while root-owned mode-0600 images retain files.
An ordinary IDE cannot directly browse or edit those retained output paths. The
CLI exposes only the last 1,000 bytes of each appended persistent log and has no
consumer-facing export, inspect, resize, repair, or image-removal command. Logs
compete with incremental output for the fixed image's usable filesystem space.

Budget ownership is pool-local. Separate pools, outside tools, existing builds
and helper daemons remain outside its aggregate admission. Same-pool consumers
serialize for their entire lifetime, so concurrent IDE/build activities cannot
simply be assigned separate image names and run concurrently. The implementation
also explicitly refuses non-AArch64 hosts and needs its specified privileged
Linux facilities. None of these prototype limitations should be hidden by a
claim that the overall repeated-exhaustion problem is already solved.

## Reproduction and evidence

Harness: [adversarial/workspace_v3.py](adversarial/workspace_v3.py).
From the repository root, after removing only inactive fixtures from a prior run:

```sh
orb -m ubuntu python3 workbench/spikes/storage-lifecycle/adversarial/workspace_v3.py \
  --output build/reviews/storage-20261024/fresh-workspace-v3
```

Ignored evidence: `build/reviews/storage-20261024/fresh-workspace-v3/results.json`
and `run.log`. The completed review records 57 cases, including independent mount,
outer allocation, process teardown, FD transfer, invalid-request boundaries and
shared-memory device equality. The main harness was run end to end; FD transfer
and the final boundary/device assertions were then executed as targeted additions
against the same fixtures. Cleanup independently checked all five outer images,
found no host fixture mounts or loop associations, and removed only those fixtures.

Peak outer fixture images were 96 MiB: three 16 MiB main images, one 16 MiB
interruption image and one 32 MiB outer filesystem. Its nested accepted image
adds another 16 MiB if counting apparent regular-file lengths recursively inside
the mounted outer fixture, still below 128 MiB total. No real devices, build trees,
real caches, global quotas, installed tools, cloud or network were used. Bulky
fixture images are disposable; retain the harness and compact findings.
