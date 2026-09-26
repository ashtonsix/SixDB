# Orbital consensus

Design specification, 18 September 2026; revised 19 September 2026. Not yet implemented or model-checked.

This specification defines the behaviour required of the TLA+ model and consensus reference implementation. The engineer developing them selects the agreement and reconfiguration protocols, resolves the message and persistence details, and records those decisions alongside the model. The reference implementation must cover submission, local and global admission, execution and continuations, recovery, witness replacement, and PITR. Section 11 defines the completion criteria.

## 1. State, admission, and clearance

SixDB’s logical state is a deterministic fold over admitted input. Contributors own WAL streams; witnesses agree which stream ranges enter each epoch; data members obtain those inputs and execute the epoch. Payload storage, witness agreement, and execution may occupy different machines.

For each data shard, `S[e] = Fold(S[e−1], I[e])`. The preceding state and ordered admitted inputs determine the next state, results, and continuations. Epochs clear in order. Replicas may prepare later epochs speculatively, but may expose results only after the required epochs clear.

| Event              | Guarantee                                                                                                                                                                                      |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Durable submission | The input and its pending-admission status survive the failures covered by the deployment’s durability policy.                                                                                 |
| Admission          | A witness quorum has fixed the input’s epoch assignment.                                                                                                                                       |
| Clearance          | The epoch’s transition is complete, its predecessors have cleared, and any required multi-shard coordination is complete. Retained state or inputs allow recovery, and results may be exposed. |

A submission acknowledgement must identify which event it confirms. A caller that loses the response must allow for the event having occurred. For latency-critical logging, records may also be sent to data-loss-tolerant processors alongside journal submission, before persistence or admission. This delivery carries neither guarantee and is not repeated during replay.

A workflow can complete a transition while leaving a durable continuation for work awaiting external input. Once the resumption condition is satisfied, the continuation enters a later epoch. This lets intervening epochs execute and enter snapshots while the workflow remains unfinished. Programs may discover their data accesses during execution; admission does not require complete read/write sets or advance byte claims.

## 2. Shards, journals, and identities

There is one global witness shard per deployment, multiple regional witness shards, and multiple data shards. Witness shards use 2-of-3 consensus, with elected leader, fast-follower, and slow-follower roles. The fast follower may initiate propagation once it has established agreement under the selected protocol. A VM may belong to several shards and has one role per membership.

Each data shard’s regional journal orders local epochs and imported global epochs. A global epoch has its own identity; each participant’s import records its position in that data shard’s journal. One witness shard may serve several journals and batch their metadata into a single write. Sharing witnesses does not merge their epoch boundaries or couple clearance except where multi-shard work requires coordination.

A WAL stream identifies its contributor incarnation and destination shard, with separate regional and global streams where both exist. LSNs are scoped to the stream. Ownership is permanent: a replacement VM may forward old records but uses its own identity for new appends. An existing `(stream, LSN)` always names the same immutable content.

Journal identities survive leader changes. Recorded configuration identifies authorised witnesses, data membership, routing, and durability requirements. PITR creates a new history identity when it abandons an old suffix. Discovery and returning disks must respect the recorded authority; old configurations and histories must remain distinguishable in recovered records and messages.

The model assumes authenticated, non-Byzantine participants, correct reducers, and storage honouring its declared persistence semantics. It must permit crashes and message loss, duplication, delay, and reordering. Safety must not depend on synchronised clocks or timeouts. Progress requires eventual access to the necessary quorums and retained inputs, fair retries and scheduling, terminating transitions, and sufficient resources. Permanent destruction of all copies of required history exceeds lossless recovery; any cutover that abandons history follows the PITR contract.

## 3. Submission and durability

Before accepting work, the receiving VM validates framing, routing, declared limits, and capacity. Accepted submissions are final: cancellation, contributor failure, buffer pressure, or elapsed retention deadlines cannot retract them. A business condition that fails during execution produces a deterministic result while the admitted input remains in history.

The protocol must identify the exact acceptance transition and how its obligation survives failure. Model crashes before and after that transition, including loss of the response. Receiving bytes into volatile memory is insufficient to establish an obligation that survives loss of the receiver. Durable submission requires both recoverable bytes and recoverable knowledge that the stream contains outstanding work.

