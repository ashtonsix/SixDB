# Witness failure, quorum changes and recoverable authority

Design audit, 26 September 2026. This is a revisable contribution to
[brief](../../../../orbital/BRIEF.md), not a proof of an implementation.
[The preferred handoff](HANDOFF.md) now instantiates the obligations below using
prefix consensus, one terminal entry and durable successor initialization.
The priority is loss prevention, then write
availability, then avoiding unnecessary work. Failure detection belongs in the
accompanying recovery study; this note asks what a response can safely change.

The current brief gives ordinary operation three witnesses with a 2-of-3 quorum,
while event payloads persist separately on two VMs in distinct failure domains.
Neither statement alone establishes regional disaster survival. The dormant
failover witnesses mentioned in the [older brief](../../../../orbital/stale-drafts/BRIEF2.md)
are useful candidates, but that draft's succession mechanisms are not inherited
contracts.

Ashton has fixed ordinary operation at **2-of-3**, preserving a follower's
opportunity to learn persistence and start propagation after the outgoing network
leg and required writes, without awaiting a return hop. The
[network measurements](../../cft-commit-latency/network/README.md) do not justify
assigning RTT/2 to each direction or an end-to-end latency bound. The chosen
protocol must explain what lets the follower learn safely without inserting a
leader round trip merely for notification. A local persisted record alone need
not establish admission, and propagating provisional bytes must not make them
authoritative before the applicable rule permits it.

The larger-quorum and remote-predicate examples below concern failure response.
They do not propose changing the ordinary 2-of-3 rule or adding routine remote
acknowledgements to its fast path. Background pool replicas can be nonvoting and
asynchronous; their lag remains a real limit on disaster protection.

## What becomes vulnerable after one witness disappears

Let the witnesses be `A,B,C`. With `C` unavailable, `A,B` can still form an
ordinary quorum. This is not an immediate consistency failure. It leaves no
additional witness failure margin for making progress, however, and a newly
decided epoch might exist only on `A,B`. A recovered `C` is not a third copy of
that suffix until it has actually received and durably stored it.

There are three different failure questions:

| Question | Required evidence |
| --- | --- |
| Can another epoch be decided now? | The selected consensus protocol's active authority and reachable deciding quorum. |
| Can every required historical fact and byte survive the threatened failure? | Actual durable copies or sufficient erasure-coded fragments outside that failure, including recovery dependencies and retained protocol state. |
| Can a surviving set resume the same history? | Recoverable history **and** authority to continue it while excluding incompatible old/new authorities. |

An unavailable VM with intact storage can cause an outage without destroying
data. Conversely, all three witnesses may be healthy while both durable payload
copies occupy the region about to fail. Count reachable votes, recoverable copies
and failure-domain coverage separately. A failure domain is a declared model of
correlation, not a property conferred by having different VM IDs or AZ names.

The following placement examples are deductions from these assumptions. They
are not failure probabilities or timing predictions.

## Small counterexamples that constrain the response

1. **Reducing the membership denominator adds no copy.** `C` is disconnected;
   `A,B` decide epoch 10. Changing to 2-of-2 on `A,B` does not improve the
   durability of epoch 10: the deciding/storage set is still `A,B`. Both layouts
   stop if either member becomes unavailable. Removing `C` may simplify a later
   valid membership transition, but is not a disaster-protection action itself.
   Shrinking again to 1-of-1 would require its own authority transition and would
   put every new decision on one store.

2. **A surviving payload is not a surviving admission history.** Producers retain
   events `x` and `y` outside the region; `A,B` decide `x` in epoch 10 and `y` in
   epoch 11. Both witnesses are destroyed before `C` learns those assignments.
   The payloads alone do not reveal the decided batching/order. Reordering them
   can change the application fixpoint, positions, or returned results.

3. **A five-member majority can remain wholly regional.** `A,B,C` are in region
   `R`, and new witnesses `D,E` in `S`. A 3-of-5 configuration permits `A,B,C` to
   decide without either remote member. Destruction of `R` can erase that
   admission suffix; even when `D,E` have the complete log they cannot form a
   3-of-5 quorum. For comparison, a 2/2/1 placement across three independent
   regions leaves three members after any single-region loss. That conclusion
   covers only the named regional failure model and assumes the survivors can
   communicate and recover the protocol state.

