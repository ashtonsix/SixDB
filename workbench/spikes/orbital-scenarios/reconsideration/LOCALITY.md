# A WAN transaction must not become a shard-wide pause

Ashton's correction to the initial reconsideration is a design constraint:
millisecond regional writes at very high throughput cannot share an execution
or publication barrier with occasional transactions that wait hundreds of
milliseconds across the Atlantic. Localization is essential, not an optional
performance refinement. Shared hardware still imposes resource costs; the
requirement is to avoid imposing the WAN synchronization delay on unrelated work.

The one-lane and common-barrier batch candidates fail this workload at shard or
database scope. They remain useful negative controls, not production recommendations.
Moving the barrier from execution to commit, result publication, or a shared
epoch frontier would preserve the failure.

## The pause and the recovery are different costs

For an affected queue receiving lambda writes/s, a pause of H seconds creates
approximately lambda * H queued writes. If service resumes at mu writes/s while
arrivals continue, ideal fluid recovery takes:

`recovery_after_pause = lambda * H / (mu - lambda)`

With capacity only 10% above arrivals, a 200 ms pause takes another 2 s to drain;
with 5% headroom it takes 4 s. If the affected queue receives 10M writes/s, the
pause alone adds 2M queued writes. The relevant rate is the paused queue's rate,
not automatically the whole deployment's rate. This arithmetic assumes constant
rates and omits retries, cache effects and downstream congestion; it is not a
measured latency prediction. Repeated pauses can prevent recovery altogether.

A small fraction of WAN requests therefore does not imply a small interference
cost. Nor does putting them together in a 200 ms batch restore a 1 ms local
latency target if local completion waits for that batch.

## Preserve the existing distinction between epochs and transactions

The brief already permits a complete epoch fold to leave a transaction pending.
Its C1 parts can remain prepared while other parts execute later. For example:

| Epoch | Complete, agreed result |
| --- | --- |
| E10 | Record T's continuation and issue its agreed remote request. T remains pending. |
| E11 | Complete unrelated local U and expose U's result. |
| E12 | Consume agreed remote progress and advance or complete T. |

Each fold finishes and each epoch's results appear in order. T's completion is
an E12 output; E10 never claimed T had completed. The brief does not require
transaction responses to follow their initial admission order. A total admitted
order likewise need not be a total physical completion barrier for independent
operations. The single-lane proposal added that barrier; it was not inherited
from Orbital's fixpoint requirement.

Pending must be a fully determined state, including relevant captured inputs,
protection and protocol outputs. A replica receiving an early reply cannot
silently include T's completion in E10 while another replica leaves it pending.
Remote progress must enter through agreed input or another transition determined
by agreed state/history. Tentative effects must not leak, and an admission/fold
frontier must not be mistaken for a transaction-commit frontier. This preserves
the existing determinism requirement; it does not itself establish multishard
atomicity or the validity of T's old reads.

Extension verification is transaction-scoped, as clarified in
[brief](../../../../orbital/BRIEF.md#extensions-and-publication). T must satisfy
the applicable requirements for all its extension use before publishing, including
any approved BLAKE3 exemptions. Unrelated U can complete within the same epoch.
Verification does not impose a shared epoch-publication barrier; actual dependency
and resource costs still belong in the locality account.

## Small queues can still propagate a large wait

Suppose T awaits a remote input before adding it to x. Ready local U updates
both x and y, and ready local V updates only y. Neither U nor V touches T's
remote source.

If T takes a binding future place in x's queue, U queues behind it on x while
also claiming a place on y, and V queues behind U on y, the wait spreads:

`V(y) -> U(x,y) -> T(x) -> WAN`

This can happen with no granted locks. Splitting one shard queue into many key
queues has not guaranteed localization: future queue claims became reservations.
In this example U and V can execute before T if T has not yet consumed x and
reads/applies against the resulting x when ready.

If T actually holds valid protection on x, U may need to wait. That does not
by itself entitle U to fence y while its x request is blocked. V can precede U
if the transaction semantics permit that order. If T has already consumed x,
or V participates in an invariant involving x, the example has real dependencies
that cannot be erased by changing queues. Overtaking may require recomputation
or be forbidden; it is not automatically safe.

The inquiry is which local claims must constrain later operations, for how long,
and whether waiting on one claim automatically spreads exclusion through every
other future claim. It should not start by collecting and fencing the entire
connected set again. Whole-transaction priority or all-or-none local acquisition
are simpler candidates to examine, with their progress and broad-footprint costs
still exposed. Neither alone solves arbitrary distributed transactions.

## Prior art that addresses localization directly

SLOG schedules conflicts at data granules and continues processing the log past
blocked transactions. Its multi-home transactions place per-home ordering records
among local work. The paper also documents the limit: early records block
conflicting local transactions until remote records arrive; lagging regions
amplify tails. Broad access sets still cause broad interference, and unknown
footprints use reconnaissance. [SLOG, sections 2, 3.2–3.3 and 4.1–4.2](https://www.vldb.org/pvldb/vol12/p1747-ren.pdf).

TAPIR validates accessed keys against committed/prepared transactions without
making every unresolved transaction a shard-wide validation barrier. Its
key-value mechanism does not settle arbitrary SQL predicates or large-transaction
progress. [TAPIR, section 5.2](https://syslab.cs.washington.edu/papers/tapir-tr-v2.pdf).

These establish useful distinctions, not ready-made replacements. Dependency
graphs can also localize waiting, but importing another graph/strong-component
protocol would need to justify its complexity against Ashton's simplification
objective.

## What the comparison now has to expose

Use local writes and rare delayed cross-region transactions in the **same shard**.
Start with disjoint data, then one shared key, the x/y bridge above, a broad
predicate, and actual broad writes. Include shared index/aggregate metadata:
physical false sharing can restore the convoy after row-level claims are fixed.

Distinguish execution delay, publication delay and queue-drain time. For each
local operation that waits, identify whether the cause is a required logical
dependency, an enforced order, a speculative reservation or a shared barrier.
Also count discarded broad work and broad-transaction completion; indefinite
starvation is not a successful localization result.

The target is locality of waiting while reducing the machinery governing it.
If a transaction truly conflicts with nearly all hot data, localization alone
cannot make it harmless. That case still requires an explicit choice about its
progress, competing latency, or workload semantics. It must not be confused with
a narrow cross-region transaction that merely shares a shard with millions of
independent operations.
