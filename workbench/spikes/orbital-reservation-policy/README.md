# Reservation policy study

This study compared four queue rules against a frozen baseline. The selected
rule is **protect a waiter
when its older conflicts have drained**. It removes the original blocked-waiter
bridge and retains broad-request progress in the tested continuing workload.
It still lets an already admitted younger WAN holder delay that waiter and, once
protected, its other conflicts. It is not a universal improvement in locality.
The shared operator and fixtures now live in `orbital/spec`; the maintained
regression catalog selects 33 of these checks, alongside eight checks of the
actual Tx binding. This directory retains the full original comparison.

The [catalog](cases.json) has 69 selected checks: 42 completed graphs, 13 expected
violations and 14 witnessed histories, all accepted. They took 279 seconds in
aggregate with one TLC worker and a 512 MB heap. [Retained evidence](evidence/formal/SUMMARY.md) binds
the results to the exact imported study sources, configuration, checker and jar;
full logs and captured inputs are recoverable through its verified archive reference.
These are model-checking costs, not transaction performance measurements.

## One operator, four policies

[ReservationKernel](../../../orbital/spec/ReservationKernel.tla) retains the original local arrival
order while each request is waiting or held. A locally durable fixation or valid
cancellation removes it. A request conflicts when its declared write scopes
overlap. Actual conflicting holders always prevent a grant; no policy revokes a
holder. The policies differ only in which older waiting requests protect their
scopes against later requests:

| Policy | When an older waiter protects its scopes |
| --- | --- |
| `ordered` | Always. |
| `eligible` | Never. |
| `drain` | No older live request conflicts with it. Older live requests include holders and waiters. |
| `head` | It is the oldest live local request, including holders on unrelated scopes. |

After each agreed local command, grant the earliest eligible request in original
arrival order, repeat until none is eligible, then expose the resulting logical
state and outputs. This is local queue closure: it does not wait for a blocked
request, remote fact, physical message delivery or transaction completion.
Admitting later journal records need not wait for those physical activities.

The policy is part of the agreed interpretation of a run or lineage. Replay uses
that same policy and closure rule. This study does **not** upgrade old `ordered`
journal prefixes in place to `drain`: changing their grants can change positions.

## What distinguishes the policies

| Experiment | Ordered | Eligible | Drain | Head |
| --- | --- | --- | --- | --- |
| Holder X on x; broad B on x/y/z; later z-only work | B bridges X's delay to z | z progresses | z progresses | z progresses |
| A chain of overlapping blocked broad waiters | Delay reaches the tail | Tail progresses | Tail progresses | Tail progresses |
| Broad x/y request amid infinite narrow x and y arrivals, with every granted narrow request completing | Broad completes | Fair starvation trace | Broad completes | Broad completes |
| Same narrow arrivals plus an unrelated older holder that stays pending | Broad completes | Fair starvation trace | Broad completes | Fair starvation trace |

`cycle-*` checks use actual recurring arrival and local fix actions. Weak
fairness applies to those actions, not to an abstract eventual broad grant.
`cycle-narrow-*` separately establishes continuing useful narrow completions on
the starvation configurations. The unrelated-holder comparison deliberately
does not require that unrelated holder to finish: it distinguishes conflict
locality, rather than refuting head's weaker conditional progress argument.

Drain's limit is concrete. Let X hold x, B wait for x/y/z, and younger Y obtain y
before X fixes. When X fixes, B becomes a barrier, but Y may still be waiting for
a WAN round trip. New z-only work then waits behind B even though it does not
conflict with Y. `younger-*` checks retain this boundary. Depending on arrival
time, ordered can serve that later z work sooner because it never admitted Y
ahead of B. The native comparison must count both the earlier bypass benefits
and this later cost.

For drain, the progress argument is conditional: each request has a finite
older prefix; older conflicting live requests eventually retire; once they do,
the request protects its scopes. No new conflicting younger holder can enter,
so the finite set already held can drain. This is not a time bound and does not
require unrelated older requests to finish. Head requires the entire older live
prefix, including unrelated holders, to retire. The finite checks exercise this
argument; they are not an unbounded proof.

