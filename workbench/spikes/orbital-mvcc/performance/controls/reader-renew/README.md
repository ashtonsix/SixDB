# Fresh and reusable LMDB short readers

This control compares ordinary begin/abort with reset/renew under MDB_NOTLS.
[LMDB's API](https://github.com/LMDB/lmdb/blob/LMDB_0.9.31/libraries/liblmdb/lmdb.h)
documents that resetting ends the active snapshot while retaining the handle
and reader-table slot; renewal enrolls a new snapshot. Final abort frees both.

Apply the patch inside an initialized Git root for the frozen measured
coherent-fusion baseline, verify the source postimages, then run
`reader_renew_screen.sh`. A nested parent repository can silently skip paths
even with `--no-index`; a successful exit is insufficient. The combined
cold/reader control is already applied in the corrected capture: recover that
source and use `fused_feedback_screen.sh` to replay all three cuts. The common stationary owner and 8-byte ShortLease
wrapper are identical in both arms. Fresh leases begin/abort each group; the
renew treatment prepares one inactive handle before warmup, renews inside each
complete request interval, resets at lease destruction, and aborts after the
trial. No transaction stays active between groups. Ordinary 16-byte Read still
owns independent old readers and writer bases. The store stays stationary and
outlives its one active short lease; nested leases and closing an active lease
are refused. Cached body borrows expire before the issuing lease resets.

The source patch adds a focused CMake check for independently expected row and
mapped-body publication visibility, preserved old views, moved lease ownership,
nested/close refusal, exact reset/abort counts and re-preparation. The check runs
on each captured worker cut before timing. The same oracle passed natively in
both policies, alongside 24 complete mapped CLI cases. Those native CLI
checks used the earlier fused metadata; they validate the lease mechanism,
not byte identity of the captured worker executable. The focused independent
check additionally passes on each exact worker cut.

Setup and final cleanup have separate clocks. Counters include warmup and timed
leases, retained versus active state, wrapper records and final cleanup.
Private LMDB transaction allocation size remains unavailable rather than zero.
The ordinary older begin/abort evidence stays unchanged. This matched wrapper
control cannot attribute a comparison with old binaries solely to reset/renew.

Runner report support keeps these optional LMDB columns when other competitors
appear first, leaving their unavailable cells blank. Historical report inputs
without the fields keep their existing columns. Bulky source/ELF/latency/logs
stay in the existing worker bundle; selected complete comparisons and exact
replay/control code belong in Git.
