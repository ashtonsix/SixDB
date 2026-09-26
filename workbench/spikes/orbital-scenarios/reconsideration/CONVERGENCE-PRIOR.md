# Dynamic writes: prior art and the remaining bargain

2026-09-26. Research for the next contention proposal, not an adopted contract.
The question is how to support dynamically discovered SQL effects while letting
unrelated regional transactions commit during WAN work. A transaction merely
continuing to execute is insufficient if its commit waits for the WAN transaction.
This note supplements [the earlier review](PRIOR-ART.md); it does not select a
combination of all the mechanisms below.

## What dynamic discovery makes unavoidable

Consider an attempt T that reads `x=0`, then discovers that it must write `y`.
Before T declares that output, U reads `y=0`, writes `x=1`, and commits. T's old
read requires T before U, while U's old read requires U before T. T cannot commit
both those observations. On each retry, a new U can repeat this pattern. Giving
T a stable age cannot revoke an already committed U.

This authored history challenges “snapshots plus fair output locks” as a general
progress argument. It is not an impossibility theorem for multiversion systems.
Some mechanism must prevent the contradictory observations from both committing,
change an attempt's observations, or permit failure. Prior knowledge of a
conservative output scope is one way to prevent them; retaining source
dependencies and coordinating victims is another.

## Alternatives with concrete mechanisms

### Dynamic pessimistic MVCC moves some waiting to commit

