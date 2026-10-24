# Research notebook

Start from the question, rather than the name or date of a spike. These reading
routes connect current module capabilities, experiments that explain them,
negative results and questions still worth investigating.

| What are you working on? | Reading route |
| --- | --- |
| Index routing, sparse regions, duplicate keys, summaries or buffered writes | [Indexes and maintenance](indexes-and-maintenance.md) |
| Packed records, filtering, format conversion, layout selection or retained plans | [Representations and planning](representations-and-planning.md) |
| Interleaved memory work, prefetch, Ring/stream execution, ABI or compiler expansion | [Execution and code generation](execution-and-codegen.md) |
| Allocation, placement, cache residence, retained readers or physical ownership | [Memory and lifetimes](memory-and-lifetimes.md) |
| Transaction semantics, contention, recovery or distributed execution | [Orbital questions](orbital-ideas.md), [transaction workloads](transactions/README.md), [dataflow workloads](dataflow-workloads.md) |
| Networking, dissemination, placement economics or delivery obligations | [Consensus and networking](consensus-networking.md), [source survey](dissemination/README.md) |
| Customer value, adoption or how teams share work | [Commercial questions](commercial.md), [sociological questions](sociology.md) |
| Fair comparisons, real inputs, hardware counters or experiment tooling | [Comparative benchmarking](../benchmarks/comparisons.md), [datasets](../datasets/README.md), [tools](../tools/README.md) |

[Open questions](ideas.md) keeps loose leads that cross those routes. For a
particular study, use the [complete spike catalog](../spikes/README.md).
[Calico's reference map](calico.md) locates earlier work; its designs and benchmark
claims do not carry forward automatically.

## Which account answers which question?

Module guides own implemented interfaces: [Ikea](../../ikea/README.md),
[Loom](../../loom/README.md), [Orbital](../../orbital/README.md),
[Engine](../../engine/README.md) and [Shore](../../shore/README.md).
The [design area](../design/README.md) holds wider proposals, including services
that are not implemented. A compiling prototype is not evidence of adoption.

Spikes own experiments and their qualification. Read the findings before an
old plan or runner; a completed or rejected experiment can still answer a new
question. Each result belongs to its own workload, measured operation, source
and machine. Follow its evidence reference for exact reproduction.

The notebook holds questions, source surveys and connections across experiments.
The [historical Orbital map](orbital/reference/README.md) locates earlier briefs
and verification work. [Ashton's scratch space](orbital/ASHTON-SCRATCH.md) remains
his open working outline; agents add to it only at his explicit request.

## Earlier work and recovery

[Retired investigations](retired-spikes.md) explains what replaced old trees,
what was retained and how to recover their sources. The
[first research retrospective](research-experience.md) records why useful inputs,
small runs and selected evidence replaced heavier capture habits.
[Storage lifecycle](../spikes/storage-lifecycle/README.md) investigates why those
intentions still allowed repeated disk exhaustion and tests replacement tooling
against independent adversarial workloads.
[Build lessons](build-iteration.md) records the earlier Calico compile/CPS
experiments; current commands belong in [BUILDING](../../BUILDING.md).

These routes keep interpretation near its owning study rather than collecting
tables into a new combined scorecard. When new work changes a conclusion, update
that owner and its incoming route; a second chronological report rarely helps
the next person find it.
