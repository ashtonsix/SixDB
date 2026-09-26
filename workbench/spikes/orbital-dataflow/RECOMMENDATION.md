# Recommended dataflow scope and composition

Support substantial distributed computations, including Spark-like extension
applications, through **local execution regions connected by selected exchanges
and retained state**. Let the application define progress and publication; let
Loom schedule finite physical work; let Orbital provide opaque storage, transport
and lifecycle mechanisms. This is a proposed composition for Orbital LEAD to
assess, not an adopted graph runtime, transaction protocol or universal API.

The scope is broader than map-reduce: partitioned relational work, joins and
rank refinement; iterative/recursive work; incremental/streaming updates;
conditional application requests; and reusable distributed datasets produced by
extensions. The [19 workload families](WORKLOADS.md) establish why those shapes
matter. Their common mechanics do not require the same programming language or
the same semantic completion rule.

## A small composition

**Express meaning above the physical graph.** Bind each use to its input versions,
logical coverage, code/interpretation artifacts, allowed transformations and
result/effect promise. Structured relational plans expose optimization opportunities;
batch extension regions allow substantial custom computation. An opaque function
does not acquire a legal incremental derivative or merge law just because it
can run on several workers. Keep SQL/application meaning outside Orbital.

**Execute useful regions, with deliberate boundaries.** Fuse local work where
that saves allocation, dispatch or intermediate bytes. Introduce bounded channels
for real asynchronous dependencies and useful parallelism. Materialize an
exchange where reuse, bounded replay, independent progress, spilling or plan
changes repay the write/read cost. Tasks, operators, packets, storage pages,
checkpoints and transactions can have different grains. A direct small request
should keep a direct path.

**Choose partitioning, representation and placement together.** Retain a small
portfolio: colocation or lookup, gather, broadcast, repartition, selective filters,
and application-proven reductions/factorization. Use hot-key replication or
partition grids only with complete pair/contribution ownership. Price build
state, output expansion and return traffic alongside input bytes. A cheap
network tree cannot remove replicated receiver state or a required join product.

**Give feedback and dynamic fanout explicit completion evidence.** A fixed finite
plan can use simple declared partition coverage. Dynamic children require a
parent-to-child ownership transfer that never loses unfinished work. Iteration
needs its stated fixed-point, convergence or bounded-result condition. Streams
need source closure and correction rules. Use richer frontiers only where they
earn their complexity; a partial-order frontier is not a universal requirement
for ordinary scans. A quiet queue, timer or delivered packet does not prove a
complete result.

**Separate job, outcome and resource lifetimes.** A long job can produce a
historical result, prepare a generation for bounded activation, or advance a
continuing application through finite accepted transitions. It need not be one
long transaction. Preserve the current transaction's fixed position, complete
effect envelope and verification gate where that contract is selected. Iteration,
input cut, shard epoch, delivery progress and commit remain different facts.

**Make reuse and recovery explicit physical alternatives.** Cache compatible
results when reuse repays capacity; retain reconstruction inputs/code when replay
is viable; checkpoint selected expensive or stateful boundaries when that bounds
recovery. A result checkpoint can replace computation for an allowed data-serving
use, but cannot replace required independent executions or audit evidence.
Stable logical contribution identities survive worker replacement and retries.
Input positions, derived state and owed outputs must recover as one coherent
application cut.

**Ensure a finite path to completion or resolution.** Budget working inputs,
scratch, output, retained history and reconstruction at their actual shared
resources. Separate counters make ownership visible; they do not add capacity.
An admitted step needs a feasible stopping/completion path or an authoritative
failure path. Parked continuations release execution slots while preserving
the bytes and obligations they still own. Cancellation requests and physical
retirement remain separate. New work cannot consume all capacity needed to
resolve old work.

## What the probes establish

The four prototypes use exact authored inputs, independent semantic oracles,
logical counters and one synthetic byte/tick model. They test useful choices
without pretending to implement a full database or high-fidelity simulator.

