# engine::directory read timings, Neoverse V2, 22 October 2026

[`engine-reads.sh`](../../engine-reads.sh) on `c8g.4xlarge` (Graviton4), release preset,
one pinned CPU, seven repetitions, 1M lookups per repetition. `engine_directory_check`
passed on the worker before timing.

- `before-inline-parts.csv`: job `20261022T111035Z-a13636a8`, commit `6519dd52`. There,
  `Control::part_count()` was out of line, so every Part access paid a call and a division.
- `engine-reads.csv`: job `20261022T111503Z-e5f4d7b7`, with the Part count stored in the Control.

Compared against the spike's `tree-inner512@orbital` rows in
[`../orbital-reads-neoverse-v2-20261022/inner.csv`](../orbital-reads-neoverse-v2-20261022/inner.csv):
same instance type, populations and loops. That reader used `engine::material`,
one grouped allocation per Object and hand-rolled searches. This is a comparison
across jobs, not an alternating A/B.
