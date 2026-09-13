# Layout analyser

Design spike opened 2026-09-13. **Given the information a data structure must
preserve, the operations it must support, and a workload cost function, which
planes, layouts and operation recipes should Engine choose?** Start with small
micro-analysis problems whose alternatives can be enumerated and measured.
The proposed module and interfaces below are research questions, not an Engine
contract or a selected implementation.

Two useful scales of analysis are emerging:

- **Workload / macro analysis** considers micro-indices, redundant summaries,
  field groups and the value of co-location across operations. It supplies
  semantic alternatives, constraints and objectives for more local decisions.
- **Hardware / micro analysis** takes a data-unit description and supplied
  objective, then explores partitioning, bit/byte placement, capacity, padding
  and executable recipes in a particular hardware and access context.

Both can split planes. Macro might separate a filter from its records; micro
might split high-order evidence from residual bits within that filter, or keep
the two streams adjacent. Neither scale dictates when the work runs. Expensive
training, fast selection and bounded local fitting can occur at either scale.
A local fit may revise a partition if the supplied alternatives permit it.

Initial emphasis is micro analysis and the **definition and consumption** of
cost functions. Producing reliable macro objectives from a production workload
depends on query, index and mutation mechanisms SixDB has not yet defined.
Authored workloads let us investigate that seam now without inventing them.

## Read and work here

- [Design](design.md): candidate semantics, cost consumption, reusable training,
  palettes, local fitting and retained historical representations.
- [Worked examples](examples.md): moving string-prefix bytes around a 64B
  boundary, a variable-capacity hash bucket, and spatially adjacent planes.
- [Experiments](experiments.md): falsifiable hypotheses and bounded comparisons.
- [Prior art](prior-art.md): local evidence, its limits and primary literature.
- [TuplePack reference](tuplepack-reference/README.md): the transferred small
  exhaustive analyser, executable checks and cost-input examples.
- [TuplePack search note](tuplepack-search.md): the transferred detailed reading,
  operation-recipe hypotheses and larger placement experiments.

The original cache-line question is a geometry baseline, not the objective:
for a uniformly selected densely packed slot of width `w`, starting at byte
zero on a 64B-aligned base, the steady-state full-slot expectation is
`1 + (w - gcd(w,64))/64` distinct lines. Ranking `w / expectation` rewards useful
payload per demanded line, but does not price latency, scans, dependency chains,
bandwidth, writes or padding. [The examples](examples.md#geometry-baseline)
make the assumptions and nonlinear alternatives explicit.

## Responsibility and evidence

This investigation owns Engine layout-analysis hypotheses, candidate selection
and consumer cost experiments. Ikea owns supported representation laws and local
operation capabilities; [its integration guide](../../../ikea/docs/integration.md)
describes the current owner obligations. Loom's
[memory-characterisation investigation](../memory-characterisation/README.md)
keeps hardware diagnostics and their interpretation. Layout-specific derived
measurements belong here and refer back to that evidence.

The [TuplePack investigation](../tuple-layout/README.md) retains kernel/runtime
measurements and their original provenance. Its bounded analyser and search note
now live here; the old paths retain discovery and command compatibility.
The filtering and Calico studies in
[prior art](prior-art.md) remain in their owning homes. The new spike does not
establish that their prototypes are production interfaces.

Opening this spike adds design and reproducible starting points; it does not
claim new hardware measurements or an implemented general layout analyser.

Opening verification on 2026-09-13 passed all 12 reference checks on Linux,
replayed 392 retained cost rows into the 28-candidate ranking, and reproduced
the 56-plan mixed reports. Old-path and canonical synthetic rankings agree.
The [reference guide](tuplepack-reference/README.md) documents the replay route;
code/fixture and historical-evidence hashes were preserved during relocation.
