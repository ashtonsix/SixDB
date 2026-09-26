# Choosing the contention contract

2026-09-26. **Declare complete possible mutations, allocate and announce their positions,
then release allocation access and execute at one fixed serial position.** This is the recommendation now carried into
[brief](../../../orbital/BRIEF.md). It replaces the earlier proposal's source
renewal and bounded retries, and the original brief's retained read protection
and component arbitration. It does not add an automatic locking or optimistic
fallback.

The SQL examples here exercise an **Engine binding**, not Orbital's knowledge
of database structures. Engine supplies complete dependency and effect coverage,
including index and constraint consequences; Loom binds execution and resources.
Orbital enforces ordering and durable outcomes over opaque descriptions. Another
application can describe durable objects or memory regions through its own
binding. It still owes complete effect coverage before fixing the position;
conservative declarations can make interference broad. The probes test histories
and overlap rules, not a production Engine–Loom–Orbital interface. Calico's
memory-like adoption model is not established for this proposal.

The bargain is explicit: source-only observations do not hold up independent source
writers, but uncertainty about outputs can make allocation and value/predicate
waiting broad. This is the smallest coherent mechanism we found that handles stable
point writes, broad observations and dynamically computed SQL effects without
depending on contention retries succeeding. It is **not** the fastest candidate
in every modeled workload, nor a guarantee of low latency for every pair of
transactions whose eventual writes happen to be disjoint.

The [earlier comparison](COMPARISON.md) retains the 1,783-run timing study and its
27 semantic histories unchanged. There, “BRIEF2” means the former
compute/promote/renew proposal. This follow-on adds computed SQL programs,
conservative-envelope costs, distributed authority counterexamples and explicit
epoch-publication dependencies. The earlier cores are reused unchanged. The final
[allocation-lifetime comparison](PIPELINING.md) adds ordered pending versions in
new cores: exclusive allocation ends once the position is published, before
computation. Its 148 timing runs, semantic probes and costs have separate provenance.

## Why this choice, rather than another repair

| Candidate | What it does well | Why it is not the selected general path |
| --- | --- | --- |
| Original component arbitration | Preserves useful preparation and chooses compatible work. | Broad read protection recruits source writers; component collection, union reservations, invalidation and recovery enlarge the mechanism. The timing model gives collection an optimistic complete view. |
| Dynamic priority locking, including precise predicates | Handles unknown targets; a semantic MAX lock permits some losing-row changes that a collection mutation envelope excludes. | Retains source exclusion during slow work and requires victim fencing, lock discovery and possible restarts. A broad scan still constrains source writers. |
| Optimistic MVCC, SSI/SSN-style certification | Short actual-output ownership; source writers move. | Safe retry does not guarantee completion against new conflicting transactions. More retries did not repair the hot workloads. These are distinct certifiers, not all equivalent to the conservative test implementation. |
| Output admission after computation | Orders actual outputs without declaring possible targets. | The computation can already be stale. Refreshing it after admission can change its targets again. |
| Fixed execution after footprint prediction or expansion | Removes renewal when the prediction stabilizes. | Moving winners, cascade members and derived index keys defeat predictions. Retaining gates across repair helps some cases but introduces another lifecycle without solving arbitrary discovery. |
| Starvation-free dynamic MVCC | KSFTM establishes a genuine stronger progress result under its assumptions. | Reader identities, timestamp intervals and coordinated victim handling are substantial machinery to distribute. We do not claim this route is impossible. |
| Symbolic/lazy execution | Defers concrete reads or work for suitable commands. | Arbitrary extension byte programs still need concrete inputs. Futures, condition resolution and deferred reader dependencies do not remove the general problem. |
| Complete mutation envelopes and fixed execution | One admission followed by one computation; source reads do not retain exclusion; targets can be discovered inside the declaration. | Selected with allocation-only access and ordered pending versions, accepting conservative dependency waits, extra preparation exchanges and explicit resource failures. |

