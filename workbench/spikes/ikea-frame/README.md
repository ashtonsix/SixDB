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

- **Representations** follow Orbital's data model: Frame formats are
  representations of one Object, logically identical, convertible, with writes
  applied deterministically. Byte identity across replicas is not required.
- **Structure and scope**: SeriesPack/TuplePack, not FastLanes, are the
  structuring idea. Strings (FSST included) are handled separately; Frame may
  hold prefixes and lengths while StreamPack does most string work. Sets,
  bespoke structures and string collections are Frame's peers.

## How this spike works

Ashton's direction (2026-10-22):

- **Expansive, not thin.** The spike identifies architectural seams through
  prototyping across the design space. No vertical slice is chosen up front;
  scope cuts are considered only when moving from prototype to graduation, or
  under overwhelming friction during prototyping.
- **The brief says what to disambiguate**, not what the answer is. Questions
  that experiments can settle stay in [QUESTIONS.md](QUESTIONS.md).
- **Find the metadata knee.** Prototyping looks for the point where most of
  the performance gain across a wide range of operations is realised, before
  metadata hits diminishing returns. Genuine trade-offs become curated presets
  that Engine composes or chooses from.
- **Performance bar.** At least 2× faster than a strong baseline such as Polars
  or DuckDB for at least three examined operations per family, preferably with
  less memory. The 2× is headroom for Engine; keep going until headroom is
  exhausted rather than stopping at the target. At least parity with Calico.
  Encoding, decoding and compression get the most attention at first.

## Contents

- [Prior-art survey](SURVEY.md): what Calico exploited, what current Ikea
  already supplies, and the most relevant external work, organised as an
  encodings × operation-families capability matrix.
- [Data encoding, rev 1](ENCODING.md): proposed pipeline, geometry, exceptions
  versus overrides, write and read paths, and the Ikea primitives each stage
  reuses. The current step; full Frame scope is set aside.
- [Writes and Orbital](WRITES.md): what Orbital, Ikea and `engine::directory`
  already establish about epochs, drafts, take versus copy, byte damage, locks
  and coherent reads, and what that means for the encoding.
- [Research questions](QUESTIONS.md): every open experiment and design
  question, with why it matters and where it came from.

Related routes: [representations and planning](../../notebook/representations-and-planning.md),
[residual bounds](../../notebook/pfor-bounds.md), [Boundlist](../boundlist/README.md),
[packed filters](../packed-filters/README.md) and the
[Calico reference map](../../notebook/calico.md).
