# Completed Loom composition recovery copies

Loom released six export/recovery/replay directories after its maintained
composition v1 campaign passed native qualification and portable recovery.
The existing eviction helper released 1,567,716,942 logical bytes
matching freshly verified analysis and raw worker archives. Local receipts,
proofs, logs and all differing or unmatched files remain; every one of the
10,512 protected files was checked again by size and
SHA-256 (559,843,176 bytes). Current work and compact
evidence were excluded. Logical totals do not measure exclusive physical space.

The [recovery-map bundle](receipts.json) contains the owner release, selection
and preservation hashes, exact original-path to archive-member maps, original
references, operating log and release recipe. Fetch it to a fresh ignored path:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch workbench/tools/evidence/storage-20261016/loom-composition/receipts.json build/recovered/loom-composition-storage-maps
```

Each root's `.artifact-eviction-SHA.json` maps local names under `files` to an
`archive_member`. Recover that member using the corresponding reference under
`references/`, then copy it to the original relative path if needed. Maps also
remain locally beside the affected roots. The retained analysis and portable
replay instructions are in
[`maintained-composition-analysis-v1`](../../../../notebook/retired-spikes.md#hashtable-promotion-and-curation--october-17-2026).