The [prior-art review](reconsideration/CONVERGENCE-PRIOR.md) links and analyzes
the primary sources, including BOHM, pessimistic MVCC, SSI, SSN, KSFTM,
Chardonnay, LSD and lazy transactions. BOHM is the closest established bargain:
order outputs before computing their values, allow dynamic reads. Our
distributed admission, bounds and publication need their own argument; BOHM's
shared ordering and batch barrier do not establish regional locality here.

The strongest contrary example remains useful. A transaction selects `a=100`
over `b=50`, waits remotely, then marks the winner. Precise MAX protection permits
`b:=60` and blocks `b:=101`; a whole-collection mutation envelope blocks both.
The audit checked 576 changes across 64 score states. Conversely, when the
transaction writes a separate known marker, fixed execution lets even the
overtaking source writer commit physically first, with the marker transaction
earlier in the serial history. Retained MAX protection blocks that writer.
Neither bargain dominates; adding both automatically would lose the requested
simplification. See [the executable audit](ENVELOPE-AUDIT.md).

## The complete mechanism

An envelope is a conservative bound on **logical mutation identities**, including
absent identities that could be inserted. A row list, stable key interval,
tenant domain or target relation can supply it. A mutable SQL predicate alone
is not such a bound. General SQL can use the entire closure of its target
relations, including trigger/cascade/extension effects; that declaration can be
expensive. The source scan is not added just because it supplies inputs.

Allocation gates conflict by logical coverage, including row/range overlaps. Each
authority grants its complete local group or none. Transactions visit authorities
in a common order; agreed non-overtaking queues contain only the current group,
never anticipated future groups. Partial admission holds no read-blocking
position promises. It can still hold allocation gates elsewhere. Pending-version capacity must be
secured before any provisional read blockers are created.

Only after all gates are held does the transaction establish bounds at every
possible effect authority. Each bound follows committed and already announced output positions and relevant
read stamps. A provisional announcement blocks reads at or after that bound. The
coordinator chooses a unique `c`, publishes it everywhere and receives replies
before the program reads, then releases allocation access. Pending versions remain;
later transactions may allocate and compute while awaiting only the predecessor
values they actually require. Raising a provisional bound to `c` releases readers
below `c`; it cannot invalidate a completed read. This exchange must use only
agreed metadata, not wait for target discovery, user computation or verification.

Execution reads at `c`, including its own tentative effects. Each read registers
its position before waiting or scanning, including range membership and absence.
New writers follow that stamp without waiting for the reader's completion.
Earlier announced writers may finish first. Dynamic source visits and arbitrary
query/extension chains are allowed at the same position. Mutation outside the
declaration fails explicitly; it does not grow ownership in place.

After computation, actual values and the result are staged durably, and one
recoverable outcome is decided. Each participant installs or aborts and resolves
all its coverage, including unused possible outputs. Readers crossing relevant
pending versions wait. No-write conditions create no fake versions. Physical
installation can arrive out of logical order. A committed full replacement may
hide an older pending version only where it covers all of that version's possible
effects; a newer no-write outcome exposes the earlier pending value instead. Local execution
can collapse these steps within one epoch; compatible folds must preserve
complete results and atomicity, not merely final deltas.

This deletes component membership, arbitration verdicts, dependent preparation
invalidation, source renewal, position promotion and automatic footprint-repair
attempts. What remains is short output allocation, read/version metadata, possible-effect
announcements and atomic outcomes. Pending versions now form per-identity chains;
shorter exclusion does not mean fewer total states. Complete declarations and correct authority
coverage are substantive obligations, not free planner information.

## Index locality is a separate cost

Physical secondary-index entries can belong to the logical row's versioned
outcome. They need not acquire separate exclusive gates. But every authority
whose predicate evidence could change must contribute its bounds and see the
announcement **before** the position is fixed. Otherwise a remote reader can
return absence and later receive a backdated matching entry.

Row-partition-local indexes keep this authority near their rows; a query may
need to visit many partitions. A global value-partitioned index needs known
routing or conservative advance coverage of all possible destinations. A tighter
declaration of possible values can reduce that coverage; calling the entries
“derived” cannot.

