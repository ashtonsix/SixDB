# Operator recovery, replay and unavailable blob storage

2026-09-26. A proposal for [Orbital brief](../../../../orbital/BRIEF.md),
with authored failure histories rather than an implemented recovery protocol.
The priority is preserving acknowledged work, then restoring write availability,
then avoiding unnecessary work. Intentional recovery to an older point is an
operator choice to exclude later history; it must not masquerade as lossless
automatic failover. The ordinary 2-of-3 witness fast path is a fixed requirement;
this note does not add synchronous remote work to it.

The main requirement is **a recoverable, consistent history, including its
authority and interpretation**. A witness journal without payloads, cached
payloads without outcomes, or an archive without decryption/runtime dependencies
is insufficient. Moving witnesses alone cannot repair these gaps.

## What recovery promises

The following are proposed safety requirements, not existing implementation
guarantees:

- Ordinary failure recovery preserves the original lineage and its committed
  outcomes, plus durably accepted submissions still awaiting admission.
  Unavailability never licenses forgetting a possible commitment.
- PITR constructs a separate lineage at an explicit selected cut. Preserve the
  source history and available surviving tails while inspecting alternatives.
- A restored application view respects its declared atomic visibility units and
  observation dependencies. Independent per-shard stopping points are not
  automatically a database snapshot.
- Missing bytes, missing authority and missing interpretation are reported
  separately. None is silently replaced by an older value, empty object, abort,
  fresh execution against today's inputs, or successful recovery status.
- A claimed recovery window includes every dependency of the supported cuts.
  Availability of one recent checkpoint does not establish that window.

These requirements are conditional on surviving sufficient information. No
protocol reconstructs arbitrary destroyed payloads or a lost decision whose
alternatives cannot be distinguished. The system should preserve evidence and
offer provable cuts, with the unknown or excluded tail identified. The newest
complete cut may faithfully contain an accidental deletion or a bad program's
effects. For logical/operator corruption, select a known-good earlier cut whose
dependencies are still retained. Integrity hashes establish byte identity, not
application correctness. Exact historical replay preserves the historical
program and accepted outcomes; executing a corrected program is a distinct,
explicit branch operation with its own validation and external-effect handling.

## The recovery target is a cut, not an epoch number

The brief assigns a common total order to transaction positions but permits earlier
work to remain pending while later independent work commits. Shards have
independent epoch sequences. Therefore neither the largest observed position nor
the smallest numeric shard epoch establishes a complete recoverable prefix.

Consider a transaction T transferring a value from A to B at position 40. Its
coordinator C records commit in epoch 73. A installs its part in epoch 81; B has
only staged its part at epoch 54 when the incident starts.

| Available evidence | Safe interpretation |
| --- | --- |
| A@81 and B@54 application pages only | Not a publishable transfer snapshot: one side may be missing. |
| Those pages, C's committed decision, complete staged outcome and authority metadata | Reconstruct T on B under its original position, while preserving visibility dependencies until the requested view is complete. |
| Those pages and staged bytes, but C's decision is unavailable | T's outcome remains unknown. Bytes and a timeout cannot select commit or abort. |
| An earlier complete cut before T and all effects that depend on T | A possible PITR branch; explicitly exclude later history rather than rolling one participant back in place. |

For an exact target, propose a **recovery manifest** naming the source lineage,
scope, per-shard base states and replay cursors, included decisions, required
protocol state including completed observers' bounds, and an authenticated
dependency inventory. This names information to establish, not a fixed format or
new service. Engine supplies
application visibility and dependency semantics; Orbital checks the durable
history and authority it is given.

Two useful cuts have different obligations:

1. **A restart cut** can contain pending work and messages. It retains allocation
   queues, observation bounds or sufficient monotone position floors,
   provisional/exact positions, declarations,
   continuations, staged results, durable messages and decision ownership needed
   to resume safely. A checkpoint cannot discard an uninstalled committed result
   or a read-blocking possibility merely because application pages look complete.
