# Independent mutation-envelope challenge

2026-09-26. A conservative logical mutation envelope can remove the changing
physical-index-footprint problem, but **one row of exclusive ownership can still
require many predicate authorities and broad query waits**. This is a material
cost for indexed HTAP, separate from the cost of excluding writers from a broad
target envelope. The finite audit supports conditional safety and gate progress;
it does not establish a general-purpose distributed protocol.

This challenges the candidate described in the [comparison](COMPARISON.md):
acquire complete output-only coverage, reserve bounds, publish one fixed position
everywhere, then execute dynamic reads and mutations at that position. It tests
obligations and alternatives; it does not select or edit an Orbital brief.

## What must be complete before the position is fixed

Logical row ownership need not predict each physical secondary-index entry.
It must cover every authority through which an allowed mutation can change an
observation. Consider a row on A whose new index value K will route to B:

1. T announces position 10 only at A.
2. Q at position 20 queries K at B and returns no match.
3. T discovers K and installs it at position 10.

Position-ordered replay rejects Q's result. Announcing a blocker at B *after*
Q returned is too late. Announcing first but ignoring B's already certified
read position has the same problem. The two negative controls in the audit
fail for these separate reasons. Covering B before the read makes Q wait;
consulting a prior read bound of 20 instead places T after 20.

The candidate therefore needs complete **effect-authority coverage**, not merely
known row owners. A row-partition-local derived index can keep this metadata with
the row partition, with index queries visiting relevant row partitions. A global
value-partitioned index needs known routing or conservative advance coverage of
possible index authorities. Neither representation follows automatically from
calling an index entry row-owned. Routing changes and new output destinations
must preserve the original authority closure or invalidate the attempt.

The exact/unknown comparison uses the same eventual mutation `a: 0 -> 2`:

| Declared effect information | Exclusive row count | Predicate authorities | Equality query for 7 |
| --- | ---: | ---: | --- |
| Exact old/new values 0 and 2 | 1 | 1 | Completes during T |
| New value may be anywhere | 1 | 2 | Waits for T, then returns empty |

The two authorities represent even and odd values. These are logical counts,
not messages, bytes or measured timings. In both histories a disjoint primary
writer completes while T remains pending. Thus writer locality survives this
case while predicate-query locality worsens. If T waits for a remote extension,
even a finally disjoint query may inherit that wait. Calling only the actual
final row writes the conflict domain would hide this cost.

## Dynamic constraints and source discovery

Disjoint row gates do not alone enforce uniqueness. Two transactions assigning
the same unique value can execute correctly at positions 10 and 20: the second
predicate read waits for the first, then returns a business rejection. Four
authored visit/position histories check that result.

One transaction assigning the same unique value to *two of its own rows* needs
its tentative logical index effects, or a constraint check over the complete
proposed state. Checking each change against only the external snapshot permits
duplicates. This negative control passes ordinary serial replay and fails the
separate uniqueness assertion. A serializable execution can still violate an
application constraint that its program failed to enforce.

Late source-only discovery is different from late effect coverage. The actual
fixed-position core checks a source found after the target position is fixed:
an earlier source promise is awaited; a later promise is bypassed using history.
Neither case renews or moves the target position. Another 24 histories permute
three output-bound reservations and the point where a reader registers. A late
participant bound can push the writer beyond the reader; otherwise the reader
waits and observes the installed writer. Every completed read remains coherent.

Retention remains a progress obligation. A source shard may reclaim the version
at 10 before a long transaction first discovers that shard. The small retention
example shows why substituting its latest value is wrong; it does **not** model
a retention protocol. A source-domain retention lease, conservative active-cut
frontier, or explicit history-expiration failure contract is needed. An explicit
failure preserves safety while weakening the claim that one fixed execution
always completes. No new source write gate follows from this obligation.

## Ranges, acquisition order and fairness

Row/range/tenant envelopes must conflict by what they cover, including absent
identifiers. The audit normalizes small ranges into all potential finite slots;
an insertion into an absent slot waits behind the range, while a separate tenant
continues. This is an overlap oracle, **not** an implemented symbolic range-lock
manager. Existing `Admission` checks exact key sets; passing unrelated strings
for a range and an enclosed row would be incorrect.

The finite acquisition search covers three overlapping-envelope fixtures,
six stable-age orders each, and one local FIFO search per fixture: 1,705 states
and 2,947 transitions, with no incomplete terminal state. Local FIFO order denotes
agreed request order at that authority. It needs no global sequencer, global
admission age, or physical-client timestamp. Each transaction requests its
complete group once per authority, in one canonical authority order. Only the
currently requested group participates in that authority's queue.

The omitted-future-claim rule matters. A young transaction holds A and needs B;
an older transaction waits on A. If fairness also reserves the older transaction's
*future* B request, the young owner cannot acquire B or release A. Both wait.
Current-group-only registration allows the young owner to finish. This deliberate
deadlock control is outside the valid acquisition protocol.

Fairness also carries a valid but costly convoy. WAN owns x; an older group waits
for `(x,y)` atomically; a younger point writer needs y. The point writer waits
through the older group although y has never been granted. All-or-none local
ownership removes partial grants, not queued-group interference. Letting every
small writer pass would instead permit starvation of the broad group. Narrow
envelopes and finite agreed queues limit this tradeoff; they do not erase it.