The audit applies the same actual `a:0→2` mutation in two cases. Exact value
information reaches one predicate authority and lets equality lookup `7` pass.
Unknown values reach two authorities and make that lookup wait. Both own just
one primary row and let a disjoint primary writer complete. Thus even a narrow
WAN row update can delay an otherwise unrelated indexed regional operation.
There is no unconditional 1 ms guarantee for work inside this conservative
effect domain. This qualification is as important as broad target dependency waits.

Unique and foreign-key checks need ordered predicate reads and tentative own
effects. Serial replay alone does not catch a program assigning the same unique
value to two of its own rows while checking only the external snapshot; the
audit includes a separate invariant failure for that case.

## What the workloads say

The original comparison and envelope-locality adapter below retain allocation
through execution. They establish the motivation and conservative-coverage cost;
they are not timing results for the final shorter allocation lifetime. The
[148-run follow-on](PIPELINING.md) compares that change directly, including its
negative deadline/backlog results. All use synthetic ticks and service
units, **not measured milliseconds or database throughput**. Counts distinguish
success, failure and pending work.

- Broad read-only scans: 128 source writers all complete; arbitration p99 is
  93–105 across the original seed/width sweep, versus 1 for the original
  multiversion comparators. Removing source exclusion is the clear benefit.
- Sparse WAN output promises: waiting snapshots complete all 128 reports where
  the former BRIEF2 completes 54 and fails 74. Fresh reports still wait for
  earlier effects; older cuts require an explicit freshness contract.
- Hot cross-shard updates: fixed execution completes only 16/128 by tick 1,200,
  with 112 queued, but drains all by 20,000 without renewal failure. The former
  BRIEF2 completes 6 and fails 122. Fixed execution is slower than the old
  protection policies here; admission supplies progress, not capacity.
- Compatible completion-only increments: at window 128/cap 32, fixed execution
  completes all 128 with p99 306 and regional p99 1. Arbitration and wait-die
  achieve p99 202 and 191 with the same grouping. Folding's benefit is shared;
  these numbers do not justify claiming fixed execution wins throughput.
- Uncontended distributed work: modeled p99 is 80 for fixed-known versus 26 for
  old arbitration and 46 for former BRIEF2. Local short hot work remains 2.
  The coordination cost is real in this model; old graph/victim traffic remains
  underpriced, so these are not inherent performance ratios.

The new [envelope locality probe](envelope_locality.py) holds the actual program,
traffic and final single-row write constant, varying only the potential outputs.
It combines a broad source scan and slow remote input with source writers,
other-target writers/readers and outside writers. All cohorts complete in all
nine width/coverage runs. At width 16:

| Slow transaction's envelope | Source writer p99 | Other target writer p99 | Other target reader p99 | Outside writer p99 |
| --- | ---: | ---: | ---: | ---: |
| Actual single target | 1 | 1 | 1 | 1 |
| Whole target relation | 2 | 221 | 209 | 1 |
| Whole shard, negative control | 283 | 283 | 261 | 283 |

The extra source tick reflects modeled shared service, not source protection.
The target-domain cost grows with width; finite-key metadata is charged per key,
which a compressed range implementation need not do. Compression would not
remove logical waiting. The trace is identical to the fixed backend when the
envelope equals its exact writes, and unused coverage creates no mutations.

## HTAP and ELT coverage

[ELT-WORKED.md](ELT-WORKED.md) maps all 17 original write families to executable,
reused, reasoned or unresolved evidence. Its 13 new application histories compute
writes and returned results, rather than merely supply a transaction shape. The final pending-version semantics are
separately checked in [the lifetime study](PIPELINING.md); those application
histories originally use the more restrictive held-admission core.
They cover indexed MERGE, unique races, inserts, duplicate/late ingestion,
delete/reinsert tombstones, expanding cascades, INSERT SELECT with changing
destinations, row/index partial visibility, live destination corrections,
historical replacement, and atomic snapshot-plus-CDC activation.

The conservative controls succeed where repeated footprint discovery misses new
cascade children or qualifying destination keys. They also block nonmatching
writers inside the declared domain. Source-only and outside-domain updates can
continue. A maintained row-owned index removes independent entry discovery while
introducing conservative predicate waiting.

