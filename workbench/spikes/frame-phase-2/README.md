# Ikea Frame, phase 2

Design spike for `ikea::frame`, reopened on 2026-10-23 after
[phase 1](../frame-phase-1/README.md) fell short in breadth and quality. The
philosophy carries over: an encoding pipeline broken into stages Engine can
compose, primitive operations running directly on encoded data, priors that
accelerate analysis, 2^16-row parts with groups and blocks below, and headless
descriptions with damage, aggregates and effects published to the consumer.
Engine's analyser will later solve (data distribution, access patterns) →
(encoding, query plans); Frame curates the primitives it chooses from.

## What changes from phase 1

- **Every target has a mechanism.** A win must be explained by what it removes
  and where that removal pays, at each residence tier, against the strongest
  baseline for that operation. Polars is one baseline among several.
- **The split starts from the encoding stages.** Fused stages (Ikea owns the
  composition) and composed stages (Engine chooses per data and workload) set
  the seams and the division of work.
- **Leaves and the substrate come first.** Phase 1's leaves were 6–10×
  slower than a native narrow array at L2, and its points 10–20× slower.
  Dispatch turned out to be a small part of that
  ([substrate probe](substrate/README.md)).
- **All operation families.** Phase 1 covered SUM and COUNT on integers.

## Contents

- [Stage classification](STAGES.md): which stages are fused, weighted or
  composed, how sticky each is, and what phase 1 settles.
- [Mechanisms and targets](MECHANISMS.md): residence tiers, hardware priors,
  thirteen "how we win" mechanisms, the baseline roster and targets per
  operation family, stage and tier, including accepted losses.
- [Execution substrate](substrate/README.md) (front B): dispatch, selections,
  residence, unpredictable access and staging, point-read attribution.
- [SeriesPack remeasured](seriespack/README.md): SeriesPack's own reader under
  every preset against native, the starting gaps for SeriesPack's performance work.
- [SeriesPack performance](seriespack/performance.md): range SUM, COUNT and
  filter kernels over SeriesPack's own bytes (Local popcounts, striped tables,
  bit-sliced compares), compose-versus-fuse findings and the remaining gaps.
- [Run boundaries as composed stages](runs/README.md): Intervals forms, the
  bounds stage and SeriesPack packing, with the bytes/speed frontier Engine chooses on.
- [Bounds stage](bounds/README.md): choosing lo and width by guess, verify
  and refine against an exact oracle, priced on SeriesPack's tiles.
- [Integer body leaves](bodies/README.md) (front C): leaves for constant, runs
  and packed bodies across operation families.
- [Cross-thread seams](SEAMS.md): the contracts every thread shares, with owners.
- [Partition](PARTITION.md): seams, proposed internal packages, the research
  fronts, Ashton's decisions and the first Claude/Codex division.

Phase 1's findings and negative results remain the evidence base; this spike
cites them rather than restating them. Strings, including FSST, are out of
scope.
