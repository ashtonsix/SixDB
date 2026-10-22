# Ikea Frame

Design spike for a proposed `ikea::frame` submodule: the container for
SeriesPack and TuplePack that packages encodings and the operations Engine runs
over them. Nothing here is implemented; no module code has changed.

## The question

Which operations can run directly on encoded or partially decoded data, at what
cost, and how should Ikea make those capabilities legible to Engine? Ikea
supplies the vocabulary and curated presets (where fusion pays or choice adds
little); Engine defines pipelines and decides layouts from its actual workload.
The goal is exploiting encoded structure, not only avoiding decode overhead:
for 64-bit values sharing a 56-bit prefix P with 8-bit suffixes S,
`Σ X_i = N·P + Σ S_i`.

## Starting context from discussion with Ashton

These are working notes from the project chat (2026-10-22), not settled contracts.

- **Operation families** Ikea will support: encoding (pack, unpack, transcode);
  data movement (read, write, gather, scatter, append); comparison and selection
  (compare, predicate → mask, filter); search and lookup (find/indexof, bounds,
  rank ↔ offset, hash probe); transformation (arithmetic, bitwise, hashing);
  reduction and scan (aggregates, grouped/segmented reductions, prefix/windowed
  scans); reordering (permute, partition, sort, merge, compact). Joins are
  Engine's: Ikea feeds them. Frame owns a significant portion, including the
  public query interface for SeriesPack and TuplePack.
- **Encoding pipeline, rev 2** (a choice tree; most blocks skip most stages):
  analyse from priors with bad-guess fallback → early exits (constant, runs) →
  type conversion (decimal floats, ALP-style fixed point checked by round trip,
  offset binary only where order must survive) → splitting (category routing
  fused with null elimination, standalone null compaction, high/low split with a
  prior-chosen split point) → value mapping *or* prediction (rerank vs
  none/previous/linear/category residuals) → Boundlist-style checkpoints
  (Sizes or Relative Ends; Boundlist likely renamed for a third consumer) →
  frame of reference over unpatched values → patches → packing.
- **Writes**: SeriesPack writes in-bounds values in place; other values join an
  outlier list with allocated slack, and exhausting slack forces compaction.
  Patches come after delta. Checkpoints are ordered and regular; outliers can
  arrive later, unordered.
- **Layout hierarchy**: a frame fills a part of 2^16 physical key positions,
  made of blocks (typically 256 positions). Hot in-memory parts use groups of
  about 4096 positions with their own virtual-memory pages (the part is one
  Orbital allocation) and their own patch buffers, localising compaction,
  churn, growth and page-level COW/remap.
- **Wider responsibilities**: row-level access control and protections. Frame is
  where low-level kernels meet user requirements and the Engine interface.

## Contents

- [Prior-art survey](SURVEY.md): what Calico exploited, what current Ikea
  already supplies, and the most relevant external work, organised as an
  encodings × operation-families capability matrix.

Related routes: [representations and planning](../../notebook/representations-and-planning.md),
[residual bounds](../../notebook/pfor-bounds.md), [Boundlist](../boundlist/README.md),
[packed filters](../packed-filters/README.md) and the
[Calico reference map](../../notebook/calico.md).
