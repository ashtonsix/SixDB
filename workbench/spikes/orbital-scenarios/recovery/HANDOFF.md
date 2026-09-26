# One terminal handoff entry transfers the journal and authority

2026-09-26. The preferred construction carries Ashton's three facts together:
**the exact journal prefix/suffix, who will cease further admission, and the
named successor**. Pre-copy while ordinary 2-of-3 continues, then choose one
terminal handoff entry in the existing sequence-consensus journal. Its durable
old-quorum certificate seals that configuration and authorizes initialization
of the named successor from exactly that final sequence.

This uses Sequence Paxos's terminal stop-sign mechanism, with Orbital-specific
history, coverage and policy bindings. It removes the separate irreversible
freeze lifecycle and extra handoff consensus instance from the earlier candidate.
Successor readiness is durable initialization plus its ordinary election/quorum
rules, not another consensus decision called ADOPT. The construction is a
proposal with authored checks; it is not an implemented or fully proved protocol.

Physical witnesses can change from C0={A,B,C} to C1={A,B,D}, or to disjoint
C1={D,E,F} outside a threatened domain. Logical shard, journal, stream,
transaction and durable-object authority identities remain unchanged. A
membership transition introduces an explicit admission pause after terminal
proposal; source or target failure can turn that pause into an outage. Changing
a flow or transferring leadership within C0 does not require this terminal entry.

## The selected consensus and early-learning primitives

Use a crash-fault, authenticated Sequence Paxos journal with three acceptors,
2-of-3 phase-1 recovery and 2-of-3 phase-2 acceptance. A ballot belongs to one
leader and exact configuration. Phase 1 selects the sequence with the highest
accepted ballot, longest among reports at that ballot; the leader extends only
that sequence within the ballot. Promises, accepted ballot/sequence and learned
frontier or equivalent certified evidence survive restart. Equal-ballot reports
must be prefix-comparable. A majority accepting a common prefix chooses it.
The prepared ballot serves successive, pipelined epochs: phase 1 is not repeated
per event, frontier submission or epoch, and no fresh leader-liveness proof is
required on each append. A higher promise still prevents lower-ballot acceptance.

