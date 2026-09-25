# Executable contention slice

This spike interprets the work-in-progress [Orbital brief](../../../orbital/BRIEF.md)
as read on 2026-09-25. It explores preparation, retained protection, discovery,
retries and yielding. These choices are revisable model policies, not additions
to the brief or a proposed production decomposition.

The rules below describe the original individual-reservation policies. The
[component-batch extension](BATCH.md) adds coordinated retry cuts, component
fences and delayed verdicts, with stronger snapshot/application abstractions
and separate selected evidence. It retains the same part and C2 invariants.

## Boundary

Input events are already persisted and admitted. Each shard receives a sequence
of agreed epochs. A deterministic reducer consumes an epoch and the control
messages available to it. The simulator supplies their timing and order.
There is one logical consumer per shard, representing its agreed state.

An epoch executes one of L, C1 or C2, chosen by a rotating fair selector among
nonempty queues. Control messages are processed first as abstract agreed inputs.
For L/C1, epoch capacity limits attempted parts, not completed parts. A C2 slot
applies one transaction's entire set of parts on that shard. A shard's next epoch
occurs after its configured period. This is a service-capacity assumption:
there is no CPU, witness, disk or link queue model underneath it.

The control delay applies to preparation reports, yield requests and C2 delivery,
including coordinator-local messages. It is an abstract delay, not a count of
physical network hops. Root admission and successor activation enter shared
agreed state immediately; separate dispatch and result-sharing delays are omitted.
No loss, failure,
election, reconfiguration, durable journal or recovery protocol is modeled.
In particular, invalidation is one atomic authority transition across the
affected parts. Its distributed implementation remains outside this slice.
A delayed report still tests stale-work fencing, but this is not a distributed
commit implementation or a safety proof for one.

## State and transitions

Transactions have immutable IDs, coordinators and arrival times. Each has a
finite authored DAG of parts. The simulator knows that DAG; the coordinator
initially knows only roots. A successful part's report registers its successors
before marking that part reported. Successors become ready only after every
parent has reported its current generation. Thus a coordinator cannot mistake
an undiscovered successor for completed preparation.

Each part requests a set of read/write key locks on one shard. Read/read and
same-transaction protection are compatible. Predicate locks and object-defined
conflict semantics are represented only by these toy keys. Acquisition is
all-or-none. Prepared C1 parts retain their locks until C2 or invalidation.
Within an epoch, all attempted contenders are ranked by arrival time then ID;
conflicting losers fail even if an L winner releases in that same epoch.

Retries derive from shard epoch numbers, attempt counts and a stable hash of
transaction/part identity. No wall-clock or unrecorded random decision enters
the reducer. Backoff is capped, with deterministic jitter. Failed attempts
are trace entries, not separate admission requests or coordinator messages.

After a configurable failure threshold, a part may reserve its complete lock
set. Reservations may overlap granted protection, but cannot overlap other
reservations, including read/read overlap. They authorize no execution. New overlapping grants are
blocked until the reservation's owner succeeds or is invalidated. A successful
reservation emits yield requests naming the current blocking parts/generations.
Requests are retried at subsequent failed attempts, so rejection is observable.

Yield policies are deliberately simple comparisons:

- `reject`: reject all requests; useful for exposing stable waits.
- `older`: invalidate the requested part and transitive dependents only if the
  requester is older (arrival time, then ID).
- `work`: accept if the invalidated prepared work units do not exceed the
  requester's retained work units. Work units are authored logical weights.
- `arbitrate`: apply `older`, then at each epoch detect wait-for cycles and
  invalidate all discovered parts of the youngest preparing transaction in a
  cycle. This global, instantaneous cycle oracle is an idealized comparison
  with stronger coordination assumptions, not a specified distributed arbitrator.

Invalidation preserves transaction identity, increments affected part
generations, discards affected reports/results and releases grants/reservations.
Descendants become undiscovered; the affected root retries. Reports and yield
requests carry generations and cannot resurrect superseded work. Invalidation
can happen only during preparation. C2 authorization closes that window;
delayed yields are rejected once authorization is ordered.

C2 is authorized only when all coordinator-known parts have current reports.
Each participating shard executes its parts in a C2 epoch and releases their
protection. Transaction completion means every participating shard has applied
C2. This is a completion observation, not a model of atomic read visibility.
L is represented by a single part that acquires, executes and releases within
one L epoch, with the same retry/reservation exception as the brief.

## What the checks can establish

After every simulator transition: no conflicting grants across transactions;
no overlapping reservations; each granted/reserved part has its complete set;
prepared parts retain grants; applied parts have applied exactly once; terminal
transactions retain no grants or reservations; only reported preparation can
authorize C2; current dependency generations precede prepared descendants.
Tests also exercise stale reports/yields and compare byte-identical replay.

A deterministic schedule is one permitted schedule chosen by this model. Replay
agreement does not prove that every permitted concurrent fold reaches the same
fixpoint. Shard event tie order can be varied to look for counterexamples;
there is no exhaustive interleaving search or TLA+ proof here.

## Reading performance

Times are synthetic microseconds derived from explicit epoch periods and control
delays. Work units and retry counts are logical counters. Report completed,
pending, not-yet-arrived and oldest pending age alongside completion latency.
A short horizon can censor the worst waits. A wait-for cycle is an observed
state, not proof that every policy remains stuck forever. No measured network
one-way point estimate is silently treated as a precise model input.

The useful separation to test is small: scenario data, deterministic transition
model, event scheduler, and browser projection. Formalization can later replace
the scheduler with a nondeterministic next relation; a reference implementation
can replace abstract agreed inputs and atomic invalidation with actual messages.
Those are possible uses of the slice, not fixed architectural seams.

The current DAG is authored in full. Its nodes become known incrementally, but
their identities, lock sets and placement do not depend on computed values.
There is no object payload/fold implementation, cancellation, unavailable
participant policy, or data-loss-tolerant processor model. Dependency generations
stand in for discarded preparation results. Work weights influence the `work`
policy; they do not consume modeled CPU time.
