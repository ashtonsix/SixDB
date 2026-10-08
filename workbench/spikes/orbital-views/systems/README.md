# Backing resources and journal service

Two native comparisons expose costs hidden by the original per-part memfd and
per-record durability fixture. Run `bash workbench/spikes/orbital-views/systems/run.sh`
from the Linux repository with pinned Clang. The runner builds independent TUs,
pins one allowed CPU and alternates three forward/reverse trials. `SIXDB_RESULTS`
and the usual build/ISA/tuning settings select the destination and target.

The embedded baseline can retain inputs in memory; file sync service below
prices a stronger guarantee. [Consolidated design](../../../design/orbital/DESIGN.md#embedded-path) owns that distinction.
The parent [check runner](../check.sh) exercises the four compositions below
without running the performance matrices.

`intent_probe` runs one compiled owner replay law with actual TuplePack stores,
part interpretation, membership, count and sum. A retained-memory case groups
six producer positions into two epochs, folds them privately and installs one
head per epoch. Capacity retry leaves the old cut intact; later events consume
earlier private work. A no-effect event does not skip a later mutation. Duplicate
delivery validates immutable admitted content and membership before returning
completion. Batches are authored, not adaptively selected.
The file cases sync complete input before native execution and reopen in fresh
processes after admission, private interruption and installation while derived
material is stalled. Checkpoint adoption preserves a promised old cut and retry
cursor before prefix deletion; reopening scans one suffix record. File cases
use one command per epoch and one checkpoint. No power-loss test or production
frame/interpreter format is established.

`binding_probe` supplies raw scratch reserve/grow/reuse, retained native windows,
generic captured memory/file source identities and owner-only result settlement.
Failed acquisition exposes no window; a cancelled backend retains its actual
source/target until completion, and its late receipt cannot install over a newer
generation. The arena survives the group through an outstanding allocation.
Owners are serialized, backend context is supplied for the request lifetime and
allocation failure handling is not implemented. Native and ASan/UBSan checks pass
locally; there are no performance timings for these two probes.

`composition_probe` connects real TuplePack native stores, resolved issued-byte
coverage, stable post-call capture, a synced replayable afterimage journal,
shared-page material and one immutable bytes/summary head. Old cuts survive
reuse of the same writable home. A fresh process recovers epoch 3 while the
bounded derived sink remains stalled; late/failing completions leave the epoch 5
consumer head intact. Gated background threads retain the submitted bytes; one
writes and syncs its old snapshot, while another really fails on `/dev/full`.
This is a small full-snapshot sink fixture, not a chosen material wire format.
The fixture supplies single-owner physical exclusion and
uses the publication study's mechanisms. It does not implement semantic intent
execution, multi-object admission, consensus or a worker sandbox.

`unit_boundary` is a correctness-only Bec256 composition. Empty/full encodings
have zero body bytes and effects; changing caller-owned interpretation and summary
material is necessary to preserve their different values across old cuts and reopen.
It was added after the worker timing capture; native and ASan/UBSan pass locally.

## Sources

`resource_probe` compares 1,024 immutable initial parts with separate sources
against sources grouping 16, 128 or all parts. Each cell runs in a fresh child.
All policies initialize identical payloads and read the same selected native
byte pairs; allocation/source construction, preparation/drop and repeated reads
are separate phases. Shapes retain everything, every tenth part, or the first
tenth. The experiment reports live FDs, VMAs, RSS, address reservation and retained
logical backing bytes. Checks inspect distinct values at each selected identity.
The first worker run actually exhausted its 1,024 soft FD limit in the per-part
cell. The comparison now explicitly raises only its own soft limit to 1,088
when needed (within the existing hard limit), and records that adjustment.
This permits measuring the baseline; it is not a proposed production remedy.

This isolates initial immutable trusted data. It does not compare mutation,
growth, permissions, durable extent allocation or a common fully prepared scan.
Only the first page of each separate part is prepared. RSS excludes unmapped
shmem/page-cache backing; logical backing bytes are not disk traffic or RSS.
Grouped-source retention coupling belongs to this simple sealed-chunk primitive:
a shared database file with independent extent retirement need not pin every
extent merely because one remains live. The comparison supports separating
logical parts from OS resource granularity, not adopting a fixed chunk size.

## Journal

`journal_probe` writes identical framed records using concatenation or header/body
`writev`, then `fdatasync` once per batch of 1, 16 or 64 events. It reports actual
write/sync calls, user payload copies, wire bytes, service time per event and the
maximum service batch within each trial. Payload construction/hashing is outside
those timings. Gather-vector management is inside; gather is not free. There is
no async queue or batch-fill wait, and maxima are not latency percentiles.
The runner records the mounted filesystem. EC2 root gp3 timings cannot select a
local-NVMe, replication or quorum policy.

Checks force real partial writes across header/payload boundaries, reopen in a
child, reject changed bytes and recognize an incomplete suffix. The frame and
checksum are experimental, with no selected security/production format.
`fdatasync` plus process reopening is not a forced power-loss experiment.

Summaries retain all equal-work cells, medians and ranges of six trial means.
Full compiler output and binaries belong in the captured artifact; the parent findings name
useful comparisons and limits.