Larson et al.'s pessimistic protocol retains version read locks and bucket locks
for absence/range observations. A writer may eagerly update a read-locked version
or insert into a locked bucket, but cannot precommit until the conflicting
readers release their locks. Closing admission to further readers/inserters
prevents an endless stream from extending that wait. Optimistic writers must
also honor the pessimistic locks. See §§4.1–4.5 of
[High-Performance Concurrency Control Mechanisms for Main-Memory Databases](https://www.microsoft.com/en-us/research/wp-content/uploads/2011/12/MVCC-published-revised.pdf).

This accepts unknown footprints and provides a conventional alternative to our
output declaration. It does not meet the desired broad-source behavior: a WAN
scan still delays commits to its sources. Shared-memory wait-for counters are
also not a distributed recovery protocol. Removing execution stalls would not
remove the user's regional-latency objection.

### SSI and SSN remove source exclusion but retain possible starvation

PostgreSQL SSI tracks nonblocking read dependencies and aborts transactions when
a dangerous structure could form a serialization cycle. Its safe-retry rule
arranges that a retried victim cannot meet the same committed culprit again;
new transactions can create new conflicts. Prepared transactions constrain
victim selection, and §7.1 explicitly describes circumstances in which safe
retry cannot be preserved. Long transactions also prolong dependency-state
retention. Safe read-only snapshots avoid subsequent SSI aborts, but acquiring
one need not finish within a fixed time. See §§4, 5.4, 6 and 7.1 of
[Serializable Snapshot Isolation in PostgreSQL](https://arxiv.org/pdf/1208.4179).

SSN is a serializability certifier over a suitable underlying concurrency-control
scheme, not a replacement for write admission. Its safe-retry result likewise
excludes repeating the same failure while explicitly allowing new transactions
to cause another violation. See §3.2 and Theorem 7 of
[Efficiently Making (Almost) Any Concurrency Control Mechanism Serializable](https://arxiv.org/pdf/1605.04292).

These are credible general alternatives if retry/failure is the contract. They
do not justify claiming that an arbitrary broad transaction eventually wins.
Distributed dependency publication, prepared outcomes and predicate coverage
would still need a protocol; replacing a component graph with stamps is not by
itself evidence that the total implementation is simpler.

### Starvation-free dynamic MVCC exists, at a substantial coordination price

KSFTM explicitly addresses the case where stable initial age alone fails because
newer readers have already committed. It boosts a transaction's working
timestamp across retries and adds validity bounds to preserve real-time order.
Versions retain reader identities. Commit locks written objects and relevant
transaction state, checks/updates bounds, and can abort eligible live readers.
The proof assumes bounded termination of attempts under a fair scheduler; it
does not cover a crashed or indefinitely delayed participant as though it had
completed. See §§3.1–3.6 and algorithms 18–20 of
[Achieving Starvation-Freedom in Multi-Version Transactional Memory Systems](https://arxiv.org/pdf/1709.01033).

This is a useful disconfirmation of any claim that dynamic footprints and
starvation freedom are intrinsically incompatible. It is not a simple SixDB
drop-in: the published shared-memory mechanism uses a global logical counter,
reader lists and coordinated transaction-state changes. A distributed version
would need durable victim fencing and bound updates. Commit coordination can
reach readers well beyond the final output set; the paper supplies no guarantee
that those operations preserve the required WAN locality.

### Reconnaissance predicts footprints; it does not make them immutable

Chardonnay dry-runs a transaction on a snapshot to prefetch data and approximate
its read/write footprint. It then uses strict two-phase locking, normally in
ascending key order. If execution discovers an unpredicted read or write, it
falls back to wound-wait. This preserves general execution without treating
every prediction mismatch as a mandatory restart, but the real transaction
retains read locks. The design targets a single datacenter, and explicitly
identifies cross-datacenter 2PC latency as outside its intended setting. See §§1,
7 and 8 of
[Chardonnay: Fast and General Datacenter Transactions for On-Disk Databases](https://www.usenix.org/system/files/osdi23-eldeeb.pdf).

This is stronger prior art than a speculative pass followed by “retry if wrong,”
but it buys generality by keeping an additional locking path and broad source
exclusion. Its successful measurements do not support transplanting that tradeoff
to a 200 ms WAN transaction among 1 ms regional writes.

### Symbolic or lazy commands help a narrower class of programs

Lazy State Determination returns futures rather than immediately materializing
values. Write keys can themselves be futures. Its OCC commit resolves them while
locking their underlying input keys, acquires the discovered write keys, and
validates concrete observations and conditions. Its prototype combines 2PC and
wound-wait; unresolved destination partitions can require another exchange.
Concrete-state consumers do not obtain the full benefit. See §§3.2–3.4 and 4 of
[Improving the Scalability of Transaction Processing with Lazy State Determination](https://sites.fct.unl.pt/hipstr/files/vl20_-_lsd.pdf).

This can represent `v := v+1` and derived index keys symbolically until the
primary value is secured. It does not turn an arbitrary WASM query/result/write
chain into a commutative command. General symbolic conditions add an expression
and constraint mechanism; concrete broad source reads still need validation or
locking. Native associative folds are a deliberately smaller contract.

Lazy transaction evaluation instead commits placeholders and defers their
materialization. Its eager phase must determine outcomes, discover the necessary
footprint and perform required index work. Readers may inherit deferred work
and dependencies. See §§2.1–2.3 of
[Lazy Evaluation of Transactions in Database Systems](https://www.cs.umd.edu/~abadi/papers/lazy-xacts.pdf).
It can reduce work or move latency, but does not erase dynamic discovery or
justify counting acknowledged commands as fully evaluated SQL results.

### Predeclare outputs, then execute at their position

BOHM fixes transaction order and creates output-version placeholders before
execution. Reads may be unknown; writes must be deducible, declared, or predicted
with abort on a wrong prediction. Its shared-memory design uses an ordered input
and batch coordination. See §1 and §§3.1–3.3 of
[Rethinking Serializable Multiversion Concurrency Control](https://www.cs.umd.edu/~abadi/papers/rethink-mvcc.pdf).

This is the closest established bargain to fixed-before-execution. It supports
the motivation for separating output ordering from value computation. It does
not establish our distinct distributed protocol: per-output gates, participant
lower bounds, exact-position publication and captured read bounds need their
own safety/progress account. BOHM's common batch barrier cannot be imported while
claiming independent regional progress.

## A smaller proposed generalization: output envelopes

The strongest simplification to investigate is to declare a **complete
conservative mutation envelope**, rather than predict exact final keys. This is
our proposal/inference, not a mechanism proven by the cited papers.

An envelope can name primary rows, primary-key ranges, a tenant's target rows,
or whole target relations. It includes possible inserts and all allowed cascade,
trigger and extension effects. Source-only tables are not included merely
because execution scans them. Schema changes affecting this meaning must also
be ordered or excluded. Arbitrary dynamic SQL requires an explicit allowed
target closure; code cannot obtain unbounded mutation authority by discovering
another table after admission.

The protocol shape remains one mode:

1. Acquire write-only gates covering the entire envelope, with fair admission
   and a canonical acquisition order. Partial acquisition creates no read-blocking
   promises. The transaction cannot add gates during its executable phase.
2. After every gate is held, establish participant bounds and publish one exact
   position before executing user reads. This metadata step needs no user data
   computation. Conservative pending declarations cover potential observations
   of those outputs until their values and exact target set are known.
3. Execute all reads and dynamically discovered effects at that position. Capture
   read bounds, honor earlier pending versions, and keep every effect inside the
   declared envelope. A coverage violation is an explicit failure of the
   declaration, not an automatic expansion-and-retry subsystem.
4. Resolve the whole outcome durably. Publish only actual effects and release
   the envelope; unused potential outputs never become rows or versions.

The conditional progress argument is finite predecessors plus non-overtaking
output admission, followed by read dependencies descending through fixed
positions. All position-finalization work must finish independently of user
execution, and every admitted program must terminate or have an agreed resource
failure. Recovery must resolve abandoned owners. Physical message arrival cannot
choose grants, positions or outcomes if consumers must derive one epoch fixpoint.
These are requirements for the proposal, not results established by the current
point-key experiments.

This supports unknown primary targets inside a declared target range: a MERGE
may choose rows during execution, and a cascade may discover children inside
its declared closure. It also removes the retained-gate expansion state from the
earlier index counter-probe. Stable row ownership can cover derived nonunique
index representations if publication and all predicate coverage are correct;
it need not acquire every value-dependent physical index entry as a separate
logical output. Until new values are known, index predicates may require broad
pending-row checks. Unique checks must be ordered observations of these pending
and committed rows, including absence; row ownership alone proves no uniqueness
guarantee.

The price is **potential-output exclusion**, including outputs eventually unused.
A point update can stay narrow. A WAN update with arbitrary targets across a hot
relation may exclude every competing writer to that relation for its execution.
An index query may also wait on a conservatively relevant earlier pending row.
This is more localized than source protection, but is not an unconditional
regional-latency guarantee. Representation compression does not reduce the
semantic overlap. Oversized envelopes need an explicit supported-operation or
admission policy; silently broadening them would conceal the central tradeoff.

## Keep versioned data products an explicit semantic alternative

Iceberg writes immutable files and metadata, then atomically swaps a table
metadata pointer. Some expensive append work survives a retry; other operations
must revalidate their assumptions, such as whether rewritten files remain
present. See [Iceberg's reliability account](https://iceberg.apache.org/docs/latest/reliability/).

An ETL job can therefore deliberately consume a named immutable source version
and publish a new output generation. Source writers keep moving, and publication
mostly contends at the output. This is an attractive application contract, not
a transparent implementation of every serializable scan-current-and-update
transaction. Source/target feedback, current invariants, returned results and
activation semantics determine whether that transformation is legal.

## Decision exposed by the evidence

Output envelopes are the smallest credible extension of fixed-position
execution found in this investigation. They delete source revalidation,
footprint prediction, repeated expansion and component arbitration by making
possible outputs an explicit contract. They do not make a broad possible output
set cheap or localized.

If that restriction is unacceptable, the strongest alternatives are genuine
dynamic dependency certification with possible repeated failure, or dynamic
pessimistic/MVCC coordination that constrains source readers/writers or reader
victims. KSFTM prevents claiming that no stronger progress mechanism exists;
its machinery also prevents describing that progress as free. The next brief
should choose and state this bargain rather than quietly accumulate all three.