| Evidence | Consequence for the composition |
| --- | --- |
| [Exchange](EXCHANGES.md): small-build broadcast uses 6,144 input-wire bytes versus 67,584 for repartition; a wide-build case reverses the winner. Exact filtering can reduce bytes while increasing setup work. | Keep conditional alternatives; rows, bytes, build residency and selectivity all matter. The viability predictor here knows complete inputs. |
| [Exchange](EXCHANGES.md): hot-key grid produces 65,670 pairs within its build-state cap; exact count instead needs 1,304 modeled wire bytes versus 1,621,952 for grid input plus gathered rows. | First choose the legal required representation. Counting cannot substitute for consumers that need the actual pairs. |
| [Exchange queues](EXCHANGES.md): independent queues still stall healthy work when one receiver owns the whole shared pool; fixed caps rescue that trace but unnecessarily refuse 12 inputs in a smoother one. | Scheduling isolation and admission isolation are separate; the example does not select a fixed cap or prove an adaptive allocator. |
| [Resource lifetimes](RESOURCES.md): an acyclic flow stalls after 7/12 chunks when RAM intermediates survive until parent resolution; separate budgets stall after 6/12. Replayable input retention or a sufficient spool completes. | Inspect wait cycles through retention/publication as well as operator edges. Spill and replay need finite capacity and a valid reconstruction basis. |
| [Lineage](SPARK-STYLE.md): a fitting cache halves repeated record computation; a half-sized sequential-scan cache gives no reuse. A halfway checkpoint halves the tested partition's 20-round reconstruction, but a partial checkpoint may miss the lost partition. | Reusable distributed data is worthwhile scope; cache admission, checkpoint coverage and artifact identity are first-class costs and validity conditions. |
| [Progress/recovery](PROGRESS.md): finite coverage can remain incomplete after a seal/high sequence arrives; incorrect split input/output checkpoints duplicate or omit contributions. | Completeness and recoverable publication require more than transport receipt or offset advancement. The positive atomic transition is a supplied assumption. |
| [Recursive maintenance](PROGRESS.md): naive support counts falsely preserve an unrooted cycle. Correct specialized incremental maintenance uses 1 versus 199 edge visits for a leaf deletion, but 401 versus 0 reachable-edge visits after removing a bridge into a cycle. | Preserve full recomputation as an alternative. The zero traversal count excludes setup/index updates; these are logical work comparisons, not performance predictions. |
| [Cancellation](RESOURCES.md), [retention](PROGRESS.md): small grains shorten stopping, but slow submitted IO, unresolved outcome, another reader or a committed intent can keep resources alive. | Stop execution, resolve authority and retire each owner separately; do not free shared data on caller cancellation. |

The Spark-like direction is developed in [SPARK-STYLE.md](SPARK-STYLE.md):
versioned partitioned inputs, inspectable plans plus partition functions, lazy
actions, reusable intermediates and explicit job/results. This supports a real
application surface beyond scalar UDFs while leaving language bindings and any
compatibility layer unselected.

## Alternatives worth retaining, without making them defaults

Full operator graphs with distributed progress tracking are useful for complex
feedback and dynamic discovery, but cost control traffic, persistent identity
and recovery machinery. Simple finite coverage and local loops can be sufficient
elsewhere. General differential execution offers broad incremental expressiveness;
specialized keyed operators and recomputation can be much smaller and cheaper
for a particular workload. Neither direction is ruled out.

Permanent intermediates simplify reuse and stage recovery but spend writes,
space and cleanup work. Reconstructing every missing value minimizes selected
materialization but can retain old source versions indefinitely and repeat
expensive extensions. A private materialized result is not automatically a
published product, and an evictable cache is not a durability promise.

Whole-query dispatch is still useful for small jobs and locality. Dividing work
across replicas is useful only with complete, nonduplicated coverage at a usable
version; it does not reduce mandated state folds or execution checks. Arbitrary
hedging, blind task retry, uniformly broadcasting every artifact and randomly
splitting both sides of a join are unsupported defaults.

## Where stronger evidence is still needed

These probes do not compose real version-bound object views, extension runtimes,
Loom scheduling, network delivery and persistence into one execution. The
[physical-services study](../orbital-objects/README.md) investigates the OS side.
Mapping a page or reporting a fault cannot substitute for the application's
logical read coverage, and ordinary loaded bytes must remain valid throughout
their backend users' lifetimes.

Material unknowns are distributed coverage/progress recovery; safe live state
repartitioning and plan changes; practical observation-based estimates instead
of oracle input sizes; spill/index memory and metadata overhead; artifact and
snapshot retention over long jobs; workload isolation for short transactions;
and measured cold-start, throughput and tail behavior. External sinks and
arbitrary checked-native distributed jobs need their own supported contracts.
The selected shard-local checked-native lane remains intact.

The evidence supports the scope and the small composition above. It does not
select a universal DAG API, a single shuffle/queue algorithm, an automatic
optimizer, a general differential runtime, an external workflow language,
Spark compatibility or a calibrated latency claim. Orbital LEAD owns any
promotion into the brief.
