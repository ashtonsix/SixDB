# Hashtable

A new investigation of packed sets, maps and multimaps, composing SeriesPack
fingerprints, TuplePack bucket fields and StreamPack overflow/value chains.
The intended submodule name is `hashtable`. This directory is a spike, not a
production format or a promise that every experimental option will survive.

The goal is broad throughput advantage over strong existing tables while
using less memory for sets/maps. Dependent lookup latency targets at least parity
in L1/L2, with LLC parity desirable; a small DRAM latency deficit is acceptable
only with compensating throughput advantage. Overlapping misses through Loom
should provide the larger gains for large working sets. Correctness, instruction
cost, memory, ownership and interface
simplicity constrain each candidate together.

- [Design](DESIGN.md): current choices, invariants and experiments that can change them.
- [Experiments](PLAN.md): comparisons, workloads, accounting and outstanding work.
- [Review](REVIEW.md): independent challenges and disposition of the supplied critique.
- [Reading](READING.md): strong competitors and relevant primary literature.
- [Findings](FINDINGS.md): results from the fresh experiments and their limits.

This starts afresh. The sibling repository's
[quotient experiments](../../../../calico-simple-containers/flatmap16-prototype/experiments/unified_quotient/README.md)
and SixDB's [bucket composition probe](../layout-analyser/bucket/README.md)
supply useful mechanisms and counterexamples, not inherited conclusions.

Spatial-prefetch measurement already exists in
[memory characterisation](../memory-characterisation/METHOD.md#spatial-fetch-learning-lookahead-and-retention),
including single-demand neighbour probes and fleet evidence. The
[layout study](../layout-analyser/spatial/README.md) separately measures 96-byte
consumer layouts. Its line-touch accounting does not establish hardware fetch
behaviour. Loom found speculative stimulus contamination in the old probe and
runtime calibrator, corrected their fencing, and replaced the old adjacent
observations with [controlled native measurements](../memory-characterisation/ADJACENT.md).
The runtime now exposes qualified observations in its cold snapshot. Hashtable will consume
qualified observations, including unknown results, rather than infer behaviour
from a CPU name.
