# Memory and lifetimes

[Orbital allocation](../../orbital/README.md#allocation) and
[Object material](../../orbital/docs/material.md) own current mechanisms.
[Buffers and allocation](buffers-and-allocation.md) keeps the wider discussion
of logical state, representations, Parts, memory views and ownership. These
questions intersect, but physical retention does not itself supply immutability,
version authority or permission to write.

## Residence, demand and hardware behavior

| Question | Start here |
| --- | --- |
| Construct hot, large touched-population or conditioned-access scenarios | [Residency tools](../tools/residency.md) and [memory-characterisation findings](../spikes/memory-characterisation/FINDINGS.md) |
| Gather available hardware counters | [PMU probe/helper](../tools/pmu/README.md); the [memory study](../spikes/memory-characterisation/README.md) records actual ARM event refusals |
| Understand dependency chains versus streams | [Memory concurrency findings](../spikes/memory-characterisation/FINDINGS.md#random-access-and-stream-concurrency-are-different-axes), then [execution routes](execution-and-codegen.md) |
| Interpret adjacency, training and the entry-barrier confound | [Corrected adjacency study](../spikes/memory-characterisation/ADJACENT.md), rather than isolated older numerical claims |
| Compare machines, ISA choices and price assumptions | [CPU snapshot](../spikes/ikea-cpu-comparison/README.md), [Neoverse V3 comparison](../spikes/ikea-neoverse-v3/README.md) |

Dataset size, issued hints, logical counters and actual service events describe
different things. Use the owning study's qualification; do not turn a nominal
allocation size or available cycles counter into a residency claim.

## Geometry, placement and reclamation

| Question | Earlier experiment and later consumer |
| --- | --- |
| Descriptor addressing and independent growth | [Memory-descriptor findings](../spikes/memory-descriptors/FINDINGS.md), [virtual growth](../spikes/memory-descriptors/VIRTUAL.md) |
| Page sharing, identity placement and compaction | [Placement findings](../spikes/memory-descriptors/FINDINGS.md#placement-and-compaction), [small-slot packing](../spikes/memory-descriptors/ALLOCATION.md#small-slot-packing), [palette geometry](../spikes/allocation-palettes/geometry.md) |
| Class palettes, slack, growth and actual Part ownership | [Allocation palettes](../spikes/allocation-palettes/README.md), then [directory memory accounting](retired-spikes.md#index-directory-spikes--october-2026) and [native allocation/co-lifetime control](retired-spikes.md#index-directory-spikes--october-2026) |
| Reuse preference versus cold-first reclamation | [Reuse-temperature findings](../spikes/allocation-reuse-temperature/FINDINGS.md): selection omitted temperature/CPU ranking; reclamation retained a different use for temperature |
| Coarse Groups, live tails and physical drain | [Live-tail study](../spikes/allocation-live-tail/README.md), [directory drain evidence](retired-spikes.md#index-directory-spikes--october-2026) and [repeat qualification](retired-spikes.md#index-directory-spikes--october-2026) |

The modeled placement scripts and serialized descriptor prototypes are useful
counterexamples, not interchangeable allocators. Grouping, class routing,
identity placement, growth and retention can each change waste. A compact home
does not settle VMA count, mapped slack or an unrelated reader's co-lifetime.
Complete consumer observations belong with their actual source and workload.

## Logical cuts, interpretation and escaping physical access

[Object integration outcomes](../spikes/orbital-mvcc/OUTCOMES.md) gives the
short route through coherent later discovery, absence, private preparation,
alternative replacement, changed Part counts and exported Parts. The
[retained integration checks](../benchmarks/retired-drivers.md#current-integration-checks)
exercise production modules; the former performance drivers are retired.

[Metadata findings](../spikes/orbital-mvcc/metadata/README.md) distinguish
ownership/authority, immutable interpretation and small call packets. Its
[complete collection census](../spikes/orbital-mvcc/metadata/composed/README.md#initial-collection-inventory)
includes shared plans and allocator capacity. Read it with the
[Schema construction negative](../spikes/orbital-mvcc/performance/FEEDBACK.md#transient-schema-construction-phase):
resident-byte reduction was rejected when repeated construction became much slower.

For mutation, append and conversion, the
[representation route](representations-and-planning.md) leads to body/padding
coverage, source/target overlap and copy-grain evidence. Earlier prototypes
using one completed external owner are historical controls; current source
composition belongs in the [material guide](../../orbital/docs/material.md).

Local Object cuts, journal Epochs and physical availability remain distinct.
The [wider design](../design/orbital/DESIGN.md) includes Journal/Port proposals
without supplying those production services. Durable retention and recovery
questions live with [Orbital's research questions](orbital-ideas.md#failure-recovery-and-retained-evidence)
and [formal models](../../orbital/spec/README.md), under their stated assumptions.
