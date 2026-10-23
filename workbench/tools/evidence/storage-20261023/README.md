# Closed local output recovery — October 23, 2026

This storage checkpoint keeps older source snapshots, exact executables,
disassembly and research output recoverable in S3 while releasing their local
copies. Current source, useful incremental builds and active datasets remain
local. The retired Hashtable/Iceberg owners explicitly released their old build
state and stale pin.

`closed-output-NNN.json` references contain selected older large files. Archive
member names are relative to the original `build/` directory. Each published
bundle is downloaded and verified by size and SHA-256. Members are hashed from
that verified download and compared with local bytes before removal; changed
or newly pinned files stay. The interrupted first batch used the equivalent
fresh streamed verification. Small notes, recipes and surrounding context remain
beside the original output paths.

Recover a selected member into a new ignored directory:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/tools/evidence/storage-20261023/closed-output-001.json \
  build/recovered/storage-20261023/part-001 \
  --file boundlist-u/encoding-avx512-zen5.s
```

The complete-tree references
([Hashtable](hashtable-design-tree.json), [Iceberg](iceberg-swiss-active-tree.json))
preserve the remaining closed
`build/hashtable-design` and `build/iceberg-swiss-active` trees, including their
small sources, notes, symlinks and earlier eviction maps. These ordinary artifact
bundles contain one inner `.tar.gz`; fetch the reference and extract that inner
archive into a new directory. Its root name reproduces the old build-tree name.
Full-tree release additionally compares every original file by SHA-256, checks
links, and checks the complete path/identity set before removal.

The earlier [October 16 checkpoint](../storage-20261016/README.md) retains the
17 parts and source-eviction mappings for output already released then. The
[Hashtable retirement](../../../notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026)
locates its retained runnable/source snapshot. Those recovery references remain
valid.

Operational plans, per-path removal receipts and interrupted-transfer recovery
are under `build/reviews/disk-reclamation-20261023/`, with a verified S3 copy
referenced by [operations.json](operations.json). Compiler objects from idle
or explicitly relinquished build trees and disposable package/browser caches
were reclaimed separately. The final physical free-space change is recorded in
`summary.json`; conservative allocated-byte totals are not added together as a
claim about exclusive disk use. No periodic storage maintenance was installed.