Use durable stream registration and WAL tails to discover pending submissions. Register the stream, destination, durability policy, and holders before confirming durable submission. Recovery compares surviving complete WAL tails with the journal’s admitted frontier and resubmits the outstanding records. Holder changes must become discoverable before old copies are removed. This permits stream-level bookkeeping without a separate pending ledger for every operation.

WAL framing must distinguish complete records from torn tails. Reserving LSNs must not leave permanent gaps before accepted work. Interpretation references must retain the code, query plans, dictionaries, models, and other versions needed to replay the input.

Transport retries preserve the original stream and LSN. An already admitted record returns its existing assignment. A retry through a new stream is a new submission unless the application supplies semantic deduplication. Continuations have their own stable identities for this purpose, as described in section 7.

The durability policy names the failures the deployment promises to survive. One additional VM copy can protect a WAL against loss of either VM; AZ, region, and provider survival require corresponding placement. The same policy must cover admission metadata, since payloads alone cannot recover their agreed order.

A storage receipt identifies persisted content, its holder, and the holder’s retention obligation. Forwarding a receipt creates no additional copy. When witness and data roles overlap, a payload and witness vote may share a write. Every possible deciding witness pair must satisfy the durability policy, and each voter must retain the payloads its role requires. Separate witnesses require evidence of durable external coverage for every range before voting, allowing payloads to remain within their jurisdiction.

Define receipt validity and repair behaviour under holder failure and relocation. A surviving vote record beside a torn required payload is not a durable vote. A witness that loses previously acknowledged storage must recover its required history or be fenced against voting with incomplete state. Deletion requires a durable replacement meeting the applicable recovery and retention requirements in section 9.

## 4. Witness agreement

The leader buffers submissions until durability and admission prerequisites hold. It admits consecutive LSN ranges from each stream, places continuations after their creating epochs have cleared, and requires complete participant lists and WAL fragments for coordinated work.

Witness agreement fixes assignments of the form:

`Admit(data_shard, epoch, ordered_stream_ranges, interpretation_bindings)`

Each range names a stream, starting LSN, count, and content identity. The reference execution order is increasing LSN within a range and journaled order between ranges. Interpretation bindings identify the retained versions used to decode and execute the input.

Witnesses also order configuration, stream registration, imports, and verification metadata. Processing these control records must not wait for completion of the data epochs whose progress they enable.

| Frontier                            | Meaning                                                              |
| ----------------------------------- | -------------------------------------------------------------------- |
| Stream admitted frontier            | Highest consecutively assigned LSN.                                  |
| Journal committed metadata frontier | End of the ordered metadata sequence known to be fixed by consensus. |
| Data-shard cleared frontier         | Last epoch in the consecutive sequence whose results may be exposed. |

Agreement can advance while clearance waits. These frontiers do not replace the accepted witness state needed for election recovery.

For example, with LSNs through 40 admitted, 42 waits for 41 while other eligible streams may proceed. Once 41 arrives, the leader may assign both records in one batch or successive batches. A receiver notified of the later batch first obtains the missing metadata or equivalent committed-prefix evidence. Recovery preserves both committed assignments even if neither notification reached a caller.

