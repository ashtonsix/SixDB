# Thinking about Loom objectives and architecture

2026-09-13. Initial exploration at Ashton's request, without selecting an
architecture. The [module outline](../../loom/README.md) supplies the current
scope; the questions below could change its seams. No runtime or analyser is
being promoted by this note.

## What would we want Loom to achieve?

[SixDB's direction](../../README.md) puts mixed transactional and analytical
work, ingestion, distribution and durability in the same database. Loom's
objectives therefore need to describe what happens when those activities run
together. A pointer-chase throughput improvement is useful evidence, but it
does not tell us whether a transaction, a scan and a durability backlog can
make satisfactory progress on the same machine.

Possible objectives, with their tensions still unresolved:

| Objective to explore | What makes it difficult |
| --- | --- |
| Complete useful database work efficiently | Speculation, decoding, queueing, retries and cancelled work can consume resources without advancing the requested result. Busy cores alone do not measure success. |
| Give short requests predictable latency while sustaining bulk work | Larger batches amortize overhead and may preserve streams; they also delay opportunities to serve another request. Which latency goals does Engine expose? |
| Keep admitted work able to progress within finite resources | A suspended operation can retain input leases, output space, version history and unpublished changes while needing more resources to finish. |
| Preserve useful locality while sharing spare capacity | Moving work may shorten its queue but lose private-cache contents, disturb streams or introduce ownership transfers. Leaving it in place may strand capacity. |
| Contain interference between foreground work and maintenance | Flush, recovery, replication, ingestion and compaction can compete with queries, yet deferring some of them indefinitely can damage foreground progress later. |
| Adapt to different machines and changing conditions at tolerable cost | Measurements have uncertainty; workload phases change; exploration itself consumes capacity. A previous optimum can cease to apply. |
| Make decisions understandable and controllable | Rich accounting and flexible policy have their own execution, memory and implementation costs. Which observations repay that cost? |

There is no proposed weighting of these objectives yet. Even the unit of
fairness needs thought: query, transaction, session, tenant, shard, or a causal
group including its background work. Cost per completed request and performance
per provisioned machine may suggest different choices from maximum throughput
on an otherwise idle core.

## How much scheduling should a piece of work encounter?

The [Ikea integration discussion](../spikes/ikea-composition/semantics-and-integration.md#loom-resources-progress-and-retained-state)
already distinguishes immediate native execution, short DRAM interleaving and
completion-driven waits. That distinction offers a starting vocabulary. It
leaves substantial choices about how the mechanisms compose.

An execution region could run directly over admitted bytes; a compact set of
independent dependent-access contexts could rotate between address discovery
and consumption; a longer wait could retain stable operation state until a
buffer, I/O result or resource grant becomes available. DRAM prefetch supplies
no readiness event, so rotating such a context and awaiting an I/O completion
cannot have identical readiness semantics. They might still share accounting,
placement information or authoring adapters.

Where should each boundary fall? A hash lookup may expose several address
discoveries. A TuplePack scan may have a long useful native region. A lookup
that discovers a nonresident value may move from the first situation into the
third. The cost of saving state, changing code paths, reconstructing bindings
and filling or draining a small batch belongs in this comparison.

Storage unit, acquisition window, native execution grain, scheduling quantum,
number of independent chases and number of ordered streams can all differ.
Would Loom expose them separately to a driver, infer some from an access
description, or choose among recipes supplied by Engine? In particular, the
number of outstanding hardware loads need not equal the number of ring slots:
each slot's useful work and dependency structure also matter.

The [Calico Loom report](../../../calico/loom/report/AMAC.md) is a useful warning
from prior work: interleaving helped some independent miss-bound consumers,
while small or already streamed shapes could lose; end-to-end gains also
depended on the enclosing address-resolution path. Its implementation and
numerical tuning remain prior evidence, not SixDB defaults.

## Who owns work, and where can it run?

Several arrangements are worth keeping in view. These choices concern
different parts of the design and need not come as complete packages.

| Architectural axis | Alternatives and the question between them |
| --- | --- |
| Ordinary work ownership | Per-query work queues, workers owning local queues, or queues associated with data partitions. Which keeps coordination and locality costs low under skew? |
| Coordination domain | Independent workers, coordinators per shared-cache/NUMA domain, or wider coordination. How much information must cross domains before load balancing becomes worthwhile? |
| Balancing action | Move a task, move an unstarted batch, route a request to a data owner, replicate immutable data, or change placement of future allocations. Which actions are legal and repay their cost? |
| I/O completion handling | Resume at a chosen home, resume near the completion, or make ready work available to a bounded set of workers. How much affinity survives a long wait? |
| Execution mechanism | Explicit state machines, coroutine adapters, task callbacks, or specialized loops around bounded native bodies. What does each cost at the actual suspension boundaries? |
| Resource control | Independent limits per resource, shared admission decisions, or local limits with a wider budget. How are coupled demands and progress dependencies handled? |

A worker affinity hint might mean several things: required execution authority,
cheap access to resident data, a warm prepared binding, or merely recent history.
Engine's shard ownership and mutation rules can make some placements illegal;
an empirical locality preference can only rank the placements that remain.
Could that distinction be expressed without every task carrying a large
universal descriptor?

The [memory characterisation findings](../spikes/memory-characterisation/FINDINGS.md#smt-cache-domains-and-numa-materially-change-the-answer)
make “same NUMA node” an insufficient locality description for the tested
Zen 5 host. They also show a peer-cached line costing more than the matched
cold control across some domains. Those observations invite a cost model
conditioned on the producer, consumer, memory placement and sharing pattern;
they do not establish a direct L2-to-L2 route or a fixed ordering of all tiers.

For a producer/consumer pipeline, we could compare keeping both ends on one
core, batching transfers within one LLC, and spreading them farther apart.
Reader fanout and mutable ownership ping-pong are different workloads. An SMT
sibling might be attractive for one pairing and disruptive for another. How
much of this can be captured by a few workload classes, and when does it need
feedback from the actual operator?

## What does owning the buffer pool involve?

Loom's current outline gives it the buffer pool. The design question is how
much responsibility that entails: residency accounting, allocation and
placement, admission, victim selection, prefetch, writeback scheduling, or
some combination with policy supplied by Engine and services from Orbital.
Calico's narrower residency controller left victim eligibility and selection
outside Loom; that boundary is useful to examine, not automatically inherit.

The [Ikea owner guide](../../ikea/docs/integration.md) already separates the
intended data version from the lifetime of its resident addresses. Physical
cache warmth is another, weaker condition. A version can remain valid while
its mapping is released; reacquiring resident bytes does not by itself prove
that an old binding denotes the intended version. These distinctions matter
when considering task migration, eviction and resumption.

An explicit acquisition interface could expose bounded demands and make
admission visible to scheduling. A fault-mediated mapping could allow more
ordinary pointer access but move some waits into the fault path. A hybrid
could prepare likely accesses explicitly while retaining fault handling for
other cases. The intended Orbital UFFD/COW mechanism is relevant to page
versions; it does not by itself settle residency policy or safe scheduling
stops. What are the costs and failure behavior of each arrangement for reads,
in-place writes and long-lived snapshots?

The pool may also have several useful representations of related data:
compressed backing, decoded working buffers, temporary results, and cached
remote bytes. Would one accounting system cover all of them? Who estimates
reconstruction cost or decides that retaining a decoded buffer is more useful
than another input window? A scan's lookahead can improve overlap while also
evicting bytes needed by point requests.

A concrete progress problem is two operations each holding an input while
waiting for an output allocation, with the pool already exhausted. A simple
per-request concurrency limit does not resolve that dependency. Candidates
include reserving a bounded input/output closure, retaining compact progress
and releasing inputs, spilling, or preserving capacity for completion. Each
has costs and cases where it may not apply; a dependent variable-length access
may not reveal its whole closure in advance.

Cancellation adds another distinction. Work no longer wanted by a query can
still own an active I/O buffer or dirty state needing resolution. Capacity
cannot be considered free merely because its consumer stopped waiting. How
would task ownership, backend tokens and Engine's publication outcome remain
connected without making the ordinary read path cumbersome?

## How broad should the resource model be?

Storage and network extend this beyond a memory scheduler. A remote read may
consume request slots, network buffers, decompression CPU, resident capacity
and eventual result space. Each independent controller could see spare room
while the combined operation accumulates too much retained state. Conversely,
a single conservative cap could leave several resources unused.

It is worth considering separate control scopes for local execution and short
interleaving, device or peer concurrency, and whole-operation admission. A
shared accounting vocabulary might connect them without requiring a single
policy or a single queue. This is a candidate decomposition; whether its
coordination costs repay themselves remains a question.

EBS, instance store and remote services also differ in the meaning of the
bytes they hold. A fast temporary copy, a cached remote object and authoritative
durable data cannot become interchangeable because of measured latency.
Engine and Orbital need to supply the relevant identity, recovery and
durability rules; Loom could schedule only the permitted choices. A successful
backend completion also needs its service-specific meaning before it can be
treated as satisfying a transaction dependency.

Could a bulk scan filling queues delay the completion or writeback work needed
to release its own resources? Would separate queues, reserved capacity,
dependency-aware priority or admission at a larger scope help? Local fairness
also need not compose into fairness for a distributed query whose slowest
participant determines completion. These are useful cases for testing an
architecture on paper before choosing interfaces.

The spike's storage measurements are conditional short filesystem probes;
its network diagnostics do not measure controlled peer throughput or tails.
They justify keeping these resources in the discussion, without supplying
production queue depths or service guarantees.

## Where might the analyser fit?

One possibility is a facility near Loom that exposes topology, capabilities
and conditional cost observations. Another is an Engine analysis service that
combines host measurements with retained plan evidence. A smaller shared
facility could supply facts while each consumer owns its interpretation.
None of these placements is selected.

The distinction between reported facts, observations and chosen policy seems
useful in all three. A reported line size or available page option has a
different status from an observed stream cliff, and neither alone chooses a
bucket layout or ring width. Engine knows useful bytes, hit rate, dependencies
and semantic alternatives; Loom can know admitted concurrency, placement and
current resource pressure; Orbital can expose backend capabilities and state.
How should these views meet without making every decision a cross-module call?

For example, a larger hash bucket may save a second probe while bringing more
lines into the cache. A larger TuplePack may improve sequential work while
increasing sparse-access amplification. More ring slots may hide random
latency while disrupting ordered streams or increasing retained state. These
are joint layout/workload/scheduling questions; the spike's candidate sets
are starting evidence for comparisons, not answers to them.

The [lookup prototype](../spikes/memory-characterisation/FINDINGS.md#recognition-lookup-and-initialization-cost)
also raises questions about partial knowledge. Which decisions need an answer
before work starts, which can use a conservative initial choice, and which
can wait for observations from useful work? Profile validity may depend on
kernel, page policy, actual allocation, sharing and executable context as well
as CPU identity. Different facts need different invalidation rules.

Runtime adaptation introduces its own coupling. If Engine changes batch shape
while Loom changes concurrency, both can misattribute the resulting movement.
Holding one choice steady during a comparison, recording policy revisions,
limiting exploration, or adapting at different timescales are possibilities.
How quickly should an improvement repay its measurement and transition costs,
especially for short queries?

## Concrete situations to think through

These are discriminating examples, not a planned implementation sequence.

| Situation | Questions it exposes |
| --- | --- |
| Hot point requests sharing a core with a long TuplePack scan | Direct-call overhead, useful batch duration, tail latency, fairness and cancellation frontiers. |
| A hash probe alternating resident metadata, random payloads and a missing remote value | Where short interleaving becomes a longer wait, what state survives, and how bindings are reacquired. |
| A producer emits packets for one consumer, then many consumers | Placement, transfer batching, false sharing, read fanout and mutable ownership. |
| Two operators expand variable-length outputs under a nearly full pool | Admission dependencies, retain/recompute/spill choices, and whether admitted work can finish. |
| A scan and ingestion compete while dirty writeback or replication falls behind | Resource coupling, foreground/background attribution and protection of progress. |
| A distributed query receives one slow partition while other partitions produce results | Backpressure across hosts, bounded buffering, placement authority and cancellation of outstanding work. |
| A reused plan changes representation or a VM's effective CPU environment changes | Which bindings, cost observations and policies remain applicable. |

Comparisons could charge useful completions, latency distributions, CPU cost,
retained and peak bytes, bytes moved, queue time, and abandoned work together.
Where possible, separate time waiting for admission, runnable time waiting for
a worker, backend service, and continuation work; otherwise a lower observed
I/O latency may merely hide queueing elsewhere. Baselines would depend on the
question: ordinary bounded loops, an explicit interleaver, or a simple fixed
I/O limit can all be informative.

The next discussion can start from any of these situations or from the desired
behavior under mixed load. The ownership boundaries, policy hierarchy,
execution vocabulary and objective priorities are still open.