The acquisition search always lets a fully admitted owner resolve and release.
It does not establish progress of data execution, failures, infinite arrivals or
stalled peers. Fixed-position data waits descend only once positions are exact.
Position publication and abort resolution must progress independently of blocked
user programs. A transaction cannot publish before all its extension use satisfies
applicable verification requirements, including approved exemptions. That does not
gate unrelated transactions sharing its epoch. This audit does not validate the
verification protocol or its composition with authoritative ordering metadata.

## A different bargain: dynamic priority locking with predicate precision

Dynamic strict two-phase locking with stable transaction priority and wound-wait
can avoid declaring a complete mutation envelope. It acquires source and output
protection as they are discovered, resolves younger victims, and retains granted
protection through commit. It replaces envelope prediction with lock decisions
and possible whole-transaction restarts. Spanner documents wound-wait and the
blocking cost of long locking reads; it does not establish this proposed SixDB
composition. [Spanner concurrency control](https://docs.cloud.google.com/spanner/docs/concurrency-control).

A useful competitor for indexed `MAX` observes and protects the maximum
`(score, stable-id)` and the suffix from it to infinity, then protects the chosen
row before observing its remaining state. Writers check both their old and new
index keys. Deleting/lowering the winner or inserting an overtaking value
conflicts; changes that stay below the maximum can proceed. Gap/record locking
is established machinery, while this exact MAX primitive is a proposed
composition. [MySQL next-key locking](https://dev.mysql.com/doc/refman/8.4/en/innodb-next-key-locking.html).

| During T's remote wait after selecting a=100 over b=50 | Predicate protection | Whole target envelope |
| --- | --- | --- |
| U changes b from 50 to 60 | Completes | Waits |
| V then proposes b=101 | Waits | Waits |
| A writer outside the target collection | Completes | Completes |

T marks a and releases protection before V proceeds. `U,T,V` is a valid serial
explanation. Across 64 three-row score states and 576 one-row changes, every
permitted change preserves the selected maximum, including deterministic ties.
This checks the logical protection rule, not a distributed wound-wait executor.

The opposite case is executed through `FixedStore`: T reads MAX but writes a
separate known marker. An independent challenger physically commits before T
while replay orders T before the challenger. Retained MAX protection blocks that
challenger. Thus narrowing mutation coverage and narrowing read protection serve
different workloads; neither candidate dominates.

Distributed MAX must protect each shard's local answer before reducing it.
An empty shard needs whole qualifying-domain protection; losing shards can
retain more protection than the final winner alone suggests. An unprotected
candidate-gather counterexample has no valid serial order: T sees A:10/B:9;
U reads A's old marker and inserts B:12; T later marks A. Protecting only the
apparent winner after gathering cannot repair that cycle.

Priority locking needs an enforced progress rule, not hopeful repeated restarts:
stable well-founded priority across attempts, priority-consistent waits, completed
abort resolution before regrant, and a wounded victim held dormant until its
older blocker transaction completes. With finitely many older predecessors and
terminating work/recovery, each older identity can cause at most one such wound.
Irrevocably prepared owners acquire no further locks and must resolve. This is
a conditional design argument; no numeric retry bound or full implementation is
supplied here. Broad source scans still retain broad exclusion during WAN work.

## Assessment and evidence

The envelope candidate simplifies one important part of contention: final
execution no longer alternates discovery, promotion, validation and repair.
Compared with the old component arbitration, ordered output admission plus
descending fixed-position reads is a smaller reasoning surface. Compared with
BRIEF2 overall, it exchanges renewal/retry machinery for effect closure,
pre-execution metadata, retained output ownership and conservative waiting.
Neither the closure analysis nor the row/index visibility protocol is free.

It is a defensible single mechanism when conservative target ownership is an
accepted workload cost. It is not yet evidence that arbitrary SQL with rare WAN
work preserves low latency for unrelated *actual* rows and predicates. In
particular, unknown-winner updates and globally routed unknown index effects can
make the conservative domain much larger than the final mutation. Dynamic
priority locking is a credible competitor where precise predicates admit useful
concurrency; it gives up source-writer freedom and much of the simplification.
Adding it automatically as a fallback would retain both mechanisms.

Run from the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_envelope_audit.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/envelope_audit.py --output build/orbital-envelope-audit/audit.json
```

The runner keeps 42 authored histories (including two deliberately invalid
predicate histories, a constraint violation and a queue-deadlock control), the
finite gate search, the MAX comparison and source hashes. Eleven tests pass.
The source is the retained evidence; reproducible JSON goes to ignored storage.
All sizes and positions are authored logical quantities. There are no measured
throughput or latency results, recovery claims, or distributed liveness proof.

The index audit replays completed predicate lookups against position-sorted row
state. Lookup and replay share the row-to-predicate projection; supplied writes
and tentative overlays are trusted except where separate invariants check them.
It does not implement a separately maintained physical index. The gate search
models protocol interleavings, not replica agreement on one interleaving.
