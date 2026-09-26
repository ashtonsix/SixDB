# Failure histories that change the recovery decision

These are authored counterexamples and proposed checks for Orbital, not measured
incident probabilities or an adopted recovery protocol. The priority is preserving
data, then write availability, then avoiding unnecessary work. The starting point
is the three witnesses, separate producer persistence and pending-state
obligations in the [brief](../../../../orbital/BRIEF.md).

An anomalous response time starts investigation. Ashton's illustrative 100 ms
between VMs in different AZs is neither a universal failure timeout nor proof
that a witness, AZ or region has failed. Cheap precautions can start before a
diagnosis: protect retained evidence, probe other paths and ready an independently
usable failover target. Removing authority requires the recovery protocol.

## What historical incidents establish

These reports establish possible dependency combinations. They do not estimate
their current frequency, predict a provider's next outage or prove that every
service in a named region shares the same failure domain.

| Primary report | Reported behavior | Consequence to test in Orbital |
| --- | --- | --- |
| [AWS S3, 28 February 2017](https://aws.amazon.com/message/41926/) | S3 API unavailability also impaired new EC2 launches, EBS volumes needing snapshot data, and status-dashboard updates. Recovery backlogs outlasted the initial fault. | A spare that still needs an image, snapshot, object manifest or launch API from the impaired system may not be usable. A silent status page is weak negative evidence. This does not say existing EC2 instances all failed. |
| [AWS EBS/EC2/RDS, 21 April 2011](https://aws.amazon.com/message/65648/) | A network error triggered re-mirroring that exhausted free capacity. Repeated searches and recovery requests overloaded a regional control plane, affecting replacement operations in other AZs. Some recovery throttles initially blocked useful work too. | Reserve and exercise recovery capacity; distinguish productive transfer from retry traffic. Protect resolution traffic while limiting bulk repair. Merely putting replicas in different AZs does not isolate their recovery dependencies. |
| [AWS service event, 7 December 2021](https://aws.amazon.com/message/12721/) | Internal network congestion impaired monitoring and provisioning. Running EC2 instances were unaffected, but new instance launches failed; credential and endpoint dependencies also had impact. S3 itself was available, while access through VPC endpoints was impaired. | Classify a failed path separately from a failed storage service. Preserve healthy running capacity. Recovery must be possible despite stale telemetry and impaired launch or credential services. |
| [Google Cloud networking, 2 June 2019](https://status.cloud.google.com/incidents/Nm7HSYZu9RqCY2HXRQQf) | Automation descheduled network control jobs across physical locations. Several regions experienced compute connectivity and regional Cloud Storage impact. Congestion also impaired diagnostic tools and configuration recovery. | Exercise simultaneous compute, object-access and recovery-tool impairment. A second region is not automatically independent of shared software, backbone routing or administration. |

## Separate observations from authority

The [phi accrual detector](https://dspace02.jaist.ac.jp/dspace/bitstream/10119/4784/1/IS-RR-2004-010.pdf)
separates an adaptive suspicion level from the action selected by an application.
It supplies a useful shape for staged responses; it does not supply an Orbital
timeout or a calibrated probability of a correlated disaster. A changing latency
distribution can also make a historically unusual delay ordinary to the detector.

[SWIM](https://www.cs.cornell.edu/projects/Quicksilver/public_pdfs/SWIM.pdf)
uses direct and indirect probes and a suspicion interval to reduce false failure
declarations. Its weakly consistent membership view is not a consensus membership
decision. [Lifeguard](https://arxiv.org/abs/1707.00788) additionally considers the
health of the observing detector: slow local processing can falsely implicate
healthy peers. These are complementary diagnostic ideas, not an election design.

For this investigation, retain separate observations for:

- **Observer and path:** local scheduling delay, outstanding requests, direct and
  indirect reachability, route/endpoint identity, direction and observation age.
- **Witness service:** process responsiveness, durable journal progress, committed
  configuration identity and acknowledged frontier. A heartbeat is not a flush.
- **Recovery material:** which authenticated history, payloads, snapshots,
  representation dependencies and executable identities can actually be read.
- **Correlated exposure:** physical placement plus shared storage, credentials,
  KMS, deployment, network and control-plane dependencies. Unknown independence
  remains unknown; multiple observers behind one broken path are not independent.
- **Repair feasibility:** a usable target, durable catch-up progress, available
  bandwidth and storage, resolution capacity, and the age of the least-protected
  acknowledged history.

Ordinary successful traffic can supply evidence without extra probes. Probe work
should have a bounded budget, deduplicate shared host/path checks across shards,
and leave room for admission and recovery decisions. Neither one successful ping
nor many correlated failures establish durable state or physical destruction.

## Local symptoms and ambiguous failures

Each case states a concrete history, the observation that changes the decision,
and the behavior that a proposed protocol must preserve.

| Case | History and useful discrimination | Safe response to exercise |
| --- | --- | --- |
| **S1: transient observer pause** | A pauses while B and C continue communicating and persisting. A wakes to old timeout events and declares both failed. Compare A's scheduling lag and B↔C observations; include delayed replies with fresh-looking arrival times. | Discard obsolete probe conclusions by request/incarnation identity. Keep the current authority until an authorized transition; avoid simultaneous replacement of both healthy peers. Let a recovered observer relearn committed state. |
| **S2: asymmetric path** | A cannot receive B, but C can exchange with both; later reverse one link. An alternate path works for small probes but loses large data transfers. | Use indirect evidence to choose a route or transfer path, while separately checking journal and payload progress. Do not count a relay as another durable replica or confuse small-packet reachability with catch-up completion. |
| **S3: live process, stuck storage** | B answers health requests while its PLP writes stop completing; A and C continue. Then delay A's flush as well. | Probe the actual persistence path with bounded work and inspect frontier progress. Stop counting B's non-durable acknowledgements. A live socket cannot authorize admission; preserve pending requests and deduplication outcomes during repair. |
| **S4: slow or paused leader resumes** | A sends an epoch, pauses, and misses an election or membership change. On resumption its old epoch, commit notification and client reply arrive after the new leader's messages. | Reject new actions claiming obsolete authority, while validating delayed evidence against the term/configuration in which its value was chosen. Valid old evidence or a reply may establish an outcome that still needs installation in the original lineage; its age alone is not grounds for rejection. Test duplicate replies and uncertain client outcomes. A fresh heartbeat cannot restore A's old authority, and an unseen commit notification cannot prove an entry never committed. |
| **S5: one witness gone, two remain** | C becomes unreachable; A and B continue committing later history. The two live journals now carry every new commitment. If C was the usual fast follower, every decision now waits for B's slower path; tails can worsen even though 2-of-3 remains available. B becomes slow while a replacement D is copying. | Preserve the existing election rules and measure the vulnerable interval through D's verified durable catch-up and authoritative installation. Copy start, process readiness and the name “fourth witness” do not create protected history. Do not retire A or B early. |
| **S6: insufficient evidence after partition** | A can see no quorum, but B and C may have committed elsewhere. A has an old checkpoint and healthy payload copies. | Do not lower the required authority based on elapsed time or create a new authoritative branch from A's local high-water mark. Preserve evidence, search other paths/archives and report the unresolved history boundary. Restore as an explicitly separate lineage only under the recovery procedure. |

## Correlated failure and the cost of repair

| Case | History and useful discrimination | Safe response to exercise |
| --- | --- | --- |
| **S7: region threatened during evacuation** | One AZ disappears; the remaining AZs show correlated deterioration. D and E outside the suspected region are healthy, but only D has caught up when the region becomes unreachable. | Copy existing history and separately ensure future acknowledgements meet the selected external-survival requirement. Retain old copies until handover finishes. Record exactly which prefix survives each interruption point; relocation cannot protect bytes that never arrived. An instantaneous loss without independent prior copies can outrun any detector. |
| **S8: witness escape, payloads trapped** | All witness history is copied outside the region. The two producer copies, snapshot dictionary or staged transaction result remain inside it. The region is then lost. | Treat recoverability as a dependency closure, not a witness count. Copy or retain sufficient payload/state material and its decoding dependencies outside the threatened domain. A surviving admission record alone does not reconstruct its data. |
| **S9: cold failover during blob/control failure** | A spare reservation exists, but boot requires an unavailable object service, image registry, KMS, identity service or provisioning API. Existing remote VMs still run. | Distinguish reserved, running, authenticated and durably caught-up capacity. Exercise the pool with those dependencies denied. Prefer usable existing capacity where the authorized policy permits; do not destroy healthy witnesses in anticipation of capacity that has not materialized. |
| **S10: object service versus access path** | Consumers lose GETs via a regional endpoint while another path or a peer's cache remains usable. PUTs fail independently; reads later recover first. | Probe operation and path separately. Use authenticated peer objects where complete dependencies exist. Keep unarchived durable logs/results until a verified replacement exists, bound archive debt, and stop admitting work before retained data exhausts capacity. Read recovery does not imply archival writes are safe again. |
| **S11: pool exhaustion and repair storm** | Many shards share the failed AZ. Each launches replacements and repeatedly scans the same peers for capacity. Bulk copies saturate the two remaining witnesses. | Reserve recovery resources before committing to transfers, coalesce requests and back off unproductive work. Schedule repairs by actual exposure and productive progress. Bound bulk traffic while reserving capacity for elections, metadata decisions and retention bookkeeping. Count waiting and unfinished repairs, not just completed repair latency. |
| **S12: capacity creates a decision cycle** | A pending transaction occupies the capacity needed by recovery; recovery blocks the metadata needed to resolve that transaction. Alternatively a new announcement is published before its resolution space is secured. | Inject exhausted bytes, queue slots, worker capacity and verification capacity separately. Resolution needs a runnable, authorized recovery owner with the inputs, keys and code to decide without awaiting the blocked application; spare bytes alone are insufficient. The [existing capacity cycle](../RELEASE-AFTER-POSITION.md#capacity-must-not-introduce-a-metadatadata-cycle) shows why logical metadata independence alone is insufficient; resource reservation must precede publication of blockers. |
| **S13: shared software or administration** | Geographically separated witnesses and failover nodes all receive the same bad rollout, topology edit or credential revocation. A distant spare has exactly the same dependency. | Model non-geographic failure sets. Check whether a candidate actually escapes the observed cause; preserve rollback/recovery material and authority outside the affected administration path where provisioned. Geography alone is not evidence of independence. |
| **S14: healthy again, unsafe to shrink** | C resumes answering after a long outage; replacement D is already authoritative. C has an old journal, and archive debt is still high. The controller wants to remove the expensive remote nodes. | Use hysteresis for cost-saving shrinkage, plus durable catch-up and authoritative handover. Keep one transition in control of the relevant configuration. Recovery of latency does not establish redundancy, and a delayed old “healthy” event cannot reverse a completed configuration change. |

Changing a quorum's cardinality does not by itself repair these histories. Two
survivors of a three-witness group still provide only two surviving journal copies;
requiring all three stops new commits while the third is absent. Adding witnesses
inside the same predicted failure set may add no protection against that set.
An arbitrary new threshold also risks breaking the consensus protocol's election
and reconfiguration assumptions. Test the actual old/new quorum families and
handover evidence, not a mnemonic such as “more votes is safer.”

A stronger acknowledgement condition can protect **new** history once the
necessary independent copies exist. It cannot retroactively protect earlier
acknowledgements; copying that earlier prefix remains a separate obligation.
Durable survivors after a failure also need not form an immediately usable
election quorum. Report survival and write availability separately.

A stronger remote acknowledgement gate applies to all work within its declared
scope, including transactions with disjoint effect envelopes. Absence of an
application conflict does not bypass the required remote durability evidence.
Measure that shared latency and availability cost separately from transaction
contention; the gate's scope is part of the policy, not an inferred exemption.

## Replay, caches and stale recovery

These cases complement a detailed PITR protocol. “The object is cached” is an
observation about bytes and placement, not authority to choose a recovery history.

| Case | History and useful discrimination | Safe response to exercise |
| --- | --- | --- |
| **S15: fragmented cache coverage** | Blob storage is unreachable. Three consumers jointly cache all objects needed by a checkpoint, but no one consumer has its dictionary, manifest and payload closure. One cached representation belongs to another version. | Discover and transfer immutable objects by verified identity from authorized peers, including representation dependencies. Validate the complete closure before claiming recovery at the target cut. Do not interpret a cache miss as data deletion or equal object names as equal content. |
| **S16: cache provider disappears or evicts** | A consumer advertises the last copy of a required object, then memory pressure or restart removes it before transfer completes. Its progress report is still cached. | Pin required material through completed, verified durable transfer when relying on it for recovery. If no retention commitment exists, label the cache opportunistic and keep the original recovery obligation. Avoid reclaiming sources on an inventory advertisement alone. |
| **S17: encrypted bytes, absent keys** | All ciphertext and manifests survive, but restoring on new VMs requires unavailable KMS or expired credentials. A running peer can decrypt using already authorized material. | Test authorization/decryption reachability independently from object reachability. Authorized peer service may help where policy permits; do not assume key export or indefinite credentials. Report the cut as presently inaccessible rather than silently selecting older plaintext or weakening authentication. |
| **S18: stale archive and a surviving tail** | An operator restores the newest visible snapshot while an isolated witness or producer holds later admitted history. A listing or manifest read is stale; the archive omits an acknowledged suffix. | Establish the verified recovery frontier and report the interval still uncertain. Preserve the source lineage and tail evidence. Missing archive entries are not proof of no later commitment; distinguish a chosen older PITR target from the latest provably recoverable cut. |
| **S19: participant restored before its decision** | A cross-shard transaction is staged on A, committed on its coordinator, partially installed on B, and then recovery cuts differ across shards. Another transaction is only announced and still undecided. | Restore a cut that respects the application's visibility unit and includes required decisions, bounds and pending state. Recover or fence the original decision authority before resolving uncertainty. A timeout cannot independently abort a possibly committed transaction, and replay cannot discard its blocker merely to resume writes. |
| **S20: old and restored lineages meet** | Recovery creates a new branch; an old witness rejoins and a delayed external-effect request is delivered. Clients retry uncertain operations against the restored branch. | Reject source-lineage actions that would mutate the new branch, while permitting inspection of valid source-history evidence for recovery. Retain outcome identity within the declared recovery semantics. Replay state and effect dispatch separately: reconstruction is not permission to repeat an external action. Make any chosen PITR loss and deduplication horizon explicit. |
| **S21: faithfully replicated logical error** | A bad program or accidental deletion commits, replicates and enters the newest valid archive; all hashes verify. | Select a known-good retained earlier cut and verify application meaning. Preserve its code/data dependencies. Exact historical replay and corrected execution in a new branch are distinct operations; additional replicas alone cannot undo the mistake. |
| **S22: completed observer's bound disappears** | Initially x=y=0. T@20 reads x=0 and commits y=1. Recovery loses T's observation bound at x but retains its committed result. U is then allowed position 10, reads y@10=0 and commits x=1. T's read requires T before U; U's read requires U before T, creating a cycle. | Preserve or reconstruct the observation bound, or an equivalent constraint excluding such earlier output positions, before admitting U. T's completion does not by itself permit reclaiming this constraint. Test completed work as well as pending transactions; correct application pages alone do not preserve serializability. |

Peer exchange changes availability only for material that survives somewhere
reachable and usable. It cannot replace missing admission authority, reconstruct
unavailable keys, invent a log suffix or provide durability for the last volatile
copy. Fresh recovery endpoints must authenticate peers and validate fetched
material without assuming the failed object service is available to do so.

## SOS exports before a complete recovery picture exists

In the user-requested SOS mode, reachable participants pause new admission
altogether and immediately export whatever forensic evidence and bytes can
escape. Beginning export requires no new epoch, quorum, global cut, completed
manifest or recovery-target grant. Use already authorized outbound paths and
recipients; the exported material grants no new voting or write authority.
Inventory and reconciliation can develop incrementally after transmission starts.

A local pause records what that participant has stopped doing. It does not prove
that every deciding quorum has stopped, that the last locally known committed
frontier is final, or that bytes not yet found have been destroyed. An actual
stop of the old authority needs durable, protocol-valid evidence that intersects
every legal old deciding quorum and remains effective through restart. SOS
salvage must not wait for that stronger evidence before sending useful material.

| Case | History and useful discrimination | Safe response to exercise |
| --- | --- | --- |
| **S23: only tiny messages escape** | A region's bulk transfers fail, but short SOS messages reach a distant consumer. Its archive ends at epoch 9. A witness reports its identity/configuration, locally accepted frontier 11, locally known chosen frontier 9, and hashes identifying records/payloads for 10–11. | Retain these partial, attributed observations immediately. They expose otherwise unknown work to search for and can connect later fragments. An accepted frontier is not proof of commitment; a hash identifies missing bytes without reconstructing them. Report 10–11 as unresolved evidence/coverage, not absent work or a complete cut at 9. |
| **S24: complementary fragments survive** | One witness exports chosen evidence and admission ordering, a producer exports some payload chunks, and consumers export a checkpoint, dictionary and remaining chunks to different reachable recipients. No exporter has a complete inventory or closure. | Preserve independently useful fragments and reconcile their immutable identities, lineage, configuration and ranges later. Track conflicting evidence and missing dependencies explicitly. A complete recovery path may emerge from their union, but neither the first fragment nor a count of exporting peers proves completeness or authority. |
| **S25: exporter dies midway through inventory** | A sends a header, several object identities, one accepted record and two data chunks, then disappears before finishing either its inventory or a large object. The receiver has independently verified chunks but no final marker. | Keep verified fragments and partial inventory as salvage evidence; do not require an all-or-nothing transfer or discard earlier useful records. Record truncation and unknown unenumerated material. A successful message, complete individual object or end-of-connection does not certify a completed export, final suffix or recoverable cut. Missing remainder is not proof of physical destruction. |
| **S26: local SOS pause, hidden quorum continues** | A pauses admission and exports but cannot contact B or C. B and C remain connected to each other and decide later epochs. Salvage collectors subsequently see only C, which can send preexisting chosen evidence and may still be communicating with a hidden B. | Treat A's pause as local cessation, not a global seal. A lone member cannot choose new values under 2-of-3, but one visible member may belong to a still-active hidden quorum or hold valid earlier chosen evidence. Preserve and reconcile that evidence; establish an intersecting durable stop or other protocol-valid fencing before claiming old admission has ended. Never choose a new writable history solely from the apparent lone survivor. |

## Small checks with useful counterexamples

The checks below are proposals. Their timing inputs should be declared synthetic
unless drawn from a named trace; passing a finite model is not a consensus proof.

1. **Same delay, different cause:** feed identical missing-reply intervals for a
   local pause, one-way path fault, PLP stall and AZ loss. Vary independent probe
   results and observer health. Check that local suspicion cannot mutate
   authority; report needless copies/removals alongside time to useful evidence.
2. **Cut every handover step:** interrupt before catch-up, after durable copy,
   after the old configuration authorizes the transition, during mixed delivery,
   and after activation. Replay duplicate/stale messages, including valid delayed
   evidence of an old chosen value. Check a unique legal history, preserved
   committed prefixes, rejection of new actions under stale authority, and
   installation of valid original-lineage outcomes. Missing required evidence
   must produce an explicit blocked state.
3. **Enumerate actual failure sets:** remove one VM, an AZ, a region and a shared
   service dependency from each transfer step. Evaluate surviving witness
   evidence, payload/replay closure and possible election/write quorums
   independently. This exposes S7/S8 even when witness count looks healthy.
4. **Retention under an object outage:** disable GET and PUT independently while
   writes and transactions consume finite local space. Repeatedly remove a cache
   peer and expire an advertisement. Check that acknowledged recovery material is
   not reclaimed and that admission stops before consuming resolution reserves.
5. **Repair under shared scarcity:** many shards, a bounded failover pool and one
   bottleneck source. Compare eager replacement with deduplicated, bounded repair;
   include failure of the bottleneck mid-transfer. Report exposed-prefix age,
   bytes durably copied, admission progress, redundant transfer and unresolved
   work. Do not derive incident probabilities from this workload.
6. **Resource-cycle construction:** let pending work consume each resource needed
   for finalization in turn. Check that recovery/decision work can finish without
   waiting for the work it must unblock. Include retained history, not just a
   separate network queue or reserved thread. With spare capacity still present,
   separately remove the recovery owner's authority, executable or required
   input: headroom alone must not count as a usable resolution path.
7. **PITR closure and lineage:** author a snapshot plus a missing dictionary,
   missing decision, missing admitted payload and unarchived suffix, one omission
   at a time. Verify the declared recoverable cut and refusal to claim a complete
   later cut. Reintroduce old nodes and duplicate effects after branch creation;
   inspect valid source evidence without permitting cross-lineage mutation.
   Replay S22 with the completed observer's bound removed and then restored.
8. **Repeated flapping:** alternate symptoms just before and after suspicion and
   recovery boundaries. Count topology churn and lost useful transfer progress.
   Keep durability prerequisites independent of cooldown policy, and let fresh
   evidence of a wider failure escalate without waiting for a cost-saving timer.
9. **Interrupt every SOS message:** constrain escape bandwidth to short records,
   scatter complementary fragments across recipients, and kill exporters after
   each prefix of an incomplete inventory or object. Check that salvage starts
   without an epoch/quorum/cut/manifest/target-grant prerequisite, verified
   fragments remain useful, and no prefix is promoted into a completed cut.
   Continue admission on a hidden B,C pair after A pauses, then expose only C.
   Distinguish observed local cessation, valid old chosen evidence, unresolved
   suffixes and a proved durable stop of all old deciding quorums.

The unresolved policy choice is how much protection the ordinary placement must
already provide against a failure with no warning. Detection and evacuation can
reduce an interval of exposure; they cannot promise escape from a correlated
failure that arrives before sufficient independent recovery material exists.
