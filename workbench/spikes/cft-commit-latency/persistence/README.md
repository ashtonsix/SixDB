# What a durable write measures

A log record can pass through the page cache, filesystem and device cache before
reaching power-safe storage. This study times completion at the persistence
boundary, then compares how much work different paths do before reaching it.
The [findings](FINDINGS.md) explain which choices changed the measured cost.

## Separate three choices

| Choice | Alternatives in this study | What changes |
| --- | --- | --- |
| Prepare the destination | Grow a file; reserve unwritten extents; overwrite initialized extents; use a prepared raw region | Allocation and metadata work that remains when a record arrives |
| Deliver the write | Buffered or direct I/O, using blocking calls or `io_uring` | Page-cache involvement and submission/completion handling |
| Require durable completion | Write followed by `fdatasync`, or write through an `O_DSYNC` descriptor | When the application may count the record as durable |

**Preallocation** reserves file space. On the tested ext4 path, those extents
remain unwritten until their first write. **Initialization** writes the region
and synchronizes it before the measured phase, so subsequent records overwrite
already initialized extents. A real log using this path needs to prepare segments
ahead of demand; the measured overwrite latency excludes that preparation.

**Direct I/O** bypasses the page cache for the data transfer. A direct write to a
file still goes through the filesystem, and `O_DIRECT` alone does not promise
durable completion. A raw-device write avoids file allocation but still needs
a durable completion contract and a way to recover valid records.
[Linux's open interface](https://man7.org/linux/man-pages/man2/open.2.html)
distinguishes direct I/O from synchronized data-integrity completion.

## What makes completion durable

The admitted paths use `fdatasync` after a successful write or `O_DSYNC` on the
write itself. The `io_uring` variants preserve those semantics: a linked write
must succeed before its datasync completion counts. See
[fsync/fdatasync](https://man7.org/linux/man-pages/man2/fsync.2.html) and
[the io_uring fsync helper](https://github.com/axboe/liburing/blob/master/man/io_uring_prep_fsync.3).

At the block layer, a **flush** makes earlier cached writes reach non-volatile
storage; **Force Unit Access (FUA)** requires the flagged write's data to be
non-volatile before completion. Linux translates the requested contract according
to the device's advertised capabilities. A device without a volatile write cache
may need no explicit flush. These distinctions explain why the D3 diagnostic
examines issued commands as well as syscall names.
[Linux flush/FUA semantics](https://www.kernel.org/doc/html/latest/block/writeback_cache_control.html).

Qualification relies on OS/device completion contracts and AWS's documented
[instance-store persistence after power failure and reboot](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/instance-store-lifetime.html).
Loss on stop, termination, host recovery or disk failure remains a separate
failure mode for replication to cover. The study cannot physically cut power
to AWS hardware or certify its capacitors. Readback validates content; it does
not establish physical power-loss protection (PLP).

## Probe, controls and validation

[bench.cpp](bench.cpp) measures a write together with its durability operation,
including submission/completion processing for `io_uring`. The methods are
buffered write + fdatasync, direct write + fdatasync, direct `O_DSYNC`, and the
two corresponding direct `io_uring` paths. Setup and 32 warmup writes precede
timing. Initialization and parent-directory fsync also precede the measured phase.

The filesystem matrix includes ordered ext4, ordered with
`noatime,max_batch_time=0`, and tuned writeback with synchronized writes. Required
barriers stay enabled. Journal-free filesystems are excluded because ordinary
readback does not establish their metadata crash consistency.

Raw experiments use a bounded initialized region on positively identified,
disposable non-root study storage. The runner records model/serial, size,
mount and partition ancestry, sector sizes, cache/flush capabilities, filesystem
settings and host identity. Device names alone do not authorize a target.

Payloads carry an operation identity at every word. After closing the writer,
all records are reread with direct I/O to check identities and offsets. No final
fdatasync is added before verification to conceal a missing per-write durability
operation. Raw record recovery also needs torn-record and durable-prefix handling.

The initial screen spans methods, allocation modes and sizes. Shortlisted
settings receive longer independent passes; sample counts and pass spread
accompany the tails. A screen with few observations beyond p99.9 supports only
a limited estimate of that percentile. [run.py](run.py) defines the matrix;
[analyze.py](analyze.py) combines its samples, and
[diagnostic_analyze.py](diagnostic_analyze.py) handles the D3 traces. The
[overall study](../README.md) combines selected paths with actual quorum commits.
