# A concrete candidate: version ordering, write certification, finite failure

This is an algorithmic candidate and executable point-key probe, not a selected
Orbital protocol. The previous catalog did not earn a recommendation. This probe
tests a specific way to remove broad read exclusion while retaining serializable
outcomes and allowing unrelated work to finish out of transaction-start order.

The explicit proposed tradeoff is **no guaranteed completion of an arbitrarily
contended updating transaction**. Failed attempts get new ordering labels, up to
an agreed attempt/work budget, then return serialization failure. There is no
component arbitration, partial yielding, or exclusive fallback. This is a
substantial service-contract choice, not a claim that retry tuning ensures progress.

## The rule

Give each attempt a unique serialization label t. This label is distinct from
the transaction's stable identity and from its physical completion order. Each
committed key version has its writer label w and the greatest reader label r
that has consumed it.

1. A read selects the committed version with greatest w <= t and raises that
   version's r to at least t. Selecting the version and registering the read are
   atomic with respect to certification. Own writes come from the private buffer.
2. Computation and dynamic discovery continue without reserving source keys
   against newer writers. Buffer all effects privately; finish the entire program
   before making write-certification promises.
3. For each buffered write at t, find its predecessor version. Reject the whole
   attempt if that predecessor's r > t: inserting this write into the history
   would change an observation already made by a newer transaction.
4. Otherwise publish all writes atomically with writer label t. An older blind
   write may be inserted behind a newer committed version; the newer version
   remains the head. Physical completion order need not equal serialization order.