A terminal entry must be the final command for its source configuration. If a
recovered sequence contains it, the leader preserves it and appends nothing in
that configuration. A higher ballot can still recover or retransmit the sealed
sequence. Initialization of a successor uses the final source sequence as its
immutable base. These are the relevant rules of
[Haridi, Kroll and Carbone, Algorithms 4, 8 and 10, §7](https://arxiv.org/pdf/2008.13456).
The normal Paxos majority-learning basis is also described in
[Paxos Made Simple, §2.3](https://lamport.azurewebsites.net/pubs/paxos-simple.pdf).

For early propagation, the leader durably accepts its valid prefix, then sends
its acceptance evidence with the exact `(history, configuration, ballot, prefix
identity, absolute length)` to followers. A follower which durably accepts the
same prefix has evidence of two acceptances and may learn and propagate that
common prefix without returning a message to the leader first. This also works
for a terminal prefix. Its evidence covers neither extra follower-resident bytes
nor a different ballot. Required persistence and result gates still apply; the
[branch-start model](../../cft-commit-latency/network/README.md#optimize-the-first-durable-followers-branch-start)
is `leader_write + min(outgoing_i + follower_write_i) + overhead`. There is no
return leg in that model. RTT/2 is only a symmetric approximation; the spike does
not measure the joint durable fanout or establish its production tail latency.
Ingress/forwarding to the ordering leader and queueing add costs before this
boundary; any-witness ingress does not remove that ordering dependency.

This evidence-carrying optimization is our composition inference, not a claim
that generic Raft replication supplies it. Ordinary operation remains 2-of-3;
nonvoting pool copies add no mandatory normal-path acknowledgement.

Use full-prefix semantics as the baseline. Incremental encoding binds parent
identity, absolute length and ballot; missing predecessors require catch-up or
resynchronization. Same-ballot duplicates cannot shorten a sequence or append
an entry twice. Valid higher-ballot synchronization may replace an unchosen
lower-ballot tail while preserving the chosen prefix. The paper's optimized
FIFO/session assumptions must be supplied or replaced explicitly for Orbital's
reordered datagrams. A cached predecessor is not an accepted prefix.

## Keep submissions moving across route and leader changes

Origins submit eligible **producer-stream frontiers**, not competing epoch
proposals. Any witness may receive, retain within its capacity and forward them;
only the current prepared leader orders them into epochs. Compatible verified
frontiers coalesce monotonically, with stable stream/incarnation and content
identity. Duplicate or already-covered submissions need no new admission.
Concurrent submissions from different followers are useful input, not election
losers. Missing persistence evidence, conflicting content, policy and capacity
limits remain explicit reasons to defer or reject. An ingress receipt alone is
neither consensus acceptance nor admission; any promised retention must survive
restart under its stated durability rule.

Use an available route immediately; there is no mandatory leader-discovery RPC
before sending. A stale witness can forward to a known leader or retain pending
input while routing converges. Replay the same identities and verified frontiers
through an updated route, including the certified successor after handoff.
Reconcile them against recovered stream frontiers before ordering new ranges;
never regenerate or reorder already chosen epochs. Bound forwarding and retries,
deduplicate across paths, and prevent repeated visits/circular redirects within
an attempt. Leader hints are ballot/configuration-scoped routing information,
not an authority grant. Unknown leadership can delay ordering, but need not
block bounded ingress or make callers repeatedly discover a leader. The routing,
retry and durable-inbox mechanisms satisfying these requirements remain unbuilt.

Three changes have different authority costs:

| Change | Required boundary |
| --- | --- |
| Different validated flow tuple between the same witnesses | Preserve authenticated voter/incarnation, configuration, ballot and prefix identities. Deduplicate/reorder old and new flow traffic and resynchronize missing prefixes. No election follows merely from a port change. |
| Planned leader transfer within the same three voters | Catch up the candidate while the old leader serves, then use a higher ballot with ordinary quorum promises and complete prefix recovery before the candidate appends. Forward/replay pending submissions; no terminal membership entry or application-drain barrier. |
| Witness replacement or relocation | Use the terminal Handoff below, including durable successor initialization. A measured faster candidate alone has no voting authority. |

For same-configuration transfer, pre-copy can reduce recovery work but cannot
replace phase 1: a locally known chosen frontier can omit a later chosen or
possibly chosen suffix. Higher-ballot quorum promises fence conflicting old-leader
progress; earlier acceptances remain recovery obligations. The new leader must
recover the compatible sequence before new authoritative appends. This is an irreducible recovery
dependency in this candidate, with a possible admission gap; it is not a periodic
pause to confirm a stable leader. Ingress, catch-up and dissemination of already
chosen history can overlap it. No zero-stall transfer or fixed time bound is
established. Leadership/witness changes respond to validated network or health
evidence, not scheduled rotation; flow selection can avoid a transfer entirely.

Early learning remains evidence-based during transition. A follower which has
promised a higher ballot must refuse a newly arriving lower-ballot Accept. But
a follower that has not made that promise may complete a delayed matching old
acceptance. Whole-prefix recovery must preserve any resulting chosen prefix.
Two genuine durable acceptances of the exact common prefix still prove choice
when their evidence arrives late; neither a newer leader nor a changed flow
invalidates that historical proof. Do not combine acceptances from different
ballots or prefixes. Learners need no current-leader confirmation to propagate
the proven prefix, while enforcing its configuration/policy and ordered delivery.

Flow control must also preserve the ordinary quorum path. The
[experimental commit pipeline](../../cft-commit-latency/commit/README.md#batches-buy-amortization-windows-buy-overlap)
retains slots until **both** followers acknowledge: a missing third eventually
stalls new admissions despite 2-of-3 completion. Do not inherit that restriction.
Separate bounded active transmission slots from lagging-replica catch-up and
durable retention duties, with a recoverable source or certified checkpoint for
what is no longer in the send window. This is an implementation requirement,
not a measured solution. Neither unbounded buffering nor deleting still-required
recovery bytes is acceptable; exhaustion of real durable capacity can still stop
admission and must be reported as such.

## The entry, certificate and initialization

Let P contain the data/control journal through absolute position K, including
any certified compacted base. Propose a terminal entry at K+1:

`Handoff(history, C0, prefix_identity(P), K, C1, coverage, result_policy)`

C1 names exact successor voters/incarnations and the configuration's quorum
rule. Coverage identifies prerequisite retained material and durable holder
receipts; result_policy names its scope, activation boundary and retention
obligations. Epoch/stream identities and chosen history remain those of the same
lineage. Pending work can remain pending.

The entry binds **the preceding prefix P** and its recovery material. It does
not contain a hash or receipts for its own already-completed sequence. Define
S=P+[Handoff] after constructing the entry; the ordinary source quorum's durable
acceptance certificate binds S, its absolute length K+1, the exact ballot and
configuration, and the accepting voter identities. This avoids a recursive
hash/receipt requirement. The suffix can be sent beside the certificate and
referenced by its exact identity, rather than embedded in every control message.

That certificate supplies both the successor grant and the evidence for old
admission closure: its named voters accepted a terminal sequence under the
protocol's no-extension rule. Once chosen, every legal old-config recovery must
preserve that exact terminal prefix, so no old deciding quorum can authorize a
new command beyond it or a conflicting successor. This does **not** require
voters to refuse all future recovery ballots or historical evidence service.

Each new voter must durably initialize from S and its valid source certificate
before becoming eligible in C1. Store the certified base/suffix, terminal entry,
authority chain, policy and required protocol state; a hash-only installation
is insufficient. C1 may admit new entries only with its ordinary legal leader
and quorum of eligible initialized voters. The first election's checks can
establish that readiness; there is no independent ADOPT consensus instance.
Overlapping physical voters keep C0/C1 protocol state distinct. The inherited
terminal closes C0, not C1. Configuration authority follows the certified chain,
not an invented larger scalar ballot.

## Preferred sequence

1. **Pre-copy without authority.** Keep C0 admitting while the proposed successor
   receives a certified base, evolving accepted journal and needed recovery
   dependencies. Check usable storage, keys, code and transport outside the
   threatened failure domain. Preserve source copies. A learner does not vote
   merely because it is warm or mostly caught up.
2. **Prepare the terminal prefix.** The valid leader obtains/reconstructs its
   complete accepted sequence under the ordinary recovery rules if needed.
   Choose a candidate prefix P, bring prerequisite successor copies up to its
   exact identity and collect the configured coverage receipts. Bind the target
   and policy in Handoff. Pause adding further ordinary entries in that leader's
   ballot once proposing S; an old or recovered leader cannot append after a
   terminal entry. This does not await completion of P's application programs.
3. **Choose the entry through ordinary C0 consensus.** Voters check the whole
   prefix, source/target identities, terminal placement, coverage prerequisites
   and policy before durably accepting. An acceptance alone is provisional.
   A matching 2-of-3 certificate chooses S and fixes the successor. The early
   follower can assemble that evidence and send it outward without waiting for
   a return hop through the old leader. Pre-copy receipts for P alone do not
   establish that the terminal choice occurred.
4. **Export the chosen prefix and initialize C1.** Transfer any final suffix and
   the terminal certificate to the named target. Its eligible voters durably
   install S and initialize their configuration state. Once the ordinary C1
   election and deciding quorum are available, its first new entry is K+2.
   If K+1 is exposed separately as a control index, preserve that distinction
   consistently; never confuse K data entries with the K+1 terminal boundary.
5. **Reopen scopes and retire copies.** Establish the constraint frontier below
   before serving affected authoritative work. Existing consumers can continue
   from their already established state, while restored scopes retain pending
   blockers and completed-reader floors. Retire source copies only when the
   complete witness evidence and required payload/replay material satisfy the
   retention/failure policy. Witness activation is not completed evacuation.

A newly elected C0 leader must recover a terminal proposal just like any other
accepted sequence. If phase 1 selects a terminal-bearing sequence, preserve its
exact entry and stop. If valid recovery selects a different sequence excluding
an unchosen terminal, the proposal can be superseded; any new entry must bind
that recovered prefix and refreshed coverage. Never delete an accepted terminal
on a timeout or because its named target seems unhealthy.

## Worked replacement and relocation

**One replacement.** C is unavailable. A/B keep C0 alive while D catches up.
The leader's recovered prefix P has 42 entries, including an accepted suffix
beyond the locally learned frontier. It arranges required copies of P and then
proposes terminal entry 43 naming C1={A,B,D}. A's durable self-acceptance plus
B's matching durable acceptance chooses the whole sequence through 43. A/B/D
initialize from that certified sequence; C1's next entry is 44. Count D as a
repaired copy only after it actually installs the boundary, even if A/B can
already form a legal C1 quorum. A further D failure can leave availability but
has not restored the intended redundancy.

**Regional relocation.** Pre-copy to D/E/F outside the predicted domain, including
payload/replay closure for the desired protection. The terminal entry names
C1={D,E,F}. Once it is chosen and its certificate plus complete required state
are recoverable at a new quorum, D/E/F can initialize and elect without another
old-region round trip. Loss of the old region before terminal evidence escapes
can still leave a fully copied P with no proven successor authority. Do not
activate from that prefix alone. Loss of the named target after the terminal
choice cannot authorize an alternative target from C0; the named configuration
must recover or perform its own valid next handoff.

Copying only witnesses can complete authority transfer without completing
regional data protection. Conversely copied objects without the terminal
certificate do not establish succession. Report those milestones separately.

## Failure and competing-proposal histories

| Interruption | Safe interpretation and continuation |
| --- | --- |
| Candidate dies during pre-copy | C0 can continue under its quorum; no target authority exists. Resume or choose a different candidate before terminal choice. |
| A alone accepts a terminal naming D | Same-ballot extension stops at A, but no successor is proven. Higher-ballot recovery remains permitted. B/C can recover a different unchosen sequence if their reports legally select it. A must not acquire a permanent all-ballot freeze merely from this proposal. |
| Two leaders nominate different successors | They propose different terminal values through ordinary ballots. Unique ballot ownership, prefix recovery and no append after terminal prevent both terminal prefixes becoming chosen. Do not include two terminal entries in one C0 sequence. |
| A/B choose a terminal; replies disappear | The exact terminal is preserved by every subsequent legal C0 recovery. Recover/recertify the final sequence; lack of local knowledge of choice is not permission to retarget. |
| A accepts a terminal; B accepts the delayed matching proposal later | The second acceptance can complete choice after the original leader has paused or crashed. Valid late evidence remains usable. Local cessation is not the instant of the last global choice. |
| Old quorum is lost before a recoverable terminal certificate exists | Preserve reports and seek missing evidence. A precopied P or unproven Handoff value alone cannot activate C1. SOS remains useful. |
| Old region disappears after the terminal certificate and state reach C1 | The new initialized quorum can proceed under its ordinary election; C0 need not respond again. |
| Certificate reaches C1 but prefix bytes have holes | The choice and successor are known, but incomplete voters are ineligible. Fetch the exact missing material; never fill a hole with a no-op or older bytes. |
| One target initializes, then another crashes | One initialized voter cannot manufacture the target quorum. Source authority stays terminal; wait for enough initialized target voters. |
| A source voter restarts | Recover its promises, accepted sequence and certified terminal evidence. It may help historical recovery but cannot append beyond a chosen terminal. Blank/rolled-back storage does not retain old voting identity. |
| Old chosen evidence arrives after C1 is serving | Validate and retain/install the historical outcome under its original lineage/configuration. Reject use of old authority to create new post-terminal input. |
| Retirement crashes halfway | Partial copy deletion cannot release remaining retention duties. Duplicate transfers/receipts are idempotent; neither a copied certificate nor a partially initialized target permits premature GC. |

There is a real exposure interval between source terminal choice and independent
durable retention of its certificate. Send evidence from available origins as
soon as it exists; no source-side promise of successful evacuation should hide
that gap. Coverage receipts for P cannot close it retroactively.

## Constraint frontier and result gates

The chosen terminal prefix is not automatically a consumer's restart frontier.
Before reopening a scope, recover its metadata from a certified base through
all relevant inputs in S. Restore allocations/queues, pending announcements,
exact positions, outcomes, historical domain meanings/mappings and observation
constraints, or a sufficient conservative representation. Register new metadata
only against this state. Independent scopes can reopen when their completeness
is established; without a justified scope-coverage index, recover the broader
prefix. Pending computations may remain pending with their blockers, resources
and independently executable decision owners represented.

Completed readers leave constraints. T@20 reads x=0 and commits y=1. If recovery
drops x's observation bound because T completed, U can incorrectly choose 10,
read y@10=0 and write x=1. T's read requires T before U; U's read requires U
before T. Retain the bound or a sufficiently restrictive monotone allocation
floor. A checkpoint of pending transactions alone is insufficient.
[PITR](PITR.md) owns the detailed restart/retention closure.

Witness succession keeps logical authorities fixed and orders their input
registrations before or after the terminal handoff. Unanswered requests retry
under stable identities; durable accepted-but-unadmitted submissions remain
obligations. An unjournaled registration side channel would be a composition
hole. Changing object/domain authority is outside this witness transfer: it
would additionally need a registration cut closing all concurrent observations
and announcements, carrying completed-reader floors, and granting the new
mapping before reopening. Historical mappings alone do not close that race.

The terminal binds an emergency result policy to a precise boundary and scope.
The first follower, leader, every consumer and every independent origin of an
authoritative reply/effect must enforce its required evidence. Provisional
forwarding cannot bypass that gate. Valid pre-boundary historical outcomes retain
their recorded meaning; post-boundary outcomes cannot use the old policy.
All work in the declared scope is affected, including disjoint envelopes.
Returning to ordinary policy preserves promises already made under the stronger
one. Losing a usually faster follower can also worsen ordinary 2-of-3 tails;
that differs from gate latency, handoff pause and transaction-allocation WAN cost.

## Why this is simpler, and what still needs checking

Whole-prefix recovery is necessary here. If A alone accepted `[x, depends(x)]`
at ballot 3 and C later accepted `[y]` at ballot 4 after legal recovery on B/C,
independent slot maxima create `[y, depends(x)]`. Sequence recovery selects the
whole higher-ballot sequence instead. A lower-ballot minority tail may be
displaced, with its evidence/producer obligations retained, but it is never
spliced into a different predecessor. This retains prefix pipelining and each
follower's early learning opportunity without a mandatory chosen-predecessor
return trip for each extension.

With that selected prefix protocol, a terminal entry already binds the final
history and unique successor. A separate forever-frozen voter lifecycle would
unnecessarily prohibit useful recovery after partial acceptance; a separate
terminal Paxos register would repeat successor selection. Durable target
initialization is essential, but need not be another agreed control value.

Remaining implementation obligations are prefix-aware acceptance/learning;
terminal placement and recovery rules; durable prefix/certificate initialization;
checkpoint completeness and retention; actual transport synchronization; and
all-origin gate enforcement. Resources must support an independently authorized,
executable resolution path with available inputs/keys/code. Spare capacity alone
cannot resolve an unknown decision. No safety argument authorizes availability
when the necessary old evidence or new quorum is missing.

[SOS broadcast](SOS.md) is separate. It stops all ordinary admission locally
and exports whatever existing evidence can escape, without choosing a terminal
entry or requiring a complete snapshot. A subsequent handoff needs its separately
authorized transition; export does not wait for it. Local SOS cannot prove that
an unseen source quorum stopped, and a highest observed frontier never proves
absence of an unobserved suffix.

The [authored probe](handoff_probe.py) passes 30 checks of selected prefix and
authority-boundary examples using assumed genuine reports/votes/certificates. It does not execute
or prove the full consensus, storage or transport protocol. Its recorded output
contains the source hash and explicit limits:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-scenarios/recovery/handoff_probe.py --output build/orbital-recovery/handoff-probe.json
```
