# Layout analyser

The analyser should choose **representations together with programs for reading
and maintaining them**. A filter, prefix, exact core, residual, patch stream or
directory changes which information an operation must acquire and when it can
acquire it. Plane splitting, packing, placement and packet arrangement realize
those choices. Their value comes from complete operations over a workload and a
lifetime, not a universal score for a byte width.

This spike investigates that direction through a connected record and bucket
problem. Expensive analysis may produce a small family of representations and
programs; bounded local fitting adapts it to data and demand in a key region;
cheap selection chooses how to use a retained image. New advice need not require
historical re-encoding.

The original macro/micro distinction remains a division of scope. Macro considers
maintained facts, micro-indices and workload objectives. Micro realizes admitted
alternatives in actual codecs and hardware conditions, and can return a frontier
or a counterproposal. Both can split planes. Training versus selection is a
separate axis. We can investigate objective definition and consumption now;
production from real Engine telemetry depends on work not yet defined.

## Read the argument and its material

1. [Design synthesis](design.md): how conditional information needs lead to joint
   representation/program search, contextual costs, reusable fitting and history.
2. [Connected worked case](case-study.md): follow the 65B record through reads,
   filtering, byte relocation, packet choices, updates and changing key regions;
   use a nested bucket/directory to test the account's reach.
3. [Research dossier](prior-art.md): the original filtering, three-array and PFOR
   material alongside current SeriesPack/TuplePack, Bec256, hardware and literature;
   what each contributes and how the mechanisms connect.
4. [Next experiments](experiments.md): test the joint account, its reuse and its
   value while historical images remain, rather than widening isolated benchmarks.

The synthesis was rebuilt on 2026-09-14 after the first empirical pass proved
better at exposing local counterexamples than assembling an overall account.
It proposes a direction for research; it does not specify an Engine API.

## Supporting work

- [First findings](findings.md) and [evidence/reproduction](evidence/README.md):
  measured prefix/bucket/spatial consumers, modeled Boolean refinement and offline
  palette selection. Their captured sources and limitations remain unchanged.
- [Geometry and original fixtures](examples.md): 1..128B slots, 63/64/65B prefix
  boundaries, variable bucket capacity and split/dense/padded spatial placement.
- [TuplePack reference](tuplepack-reference/README.md) and
  [search note](tuplepack-search.md): transferred finite exhaustive analysis,
  operation costs, checks and detailed earlier reading.

This task owns the synthesis, candidate analysis and derived consumer experiments.
Ikea owns representation laws and local operation capabilities. Loom owns
[memory-characterisation](../memory-characterisation/README.md) and hardware
interpretation. Original runtime evidence remains in
[tuple-layout](../tuple-layout/README.md); the filtering and Calico sources remain
in their owning homes. The dossier distinguishes current capabilities, retained
measurements and proposed extensions.
