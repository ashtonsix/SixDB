# Released review output recovery

These seven completed review trees were released by Orbital and Loom for
archive-verified local retirement. [index.json](index.json) identifies the old
`build/reviews/` prefixes, bounded archive parts and actual matching files
removed. Sources, recipes, patches, diagnostics and qualification context were
archived together. Git administrative files, links, pins and fresh files were
excluded. Existing embedded Git worktrees remain local; their regular source
bytes are also present in the archive.

This checkpoint makes no new performance or correctness claim.
`orbital-primary-choice` remains a qualified **private, unselected**
alternative-discard/promotion experiment with policy undecided. Its handoff,
patch, probe/source and read/sanitizer evidence have not been promoted or
declared superseded by storage maintenance.

Each part uses the ordinary [artifact format](../../../artifacts.md), was
uploaded/downloaded and checksum-verified, and was read from S3 again before
unchanged local copies could leave. Durable local `.artifact-eviction-*` maps
were written before unlinking. Edited, pinned, unarchived and Git-worktree files
remain in place.

Member names preserve their path relative to `build/reviews/`. Recover an exact
file into a new ignored directory:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch workbench/tools/evidence/storage-20261016/reviews/part-001.json build/recovered/review-part-001 --file orbital-primary-choice/HANDOFF.md
```

The index lists which parts contain each review tree. Its `lookup.json` reference
recovers `paths.json`, an exact original-path-to-part lookup, plus operational
selection/eviction receipts. Fetch whole parts into separate new directories
to recover all the context; their member paths retain the original hierarchy.

The historical paths cited by `prepared-feedback.json`, including
`build/reviews/orbital-stage2-reattempt/projected-metadata-source` and
`build/reviews/orbital-stage2-reattempt/prepared-feedback`, are covered by this
lookup. Recovery preserves the historical bytes and their existing qualification
limits; it does not turn them into the current implementation.
