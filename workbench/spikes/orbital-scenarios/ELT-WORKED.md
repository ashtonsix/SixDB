# Worked mutation and ELT histories

[dynamic_sql.py](dynamic_sql.py) contains 13 executable application histories;
[check_dynamic_sql.py](check_dynamic_sql.py) checks their returned results,
invariants, failure cases and exact replay. These extend the fixed-key workload
suite with computed writes. They use the unchanged `FixedStore` and admission
core. They are not a SQL implementation or a distributed recovery proof.

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_dynamic_sql.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/dynamic_sql.py \
  --output build/workbench/orbital-dynamic-sql/probes.json
```

The retained JSON includes complete application histories and executable source
hashes. All 13 histories pass serial replay; all 11 checks pass. Counts describe
logical work, never elapsed performance. Control delivery is immediate, and the
key universe is finite and includes known absent slots. A real relation/range
envelope must cover identities that do not yet exist; enumerating these test
slots is not an implementation of that facility.

## Programs and discriminating results

**MERGE, upsert and ingestion corrections.** Two customer rows have unique
emails, source sequence numbers and optional maintained email-index entries.
One feed record changes a customer's email. A preceding update changes its old
email after discovery. Independently admitting predicted index entries misses
the now-required old entry, aborts, and succeeds only after rediscovery. The
same row program with a derived index has stable primary outputs and succeeds
without that miss, but lookup becomes a row scan. This is a representation
tradeoff, not free index maintenance.

A separate concurrent unique-key race fixes positions for two distinct primary
rows before either checks the email. The earlier position succeeds; the later
reader waits and then commits a no-write constraint rejection. There are no
exclusive unique-key gates and no serialization abort. The constraint read
covers the relevant absent value as well as existing rows. This small example
uses a full row predicate read rather than claiming an implemented index plan.

An upsert starts from an absent row, then deletes and reinserts it. Deletion
retains the source sequence tombstone. A delayed older event cannot resurrect
the row; replay of the same merge is a no-write outcome. A later conflicting
email produces a business rejection. The accepted ingestion contract is one
source's usable per-key event order, not reconciliation of independent sources.
This is a one-record MERGE program, not all SQL MERGE matching/error rules.

**A maintained nonunique index need not own separate mutation gates.** The
row-owned-index history admits one logical primary row. Its maintained index
entries belong to that row's versioned outcome. The row promise stays unresolved
through partial physical row/index installation. A predicate lookup checks the
collection's pending row effects and read bound: both a matching lookup and an
ultimately nonmatching lookup wait. Another primary-row writer completes at a
later position while the first index update is incomplete. Afterward, the waiting
queries use retained index versions and return the same cut as primary-row
reads, even though the unrelated row's physical head has advanced.

This removes index-entry footprint retries by accepting conservative predicate
waiting and versioned projection work. It does not show that every index query
is cheap or nonblocking. The probe uses one known row-partition authority. For
a remotely routed index, **every authority whose predicate evidence a possible
new value could affect must contribute bounds and receive the unknown-effect
promise before the final position is fixed**. An alternative is a partition-local
index whose query visits the known row authorities. A reader on an uninformed
remote index shard cannot safely return absence and later receive a backdated
entry. Centralized access to the test store is not a solution to that routing
problem.

**Predicate deletion, FK cascade and returned effects.** Delete matching parents
and all referencing children, returning the exact parent and child IDs. Insert a
new child after each discovery pass: four passes exhaust a four-attempt budget
without deleting anything. A quiet fifth round succeeds, which is no general
progress guarantee. In the complete-envelope comparison, a family domain is
declared before current membership is read. A child added before the final
position is included in the one execution. An outside-tenant write completes;
a child insertion and a nonmatching parent edit inside the domain wait. A later
insert referencing the deleted parent commits a foreign-key rejection. The
domain covers nine cells for three actual deletion effects: accepted exclusion
is broader than the eventual write set.

**INSERT SELECT and live destination corrections.** A new qualifying source
row changes the discovered destination identities and defeats a predicted-row
envelope. Declaring the complete destination relation avoids discovery: the
program captures its final source cut, writes both qualifying destinations, and
allows a later source update and an outside-target write to complete. A writer
inside its target envelope waits. No source relation is admitted solely because
it supplies inputs.

A separate representation uses two immutable destination-generation objects.
The program captures current destination objects at its fixed position, applies
the source changes, and publishes the two replacements together. A concurrent
destination correction survives. Publishing a previously built stale root would
lose that correction in either serial order. A reader between participant
installations waits and then sees both new objects. Publication owns two logical
objects, while private construction still copies their contents and a point edit
to the same destination partition waits behind that ownership.

An explicitly authoritative snapshot job can instead publish a supplied older
source image and overwrite destination corrections. The model demonstrates that
different contract separately; it is not an optimization of preserving live
INSERT SELECT or MERGE semantics.

**Snapshot plus ordered CDC activation.** A final history starts from a trusted
source snapshot at sequence100, sorts and deduplicates events through104, applies
an update, deletion, insertion and correction, and ignores an event preceding the
snapshot. It privately builds two source-owned target images, then atomically
publishes their references with checkpoint104. The source has already advanced
to105. A reader cannot observe the new checkpoint with the old second object;
repeating activation is a no-write outcome. This exercises deduplication and
activation, not obtaining a consistent source cut or implementing a connector's
snapshot/stream collision window. Preserving independent live target edits
requires the normal reconciliation program and its complete mutation envelope.

## What the complete-envelope candidate buys

The successful conservative controls declare a complete mutation domain before
final execution, acquire it once, fix the position, then discover and compute
freely inside it. They do not predict rows and keep expanding a held set.
Closure includes possible new identities and cascade targets, and index effects
belong to their logical row outcomes with complete predicate-authority coverage.
An arbitrary trigger that can write outside the declaration needs a larger
declaration or an explicit unsupported-operation result; safe repeated aborts
are not a progress mechanism.

For finite programs, finite earlier work and eventually delivered outcomes,
complete coverage eliminates this source of footprint retries. Reads may still
wait for earlier promised outcomes. A deadline may still return failure, and a
lost participant still needs the surrounding outcome/recovery protocol. The
frozen timing sweep already exposes those deadline and queueing costs.

The accepted price is target-domain exclusion. A collection-wide unknown target
requires a collection envelope; a known marker does not. Nonmatching target
writers and fresh predicate queries can wait. Different source-only relations,
tenants or target domains can continue. The tests support that semantic choice;
they do not establish an efficient range-envelope data structure, optimal scope
planning or a general distributed progress theorem.

## Coverage of the original 17 write families

The reference is [WRITES.md](reconsideration/WRITES.md). “Executable” below means
a complete small application history, not a full database feature. “Reused”
means existing probes cover the named core behavior. Unlisted feature details
are not implicitly demonstrated.

| Original family | Evidence and remaining boundary |
| --- | --- |
| 1. Broad price UPDATE | **Reused:** bulk read-dependent increments versus reports and point edits in `comparison_inputs.py`/`fixed_simulation.py`. **Executable analogue:** dynamic indexed MERGE. Joined membership and arbitrary SQL expressions remain reasoned scope choices. |
| 2. Retention DELETE | **Executable analogue:** predicate parent deletion with exact returned IDs and cascade. **Reasoned only:** compact ordered range tombstones and physical reclamation. |
| 3. Tenant erasure | **Executable analogue:** complete family envelope and outside-tenant progress. **Reasoned only:** generation/liveness hiding, external blobs, physical erasure and callback semantics. |
| 4. Append-only ingestion | **Executable:** upsert into an absent row, uniqueness conflict, ordered source-owned image activation. **Unresolved:** general allocation/routing for unbounded new identities; tests use absent slots. |
| 5. INSERT SELECT | **Executable:** moving destination set, complete destination envelope, source progress, preserving two-object publication, and explicitly historical replacement. |
| 6. MERGE/upsert | **Executable:** indexed update, absent-row insert, duplicate/old input, delete/reinsert and unique rejection. **Reasoned only:** multi-match MERGE errors, arbitrary multi-row expressions/triggers. |
| 7. CDC duplicates/corrections | **Executable:** sequence tombstone, delayed replay, deduplication, ordered correction and atomic checkpoint activation. **Contract restriction:** one usable source order; independent-source reconciliation is a normal envelope-bound program. |
| 8. Snapshot catch-up during CDC | **Executable:** activation from a trusted cut plus ordered overlapping stream, source advancing beyond the published cut. **Unresolved here:** obtaining that cut, chunk windows, gap detection and live collision reconciliation. |
| 9. Backfill/index build | **Executable subset:** row/index outcome visibility and immutable generation publication. **Reasoned only:** online build capture/catch-up, schema activation, concurrent DDL and repair. |
| 10. Partition/table replacement | **Executable:** stale-root counterexample, preserving generation merge, authoritative replacement, partial multiobject install and coarse same-partition exclusion. |
| 11. Materialized-view refresh | **Executable analogue:** source-derived generation refresh. **Reasoned only:** arbitrary aggregate/join maintenance and always-current views. |
| 12. Incremental aggregates | **Reused:** native fold and grouped increment probes, including returned-value/conditional exclusions. **Reasoned only:** delete-sensitive MIN, distinct, top-k and numeric semantics outside the modeled algebra. |
| 13. Unique/FK/cascade | **Executable:** unique race, committed constraint rejection, child insertion, expanding cascade and complete closure. **Unresolved here:** distributed index routing/authority implementation and arbitrary deferred/cyclic constraints. |
| 14. Payout/rebalance | **Reused:** quota, inventory and on-call application invariants in `invariant_probe.py`/`check_fixed_execution.py`. **Reasoned only:** large joined ledger selection and external payment dispatch. |
| 15. Migration/repartition | **Reasoned contract only:** representation-only copying should not become SQL mutation ownership. **Unresolved here:** mutable routing cutover, catch-up/replay and reclamation. |
| 16. Blind assignment/delta batch | **Reused:** blind bulk visibility, hot/disjoint writes and fold comparisons. They do not establish arbitrary constrained batch semantics. |
| 17. RETURNING/triggers/audit/IDs | **Executable:** merge old/new results, exact deletion IDs, explicit accepted/rejected outcomes; **reused:** intermediate-result fold exclusions. **Reasoned/unresolved:** arbitrary trigger closure, generated-ID allocation and external side effects. |

The coverage is now sufficient to distinguish the proposed contention contract
from discovery-and-retry and from historical-product semantics. The remaining
items must stay explicit boundaries of the architecture rather than being
claimed as implemented by these examples.
