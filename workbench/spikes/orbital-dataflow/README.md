# Dataflow beyond map-reduce

This investigation asks which distributed execution shapes SixDB and other
extension-hosted applications need, and which small composition can support
them without putting application semantics into Orbital. It follows the
[dissemination study](../orbital-dissemination/README.md), broadening the unit of
interest from delivered messages to useful work, state and publication.

Start with the [recommendation](RECOMMENDATION.md), then the
[Spark-like extension application angle](SPARK-STYLE.md) or
[19 workload families](WORKLOADS.md). [Scope](SCOPE.md) records the boundaries;
[sources](SOURCES.md) connects local ideas and selected primary literature.

| Question | Focused evidence |
| --- | --- |
| Partition, replicate, filter or combine? | [Exchanges, joins, skew and legal rewrites](EXCHANGES.md) |
| What closes a loop/change, and what survives restart? | [Progress, incremental state and recovery](PROGRESS.md) |
| Can finite resources finish or resolve the flow? | [Execution, retention, spill and cancellation](RESOURCES.md) |
| When do distributed reuse and lineage help? | [Extension-hosted jobs and reconstruction](SPARK-STYLE.md#focused-reuse-and-reconstruction-probe) |

The four Python prototypes use exact semantic examples, logical counters and
one synthetic byte/tick model. They are not a database implementation or the
deferred [high-fidelity Orbital simulator](../../notebook/orbital-simulation.md).
No API, topology or performance target is selected merely by this spike.

Run all checks and reproduce the small retained comparisons:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/check.py
```

The [evidence guide](evidence/README.md) records selection, source hashes,
assumptions and individual reproduction commands. Orbital LEAD owns brief and
module integration; this directory owns the exploration and prototypes.
