# Completed Loom v3 recovery copies

Loom released six export/recovery/replay roots after the context-sequence v3
native pair and portable recovery checks completed. The current maintained
campaign's source, workers and helpers were excluded. Compact evidence and unique verification
receipts/logs remain in place, as do all differing or unmatched files.

The existing eviction helper released 1,246,899,649 logical bytes
already covered by the analysis archive and the two raw worker archives. It
performed a fresh full S3 verification for each group, compared local bytes
again, and wrote a durable recovery map before unlinking. Small matching files
and the affected directories remain.
Logical byte totals do not claim an exclusive physical-space delta.

The [recovery-map bundle](receipts.json) contains `index.json`, the owner release,
the actual eviction log, original archive references and one exact old-path to
archive-member mapping per root/reference. Fetch it to a fresh ignored path:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch workbench/tools/evidence/storage-20261016/loom-v3/receipts.json build/recovered/loom-v3-storage-maps
```

For each local name in a map's `files`, use its `archive_member` with the
corresponding reference under `references/`, then copy the recovered member to
the original relative path if needed. Local `.artifact-eviction-SHA.json` files
remain beside each affected root. The original compact analysis evidence and
its portable replay instructions are in
[`context-sequence-analysis-v3`](../../../../spikes/loom-independent/evidence/context-sequence-analysis-v3/README.md).