4. **A larger numeric threshold can reduce availability without adding the
   needed independence.** Requiring 3-of-3 on `A,B,C` stalls immediately while
   `C` is unreachable. Once it returns, three copies in one region still fail
   together with that region. It does add a copy relative to an actual 2-of-3
   decision when all three stores work; its value depends on placement and the
   failure being defended against.

5. **Membership overlap is not quorum overlap.** Replace `C` with `D` by letting
   machines switch independently from `{A,B,C}` to `{A,B,D}`. Old quorum `{B,C}`
   and new quorum `{A,D}` are disjoint, despite two shared membership names.
   Without a transition protocol they can decide conflicting values for the
   same next position. A list of replacement nodes plus an incremented local
   configuration number is insufficient.

6. **The last known committed frontier can omit a decision.** `A,B` durably accept
   the same Paxos proposal for slot 10, so it is chosen, but all learning replies
   are lost. `B` still reports its locally known committed frontier as 9. Recovery
   from `B` plus `C` must inspect the accepted proposal and preserve slot 10;
   restoring only a checkpoint/frontier at 9 can discard it. This example uses
   Paxos's chosen rule; two arbitrary matching log records are not a universal
   commitment certificate for other algorithms.

7. **Exported receipts do not close an unknown suffix.** Remote learner `D` has
   certified history through epoch 10. `A,B` then decide epoch 11 and disappear
   before export. `D` cannot infer that 10 was the final old epoch, even if no
   client is known to have received an epoch-11 response. It also cannot tell
   permanent destruction from a partition in which `A,B` continue. Continuing
   from 10 therefore needs additional recovery/fencing evidence, not merely
   confidence in `D`'s checkpoint.

8. **Protecting the future does not repair the past.** At epoch 20 the policy
   starts requiring a remote durable copy. Epochs 11–19 and a dictionary needed
   by their payloads remain solely in the threatened region. Losing that region
   after epoch 21 can still break reconstruction. Repair the required historical
   closure, or create and certify an equivalent checkpoint with its complete
   dependencies; do not label the whole history protected at epoch 20.

