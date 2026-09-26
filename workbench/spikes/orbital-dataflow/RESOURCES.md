# Execution space and retained work

An acyclic operator graph can deadlock through its resource lifetimes. Consider
a producer, a shuffle and a private reducer. The reducer consumes each chunk,
but an execution policy retains every shuffle chunk until the parent transaction
resolves. The transaction cannot resolve until all chunks arrive. When retained
chunks fill the same memory pool needed to produce another chunk, every remaining
step waits. A fair queue or a separate accounting category cannot create space.

[resource_probe.py](resource_probe.py) makes that cycle concrete. It compares
five policies on identical exact inputs: retaining intermediates in memory,
partitioning the same memory into active/retained budgets, durable materialization,
retaining replayable source inputs and releasing consumed intermediates, and
rejecting before execution when the declared complete output bound cannot fit.
These are alternatives under stated replay/retention requirements, not equivalent
failure-recovery guarantees or an instruction to persist every exchange.

## What is modeled

Each of 12 immutable inputs occupies 16 durable bytes and produces 128 unit
records. Input `i` emits value `i + 1`, so a private integer reduction has an
independently checked exact answer. One worker computes chunks; one writer can
materialize them while computation proceeds. Input scratch and a complete output
chunk must fit before execution. All policies pin the original source in a
separate **finite** durable store until resolution. Replay mode additionally
requires an explicit declaration that the source and code permit reconstruction.
Every policy preserves the same 1,024-byte resident memory budget.

The reduction's fixed accumulator and scheduling metadata are outside that
payload budget. Unit records, scalar inputs and their encodings are authored
model objects, not a real row layout. A tick has no calibrated duration. Compute
cost is `1 + 0.125 × output_bytes` per chunk; spool cost is
`2 + 0.05 × output_bytes`. These inputs separate mechanisms rather than predict
hardware performance. Source reads count bytes but have no separate IO latency;
spool durability and eventual commit/abort decisions are supplied facts, not
implemented consensus, storage-failure or verification protocols.

The source pin and selected materialization must remain sufficient for actual
recovery. The model does not simulate recovery time, replication, capture of
external facts, artifact metadata, or reconstruction after source expiry. The
[progress probe](PROGRESS.md) separately checks selected restart semantics.

## Results and contrary cases

All 21 selected cases, including rejection and incomplete cases, are retained in
[resources.json](evidence/resources.json). Every case keeps complete counters,
the first/last eight trace events, all lifecycle events and a hash of its full
regenerable trace; repeated chunk events are omitted. The drain horizon is 10,000 ticks;
stalled work continues accruing byte-time to that horizon.

| Case | Outcome | Completion or retirement, ticks | Peak resident bytes | Spool bytes written |
| --- | --- | ---: | ---: | ---: |
| Retain every intermediate in RAM | 7/12 chunks consumed, stalled | — | 912 | 0 |
| Split active/retained memory budgets | 6/12 consumed, stalled | — | 784 | 0 |
| Spool all intermediates, sufficient store | 12/12, published | 222.4 | 272 | 1,536 |
| Retain replay source, discard consumed intermediates | 12/12, published | 214 | 144 | 0 |
| Require entire declared output bound in RAM | Rejected before obligations | 0 | 0 | 0 |
| Spool limited to 512 bytes | 11/12 consumed, stalled | — | 912 | 512 |
| Slow spool, one tick per byte | 12/12, published | 1,587 | 912 | 1,536 |
| Four-input RAM case | 4/4, published | 78 | 528 | 0 |

Spooling is a useful way to move a lifetime, with an IO and capacity cost.
It cannot make arbitrary retained output bounded. A source replay recipe can
be much smaller, but may require substantial recomputation and is invalid when
the needed observations, code or external facts are unavailable. A materialized
boundary may instead be valuable for expensive derivation, many reusers,
independent recovery or source release. Releasing intermediates is legal only
after their remaining consumers and recovery requirements have another valid
basis. The RAM policy is reasonable for the four-input case; always spooling
would add work to a flow that already fits.

The split-budget policy deliberately admits less retained output: it preserves
an active reserve inside the same total memory. This leaves free execution
space, but the next result still has no permitted retention destination. The
recommendation is to provide an actual completion or resolution path, not simply
to introduce separate counters.

For 512-byte outputs and a 256-byte pool, one indivisible output cannot start.
Using 32-byte chunks completes all 192 chunks with only 48 peak resident bytes
in replay mode. It pays 960 compute ticks and reads 3,072 source bytes because
each chunk reconstructs its input scratch. This model's arithmetic is favorable
to smaller grains for capacity, but the additional setup and source-read work is
real within the model. A different operator may require an irreducible build
state or produce output that cannot be consumed incrementally; chunking cannot
be assumed to remove that requirement.

Publication delay changes retention even without changing computation. For four
inputs, extending the publication wait from 10 to 1,000 ticks grows RAM
byte-time from 27,968 to 534,848. Replay mode keeps resident byte-time at 9,792
while its durable source remains pinned longer. Moving the lifetime does not
eliminate it; charge all relevant stores and their failure domains.

## Cancellation is a request to stop, then a resolution

With cancellation requested at tick 5 and abort known at tick 7, a 128-byte
nonpreemptible computation retires at tick 17. A 16-byte grain retires at tick 7.
Neither publishes a partial result. With cancellation at tick 20, a previously
submitted slow spool write keeps its buffer until tick 147, even though abort
is known at tick 22. Conversely, fast CPU stopping does not release replay and
tentative-state obligations while the authoritative outcome remains unknown:
the slow-resolution case retires at tick 1,020.

The model's abort events are supplied agreed outcomes; a local timeout cannot
invent them. Cancellation of one branch of shared work also cannot release
another branch's input or a committed delivery obligation. The scope and
workload discussion treats shared consumers separately; this probe has one.
Spool byte counters include completed writes; a write still active at a shortened
drain horizon retains its memory/storage reservation but contributes no partial
write bytes. Issued CPU work and CPU service before the horizon are separate.

## Composition consequence

Choose execution regions and retention boundaries together. Before creating
pending work, ensure a bounded route to completion **or authoritative failure**:
a sufficient working set, incrementally consumable output, a finite materialized
boundary, retained reconstruction inputs, or admission refusal before creating
the obligations. Reserve enough independent service for stop/decision/cleanup
work. Account for physical bytes shared by budgets, not just nominal per-stage
limits. A graph-wide worst-case reservation is safe in this example but can reject
useful streaming work; a declared bound or explicit expansion-limit outcome may
be preferable to attempting unlimited resource discovery mid-execution.

For feedback graphs, inspect strongly connected wait dependencies including
resource leases and publication, rather than only the operator edges. A seed
token or spare buffer helps only if it is large enough for a valid progress step
and protected from unrelated retention. Negation/fixpoint completeness and
transaction gates are semantic dependencies, not buffer deficits that a larger
queue can fix.

## Reproduce

```sh
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/check_resource.py
orb -m ubuntu python3 workbench/spikes/orbital-dataflow/resource_probe.py --output build/orbital-dataflow/resources.json
```

The 15 checks include exact result comparison, capacity and offered-work
accounting, cancellation lifetimes and 200 random capacities compared against
closed-form feasibility conditions for the simple RAM/replay/spool model. They
also distinguish issued work from CPU service executed before a drain horizon
and reject invalid timing inputs. These
checks cover the authored model, not arbitrary dataflow deadlock freedom.