Generation publication is an explicit application choice. Replacing a stale root
can lose live destination corrections; reading the destination at `c` and applying
the changes preserves them, while holding that destination object. An
authoritative historical product may deliberately replace them. Ordered CDC
activation uses a trusted snapshot cut and source-owned targets; it does not
solve obtaining that cut, multi-source reconciliation or a connector's collision
windows by renaming them “publication.”

These cases sufficiently discriminate the contention choice. They do not
implement all SQL: arbitrary trigger closure, unbounded identity allocation,
online schema/index activation, repartitioning and full recovery remain explicit
engineering boundaries. The finite range tests are overlap oracles, not a
production interval manager.

## Progress and composition

The conditional argument has two parts. Canonical authority acquisition and
current-group-only queues avoid gate cycles; finite agreed non-overtaking queues
let finite predecessors drain. Once positions are exact, data waits descend
through those positions. Provisional bounds must become exact or abort through
metadata work independent of the blocked programs. Dynamically discovered
source reads acquire no new allocation gates. Metadata-space reservation must
precede provisional blockers; a later capacity wait can create a cycle with an
older data program. Multiple pending versions require owner-specific resolution
and preservation of version order through reverse physical installation. The audit explores 1,705 gate states
and 2,947 transitions, including local agreed-FIFO alternatives, without an
incomplete terminal state under terminating-owner assumptions.

Fairness has a cost: if WAN holds `x`, a queued atomic `(x,y)` request delays a
younger `y` writer although nobody holds `y`. Indirect dependencies can extend
such queues. With the final lifetime, those gates last through allocation rather than WAN
computation; pending-value dependencies can still propagate WAN delay. We remove
explicit component unions, not every transitive delay.
Crashes, program nontermination, overload and finite resource limits still need
agreed failure/resolution rules. Late source discovery can fail when the required
history has expired; it must never substitute a newer value. Successful one-pass
execution is conditional on retained inputs and terminating work.

Deterministic epochs constrain every queue, bound, decision and timeout. Physical
arrival may affect which witness input is agreed; given that history, physical
execution schedules must reproduce the same fixpoint and message bytes. The
existing simulator's serial oracle does not prove that agreement property.

The [historical composition probe](reconsideration/COMPOSITION.md) retains 10
exact-byte histories, including 504 local schedules each for accepted and rejected
extension chains. Available authoritative inputs allow query/extension/write in
one epoch. Missing remote facts leave agreed continuations for later input.
Independent message origins and duplicate delivery preserve the authored outcomes.
This tests the boundary to dissemination, not a new routing design.

The probe's epoch-wide Firecracker verification gate was a mistaken assumption,
superseded by Ashton's transaction-scoped requirement. Its 200/199/198 illustrative
delays for unrelated responses, and their removal by separate execution shards,
describe that historical graph. They do not establish a placement requirement.
A transaction must satisfy all applicable extension verification requirements,
including approved exemptions; unrelated transactions in the same epoch can
publish. Real effect dependencies and resource contention remain. The probe and
retained evidence are unchanged and do not test this corrected publication rule.

The [composition note](reconsideration/COMPOSITION.md) records the corrected
scope and remaining obligations. Verification, witness routing, LSN aggregation
and arborescence construction are not implemented here; full distributed
confluence and recovery remain to be established.

## Evidence and reproduction

The envelope evidence contains 13 application histories, 42 audit histories (including
expected invalid controls), 10 composition histories and 9 locality runs. The
audit also includes the finite gate and MAX searches. Tests check serial results,
separate application invariants, replay, unused coverage, late authorities and
negative controls. Neither the count nor repeated replay is a proof of the
general protocol.

Run from the repository root:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_dynamic_sql.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/check_envelope_audit.py
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/dynamic_sql.py --output build/orbital-convergence/dynamic-sql.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/envelope_audit.py --output build/orbital-convergence/envelope-audit.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/composition_probe.py --output build/orbital-convergence/composition.json
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/envelope_locality.py --output build/orbital-convergence/envelope-locality.json
```

The [retained selection](evidence/contention-convergence/RECOVERY.md) links compact
outcomes, exact source identities and a recoverable source/raw-result bundle.
The previous comparison remains independently reproducible from its own bundle.