## Deterministic folding is part of the change

Changing only `CanGrant` is incorrect. Under bypass, several conflicting
waiters can be eligible at once. Original arrival order and numeric transaction
ID can select different winners. Moreover, choosing the earliest eligible
request is insufficient if grants may lag subsequent journal records:

1. X holds x, B waits on x/z, and C's z-only reservation arrives.
2. If C is granted immediately, a later X.fix leaves C held and B waiting.
3. If C's grant is postponed until X.fix, B is now the oldest eligible request
   and wins instead.

`deferred-*` and `id-order-*` deliberately introduce these two defects. Each
violates agreement on the full queue state and ordered logical output sequence
for all three bypass policies. `replay-*` preserves duplicate command identity,
original enqueue order and per-record closure through actual consumer cache
loss and replay. The scope of “full output” here is the queue projection's
enqueue/fix/cancel/grant events, not a claim about all Tx execution effects.

`dynamic-*` explores arbitrary valid enqueue, fix and cancellation orders for
three requests, one exact command duplicate and one consumer reset at a varying
received prefix. It compares both consumer states and output sequences at equal
prefixes, and the recovered result with the authored chosen history. Dropping a
replayed cancellation is detected. Healthy graphs contain 332,527–340,133 states.
The author never reads consumers, and consumers use only their own state and
the immutable input. Authoring first and serializing the independent consumers
preserves their local traces and final pairs without multiplying commuting
interleavings. This reduction makes no shared-resource or real-time claim.

## Distributed acquisition and cancellation

`acquisition-*` shares the exact policy operator across two participants and two
distributed requests. Each local group is atomic. The driver acquires groups in
a common shard order, receiving actual delayed grant replies before asking for
the next group. A chosen position permits independent local durable fixes;
pre-position cancellation instead fans out local tombstones. A late reservation
cannot resurrect a locally cancelled request. These checks include partial
holds, cancellation before a queued request arrives, and completed cancellation.

All four healthy policies complete; the cancellation family has 15,040 states.
Those two distributed requests have identical scopes, so they establish the
common-order/cancellation mechanics without distinguishing bypass. The additional
three-request family uses X on x, B on x/y and C on y on both shards. A witness
reaches C's lower-shard grant while B waits and X retains its lower-shard group
after issuing its upper-shard request. Its unrestricted healthy drain graph has
11,356 states, compared with 9,952 ordered; both complete. The authored broad
cancellation family completes 66,432 states. It offers cancellation for B only;
the two-request family separately varies cancellation at both owners.
Reversing the second request's acquisition order produces a genuine cycle:
one holds shard 1 and awaits shard 2, the other holds shard 2 and awaits shard 1.
The counterexample loops while unrelated local traffic actually enqueues and
fixes. Its separate progress property passes, so a busy system is not confused
with progress of the blocked requests.

This is an acquisition projection with abstract chosen commands, not a second
consensus protocol or a replacement for the actual Tx model. The recurring
narrow and independent traffic slots represent fresh local operations after
fixation; they do not recycle delayed packets or complete transaction IDs. The
native spike supplies actual multi-shard execution and latency observations.

Independent reviews checked the queue operator, cancellation crossing, common
acquisition order, actual-work liveness control and consumer-order reduction.
The first two-request acquisition review identified that identical scopes did
not exercise bypass; the three-request family above closes that coverage gap.

## Binding into the maintained model

The maintained native simulator already settles its local queue after each
agreed record. Its candidate implementation retains live enqueue order and
uses the same predicates. One forward scan is equivalent to repeated earliest
grant here: granting adds a holder and cannot enable an earlier blocked waiter.
Native output serialization is by request ID; this study emits command then
grants in logical scan order. Both are deterministic, but their literal output
sequences are not claimed identical.