This is the committed-only MVTO family, rather than a new isolation claim.
[MVTO+ in Aguilera et al., section 3](https://www.cs.cornell.edu/~junxiong/homepage/MVTL.pdf)
describes the buffered-write version; the classical timestamp check appears in
[Bernstein, Hadzilacos and Goodman, section 5.3](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/05/chapter5.pdf).

The important difference from end-of-transaction read-version validation is
that a source can change after it was read without rejecting the reader. The
new writer simply belongs later in the serial history. Read metadata protects
an ordering relationship, not an interval of unchanged live data.

## Moving the write decision across shards

Atomic certification is the small model's central idealization. A candidate
distributed realization has write participants validate and promise their local
output versions, then resolve one durable commit/abort outcome. Each participant
checks its **whole local write set** together and rejects immediately on an
incompatible claim; there is no blocking queue of future certification claims.
Rejecting any participant rejects the whole attempt. Prepared writes remain
private until their atomic outcome permits visibility.

While a promise is unresolved, a read waits only if the promised version would
replace the committed version it would otherwise select. Older reads can still
read predecessors; a reader selecting an already committed later successor need
not wait. Unrelated keys require no shared completion frontier. Other writers
on promised keys reject in this conservative model.

Read-only participants do not retain a per-transaction source lock requiring
release at the final decision. Their read evidence must already be agreed and
durable before it is relied on. Version retention is a separate necessary cost.
No claim is made here that Orbital already implements that read-evidence service.

This does **not** eliminate delays on the actual write footprint. A cross-shard
transaction can promise y while its request for x is in flight. Another reader
of y may then wait until x rejects and the abort is resolved. The model retains
that counterexample. Immediate rejection avoids waiting *behind* x's holder,
but response/abort delivery and failure recovery still take time. The absence of
component reservations is not proof of absence of every convoy.

The implementation has no network or recovery protocol: participant checks,
commit installation and abort release are atomic modeled events. Durable promise
recovery, delayed messages, fencing and atomic multishard visibility are unfinished
obligations. SQL write footprints must include implicit effects such as indexes
and constraints; they are not simply the rows named in an UPDATE.

## What the executable probe establishes

[mvto.py](mvto.py) executes the rules, rather than only checking whether a proposed
result has some serial witness. Every completed trace is compared with executing
its committed transactions in timestamp order, including their read observations
and final state. [Retained evidence](evidence/mvto.json) records the source hash.

| Case | Observed result in the model |
| --- | --- |
| Old maximum read, newer insert, then narrow output | Both commit; the old transaction writes its observed maximum even though a larger value now exists. |
| Newer inserter first reads the output marker | The old output write aborts; its insertion would invalidate that newer read. |
| Broad read/modify/write plus one newer point RMW | The entire broad attempt aborts; none of its partial writes appears. |
| Old blind multi-key batch finishes after a newer blind point write | Both commit in timestamp order; the newer point value remains visible. |
| Three fresh broad attempts, each overtaken by a point RMW | All three fail. The rule provides no starvation guarantee. |
| Pending remote work with 100 unrelated local updates | All 100 finish before the pending transaction. This is a logical trace, not a throughput result. |
| Same-participant x/y certification behind an existing x promise | Rejects without reserving y; local y work completes. |
| Separate-participant bridge promises y before requesting x | A y reader waits until x rejects the bridge. Unrelated z still completes. |
| Prepared writer versus old, affected-new, and unrelated readers | Only the read whose selected version would change waits. |

There are 16 authored traces, plus all 1,680 interleavings of three point-key
read/write/commit programs forming an x/y/z cycle. All committed outcomes pass
the timestamp-serial check. This is finite validation, not a general proof.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/reconsideration/mvto.py
```

`--full` includes complete event traces. `--output PATH` retains either format.
No clock time, queue service rate or memory cost is modeled.

## Composition with Orbital's epoch fold

The candidate can fit the brief's epoch model, but the probe does not yet
demonstrate that composition. It changes preparation as well as contention;
it is not an additional resolver for the existing retained C1 locks.

MVTO alone does not determine an epoch fixpoint. Starting with x = 0, consider
W10 writing x = 1 and R20 reading x. The retained counterexample produces:

| Physical operation order | Read result | Write outcome | Final x |
| --- | --- | --- | --- |
| W10 certifies before R20 reads | 1 | Commit | 1 |
| R20 reads before W10 certifies | 0 | Abort | 0 |

Both are serializable; they also disagree on retained versions and read evidence.
They cannot both be permitted schedules of the same Orbital epoch. Serialization
labels do not select between them. Updating a version's reader label with `max`
commutes only after the referenced version has been selected consistently.

A compatible embedding would derive the logical order of noncommuting read,
certification and decision transitions from previous agreed state and epoch
input. Independent computation may run concurrently, and speculation may execute
out of that order, but physical completion speed cannot choose observations,
winners or protocol outputs. Internal read events need not each be witness
events if the fold derives them. The current probe instead receives an authored
agreed operation sequence; it does not implement this derivation.

That logical order must not require an entire older transaction to finish before
ready unrelated work can advance. A shard can finish its epoch with agreed
`Pending(T, continuation)` and outgoing requests, complete unrelated local work
in its next epoch, and consume T's remote response in a later agreed epoch.
This preserves in-order epoch publication without a WAN completion barrier.
[Locality](LOCALITY.md) explains the distinction. Any local computation deferred
across epochs also needs a deterministic continuation boundary; a replica-local
wall-clock cutoff cannot choose agreed progress.

This continuation argument does not settle the brief's
[extension](../../../../orbital/BRIEF.md#extensions-and-publication) and
[dissemination](../../../../orbital/BRIEF.md#dissemination-and-adaptation) requirements. A
same-epoch extension/query/write chain needs derived internal progress, with
exact bytes at ordinary extension boundaries. Firecracker output verification
gates visibility of its execution epoch; marking other work pending cannot
remove that gate. Early propagation and multiple origins deriving protocol
messages also need to compose with the agreed outputs and publication rules.

The object's associative fold remains an owning-module responsibility. Native
compatible contributions must not automatically become ordinary MVTO
read/modify/write programs: that conversion can introduce conflicts and remove
the algebraic freedom being preserved. The object must define how contributions
become versioned observations and how read evidence and certification interact
with them. If a read may occur between contributions' serialization labels, a
single final aggregate is insufficient to explain its result. The existing
[integer-delta fold probe](../folds.py) and this MVTO probe establish separate
properties, not their composition.

The replacement would therefore retain the core epoch/fixpoint requirement,
payload-persistence and frontier-admission model, and stable transaction identity,
while revising C1's retained source protection and the subsequent write decision.
Attempt labels, retries, discovered continuations, outgoing messages and durable
commit/abort resolution must all agree under replay. Late messages must identify
their attempt; a fresh serialization label does not change the global transaction
identity. None of these requirements is satisfied merely by passing a serial
history check. Atomic multishard visibility remains an additional open obligation.

## Where the candidate is still incomplete

**General predicates.** The probe handles known absent keys with explicit
tombstones. It does not discover or protect an arbitrary empty range. Scanning
only existing rows would miss a later backdated insertion. Versioned index/range
read evidence is a real mechanism to implement and charge, including false
conflicts and physical metadata sharing. Cicada's absent-key and range handling
provides a concrete reference, not an implemented feature of this probe.
[Cicada, section 3.6](https://hyeontaek.com/papers/cicada-sigmod2017.pdf).

**Agreed fold behavior.** The embedding described above has not been implemented
or checked. The decisive comparison needs the same epoch inputs under different
permitted physical schedules, with identical complete logical state, read
evidence, attempts, decisions and outputs. It must include native associative
updates and unrelated local completion while remote work remains pending.

**Labels, snapshots and reclamation.** A label t is not automatically an immutable
`AS OF t` snapshot: older labeled versions can still arrive on untouched keys.
Completed observations are protected by retained read evidence. Read-only
completion, cancellation and failover cannot simply erase that evidence. Keeping
evidence from aborted readers can cause conservative additional aborts, also
covered by a fixture. Timestamp allocation, promised real-time ordering, named
snapshot cuts and safe reclamation remain explicit design work.

**Wide writes under sustained conflicts.** One newer reader on one output key
can defeat a broad attempt. Giving it a fresh label permits another attempt but
does not ensure success. Adding priority prewrites or reservations could change
that bargain and restore obstruction over the write set. They are deliberately
not smuggled into this candidate as an automatic escalation path.

The positive result is specific: broad reads need not exclude newer source
writes, and certification can concern the output footprint. The unresolved
distributed and SQL obligations, and the explicit failure tradeoff, prevent
calling this a complete Orbital solution yet.
