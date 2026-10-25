# Persistent workspace capacity: host feasibility

October 24, 2026. One isolated real-filesystem fixture passed; no production
rollout, real-device format, global Btrfs quota change, benchmark build or cloud
work occurred. The only formatted object was a newly created regular image file.

## What this host permits

The guest root is an OrbStack LXC Btrfs subvolume mounted with
`nodatasum,nodatacow`. Passwordless `sudo -n` is available; root has
`CAP_SYS_ADMIN`, `NoNewPrivs=0`, `Seccomp=0`. `mount`, `unshare`, `losetup`,
`mkfs.ext4` and `fallocate` are installed, and loop-control/device nodes exist.
Ext4 was initially unloaded but its matching current-kernel module and
dependencies were present. Standard filesystem-driver autoload during the
private fixture mount worked. No custom module was installed or loaded.

`btrfs`, `fuse2fs` and `setquota` were not installed. The fixture required none
of them. A timer or retrospective scanner was not involved.

## Actual fixture result

Run from Linux with `orb -m ubuntu` from the Mac:

```sh
python3 workbench/spikes/storage-lifecycle/adversarial/quota_image_probe.py
```

The script creates one **64 MiB regular image with `O_EXCL`**, checks its identity,
type and size before formatting, and mounts it only inside
`sudo unshare --mount --propagation private` with `loop,nodev,nosuid,noatime`.
No storage-manager API participates in the writes.

| Check | Observed result |
| --- | --- |
| Native `dd` bulk write | Failed with actual `No space left on device` |
| Tiny-file creation after releasing the bulk file | 2,034 files, then errno 28; all 2,048 filesystem inodes used despite about 52.5 MB data space still available |
| Previously acknowledged, fsynced incremental marker | Exact bytes survived both exhaustion modes |
| Unmount/remount of the same image | Marker and all 2,034 tiny files persisted |
| Image size before/after | Exactly 67,108,864 logical bytes; did not grow |
| Outer allocated bytes after the workload | 66,916,352 bytes, measured independently via `st_blocks` |
| Final teardown | Successful unmount; no remaining loop association or private mount; image removed after report saved |
| Duration | 1.03 seconds |

Marker SHA-256: `7dfc85a2980017d46e6906a1c7b08f059d05062697e4ae759f7692c6e3c7ced6`.
Fixture source SHA-256: `02dd5a5c5ccbeccd60ca4e200edd78efa3cbf9eba41dcc83822582f18328bd26`.
Full tiny report remains at
`build/reviews/storage-20261024/quota-image-probe/0c716317c2c44fb0a615ea4cfca5aae9/report.json`.
That directory contains no large image after successful cleanup.

## Smallest viable build boundary

A fixed-size persistent filesystem image is a viable enforcing boundary for
ordinary compiler/build output on this host. The inner filesystem owns its
allocation, directories, inode limits, open-unlinked files and ENOSPC behavior;
a Python margin no longer has to emulate a filesystem allocator. The image
survives the namespace/session and can preserve incremental builds. Tmpfs would
only provide ephemeral, potentially swapped storage and is not the durable
replacement.

The smallest safe rollout would have one explicitly sized persistent build/cache
pool, one mount owner and ordinary unprivileged writers inside it. Keep source,
unique evidence and immutable recovery identities separately recoverable. Give
the producer's build roots, Cargo target output, temporary files and stdout/stderr
paths capacity inside the pool. Writers allowed arbitrary writable paths outside
it can still bypass the boundary; that is a sandbox/namespace rollout obligation.

Do not independently mount the same ext4 image read-write through multiple loop
devices/namespaces. Serialize image mount ownership or use one supervisor-owned
mount shared by consumers. Hold ownership until the final consumer is done and
teardown is verified. Protect the backing image against ordinary writers changing
its length; administrative growth must reserve additional capacity first.

One global admission record must reserve the **aggregate maximum sizes** of all
images, not just their currently sparse allocated bytes, plus outer-filesystem
metadata/backing headroom. Starting many nearly empty sparse images is otherwise
another route to oversubscription. Other applications, external snapshots and
the Mac backing volume remain outside this local boundary.

The replacement store must still decide what can leave and preserve active,
pinned and unique data. A hard limit prevents device exhaustion; it does not make
an impossible working set fit. Legacy output needs explicit owner-coordinated
migration before claiming the real workflows are bounded. This fixture proves
host feasibility and persistence under data/inode exhaustion, not abrupt power
loss, a complete build wrapper or the full storage replacement.