2. **A materialized historical view** includes a closed set of committed effects
   and their dependencies. Every included atomic outcome is represented across
   its required scope. Work excluded at that target stays excluded in the new
   lineage; it is not declared aborted in the source lineage. A general view
   claiming all effects through position p additionally needs proof that no
   authority can introduce an unaccounted effect at or below p. Advancing some
   shards beyond p does not supply that proof.

An operational cut may need protocol records later than the selected application
view to establish an earlier-position decision. Reading those records is not
permission to expose every later application effect. Conversely, a historical
view that includes a transaction decided after the requested wall-clock time is
not an exact “as visible then” restore merely because its serial position is old.

Initially, expose named, certified recovery cuts and show their time bounds. A
wall-clock request can select a known cut no later than the requested time under
the stated clock bounds, reporting any gap. Exact wall-clock PITR needs an
explicit mapping from time to outcome/publication history; the brief's logical
positions deliberately do not provide it. Cut construction, closure granularity
and certification cost remain open. This does not require globally pausing every
ordinary transaction to create each shard checkpoint.

The distinction between local snapshots and a consistent global state is classic;
Chandy–Lamport includes channel state as well as process state. Its FIFO-channel
algorithm is not directly selected for Orbital's duplicate-tolerant routing.
[Distributed snapshots](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/12/Determining-Global-States-of-a-Distributed-System.pdf).
FoundationDB demonstrates a different concrete choice: combine an inconsistent
data copy with change logs to reconstruct a consistent point-in-time database.
[Backup and restore](https://apple.github.io/foundationdb/backups.html).

## The frontier for reopening a recovered scope

Restoring a checkpoint does not itself establish where new work may start. For
each affected scope, recover an exact frontier that identifies the source
lineage, logical authorities and domain definitions; the inherited chosen epoch
prefixes and admitted stream ranges; and the control state implied by those
prefixes and any inherited obligations. This frontier comes from the authority
succession/recovery protocol, not from the newest locally readable file. It must
account for possibly chosen suffixes and in-flight decisions that the recovered
authority is required to preserve; a convenient older checkpoint cannot erase
them. The proposed manifest can record this frontier without prescribing a wire
format or a separate consensus service.

Before reopening a scope, establish its **recovery closure**: every announcement,
observation bound, position, outcome and authority dependency that can constrain
new work in that scope is reconstructed or conservatively represented by an
explicit unresolved obligation. Restore the relevant cross-shard decisions and
messages, or retain the exact blockers and ownership needed to recover them.
Missing evidence must not look like an absent predecessor. Replay cursors are
inclusive/exclusive by an explicit convention so neither gaps nor double
application can hide at a checkpoint boundary.

This is a control-state frontier, not a requirement that all admitted programs
finish. Older computations may remain pending at their original positions, with
their captured inputs, continuations, effect coverage and retained resources.
New work may proceed only under those recovered constraints and ordinary
publication rules. A dependent observation can still wait; independent scopes
can reopen when their own closure is established. A completed historical view
has the stronger visibility obligations described above.

Completed observers also constrain the future. Starting with x=y=0, T@20 reads
x=0 and commits y=1. Suppose recovery or GC drops T's observation bound on x
because T is complete. U may then incorrectly receive position 10, read y@10=0,
and write x=1. U@10 preceding T@20 contradicts T's observed x=0: the observations
require both T before U and U before T. Retain x's observation bound, or a
monotone position floor at least as restrictive, before deleting T's individual
record. That floor must survive checkpointing, ownership succession and domain
remapping. A pending-transaction inventory alone cannot establish restart safety.

Reopening also requires a decision/control path that is independently authorized
and executable: usable current ownership, access to the required historical
evidence, control-code/verification dependencies and reserved persistence,
execution and messaging capacity. Spare bytes alone do not establish that path.
Required metadata service must not wait for publication of the blocked
transaction's results when that transaction needs the metadata to progress.
The transaction itself may still await execution or verification. Where authority
remains unavailable, preserve the unresolved scope rather than manufacturing an
abort; this requirement does not promise progress through an arbitrary partition.

Physical witness succession preserves logical shard, transaction, coordinator
and domain-authority identities. It changes who may exercise that authority
under the recovery protocol. Valid proof that an outcome was already chosen
under an older term/configuration may still install that outcome in the original
lineage. Reject stale authority for **new** actions, not historical evidence
solely because its term is old. A PITR branch is a separate lineage and cannot
accept such a message as an unsolicited new mutation.

## What must survive replay

For each supported cut, retain a complete path from a usable base to the target.
Different paths may use materialized state or replay; every path needs its own
dependency closure. The inventory below follows the brief's current composition.

| Dependency | Why payload logs and checkpoint pages alone are insufficient |
| --- | --- |
| Agreed epoch history, contiguous admitted producer ranges and payload identities | Persistence alone is not admission. A gap cannot be crossed by accepting a later LSN. |
| Accepted submissions not yet admitted | Recover stream registration, holder discovery and durable WAL tails; compare them with admitted history and resubmit under stable identities. Losing the producer or its reply must not erase a promised submission. |
| Authority history and protocol state | Recover membership transitions, logical coordinator/attempt ownership, allocation queues, observation bounds or sufficient monotone position floors, provisional/exact positions, declarations, terminal outcomes and late-message fencing. Completed observers' constraints survive GC. Validate chosen historical evidence under its original authority; it does not reinstall old credentials or authorize new actions by old owners. |
| Complete outcome and installation state | A commit can precede installation on some participants. Retain the complete staged result or a valid reconstruction path, including responses and effects beyond changed pages. |
| Object/domain definitions and historical authority mappings | Replay must use the overlap, coverage and interpretation rules under which declarations were admitted, including effects that eventually produced no write. |
| Physical representation closure | Object chunks, dictionaries, codec/format versions, conversion inputs, base/delta ancestry, indexes when not reconstructible, and exact authenticated representation identities. |
| Source, executable and deterministic runtime closure | Journal exact extension source versions and executable artifact identities used by each transaction, with applicable verification requirements or approved exemptions. Retain referenced source/executable artifacts for replay and audit, alongside required Engine/runtime versions, WASM modules or VM images, adapters, ABI/configuration and relevant numerical/encoding behavior. A source commit alone is not an executable recovery dependency. |
| Captured observations and accepted outputs | Remote responses, timestamps, randomness and other accepted external facts must replay from agreed inputs. Preserve exact extension inputs/outputs, identity, applicable verification evidence and approval context bound to the transaction's extension uses. A cached digest alone does not establish the authority to reuse an output. |
| Continuations and resource obligations | Pending execution must resume with the original inputs and position. Rebuild both reserved resolution capacity and an independently authorized, executable control/decision path before allowing new blockers. |
| Cryptographic access and trust material | Available encrypted bytes require usable authorized key versions and trust roots. Identify KMS/IAM dependencies, key rotation and deliberate key destruction explicitly; never archive plaintext keys in manifests. |
| Identity and deduplication history | Stable producer/transaction/effect identities, known results, connector offsets, replay branch ancestry and the retained outcome horizon prevent retries from becoming new work. |

An authenticated, self-contained materialized checkpoint can retire earlier
replay dependencies only when no retained recovery cut, unresolved transaction,
representation or continuation still references them, and all still-required
ordering constraints are preserved, individually or as sufficient monotone
floors. Completion of the transaction that created a read bound is not permission
to delete the bound. Hot-swapping code and rotating dictionaries therefore
participate in retention, not merely deployment.

Required control-metadata service must remain independent of blocked application
execution. Restore its authority, executable dependencies and capacity before
demanding completion of the programs whose dependencies it resolves.

Verification is **transaction-scoped**: all of a transaction's extension use must
satisfy its applicable verification requirements before its results become
visible. Specially approved extensions do not require BLAKE3 verification;
preserve extension identity and the approval context needed to interpret recovered
evidence against the journaled source version and executable identity used by
that transaction. Authorship or an audit does not itself establish that approval.
Recover pending verification and its exact inputs, outputs and evidence with the
transaction. An invocation still awaiting required verification does not gate its
entire execution epoch; unrelated transactions in that epoch may progress and
publish subject to their own requirements and actual dependencies. Preserve the
pending transaction's blockers and outcome obligations. A replacement consumer
may reuse an accepted artifact only with valid historical authority; re-execution
must satisfy applicable requirements. This specifies recovery obligations, not a
verification protocol or approval mechanism.
See [publication composition](../reconsideration/COMPOSITION.md) and the
[capacity cycle](../RELEASE-AFTER-POSITION.md#capacity-must-not-introduce-a-metadatadata-cycle).

## Decisions, new lineages and external effects

| Recovered status | Original-lineage recovery behavior |
| --- | --- |
| Proven committed; some installation missing | Install idempotently at the decided position, including valid evidence of a choice under an older term/configuration. Do not rerun against current state or reinterpret no-effect coverage as deletion. |
| Proven aborted | Preserve its terminal outcome and reject late stages, commits and messages for that attempt. Resolve only its own announced possibilities. |
| Staged or prepared; decision unknown | Preserve its blockers and obtain the decision through the owning recovery protocol. An agreed abort requires authority that excludes a possible competing commit; a local timeout is insufficient. |
| Payload persisted, admission not established | Do not fold it merely because recovery found the bytes. Retain the stable identity for safe resubmission once admission status can be established. |
| Client timed out, commit recovered | Return the existing outcome on retry; client uncertainty does not create a second transaction. |

The transaction-commit distinction is not specific to an execution engine:
Gray–Lamport treats commit/abort as an agreement problem and describes why an
unavailable coordinator can block ordinary two-phase commit.
[Consensus on transaction commit](https://arxiv.org/abs/cs/0408036).

PITR should produce a fresh database/branch incarnation with a parent lineage and
exact cut. Use it in request, stream, transaction and protocol identities so a
delayed source-lineage message cannot mutate the new branch. Preserve source
archives rather than overwriting identically numbered epochs. PostgreSQL's
separate recovery timelines and ancestry records provide relevant precedent,
without prescribing Orbital's naming scheme.
[Continuous archiving and timelines](https://www.postgresql.org/docs/current/continuous-archiving.html#BACKUP-TIMELINES).

New identity alone does not fence a partitioned old primary at external sinks or
clients still using it. An isolated recovery branch can be inspected without
promoting it. Before replacing the production service, establish write authority
and routing fences that surviving old owners and effect destinations enforce.
If those cannot be established, advertise an isolated branch, not a proven
single replacement primary. A new branch must not reuse restored historical
leases as present authorization.

Replay suppresses outbound effects by default. In particular, the brief's
data-loss-tolerant processors run before event persistence and may already have
acted even when the event is absent from the recoverable database. Their effects
are not rolled back by PITR, and replay must not invoke them again. Exactly which
crash/restart cases the promised “at most once” invocation covers still needs a
durable invocation/ownership protocol; this note does not claim to solve it.

For recoverable ordinary effects, persist an outbox/effect identity and use sink
deduplication or a transactional integration where available. A sender crash
after external acceptance but before its local acknowledgement otherwise leaves
an unknown delivery outcome. Effect IDs inherited from replay keep their
original identity; simply prefixing all effects with the new branch would defeat
external deduplication. New branch work receives new identities. Decide explicitly
whether connectors resume, rewind, reconcile or remain disabled at promotion;
source offsets and external acknowledgements may lie beyond the selected cut.

## Peer retrieval when blob storage is unavailable

Offer a source-independent fetch operation for an immutable, authenticated
representation or verified chunk. Local caches, authorized consumer peers,
producer/follower logs and alternate archives can all supply bytes. A peer's
claim that an object is “latest” or “committed” supplies no authority. The expected
identity and its entitlement to appear at this cut come from recovered metadata.

The minimum useful contract is:

- **Discovery and access:** bounded peer inventories/directories with a cached
  bootstrap path outside the failed blob/control service. Authenticate the peer
  and authorize tenant/object/lineage access independently of knowing its hash.
  An outage is not permission to bypass authorization. Credential renewal, DNS,
  KMS and the directory itself need a recovery path within the promised RTO.
- **Identity and integrity:** fetch exact immutable versions, lengths and
  representation hashes. Range transfers need authenticated chunk boundaries and
  hashes or proofs rooted in the trusted object identity. Verify before exposing
  decoded content; preserve dictionary/codec identities. A digest received with
  untrusted bytes proves neither provenance nor committed membership. In
  particular, an S3 ETag is not uniformly a full-object content digest.
  [S3 object metadata](https://docs.aws.amazon.com/AmazonS3/latest/API/API_Object.html).
- **Completeness:** track the required dependency set and explicit missing
  objects/ranges. A cache hit rate is not a recovery proof; a hot workload may
  omit one cold dictionary, catalog record or part of a committed transaction.
  A partial cached object does not count as a whole durable copy. A missing read
  fails or waits explicitly; it cannot substitute an older representation whose
  logical equivalence has not been established.
- **Retention:** ordinary caches remain opportunistic. If a recovery claim counts
  a peer copy, turn that copy into an acknowledged, persistent retention
  obligation with a named horizon and failure domain. GC/eviction respects the
  active restore pin and all transitive dependencies. Releasing the last counted
  copy requires a completed replacement or expiry of the supported recovery
  obligation, not just a quiet timeout while blob storage is inaccessible.
- **Admission and resources:** reserve bounded recovery capacity and transfer
  budgets, coalesce duplicate requests, resume verified chunks and prefer useful
  independent sources. Protect witness/decision traffic from recovery fanout.
  If durable backlog exhausts capacity while blob writes fail, backpressure
  affected writes before overwriting retained evidence. Do not evacuate every
  cache or replace every consumer in reaction to the same transient error.

RAM cache bytes can assist immediate retrieval but do not satisfy a PLP/durable
retention claim. Consumers that retain recovery objects are taking on an explicit
storage role, even if the process also happens to execute reads. Different VMs
inside the same failed region do not establish regional independence.

Simultaneous failures are an evidenced scenario: Microsoft's storage incident
report lists Azure Storage, Virtual Machines and the Management Portal among the
affected services. It motivates testing recovery's provisioning and identity
dependencies, rather than assuming that fresh compute can always retrieve the
archive during a storage incident.
[Azure storage service interruption](https://azure.microsoft.com/en-us/blog/update-on-azure-storage-service-interruption/).

## What RPO and RTO can honestly mean

For a stated failure domain F, count recovery dependencies outside F before
claiming its loss is survivable. The ordinary two-VM, different-AZ persistence
rule does not imply a zero-loss region-failure guarantee. Neither do three
witnesses if payloads, staged outcomes or essential decoding/key dependencies
remain confined to the failed region.

An asynchronous remote archive has a latest **complete recoverable cut**. Its
newest object timestamp or most advanced shard is not that cut. Measure the gap
from acknowledged work to this cut in transactions/positions and, where the time
mapping permits, elapsed time. Separate deliberately excluded PITR work from
work irretrievably lost and work whose status is still unknown. Amazon S3
replication is asynchronous; bucket replication configuration alone does not
establish that all dependencies reached the other region.
[S3 replication](https://docs.aws.amazon.com/AmazonS3/latest/userguide/replication.html).

A strict no-loss acknowledgement against F requires enough recovery information
outside F at acknowledgement, including an unambiguous outcome or a protocol
that can safely recover it. Keeping it only in an opportunistic consumer cache
does not qualify. Independent regions/providers introduce latency, bandwidth,
trust and operational costs; selecting that failure policy is separate from
pretending a local acknowledgement already meets it.

RTO includes discovery/decision recovery, obtaining compute and authorized keys,
fetching the required objects, decoding/replay/verification, and serving-path
fencing. A lower bound for a restore is the maximum of unavoidable critical-path
work and bottleneck byte/work transfer divided by available service rate;
contention and sequential dependencies can make the observed time much larger.
If retained logs accumulate at rate lambda and replay drains them at rate mu,
catch-up cannot complete while mu <= lambda. These are feasibility statements,
not measured Orbital timings.

Report read-only availability at the selected cut, safe write availability and
full redundancy as separate recovery milestones. Lazy object retrieval can
improve first-service time only when dependencies are discoverable and retained;
it cannot establish completeness by waiting for users to encounter every cold
miss. The failover pool helps only if it has available capacity, bootstrap
metadata and credentials outside the correlated failure domain.

## Histories worth executing next

These are bounded candidate experiments, not a mandatory promotion framework.
Each should record preserved outcomes, missing dependencies and write outage;
compare resource cost separately from correctness.

| History | Required observation |
| --- | --- |
| Commit acknowledged; coordinator region fails between two participant installs | Both effects eventually materialize from retained outcome data, or the affected view remains unavailable; no unilateral abort. |
| Earlier position remains pending while later blind replacement commits; restart and restore two cuts | Preserve the late install's original position and older cut; unused envelope coverage does not become a tombstone or erase another transaction's blocker. |
| T@20 reads x=0 and commits y=1; recover or compact T; U proposes x at 10 after reading y@10=0 | Preserve T's read bound or a sufficient monotone floor; reject the retroactive allocation that would create a serialization cycle. |
| Witness succession receives valid chosen evidence from an older term alongside a stale owner's new proposal | Install the historical outcome in its original lineage; reject the stale new proposal without changing logical authority identities. |
| Pending transaction survives restart; spare storage exists but required metadata service waits for that transaction's unverified results | Restore the independently executable control path and preserve pending verification/outcome obligations; spare capacity alone does not establish readiness. Block only work constrained by missing authority, metadata or actual dependencies. |
| Choose independent shard checkpoints on opposite sides of a transfer | Reject an inconsistent manifest, or complete its dependency/decision closure before publication. |
| An accidental deletion or bad-code result commits and is replicated into every current copy | Restore a retained known-good earlier cut; verify application meaning separately from byte integrity. Do not silently substitute corrected execution for exact historical replay. |
| Blob reads and writes fail; surviving consumers collectively hold everything | Fetch verified pieces from peers, preserve pins, and compare first read, write resumption and full repair times. |
| Same outage, but one cold dictionary, key service, domain definition or decision is missing | Report that specific dependency and affected cuts; high cache hit rate must not turn the restore green. |
| Restart a peer or force eviction halfway through copying the only retained object | Resume from independently retained data or explicitly lose feasibility; a transient cache advertisement never counted as durable retention. |
| Recovered branch starts while an old primary and delayed commit messages return | No cross-lineage mutation; promotion fencing must also cover clients and external sinks. |
| Processor sent an external effect before PLP completion; PITR excludes its event | No replay invocation; external reconciliation reports the effect cannot be undone from database history alone. |
| Restore T with a Firecracker invocation awaiting required verification while unrelated U shares its execution epoch | T's results remain invisible until all its extension use satisfies applicable requirements, including approved exemptions; recover exact inputs/outputs/evidence, extension identity/approval context and pending blockers. U may progress and publish under its own requirements and dependencies, without an epoch-wide gate or metadata/execution capacity cycle. |
| Remote archive contains later epochs but an earlier payload gap; blob/KMS/IAM control paths share the failed region | Select only a provable cut and expose bootstrap/RTO failure rather than assuming later timestamps imply recoverability. |

The immediate design questions are the chosen cut/closure protocol, the scope of
wall-clock PITR, the authority proof for replayed outputs, the retained recovery
window and its failure domains, and the effect/connector contract on promotion.
No recovery latency, cache completeness or region-loss guarantee is measured by
this note.
