# Initial Frame landing

Initial scope: construction/fitting/emission and checked immutable reads over
non-null u64 columns, using pack, bound and intervals (sizes/relative ends).
Private preparation, point mutation, mutable rebuild and their scratch remain
research. The [usage guide](../../../ikea/docs/frame/usage.md) and
[source/contract reference](../../../ikea/docs/frame/reference.md) are the consumer
entry points. [PROPOSAL.md](PROPOSAL.md) records the accepted boundary.

## Current qualification

The ten combined local checks pass: Frame description, manifest, guarded basic
images, checked readers, stage oracles, ordinary example, actual Orbital owner
example, shared selections, SeriesPack storage sizing and wide Intervals.
The owner example retains old readers through external replacement and both
sizes/relative-ends same-version conversions at G16/G64. It has no Frame writer.

The exact-u128 SeriesPack dependency is committed as 2731574e (findings 7ef59f6f).
The three review closures include the caller's weighted-u128-fit obligation,
a nonaligned K64 range past the 2^20-row fold, and accurate per-call timing labels.
The independent local fold witness also passes against the current compiled
SeriesPack archive. Shared selection/geometry reuse is 22a56d7c; stage extraction
is 42da0168. Final combined source qualification and control comparisons remain
pending; this document is not yet a consumer-readiness sign-off.

## Regression panel

[landing/](landing/README.md) defines the bounded public Frame / retained spike /
qualified native comparison, with real consumed results and the original input
oracles. Emitted bytes are not a full retained-heap account. Repeated queries
and eviction sweeps establish no cache tier. Writes and cost-model/recipe regret
studies do not block this read/build landing.

Final native receipts, source identity, results and any extraction corrections
will replace this pending section after the frozen runs complete.
