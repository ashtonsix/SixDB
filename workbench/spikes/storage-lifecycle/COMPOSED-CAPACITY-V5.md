# Fresh composed capacity and lifecycle review, v5

Independent review of the complete `flow.py run` command on October 25, 2026.
The candidate is **not capacity safe** on this host: simultaneous admission
accepted two fully allocated 16 MiB images into one 17 MiB pool.
Accepted content survived the pressure and process tests performed here.
No implementation files were changed by this reviewer.

## Starting point and scope

The [storage principles](README.md#what-the-replacement-must-establish),
[research map](../../notebook/README.md), and
[measurement conventions](../../benchmarks/README.md) set the starting point:
retention thresholds are not quotas; in-flight allocations, inode metadata,
logs, interrupted writers and descendants matter. The current owning code
already composes acquisition, cache and mutable output inside one ext4 image.
This review therefore tests that complete command rather than substituting an
accounting simulation. Prior reviewer outcome reports were not used to design
the workload. A separate implementation audit subsequently supplied the
directory-enumeration hypothesis, which was independently reproduced here.

Frozen source SHA-256:

| File | SHA-256 |
| --- | --- |
| store.py | b0a4c22be48aba7a19bd9bd61d86e30b90392bcc7999cd7a824175753122b771 |
| acquire.py | 7ff36f2abf2ef6e84d9fdaf544af89793aa6851ba51852b8519dc49927d32970 |
| workspace.py | fb2cc7522d873bdb90273e639c15212a65ecc2839870c59aeb4d7cc3252b542f |
| flow.py | 122e1b75a5ece57c396d993a9e554108f0b704c0d1c660cd6d5a88af6004cfd6 |

The harness is [adversarial/composed_capacity_v5.py](adversarial/composed_capacity_v5.py).
Its only fixture root is
`build/reviews/storage-20261024/fresh-composed-capacity-v5/`; frozen copies live
under `candidate/`. Four 16 MiB image allocations consumed exactly the 64 MiB
review allowance. Other fixtures are well below 8 MiB. No real caches, builds,
cloud resources, global mount/quota configuration, or installed tools were used.

Host: Linux 7.0.14-orbstack-00380-ga7e0a2dc9535, aarch64. The pool's backing
filesystem is Btrfs, device 41, `/dev/vdb1` container rootfs with `nodatacow`.
The image files contain ext4. Every pool argument and candidate-script argument
used the native Linux `/home/ashtonsix/sixdb/...` path. The consumer script path
resolved through `/Users/ashtonsix/OrbStack/ubuntu/home/...`; independent `stat`
shows the projection and native paths refer to the same Btrfs device/inodes.

## Failure: simultaneous admissions exceed the physical pool budget

For a new pool, launch these two `Popen` calls without waiting between them:

```text
python3 ROOT/candidate/flow.py --pool ROOT/concurrent --budget 17825792
  --size 16777216 --cache-budget 1048576 run NAME ROOT/input.json
  ROOT/input.tar.gz --output shared -- python3 REVIEW_SCRIPT consumer seed ''
```

`ROOT` is the native Linux fixture path above; `NAME` is `alpha` and `beta`.
The archive is a 142-byte local gzip/tar fixture containing one 3,000-byte
independently known payload. Each consumer verifies the input and fsyncs its
own 1,750-byte sentinel. The harness requires one success and one refusal.
Observed: both returned 0, and both reported `reserved_image_bytes=16777216`.

Independent post-run oracle:

| Pool entry | Device/inode | Logical bytes | Allocated bytes |
| --- | --- | ---: | ---: |
| lock | 41 / 17444758 | 0 | 0 |
| alpha.img | 41 / 17444761 | 16,777,216 | 16,777,216 |
| beta.img | 41 / 17444762 | 16,777,216 | 16,777,216 |

FIEMAP through `filefrag -v` found disjoint physical ranges, without shared
extent flags: alpha at blocks 15,671,472–15,675,567 and beta at
15,717,460–15,721,555, in 4,096-byte units. This is 33,554,432 allocated bytes
against a configured 17,825,792-byte budget, excluding manager metadata.
Both accepted sentinels were independently read with `debugfs`. No loop
associations remained. A subsequent sequential `alpha` invocation refused
`accepted images exceed configured budget; preserve and refuse`, so the excess
is both physically present and later recognized.

The lock is not simply absent: an independent parent holding `flock(LOCK_EX)`
on that native lock caused a second process's nonblocking flock to fail with
`EAGAIN`. A tiny file-only reproduction, repeated three times on the same
filesystem, exposed the relevant observation hazard:

```python
directory.mkdir()
fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
(directory / 'child').touch()
os.listdir(fd)         # observed []
os.listdir(directory)  # observed ['child']
os.listdir(fd)         # observed ['child']
```

`workspace.py` opens its pool descriptor before waiting for the global lock,
then enumerates that descriptor in `prepare`. A contender can therefore scan
an older directory view after the preceding holder publishes an image. This
explains how a working flock can compose with stale admission input. This is
a host-specific observed filesystem behavior, not a general claim about all
Btrfs/Linux combinations. The complete command must handle or refuse it.

Evidence: `concurrent-race.json`, `inode-path-oracle.json`,
`directory-fd-oracle.json`, `sequential-after-overcommit.json`, and
`results.json` under the fixture root. The race was not repeated with additional
images because the first breach consumed the allocation allowance.

## Failure: the composed route cannot reach cleanup after inode exhaustion

Starting with an accepted sentinel and input, the consumer creates empty regular
files until ext4 refuses another inode. It created 4,065 files; direct image
superblock inspection found zero free inodes while 10,891,264 data bytes remained
available. The next composed command, whose consumer only deletes those own
test files, returned 125 before reaching that consumer:

```text
flow refused: OSError: [Errno 28] No space left on device: '.catalog-UUID'
```

The byte bound held and the accepted sentinel was unchanged. This is an
availability/recovery limitation of `flow run`, not data loss or a capacity
escape. The public `workspace.py run` route successfully ran the same bounded
own-output deletion without initializing the cache, and the next `flow` run
succeeded. The reviewer did not mutate the image with `debugfs` or mount it
manually. A cache-independent maintenance route is therefore available at the
workspace layer but absent from the composed public command reviewed here.

## Other observations and limits

| Probe | Independent consequence |
| --- | --- |
| Ordinary data fill | ENOSPC after 10,944,512 written bytes; data inode allocated 10,948,608 bytes; accepted sentinel unchanged; backing image stayed 16 MiB. A later composed cleanup succeeded for this byte-pressure case. |
| Held open, unlinked file | External `/proc/PID/fd` inspection found links=0, size=10,944,512 and allocated=10,948,608 on the deleted inode. Image capacity held; after close/reap, restart succeeded. |
| Wrapper SIGKILL | Two identifiable descendants, including a detached setsid/fork descendant, disappeared by host PID/start-time identity; reopened sentinel unchanged. |
| Consumer SIGKILL | Wrapper returned 137; the same descendant classes disappeared; reopened sentinel unchanged. |
| Consumer exits with descendants | Detached background descendants were gone when the composed command returned; sentinel unchanged. |
| Empty interrupted allocation staging | A root-owned, regular zero-length `stale.new` in an existing pool was removed by the next command; accepted sentinel unchanged. This does not establish recovery after an actual mid-format kill. |
| Stdout exhaustion | Direct image inspection found root-owned stdout size=10,953,061. It consumed the remaining image space; the next composed verification returned 125 before consumer. Sentinel remained unchanged. Logs stayed within the image. |

All completed-image checks used external `stat`, read-only `debugfs`, and
`losetup --associated`, rather than trusting the candidate's budget/cleanup
report. Process checks compared host PID and `/proc/PID/stat` start time, not
the namespace's potentially reused PID. No loop/mount associations remained at
the checks. The inode-recovery and concurrency failures are retained; successful
probes are bounded evidence, not a claim of exhaustive reliability.

The initial harness stopped at the inode-cleanup failure. `resume` performed the
public workspace recovery and continued until the concurrent admission failure.
`finish-existing` completed the remaining tests without creating another image.
Run logs and `results.json` preserve those stages. The code retains these
continuations to make that execution history explicit.

The mid-allocation/format interruption case was not run because no image
allocation allowance remained. Power-loss durability, full host-filesystem
exhaustion, device failure, arbitrary privileged writers, and other host/kernel
versions were not tested. The concurrent failure images were preserved for the
owner's read-only diagnosis, pending removal after association checks. The
pressure/lifetime images were removed after separate loop and all-readable
`/proc/*/mountinfo` checks; their final content/allocation oracles and
`cleanup.json` remain in the fixture root.
