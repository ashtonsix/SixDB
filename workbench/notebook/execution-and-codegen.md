# Execution and code generation

Start with [Loom's execution choices](../../loom/README.md#choose-the-execution-path)
for current interfaces. [Loom's objectives notebook](loom-objectives-and-architecture.md)
keeps wider scheduling, ownership and resource questions exploratory. The routes
below connect memory overlap, complete consumers and compiler boundaries.

## Independent work, prefetch and useful dependency boundaries

Read [random access versus stream concurrency](../spikes/memory-characterisation/FINDINGS.md#random-access-and-stream-concurrency-are-different-axes)
with [Loom's dependency-boundary findings](../spikes/loom-execution/FINDINGS.md#choose-useful-dependency-boundaries),
then the current [Ring](../../loom/docs/ring.md) and
[stream](../../loom/docs/stream.md) guides. More Ring slots, more independent
chases, streaming bandwidth and more frequent service returns are different axes.
[Calico's AMAC report](../../../calico/loom/report/AMAC.md) retains its earlier
ring/hand-AMAC, short-range and stream comparisons; [the reference map](calico.md)
locates the broader source context. Its historical conclusions need their own
workload and methodology qualification.

| What needs explaining? | Evidence to consult |
| --- | --- |
| Dependency depth, underfill, live scalar/vector state, direct/manual/Ring | [Loom execution findings](../spikes/loom-execution/FINDINGS.md) |
| Generic hint selection and native specialization | [Hardware-hint study](../spikes/loom-hardware-policy/README.md), then [current hint guide](../../loom/docs/hardware.md) |
| Whether overlap repays tracking, binding and sampling | [Locality findings](../spikes/loom-locality-execution/FINDINGS.md), [execution policy](../../loom/docs/execution.md) |
| Work interleaved across requests and Objects | [MVCC interleaving evidence](../spikes/orbital-mvcc/performance/FEEDBACK.md#wider-request-interleaving-and-append-growth) and [current directory evidence map](../spikes/directory/PROGRESS.md#selected-evidence) |
| Useful owner service while native work runs | [Loom service experiments](../spikes/loom-execution/README.md) and [ready-Message comparison](../spikes/orbital-mvcc/performance/FEEDBACK.md#ready-message-service-during-large-dependent-reads) |
| Worker handoff, affinity crossings, placement and joins | [Local handoff](../spikes/orbital-local-handoff/README.md) and the messaging/affinity panels in [Loom execution](../spikes/loom-execution/README.md) |

Cold-promotion and compact-heat experiments are retained as
[rejected hashtable-specific probes](retired-spikes.md#hashtable-promotion-and-curation--october-17-2026).
Their tracking and maintenance costs matter; they do not define a container heat
API for Loom. Current policy is caller-qualified. The
[memory route](memory-and-lifetimes.md) connects these comparisons to actual
touched populations, residence setup and counters.

## Calls, owning returns and assembly prototypes

[Call-boundary findings](../spikes/call-boundaries/README.md) compare actual
separate-TU returns, caller frames, ownership and native dispatch.
[Ring dispatch](../spikes/call-boundaries/ring-dispatch/README.md) is the later
complete-consumer comparison. A small owning class can still return indirectly;
an output parameter can preserve the same store/reload. Setup calls and repeated
native visits deserve different treatment.

[Assembly calls](../spikes/assembly-calls/README.md) shows how to link real Ikea
and Loom functions, handle ownership in stationary caller frames, and inspect
resolved calls. Measure the complete directory operation, including those calls,
before accepting its C++ translation. The
[Object code-generation records](../spikes/orbital-mvcc/metadata/consumer/README.md)
keep actual read/write/convert frames and negative controls alongside memory
accounting; [metadata outcomes](../spikes/orbital-mvcc/OUTCOMES.md) provide the
shorter interpretation.

The earlier Ikea [wide-result ABI probe](../spikes/ikea-composition/probes/ikea-blocks/abi/README.md)
and [value-reuse sketches](../spikes/ikea-composition/value-reuse-sketches.md)
are useful lineage. Use current module author guides for today's call surfaces.

## Fusion, sharing and compiler resources

[Filter composition](../spikes/filter-composition/README.md) is the direct route
for fused versus shared/CPS bodies across AVX2, AVX-512 and NEON, including
runtime regressions and compiler-capacity failures. Neither uniform outlining nor
CPS at every operation follows from those results. The
[Ikea composition map](../spikes/ikea-composition/README.md) connects region/grain
choices to its deeper integer, bitset and heterogeneous probes.

[Calico build lessons](build-iteration.md) separates shared compilation from
calling protocol; [compile probes](../tools/compilation.md) expose time, memory,
code size and emitted assembly. Use [LLVM-MCA](../tools/mca.md) for appropriate
leaf scheduling questions, then explain the complete measured caller.
The [build-speed survey](../spikes/build-speed/README.md) attributes current
clean/incremental build time, per-TU compile memory and output size, mostly to
combinatorial Ikea tests and full debug info.

[Executable placement](../spikes/executable-placement/README.md) preserves the
case where relinking changed costs without changing caller/kernel instructions.
Its [recovery account](../spikes/executable-placement/layout-recovery.md) motivates
retaining selected objects, archives and link commands when a follow-up needs
byte-identical relinking. It does not justify keeping every build tree.
