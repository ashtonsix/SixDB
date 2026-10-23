# Closed local output recovery — October 16, 2026

The parked hashtable workspace held tens of gigabytes of old diagnostics and
compiled output. Its owner released the inactive compiler products and allowed
other local output to leave the device after verified archival. This is a
storage checkpoint, with no new performance or correctness claims.

The later [released review checkpoint](reviews/README.md) preserves source,
qualification context and exact old-path recovery for seven Orbital/Loom reviews.
The completed Loom [v3 pair](loom-v3/README.md) and
[maintained composition](loom-composition/README.md) recovery checks have separate
maps for released archive copies; differing reports and verification receipts stay.

All 17 parts are complete: 33.79 GB of matching local output was released.
The separate launch-source cleanup removed 880 duplicate copies (7.28 GB).
These figures describe different paths; the worker-cache receipts record its
additional removals without claiming an exact combined byte delta.

[index.json](index.json) lists bounded parts of the old
`build/hashtable-design` tree. Each `part-NNN.json` is an ordinary
[artifact recovery reference](../../artifacts.md). Member names retain their
paths relative to that tree. The selection covers regular files at least 1 MiB
that had settled for an hour; pins, symlinks and Git metadata were excluded.
Small authored programs, scripts, notes and other local files remained in place
at that checkpoint. The remaining closed tree was later retired into the
[October 23 checkpoint](../storage-20261023/README.md), preserving those smaller
files and the earlier recovery maps.

Each part was uploaded, downloaded and checksum-verified, then read from S3
again to verify its members before byte-identical local copies were removed.
The existing artifact eviction helper kept recovery mappings before unlinking;
edited or unmatched files stayed. Generic file bundles preserve bytes without
asserting that an old experimental run succeeded or remains reproducible.

Recover a whole part to a new ignored directory, or add `--file EXACT/MEMBER`
for one file:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch workbench/tools/evidence/storage-20261016/part-001.json build/recovered/hashtable-large-001
```

Local `.artifact-eviction-SHA256.json` files under `build/hashtable-design`
map original paths to their part and member. The checkpoint's operational
receipts have a separate `receipts.json` recovery reference. They also account
for the independently rebuildable old CMake objects removed before archival and
the closed worker copies evicted through their existing archives. Those objects
do not need to be restored to understand the retained study findings.

Duplicate launch `source.tar.gz` copies were a separate unbudgeted footprint.
Old terminal, unpinned copies were compared with the originally uploaded S3
source object and the job's expected SHA-256; identical sources could share one
fresh remote verification. Each removed copy has a durable job-local
`.source-eviction.json` recording its recovery URI and checksum. These receipts
are also in the operational bundle. Recent access, active jobs and local edits
remain protected.

Current Iceberg, filter, Loom and Orbital source/workspaces were outside this
retirement. The [worker cache policy](../../storage.md) separately limits old,
closed archived collections while preserving recent access, pins and local edits.