Examples 5 and 6 expose established protocol obligations. Raft's joint
configuration and non-voting catch-up are one concrete solution for changing a
replicated log; its current-term commitment restriction also illustrates why
counting matching records is not universally enough. [Raft, §§5.4 and 6](https://raft.github.io/raft.pdf).
Paxos preserves accepted proposals and promises across restart so future
ballots recover possibly chosen values. [Paxos Made Simple, §2](https://lamport.azurewebsites.net/pubs/paxos-simple.pdf).

## Strengthen the right predicate

Keep three policies explicit, with the ordinary 2-of-3 rule unchanged:

- **Consensus predicate:** which evidence fixes one value at a decision position,
  and which recovery/election procedure preserves it. Changing this is a
  consensus-protocol transition.
- **Payload eligibility:** which durable storage receipts make a stream prefix
  eligible to propose under the current placement and retention requirements.
- **Authoritative-result gate:** what additional durable evidence must exist
  before a promised durable result, consumer publication, dependent reply or
  external effect becomes authoritative.

A detector may immediately withhold new work or demand stronger receipts. That
does not allow it to redefine whether an existing epoch was chosen, forget a
pending transaction, or treat a delayed response as evidence of absence. A gate
checked only by one leader is not a durable deployment policy: replacement
leaders, first propagating followers, consumers and every independent origin of
authoritative results must enforce its version, activation boundary and required
evidence. Local conservative withholding can start earlier. The stronger gate
affects all work in its declared scope, including disjoint effect envelopes;
it is a shared latency/availability dependency without a transaction conflict.

A useful candidate during regional distress is ordinary consensus plus required
durable coverage outside the suspected region. For input bytes, this can be a
pre-admission receipt. For final ordering and decisions, a proposal receipt is
not yet proof that the proposal was chosen. Two possible designs deserve
separate evaluation:

1. Make a remote voter part of the actual consensus predicate, with a complete
   election/recovery construction that respects its quorum families.
2. Keep the consensus rule and require durable export of certified decision
   history and its reconstruction dependencies before the stronger result gate
   opens. Keep chosen-but-not-yet-exported decisions visible to recovery as such;
   never reinterpret them as aborted or unchosen.

The second can protect promised results without immediately promoting a
learner. It does **not** by itself provide automatic lossless succession after
loss of the old quorum: example 7 still applies. The first does not put payloads
or checkpoint dictionaries at the remote voter unless those are separately
copied. Both must name the success boundary covered by the strengthened
guarantee, including any earlier promise to retain a durably accepted input.

Requiring a particular remote witness `D` makes `D` a write dependency. Requiring
one of `{D,E}` removes that single dependency only if the selected consensus and
recovery rules support those alternatives. Neither formulation alone means that
`{D,E}` may later elect a leader. Domain-aware write receipt requirements can be
useful without changing election quorums; do not conflate the two.

Entry into a stronger failure-response mode records its activation boundary and
the promises made under it. Exit needs stabilized health/coverage and the same
safe reconfiguration rules where membership changes, then returns to ordinary
2-of-3. Do not reclaim copies still needed by the stronger mode's existing
obligations merely because new work again uses the ordinary path.

Flexible Paxos demonstrates the reason to keep the rules paired: every allowed
phase-1 quorum must intersect every relevant phase-2 quorum. For arbitrary
cardinality quorums on a fixed set of `N` acceptors, `q1 + q2 > N` suffices;
smaller replication quorums can require larger recovery quorums. This is an
option to study, not permission to change one threshold during an outage.
[Flexible Paxos, §§3–5](https://arxiv.org/pdf/1608.06696).

## Replacement and regional relocation while a quorum still exists

Prefer recruiting and catching up an outside-domain learner while `A,B` still
retain ordinary authority. A spare with reserved capacity and credentials can
start promptly; a spare without the current state cannot immediately provide
that state's redundancy. Pre-replicated non-voting learners are more valuable
for recovery than merely bootable VMs, at the cost of steady replication and
retention work.

A candidate handoff has the following obligations; the final protocol may
combine their messages rather than implement literal serial phases:

1. Identify the old history, configuration and recovery base. Reserve transfer
   and pending-state capacity outside the predicted failure domain. Authenticate
   the candidate without granting it independent authority.
2. Copy a reconstructible prefix and live protocol state; catch up its suffix.
   Verify content and durably record retention responsibility at the receiver.
   Catch-up must account for accepted but not locally known committed records,
   configuration history and activation boundaries, not just consumer-applied
   epochs.
3. Use the recommended [terminal handoff](HANDOFF.md): choose one final entry in
   the existing prefix-consensus journal, binding inherited material, successor
   and policy. Its certificate closes old admission and grants initialization
   authority. New eligible voters durably install that exact final prefix before
   ordinary successor election/quorum service. Detector nomination is not a grant.
4. Establish that the new authority can preserve every old decision and resolve
   every possibly chosen suffix. Establish that incompatible old authority
   cannot make further authoritative progress. Test a crash between every
   durable transfer/activation step and a partition hiding an old leader.
5. Retire old copies only after the required state, bytes, authority and retention
   evidence survive the declared failure. Admission metadata may finish transfer
   before payloads or staged outcomes; that is partial progress, not completed
   regional evacuation.

A complete relocation can use disjoint final witness sets with this authority
bridge and state transfer. The preferred design needs the ordinary old quorum
until the terminal prefix is recoverably chosen; it needs no separate control
consensus instance or external configuration service. For comparison, such a
service is an alternative explored
by [Vertical Paxos, §§2–4](https://www.microsoft.com/en-us/research/wp-content/uploads/2009/05/podc09v6.pdf),
but its survival and history recovery would be additional dependencies.

Measure separate vulnerability intervals: time to an additional durable history
copy, time to an additional complete reconstruction path, and time to usable
successor authority. They need not end together. Concurrent repair consumes
network, storage and queue capacity on the survivors; bound it so it does not
prevent the surviving quorum from persisting its own recovery state. A modest
learner repair can start on weaker evidence than a full-region evacuation.

## What the learner or recovery image must retain

A scalar stream frontier says how far a stream was admitted. It cannot identify
the order between streams, prove the finality of an epoch, or replace witness
election state. The transferred recovery image must preserve or safely
reconstruct the following before dependent replies become authoritative:

| Layer | Required content or evidence |
| --- | --- |
| Consensus authority | History and configuration identities; protocol-specific ballots/terms, promises/votes and accepted suffix; proof of activated configuration and of any sealed/fenced predecessor. |
| Admission history | Exact ordered epochs and stream ranges, immutable content bindings, idempotence outcomes and sufficient evidence of chosen assignments. A checkpoint may replace older records only where its proof and retained meaning suffice. |
| Ordering constraints and pending transaction work | Outstanding allocation ownership/queues, observation bounds including those left by completed readers or their sufficient monotone floors, provisional announcements, exact fixed positions, continuations, effect-authority membership, staged results, commit/abort decisions and each participant's resolution. |
| Meaning | Versions of object/domain definitions, overlap/coverage rules, authority mapping, application code and decoding dependencies needed to interpret the retained state. |
| Reconstruction | A valid base plus required WAL suffix, durable objects, dictionaries and other dependencies, and verification evidence or retained inputs needed to regenerate it. |
| Retention | Holder identities/incarnations, actual stored content coverage, obligations and replacement evidence before any old holder reclaims its last required copy. |

Logical state equivalence is not enough if the image has forgotten an observation
bound still constraining future positions—even after its reader completed—or an
aborted transaction's durable deduplication outcome.
Reconstruction may replace literal copying of these records only if it restores
the same constraints and excludes stale messages before serving dependent work.

A certificate is protocol-specific evidence tied to the exact history,
configuration, decision coordinate and value. It may consist of authenticated
durable votes or a protocol-defined committed-prefix proof; it need not be a
public-key signature for each event. A receipt for bytes and a receipt for
authority answer different questions. A copied certificate can be valuable
recovery evidence without turning its holder into a voting witness.

## Quorum lost, durable logs survive

If fewer than an ordinary quorum are reachable, preserve their stores and seek
the missing state. Two unavailable witnesses are not necessarily two destroyed
witnesses. Restarting original members with intact protocol state can restore
ordinary operation. An old identity with a blank or rolled-back store is not an
intact member; quarantine it from voting until the protocol's recovery path has
restored its obligations.

If only `C` is accessible, there are two indistinguishable possibilities from
`C`'s view: `A,B` are dead, or they are partitioned and still deciding. In either
case they may already have chosen a suffix unknown to `C`. Electing replacements
from `C` plus available data consumers does not resolve either uncertainty.
There is no general lossless automatic recovery rule based only on health
probes, one surviving log, and a consumer majority.

An emergency transfer can be lossless if already-authorized machinery proves
both facts: enough old state has been preserved to account for all possible
decisions, and the old authority cannot continue incompatibly. One possible
pre-disaster mechanism is a durable seal/handoff certificate that names a unique
successor, binds a complete cut and resolves all accepted suffix obligations.
Every possible old deciding quorum must be prevented from extending that old
authority after the seal. A broadcast saying “we stopped” without durable quorum
or external fencing evidence does not establish this; partial delivery and
restart are the adversarial cases.

A remote archive's valid committed prefix proves that prefix exists. It does
not prove the absence of a later decision. If complete old state or fencing
cannot be established, stop ordinary admissions and use the operator disaster
recovery path with explicit retained and possibly lost ranges. A new recovery
history must reject old-history traffic. Merely changing discovery/DNS is not
fencing for old consumers, clients or external-effect sinks.

The [preferred construction](HANDOFF.md) selects complete sequence recovery,
one terminal handoff entry and durable initialization of the named successor.
Old quorum acceptance of that final prefix supplies the binding grant and
no-extension obligation while permitting recovery ballots for sealed history.
It includes the successful and interrupted
replacement/relocation traces and the constraint frontier before reopening.
The named protocols above remain comparison material, not competing unselected
handoff recommendations. Finite authored checks can expose counterexamples;
they cannot prove the full implementation's elections or reconfiguration safe.