The original TLA baseline had a binding gap that bypass exposed: ordinary
`TxKernel.Receive` folded a delivered command without draining grants, while
snapshot replay drained them using minimum Tx ID. The maintained binding now
uses one canonical enqueue-order closure in normal folding, replay and direct
`CommitAndFold` calls. Grant evidence, floors, stable command identity and
emitted facts remain in the actual semantic kernel. Integrating the policy changed
the baseline cases importing TxKernel; the [current formal results](../../../orbital/spec/RESULTS.md)
own their replacement coverage. This study's isolated comparisons retain their
own selected inputs and outcomes.

[PolicyTxBinding](../../../orbital/spec/PolicyTxBinding.tla) adds eight actual
Tx/DurableLog checks. It holds a real remote reservation request from X, lets B
queue behind X, and requires younger C to commit on B's other scope before X's
remote request is released. Owner-cache loss before C receives its grant is
recovered from an actual journal barrier snapshot. Full cached reply sequences
and emitted logical outputs match replay of the locally delivered prefix;
independent outcome checks verify the client's committed value and installed
version. Ordered stalls at the authored remote cut; deferred closure, lost
cached grants and lost replay grants each trigger their intended control.
Normal and recovery graphs complete at 204 and 216 states. This authored service
history supplements the broader epoch/recovery families, rather than claiming
an unrestricted product theorem.

The maintained [33-case selection](../../../orbital/spec/reservation-cases.json)
keeps drain safety/progress, the competing-policy counterexamples, canonical
closure/replay controls and the distinguishing distributed/cancellation cases.
All 69 configurations remain canonical under `orbital/spec/configs`; the full
[catalog](cases.json) is executable against those sources. There is no second
maintained policy implementation in this spike.

Cancellation removes only the corresponding locally accepted reservation and
keeps its tombstone for late submissions. Handoff/replay must retain the policy
interpretation and original order of live holders as well as waiters. A scoped
migration that drains old plans can avoid moving a live queue; one that moves
live reservations would owe the same ordering evidence. This study does not
introduce such a migration or an in-place policy upgrade.

## Workload comparison and remaining cost questions

Drain is selected for the maintained binding. The [native comparison](NATIVE.md)
retains 420 histories covering the workload shapes below, including both sides of
the younger-WAN boundary, long sparse-WAN streams and actual held-broad controls.
All offers are accounted for, including unfinished work in short windows.
Measured policy costs and sustained throughput remain separate questions.
Head remains a useful simpler comparator, but its unrelated-old-holder effect
makes it a weaker default candidate. Eligible is a throughput/locality control
with a demonstrated starvation cost. Ordered is the baseline, with useful
broad progress and the demonstrated bridge convoy.

Future calibration should extend the finite comparison to sustained sparse WAN
arrivals and chains of broad waiters across WAN
delay, local offered load and queue depth. Count all offered and unfinished
transactions, per-cohort latency tails, backlog recovery, broad completion age,
and actual scan/conflict work. In a bridge case, unrelated tail work should stop
tracking the old holder's WAN delay. After drain activates a barrier, broad wait
should track the last already admitted conflicting holder, not fresh narrow
arrivals. Include both sides of the younger-WAN boundary and an unrelated old
holder alongside an independent broad queue. Lower local p99 accompanied by
unbounded broad backlog is not a successful result. No measured nanosecond cost
or universal throughput benefit follows from these formal checks.

For one reproducible local check:

```sh
orb -m ubuntu python3 orbital/spec/check.py \
  --module orbital/spec/ReservationAcquisition.tla \
  --config orbital/spec/configs/acquisition-wide-drain.cfg \
  --output build/orbital-design-study/policy-rerun --timeout 90 --workers 1 --heap 512m
```

Run the maintained subset with `orbital/spec/suite.py --manifest
orbital/spec/reservation-cases.json`; the full comparison uses `--manifest
workbench/spikes/orbital-reservation-policy/cases.json`. Supply a fresh ignored
`--output` directory and `--timeout 90`. The catalog supplies each case's expected
failure or witness flag. Do not treat
an incomplete pilot, an expected-control graph, or a witness as a completed
healthy graph. The first uncommuted dynamic pilot timed out after 60 seconds;
it was sizing evidence only and is excluded from the selected results.