Select and instantiate one complete consensus construction, such as [Raft](https://raft.github.io/raft.pdf) or [Paxos](https://lamport.azurewebsites.net/pubs/paxos-simple.pdf). The model must specify election, voting, commitment, follower learning, and recovery together. In particular, it must establish when the fast follower can learn admission from its own durable vote and the leader’s evidence, before the leader receives the follower’s response. The reference implementation must exercise that path and the leader/slow-follower fallback.

Each witness persists the protocol’s election state and accepted proposals before acknowledging them. Witness acceptance means voting for an assignment; it is separate from accepting a submitted WAL record. Quorum evidence must bind the configuration, election generation, decision position, and exact value according to the selected protocol. Commitment and each observer’s knowledge of commitment are separate model state.

Expose a committed metadata prefix while allowing proposal transmission and persistence for successive batches to overlap. Track provisional assignments separately from committed ones. A dependent batch must retain its predecessor history through elections and recovery. If the selected construction can choose values beyond undecided metadata positions, preserve every possibly chosen value and show that filling the earlier positions cannot invalidate those assignments. Ineligible submissions remain buffered; they are not recorded as `Deferred` decisions for later interpretation.

A local commit watermark cannot justify deleting every later vote. Recovery must preserve decisions that may already have been made but whose notifications were lost. Inputs displaced from proposals established to be uncommitted remain in their owned WALs and are proposed again under their original identities.

After learning admission, the fast follower initiates propagation of the assignment and any required payloads. The leader may do the same after agreement with the slow follower. Duplicate propagation must be harmless. Propagation responsibility does not change the quorum requirement.

For a metadata cost target, consider a 64-byte admission header and 64 bytes per range: eight bytes each for stream handle, starting LSN, and count; a 32-byte digest; and an eight-byte interpretation reference. Ranges of 100, 50, and 200 records require 256 bytes for 350 admissions, about 0.73 bytes per record, before authentication, receipts, participant lists, and registration.

Fan-in may combine adjacent ranges while preserving content identity and execution order. The example needs three frontier checks and updates, three content bindings, and one batch acceptance per witness, potentially sharing the payload write. Measure the additional cost of fragmentation and exceptional metadata. Witnesses need not execute programs, predict accesses, or receive payloads outside their storage role.

## 5. Execution and local clearance

An epoch’s admitted inputs and preceding state determine its logical effects. All permitted execution schedules must produce the same logical state and observable behaviour. Database and extension invocations may exchange intermediate values and iterate within the epoch; derivable intermediate values need no separate journaling.

Clock values and entropy used for randomness must be journaled when they affect results. Externally computed values must likewise become admitted input. Architecture and compiler differences must preserve deterministic results; floating-point behaviour, including FMA selection, needs an explicit execution policy. Physical layouts, auxiliary indexes, caches, and lossless encodings may differ between replicas.

Local clearance requires admission, predecessor clearance, and completion of the transition with retained state or inputs sufficient for recovery. A serving replica must reconstruct the state it exposes. Specify how executors report completion, how a recovering replica re-establishes it, and what evidence permits a continuation or reader to rely on clearance. The model must distinguish completion, clearance, and each replica’s knowledge of them.

Journal output-verification reports by epoch, invocation, and interpretation version. Conflicting outputs for identical admitted inputs stop affected clearance pending diagnosis. Specify whether verification reports gate exposure and how a late discrepancy is handled. Correct reducers are a safety assumption; the verification mechanism must expose violations of that assumption rather than treat input consensus as proof of correct execution.

## 6. Global epochs and reads

A global request names its participating data shards and WAL fragments before admission. Its programs may discover records and byte ranges during execution. Work outside the declared participant set enters a later admitted step.

The global witnesses admit the request and distribute its global epoch assignment. Participating regional journals durably place the import among local epochs, preserving global order despite reordered notifications. A journal establishes nonparticipation from authoritative global metadata. The import’s identity and regional position remain fixed after agreement.

Regional placement fixes each participant’s preceding state. Remote inputs must be admitted values or values deterministically derived from those fixed participant states. Retain the required prefixes or their reconstruction inputs until no participant needs them. Execution must not consult another shard’s changing live state; this design supplies fixed inputs for the coordinated transition, without requiring a general historical-read facility.

A participant becomes ready after its predecessor clears and its own part of the computation finishes with recoverable outputs and continuations. Its regional journal records a readiness report from an authorised executor, bound to the global epoch, local assignment, predecessor, and output identity. Retained inputs and versions must allow reconstruction after executor failure. Readiness must be established without waiting for the same global epoch to clear.

The global epoch clears when every participating journal has durably admitted its import and committed a valid readiness report. The committed reports collectively establish clearance without another global ordering decision. Report cost is per participating epoch; reports may share control batches and verification writes.

The model and reference must include report validation, dissemination, retries, and recovery. A replica may expose a global result only after it has obtained evidence covering the complete participant set and reconstructed its own state. Lost reports must be recoverable from committed journal records. Keep the fact of global clearance separate from a replica’s knowledge of it.

A query spanning shards must use compatible prefixes: a global transition included in the view is included at every participant. Point lookups, scans, and indexes must agree on that view. A request causally following a global result carries enough ordering context for its destination journal to resolve the relevant imports before admitting the request.

Define and implement the reference read contract during modelling, including prefix selection, causal context, and freshness. State which operations are linearizable and the protocol that establishes that guarantee; identify any weaker read modes explicitly. Global clearance permits exposure but cannot make network delivery simultaneous.

An unavailable participant may block its global epoch and later epochs in the affected data shards. Other shards do not inherit that wait merely by sharing witnesses. Batching independent work into one coordinated epoch couples clearance to the union of participants; batching witness writes need not combine execution epochs.

## 7. Continuations and external effects

A continuation records work created by a completed transition: creator, destination journal, retained inputs, code version, and resumption condition. Derive its stable identity from the creator’s admission coordinate and output ordinal. Its meaning must survive loss of resident pointers, stacks, and queues.

For a transfer, one regional epoch reserves funds and records a continuation to finalise or release the reservation. That epoch clears. Later local epochs can execute and enter snapshots while settlement remains pending. A later epoch resumes the continuation from admitted settlement information. If finalisation needs an atomic multi-shard step, that step follows global clearance and may wait for an unavailable participant.

Represent the resumption condition in durable workflow state. Specify which admitted events make it eligible and how a dispatcher reconstructs eligibility after failure. Pending continuations remain in that state until ready; submitting an unready resumption ahead of ordinary work in a WAL would block the stream under the gapless admission rule.

Any authorised recovery dispatcher may submit a ready step. The destination fold consumes the continuation identity at most once, including when retries use different contributors’ WALs. Consumption and the continuation’s logical effects belong to one transition. Duplicate submissions consume their WAL positions but have no further logical effect. Snapshots retain consumption state.

Finalise and release must resolve through one authority in admitted order. For the transfer workload, specify that authority and the allowed reservation transitions. A journaled timeout alone cannot establish that remote settlement failed. Release requires a decision that excludes subsequent contradictory settlement; otherwise the reservation remains pending.

Replay reconstructs state and continuations without redelivering records to immediate log consumers or reissuing external effects. Live delivery of recovered effect intents requires destination deduplication or reconciliation, since delivery may have succeeded before its acknowledgement was lost. The reference must exercise this uncertainty, including crashes after delivery and before recording the response.

## 8. Sequencing and latency

Persistence, payload transfer, and proposal transmission should overlap. Persistence-status messages follow completed writes; admission-status messages follow quorum agreement; clearance follows execution and coordination. Payloads may follow inexpensive relay paths while small agreement and verification messages travel directly. Transport choice must preserve content identity and tolerate duplicate propagation.

All origin cases belong in the model and reference:

| Case                                          | Required path                                                                                                                                                                                                                |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Leader-origin, overlapping witness/data roles | Leader sends payload and proposal to both followers while writing locally, then sends persistence evidence. Fast follower persists its payload and vote, learns admission under the selected protocol, and propagates.       |
| Fast-follower origin                          | Contributor WAL persistence overlaps transfer to the leader. The leader proposes the assignment; the fast follower persists assignment metadata even if its payload is already durable, then learns from the required votes. |
| Slow-follower origin                          | The leader assigns and distributes the proposal. Leader/slow-follower agreement may establish admission and initiate propagation without awaiting the fast follower.                                                         |
| External contributor                          | Payload copies and metadata submission proceed concurrently. Witness voting waits for the required range-durability evidence. Caller notification includes the return network path.                                          |
| Global origin                                 | Participant payloads replicate concurrently, followed by global admission, concurrent regional placement, execution, readiness reports, and clearance.                                                                       |
| Fast follower unavailable                     | Leader uses the slow follower when quorum and durability requirements permit. Repeated propagation remains harmless.                                                                                                         |
| Leader failure                                | Election recovery preserves chosen and possibly chosen assignments; uncommitted inputs are retried under their original stream identities.                                                                                   |

The brief’s illustrative leader-origin budget assumes established leadership, registered streams, satisfied admission prerequisites, 15 µs durable writes, and 125 µs one-way delay to the fast follower:

| Time   | Event                                                                                                                                                                                                                                                |
| ------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 0 µs   | Leader sends payload and proposal to both followers and starts its write.                                                                                                                                                                            |
| 15 µs  | Leader’s write completes; it sends persistence evidence.                                                                                                                                                                                             |
| 125 µs | Fast follower receives the payload and starts its write.                                                                                                                                                                                             |
| 140 µs | Fast follower’s write completes and the leader’s evidence arrives. If the selected protocol establishes admission here, the follower notifies the leader and starts propagation. Execution may start when its required preceding state is available. |
| 265 µs | Fast follower’s notification reaches the leader with the same assumed return delay.                                                                                                                                                                  |

The write budget includes durable witness state as well as payloads. The schedule does not establish when the slow follower learns admission. Halving a measured p99 RTT supplies an estimate, not a measured one-way p99. Batching that delays a send or write shifts the schedule.

Validate the fast-follower learning rule in the model and measure every origin case under load. Report submission durability, admission, clearance, and caller-response latency separately, including queueing, metadata bytes, physical writes, fragmentation, and recovery overhead. Additional mechanisms chosen for succession or recovery must be included in these costs.

## 9. Snapshots, replay, and retention

A snapshot represents the state after a cleared prefix of one data-shard journal. It records that boundary, interpretation versions, covered stream frontiers, pending continuations and consumption state, and outstanding external-effect intents. Admitted epochs beyond the boundary remain to be executed during recovery.

Data recovery restores the snapshot and replays subsequent admissions in order. Pending submissions are rediscovered from stream registrations and surviving WAL tails. Witness recovery separately restores the election and agreement state needed to preserve decisions. A data snapshot cannot substitute for a voter’s lost election state.

Snapshots used together must select compatible prefixes across global participants. A global transition included at one participant must be represented at every participant in the restored view. Physical timestamps may differ. The reference must construct such checkpoints and recover from failures during checkpoint creation and installation.

Define reclamation guards for WAL payloads, admission metadata, snapshots, interpretation versions, and continuation-consumption records. Deletion is permitted only after durable replacements satisfy policy and every outstanding local or remote dependency has been accounted for. A source region’s newer snapshot does not remove another participant’s need for old inputs. Witness compaction must preserve election recovery; continuation compaction must preserve duplicate suppression.

Apply backpressure before accepting new work. Accepted submissions need retained capacity, spill, reconstruction, or another path to completion. Reserve enough resources for control traffic and repair to progress when data execution is blocked. Model resource exhaustion where it affects acceptance and retention; the reference must demonstrate bounded intake during an outage without retracting accepted work.

## 10. Witness replacement and PITR

The reference must implement leader replacement, planned witness membership changes, emergency succession, and PITR. The model must distinguish temporary unavailability from permanent loss of stored history.

| Witness availability     | Required behaviour                                                                                                                                          |
| ------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------- |
| One of three unavailable | The other two continue after communication and any required election.                                                                                       |
| Two unavailable          | Admissions pause until a witness quorum returns or lossless succession completes. Destruction of those stores may erase decisions held only by that quorum. |
| All three unavailable    | Recover retained witness state or complete succession from other sufficient records. If required history is irrecoverable, use an explicit PITR cutover.    |

A lossless membership change must select one successor, fence conflicting admissions by the old authority, preserve every committed or still-possibly-committed decision, and transfer responsibility for accepted submissions and continuations. The protocol must cover delayed messages and writes in flight across the handoff. Planned replacement can use the old quorum; emergency succession must address its absence.

For emergency succession, a data member may nominate replacements. Adoption requires `⌊n/2⌋+1` endorsements under the current recorded data-shard election membership. Endorsements must bind one successor and survive restart. When witnesses serve several data shards, define the governing electorate or coordination between their elections in configuration before a failure occurs.

The data-member election chooses the successor; activation also requires sufficient history and an effective fence. Specify where recovery obtains decisions that the old witnesses may have committed without notifying data members. If the construction requires additional retained or replicated metadata, include it in the normal protocol, durability policy, and performance accounting. A timeout or higher configuration number cannot substitute for missing history.

Specify how fencing is enforced when old witnesses remain alive and mutually reachable. The model must include their attempted admissions and returning messages after successor activation. Any external fencing service or operational action used by the construction needs a stated contract and an exercised reference path. Model the state and checks that reject old-authority requests, including failure of the fencing mechanism.

Demonstrate successful emergency succession under the construction’s stated recovery and fencing conditions. When those conditions cannot be established, identify the missing evidence and remain unavailable or enter authorised PITR. Permanent history loss cannot be repaired by an election. The implementation must distinguish these outcomes so an operator can recover quorum, supply the required recovery or fencing action, or choose data loss explicitly.

Evaluate the brief’s proposed emergency stake reductions within this protocol. Partial broadcasts and restarts must not produce incompatible election thresholds or successors. Either implement a safe form with its configuration-transition rules, or document why the proposal fails the model and how succession proceeds without it. Resolve this as part of the succession design.

PITR may abandon an unrecoverable suffix after a bounded recovery effort. Select a recoverable boundary consistent across participating journals; record the old boundary, new history identity, successor authority, and abandoned work. The cutover may lose acknowledged results and is outside the lossless-consensus guarantee.

Fence the abandoned deployment before the replacement serves as current. Returning members must synchronise to the restored history or be evicted. Old messages remain distinguishable. PITR does not undo external effects already delivered. A recovery deadline bounds the search effort; it does not establish a safe cutover. Model and exercise interrupted cutover and restart of both old and new members.

## 11. Model and reference deliverables

The model may be developed in stages, with small bounded instances and separate checks for agreement, execution, and reconfiguration. The completed model set and reference must cover the full behaviour above, including transitions between normal operation, recovery, succession, and PITR.

Record the selected protocol as state variables, message and durable-record definitions, and guarded transitions. For each participant, distinguish volatile state, persisted state, in-flight writes, and locally known decisions. Map reference handlers and persistence points to those transitions. Document abstractions, fairness assumptions, explored bounds, and the arguments supporting claims beyond those bounds.

Check at least these properties:

- A stream position has one immutable content and at most one epoch assignment; admitted ranges remain gapless through retries, elections, and recovery.
- Admission satisfies the configured durability policy. Loss of a response cannot erase a committed assignment or an accepted submission obligation.
- Permitted execution schedules yield identical logical results. Results remain hidden until predecessor and global clearance requirements hold.
- Global imports preserve order; reads and recovered snapshots contain compatible participant prefixes.
- Continuation creation and consumption survive failure; retries have no duplicate logical effects. Replay does not reissue external actions, and live delivery follows the declared destination deduplication or reconciliation contract.
- Reconfiguration establishes one authority and preserves history. PITR records any abandoned history and prevents the old deployment from serving as current.
- Retention and backpressure preserve every outstanding recovery obligation within the declared fault and resource assumptions.

Check liveness under the stated availability and fairness assumptions, alongside the expected stalls when required quorums or participants are unavailable. The reference must demonstrate successful recovery as well as rejection of unsafe transitions.

| Scenario                         | Required demonstration                                                                                                                                                                                  |
| -------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Lost messages and leader failure | Recover admissions whose notifications were lost, including a voted suffix beyond the surviving node’s known commit position; retry uncommitted inputs without changing their identities.               |
| Submission and storage failures  | Crash across the acceptance and persistence boundaries, registration, partial WAL writes, holder relocation, and response delivery; preserve every obligation the protocol has accepted.                |
| Global coordination              | Reorder imports and readiness reports, restart participants, recover missing reports, and expose only complete compatible results.                                                                      |
| Continuation progress            | Reserve funds, clear the epoch, execute local epochs during a remote outage, checkpoint, crash, recover the continuation, and settle once. Also demonstrate the stall of an unsplit global transaction. |
| Reclamation                      | Remove covered history while retaining versions, remote inputs, deduplication state, and voter state still needed after restart.                                                                        |
| Membership changes               | Complete planned replacement and emergency succession; exercise competing nominations, unreachable old witnesses, shared-witness elections, and returning members.                                      |
| PITR                             | Recover from permanent loss using a consistent cutover, account for abandoned acknowledged work, reject old-history traffic, and resume after an interrupted cutover.                                   |

The reference may use straightforward storage, transport, and deterministic workloads. It must implement the protocol’s persistence, failure, and recovery semantics and expose the read and acknowledgement contracts it claims. Optimisation can follow the working reference; mechanism selection and the recovery paths above are part of completing it.

Sparse publication of later work within a data shard while earlier epochs remain unfinished is a separate architectural alternative to this specification’s ordered clearance. If workload evidence warrants it, it requires dependency tracking, stable versions, compatible partial reads, selective installation, and snapshots and reclamation with holes. The specified continuation and multi-shard paths must be complete without depending on that alternative.
