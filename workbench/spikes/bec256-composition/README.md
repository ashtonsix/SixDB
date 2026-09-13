# Bec256 with current Ikea

Reopens the [heterogeneous bitset investigation](../ikea-composition/probes/ikea-heterogeneous/README.md)
using the implemented SeriesPack and TuplePack interfaces. Bec256 remains a
headless 256-position block. Population, byte size, directory schema, placement
and representation selection belong to the caller.

The questions are concrete:

- Can exact bounded replacement retain the fast codec's useful performance
  while preserving neighbouring bodies and reporting issued-byte effects?
- How do TuplePack records and SeriesPack population/length planes compare for
  larger bitsets under sparse, short-range and full-scan access?
- How do checkpoint spacing, point resolution and retained native metadata
  change that answer? Reconstructing an address can require inactive predecessors.
- Does a shared native pair consumer preserve the benefits of composition, and
  what does a separate continuation boundary cost?
- What do statistical size estimates and a caller-controlled encoding shortcut
  save, and what compression opportunities can the shortcut miss?

The comparison keeps headless bodies identical across directories. Native pair
decoding accepts independent addresses and produces a 512-bit bitset; neither
directory layout nor block adjacency is implied. Experiment choices here are
not public Ikea presets or an Engine analyser policy.

The supported codec lives in [Ikea](../../../ikea/docs/bec256/usage.md); this study
owns comparative policies and evidence. Historical probes and their source archives stay intact.

The [codec findings](codec-findings.md) explain the reset of the ordinary write
boundary, register bitstream assembly, rejected approaches and the remaining ARM
exact-write cost. The [Ikea guides](../../../ikea/docs/bec256/usage.md) own supported
caller and native contracts.
The [metadata investigation](metadata-findings.md) defines directory choices,
address reconstruction, prefilter semantics and the scope of the comparisons.
The [whole-bitset extension](whole-bitsets.md) restores independent Boolean
operands, complete/selected output effects and enclosing storage estimates.
