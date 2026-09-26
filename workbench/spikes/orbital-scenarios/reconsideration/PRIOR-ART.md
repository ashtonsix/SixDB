# What prior art actually removes

Research for Ashton's 2026-09-25 reconsideration of preparation and contention.
These are alternative bargains, not features to accumulate in Orbital. The
comparison concerns arbitrary SQL and long HTAP/ELT work; throughput results on
short key-value transactions are not evidence that such work progresses.

The strongest counterexample to the current direction is that **progress need
not require discovering a connected contention component**. Whole-transaction
priority, a finite batch that new arrivals cannot overtake, or exclusive
ownership can each remove that problem. They pay different costs. Conversely,
**a private write set or immutable replacement does not by itself justify its
contents against concurrent SQL transactions**.

## 1. Whole-transaction wound-wait

Spanner's pessimistic serializable mode holds read locks through commit and
uses wound-wait: a younger requester waits; an older requester aborts a younger
holder. This prevents lock-wait cycles without collecting their connected
components. Spanner explicitly identifies long reads blocking latency-sensitive
writes as a risk. Its distributed write path still includes replication and
two-phase commit. Sources: [concurrency control, pessimistic
mode](https://docs.cloud.google.com/spanner/docs/concurrency-control),
[life of reads and writes, write transaction
steps](https://docs.cloud.google.com/spanner/docs/whitepapers/life-of-reads-and-writes).

**Orbital inference:** replacing partial yielding with a whole-transaction
winner could delete component closure, component reservations, arbitrator
selection, and descendant-by-descendant salvage. Unknown discovery can acquire
more locks incrementally. But a large transaction still accumulates a large
excluded region; favoring it postpones small transactions, and losing it throws
away all preparation. Progress additionally requires a stable priority across
retries, an acquisition rule that enforces it, terminating winners and recovery
of failed holders. Merely assigning an old ID is not that rule.

Globally ordered resource acquisition is another simple deadlock prevention
rule, but dynamic discovery can reveal an earlier resource after a later one
is held. Restarting, overclaiming a known envelope, or forbidding such discovery
is a substantive extra bargain.

## 2. Optimistic versions: remove read exclusion, retain the difficult decision

PostgreSQL SSI tracks nonblocking read dependencies and rejects dangerous
histories. Predicate tracking can grow or coarsen; a sequential scan requires
relation-level tracking, which can cause more serialization failures.
`SERIALIZABLE READ ONLY DEFERRABLE` waits for a safe snapshot and then avoids
that tracking. This special read-only route is materially simpler than general
read/write SSI. Source: [PostgreSQL 18, §13.2.3](https://www.postgresql.org/docs/18/transaction-iso.html#XACT-SERIALIZABLE).

Hekaton instead validates read versions and rescans for phantoms at commit.
Its implementation also carries commit dependencies, possible cascading aborts,
result withholding, logging and version reclamation. Sources: [Hekaton, §§6–8,
especially §6.2.1](https://www.microsoft.com/en-us/research/wp-content/uploads/2013/06/Hekaton-Sigmod2013-final.pdf).

**Orbital inference:** neither is merely “C1 without locks.” Discovery can run
without freezing its inputs, but observation sets, predicates, private writes,
commit admissibility and retry work remain. Conservative commit-time
revalidation of a live broad scan may never succeed; SSI can admit useful
overlap that revalidation rejects, but adds a different dependency mechanism.
Neither source establishes a bounded fallback for an arbitrary huge transaction
under perpetual conflicting traffic. Replica-independent serializability also
does not imply Orbital's identical metadata and outputs.

## 3. Calvin: choose the order before work

Calvin orders transaction inputs before deterministic locking and execution;
replicated execution removes the ordinary failure-driven distributed commit
agreement. Deterministic application aborts still require participants to learn
the relevant decisions. Unknown footprints use an optimistic reconnaissance
query, followed by checking and possible restart. Range/phantom support is
described as future work in this paper. Source: [Calvin, §§2, 3.2 and 3.2.1;
recovery in §5](https://www.cs.yale.edu/homes/thomson/publications/calvin-sigmod12.pdf).

**Orbital inference:** fixed order deletes winner negotiation and cycle
resolution, but footprint discovery is now speculative preprocessing rather
than successful retained preparation. A huge declared footprint can convoy
later requests. If reconnaissance keeps becoming wrong, its retry loop needs
another rule. “No 2PC” does not mean no input agreement, remote input delivery,
recovery, atomic external visibility or waiting for a failed participant.
Calvin's footprint assumption is a poor default for recursive SQL, adaptive
joins or discovery through changing data unless overclaiming is accepted.

## 4. Aria: unknown footprints, with a real progress argument and a real barrier

Aria executes a finite batch against one unchanged snapshot, buffers writes,
then deterministically admits transactions. Aborts retain relative order ahead
of new work; the first transaction succeeds, yielding eventual progress. No
footprint prediction or contention-component closure is required. However,
batches have barriers: long transactions delay the next batch. Its optional
fallback adds Calvin-style locks using the discovered footprints; changed
footprints go back to the next batch. The prototype targets short, one-shot
transactions, has no SQL interface and no range-query implementation. Source:
[Aria, §§4.2, 4.4–4.5, 6, 7.1–7.3](https://pages.cs.wisc.edu/~yxy/pubs/aria.pdf).

**Orbital inference:** this disproves that progress requires physical serial
execution. A finite non-overtakable batch with terminating executions can make
progress despite dynamic footprints. The price is potentially severe stall
scope, buffered writes, conflict checks and coordinated batch transitions.
Adding Aria's optional fallback would create two concurrency-control mechanisms,
not simplify one. Choosing only its conservative batch rule is a cleaner
candidate, with batch latency accepted explicitly. Independent shard epochs
do not already supply its common batch snapshot or barriers.

## 5. Coarse serial ownership: make scope, not conflicts, the admission unit

VoltDB executes single-partition procedures serially, allowing other partitions
to proceed. Multi-partition work introduces coordinated execution; its guidance
warns that long or frequent multi-partition procedures disrupt ongoing work.
Directed per-partition procedures reduce that disruption by explicitly giving
up one atomic transaction across all partitions. Sources: [VoltDB §1.3.2](https://docs.voltdb.com/UsingVoltDB/IntroHowVoltDBWorks.php),
[§7.5, directed procedures](https://docs.voltdb.com/UsingVoltDB/SimpleDirectedProcs.php).

SQLite shows an even blunter bargain: one writer per database, concurrent
snapshot readers in WAL mode. `BEGIN IMMEDIATE` acquires the writer position
before computation, avoiding later stale-snapshot upgrade failure by excluding
other writers throughout. Long readers can delay checkpoint completion.
Sources: [SQLite isolation](https://www.sqlite.org/isolation.html),
[WAL, §2.2](https://www.sqlite.org/wal.html#concurrency).

**Orbital inference:** a predeclared exclusive scope can remove row-level
arbitration and permit unknown accesses inside it. This directly supports huge
writes, but the duration of constructing and applying them becomes everyone
else's queue delay within that scope. A scope discovered after admission cannot
silently expand: it needs preclaiming, restart, or ordered acquisition. A
distributed exclusive lane still needs durable admission, old-owner fencing,
participant completion and one publication outcome; a SQLite file lock supplies
none of those. The simplification is concurrency policy, not an erasure of
distributed atomicity.

## 6. HyPer: separate analytics; tentative updates reveal the boundary

Original HyPer runs analytical sessions on coherent copy-on-write snapshots
while short transactions continue. It admits partition-crossing transactions
only after the other transactions finish, excluding new ones for their
duration. Recovery uses consistent archives and ordered logical redo.
Source: [HyPer ICDE 2011, §§III.B, IV.C–D](https://www.cs.albany.edu/~jhh/courses/readings/kemper.icde11.memory.pdf).

The subsequent tentative-execution work moves long transactions to a snapshot,
then validates and applies their effects in the serial engine. It expressly
reports that a transaction computing and storing warehouse turnover can suffer
high abort rates as orders arrive; weaker snapshot isolation is proposed where
appropriate. Monitoring granularity trades validation cost for false conflicts.
Source: [Mühe, Kemper and Neumann, CIDR 2013, §§3.2–3.4 and
5.5.2](https://www.cidrdb.org/cidr2013/Papers/CIDR13_Paper49.pdf).

**Orbital inference:** coherent read-only analytics can leave the contention
mechanism altogether. Read-then-write ELT cannot automatically do so while
preserving a single serializable transaction. “Compute off to the side and
apply quickly” is particularly suspect for millions of writes: validation and
physical application may themselves be large. This literature falsifies both
“serial ownership handles arbitrary mixed workloads cheaply” and “a short
apply step solves its long-transaction problem.”

## 7. Iceberg: move bulk work behind one atomic publication

Iceberg builds immutable data/metadata and atomically replaces a table metadata
pointer. Readers retain a consistent snapshot. Compatible operations can reuse
generated files and retry metadata publication; conflicting assumptions still
require rejection, such as compaction whose source files disappeared. Sources:
[Iceberg reliability, concurrent writes and retry validation](https://iceberg.apache.org/docs/1.4.2/reliability/),
[format specification, optimistic concurrency](https://iceberg.apache.org/spec/#optimistic-concurrency).

**Orbital inference:** loading fresh uniquely identified data, rebuilding a
derived table at a stated source snapshot, or replacing an owned partition can
avoid protecting every new row while constructing it. The coordination scope
becomes publication of the new relation/partition version. That replaces
in-place mutation with extra storage, staging, retention/GC and an atomic
metadata authority. It does not make an arbitrary `UPDATE ... FROM live_source`
safe: concurrent target changes, source-dependent invariants, indexes, unique
constraints and readers crossing objects must be addressed or excluded. A
table-level swap is not a general multi-table SQL transaction. Chunked commits
change all-or-nothing semantics; one final manifest can retain all-or-nothing
visibility only if readers consistently follow that publication.

## 8. Cicada: evidence against “aborts bad, therefore serialize”

Cicada combines optimistic multiversion execution with early checks and a
globally adjusted randomized backoff. Its experiments find that maximizing
committed throughput sometimes favors short backoff and substantial abort
rates. Sources: [Cicada, §§3.2, 3.9 and
4.6](https://hyeontaek.com/papers/cicada-sigmod2017.pdf).

**Orbital inference:** low retry success is not enough to justify a coarse
fallback; discarded work and per-transaction progress matter. Cheap failed
point operations differ from repeatedly discarding minutes of ELT. Cicada's
throughput tuning is not a starvation guarantee for the latter, and its
multicore results are not a distributed snapshot or publication mechanism.
This is a counterweight to selecting coarse execution merely because it has a
short explanation, not a proposal to import Cicada's many optimizations.

## What actually buys progress

These notions must not be conflated:

| Rule | What it establishes | What it does not establish |
| --- | --- | --- |
| Assign an old transaction a high priority | A preferred outcome | Any enforcement against later writers |
| Enforce wound-wait at every conflicting acquisition | An acyclic lock-wait direction | Small exclusion footprint; cheap aborted work |
| Preserve retry order ahead of new work on an unchanged finite batch snapshot | An earliest transaction that can finish, under terminating execution | Low latency of batch boundaries |
| Stop admitting new conflicting work; let old work drain | A finite outstanding set, if old work can finish without new admissions | Drain completion if paused work is needed to unblock old work |
| Admit one writer inside a known envelope | No concurrent writer can invalidate its work in that envelope | Bounded runtime, independence outside an unknown envelope, distributed fencing |

The last two are proposals for SixDB, not an implementation proven by a local
database analogy. Draining an existing part-lock protocol can deadlock if
admission stops a part needed by an incumbent. A fallback which releases all
old preparation and re-executes one complete transaction avoids that particular
state interaction, at the explicit price of lost work and a larger pause.

For arbitrary state-dependent, noncommuting writes there is no reason to expect
the following package for free: unrestricted long transactions, continuously
fresh observations, low-latency conflicting updates, bounded retries, bounded
retention and a tiny protocol. This is an engineering warning rather than an
impossibility theorem. Each candidate should state which requirement it limits.

The comparison worth retaining is therefore three distinct mechanisms, not a
hybrid containing all three:

1. **Whole transactions with an enforced priority and locks.** A small policy
   replacing component negotiation; accepts broad blocking and whole restarts.
2. **A conservative deterministic batch with no late overtaking.** Unknown
   discovery and eventual progress; accepts barriers, buffering and batch stalls.
3. **An exclusive execution envelope for difficult work.** Deletes fine-grained
   concurrency inside that envelope; accepts its full duration as interference.

Snapshot analytics and immutable dataset publication can be separate workload
contracts under any of these. They should remove work from general transaction
arbitration only when the requested semantics actually permit it. They are not
automatic upgrades to arbitrary SQL.

## Local Calico evidence is narrower

At Calico checkout `ac83c82b`, `xmem/README.md` and `xmem/DESIGN.md` describe
versioned pages, instance write claims and deterministic fold epochs; the pitch
script describes pre-agreed nonoverlapping write claims. Those demonstrate a
different ownership/publication decomposition worth comparing. They do not
establish that arbitrary SQL footprints, unknown predicates or bulk stateful
updates can avoid contention. The page-claim granularity and fold restrictions
are Calico design choices, not SixDB constraints.

Read-only sources inspected locally:
`~/calico/xmem/{README,DESIGN}.md` and `~/consurgent/pitch/SCRIPT.md`.
No Calico performance numbers are transferred into this comparison.
