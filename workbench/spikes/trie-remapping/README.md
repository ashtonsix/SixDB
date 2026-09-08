# Trie remapping

**Can a trie keep its efficient natural-key regions while using locally
assigned, ordered physical keys to pack sparse or collision-heavy regions?**

The first spike is concluded, 2026-09-08. It mapped the design space and ran
a resident access-method probe. The [findings](FINDINGS.md), implementation,
runner, and verified evidence remain here for reuse or challenge. The broader
design question remains open; no continuation or production interface is selected.

Opened from Ashton's proposal: logical and physical primary keys
usually share a prefix and can remain identical through the leaf. Where
natural suffixes produce an expensive shape, remap them to positions in a
segment, leaving room for growth. The proposed physical structure remains a
trie, including the possibility of 8/16-bit navigation.

The first measurements favour keeping both **fully ordered gaps** and **order
between blocks with flexible slots inside** in play. Density and split
boundaries can follow records while physical codes retain radix geometry.
The degree of order constrains column movement, lookup, and scans differently.
Natural routing remains a useful option, including at 5% and 20% occupancy in
the measured workloads. No threshold or representation has been adopted.

The broader questions remain available:

- **Transition points:** when is the natural trie still preferable at 20%,
  5%, or other densities? How do population, collision distribution, key
  length, cache residence, and workload change the crossover?
- **Whole-database remapping cost:** columns, secondary indexes, summaries,
  pending deltas, locators, retained versions, and durable/replicated bytes,
  as well as keys. Separate conversion cost from steady-state maintenance.
- **How much ordering is useful:** full logical/physical order, ordered blocks
  with flexible placement inside them, bounded disorder, or arbitrary placement
  with a separate ordered directory. Can logical keys cheaply predict a small
  physical search region without determining exact order?
- **Adaptation over time:** cost recovery after conversion, workload drift,
  reversibility, oscillation, and independently adapting neighbouring regions.
- **Operational consequences:** tail latency, temporary space, long readers,
  concurrent publication, shard movement, compression, and scan/gather costs.

- [Design questions](design.md): lookup, identity, movement, gaps, multiplicity,
  segment geometry, and effects on the rest of SixDB.
- [Literature](literature.md): relevant mechanisms and what they do not settle.
- [Experiments](experiments.md): small probes that can distinguish the choices
  before selecting Engine or Loom contracts.
- [Executable probe](probe.md): representations, workload and timer contracts,
  and one-command build/check/measurement.
- [First findings](FINDINGS.md): density, column rank, gap policy, partial order,
  identity, and conversion, with [retained evidence](evidence/local-arm-20260908/README.md).

Two costs deserve attention early. Finding a physical suffix from a logical
key is itself an indexing problem; that work must be counted. Also, spare
physical-key positions do not automatically create spare positions in packed
columns. Calico aligns those columns to occupied-key rank, which changes when
an earlier key is inserted even if no existing physical key changes.

Useful reasons to return include a concrete mixed-region routing proposal,
a column/secondary-reference layout that makes movement costs more realistic,
or a visibility and durability contract that lets us price online conversion.
The first probe compares separate region representations with 256-position
blocks; full 65,536-position containers and mixed-region discovery remain open.
The [experiment ideas](experiments.md) preserve those avenues without scheduling
the next study.
