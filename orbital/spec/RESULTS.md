# Orbital verification assessment

The first attempt had an incomplete plan. It is preserved in the
[superseded spike](../../workbench/spikes/orbital-formal-first-pass/README.md).
The replacement [plan](PLAN.md) defined the six guarantees, protocol bindings,
internal service boundaries, distinguishing failures and required larger cases
before replacement implementation. Independent reviewers challenged that plan
and later the mechanisms, observers and composition evidence.

Current evidence accepts 485 of 486 selected cases: 200 complete graphs, 151
expected property violations in negative controls and 134 reached histories
(successful paths and explicit boundaries). The larger
admission-renewal graph is still running; the campaign is not yet complete.
A subsequent source review found that root-owner reset retained consumed volatile
replies and cleared reply suppression in surviving holders. This captured model
does not yet establish cold-owner recovery; the correction is still outstanding.
The [retained baseline](evidence/baseline-before-design/README.md) records the exact
pre-revision selection and its limits.

Final acceptance requires current captured inputs and the intended outcome for
every selected case. A stopped graph is sizing evidence only.

## Assessment against the destination

The table assesses the plan's observable guarantees, rather than treating a case
count as coverage. The family reports map every requirement ID to its actual
configurations, properties, controls and limits.

| Guarantee | Evidence and scope |
| --- | --- |
| One recoverable authority and interpretation | [Journal](JOURNAL.md) checks persistence, quorum ordering, closed handoff, competing successors, compaction and actual barrier recovery. [Admission](MATERIAL.md) checks complete body/decoder custody, contiguous producer frontiers, destruction intervals, discovery and repair. Execution consumes the journaled source/profile, including actual retained code. This is crash-fault authority with named storage identities; it is not Byzantine agreement. |
| Serial published observations and effects | [Execution](EXECUTION.md) checks the actual transaction kernel against an independent program evaluator: overlapping writers/readers, partial fixation, cancellation, verification, replacement and late installation. The four-transaction chain includes an unresolved no-effect writer, RMW, replacement and another reader. Retained-source and physical-view joins check actual input bytes; exact-cut unavailability yields an evidenced agreed failure. Positions respect supplied causality/freshness requirements and admitted historical cuts, not an implicit global real-time freshness order. Application declarations remain an external contract. |
| Complete logical confluence and independent work | Independently scheduled epoch consumers compare full logical state, bounds, continuations and canonical logical output sequences; physical delivery order may differ. Actual transaction folds consume the same agreed batches. Small unrestricted products check the independence reduction used for larger cases. A separate actual transaction completes while a remote fact for another is withheld forever. These checks distinguish absent agreed input from local physical slowness; they do not establish real-time scheduling performance. |
| Complete reconstruction for live obligations | [Material](MATERIAL.md), [delivery](DELIVERY.md) and journal recovery connect roots to actual base/code/patch bytes, pending source results, holder persistence and successor custody. Old copies really disappear in transfer cases. Discovery starts from empty inventories; recovery and late joins consume real exported records. Pending-cut transfer includes both result and no-effect resolution after controller recovery. Its authored transfer assumes the source transaction owner and retained holder survive. |
| Correct physical version, access and lifetime | [Runtime](RUNTIME.md) models actual symbolic page bytes, scoped writes/undo, read-only and asynchronous extents, no-COW reuse, reconstruction, callbacks and backend retirement. Checked execution and reopened writers use those same views and retained recipes. The platform must actually enforce the specified mapping and capability operations; syscall or runtime-hardening implementations are not proved here. |
| Conditional completion without a concealed resource cycle | Protocol progress configurations state surviving material, stable authority and eventual service assumptions. Counted capacity joins exercise journal/root work, faults, decisions, delivery retirement, bootstrap and SOS while ordinary resources are occupied. Controls retain unrelated activity while the target stalls. Finite-work completion is not a promise of broad-writer fairness under endless narrow arrivals, nor a throughput or disaster-recovery-time bound. |

## Evidence at the internal boundaries

No internal provider below is dismissed as an external trusted service. There is
also no single machine-checked theorem composing all of these models at arbitrary
size. The distinction between a checked abstraction and an executable joined
history matters.

| Internal boundary | Connection actually exercised |
| --- | --- |
| Journal → replicated owners | `JournalAuthority` checks the actual provider against `DurableLog.Contract`, including ordered delivery and barrier snapshots. Persistence/order families check their correspondence boundaries. C1 replaces a transaction driver and authority using real journal records; C8 consumes actual map-transfer data. The larger order abstraction overapproximates prefix schedules for safety; its safety does not establish liveness. |
| Transactions ↔ epoch consumers | `TransactionEpoch` shares the program and real transaction protocol. `EpochTransactionFolds` compares two full `TxKernel` folds and canonical output sequences. The reviewed pure-consumer independence argument and smaller product correspondence justify the larger scheduling representation; they do not cover shared physical-resource timing. |
| Transactions ↔ retention ↔ physical views | `RetainedTransactions`, `RetentionRaceProbe`, `PendingCutTransfer`, `CheckedViews` and `ReopenedWriter` carry actual cut/context records into roots, bytes, private execution, decision and physical reuse. C4/C5 and the added pending-cut histories check named crossings rather than an unrestricted product of every provider failure. |
| Admission ↔ journal ↔ recovery | Actual durable registration/frontier records, receipt evidence, generation renewal, namespace discovery and cold-holder recovery connect the providers. `AuthorityBootstrapSOS` separates material custody from authority and serving readiness. Admission representation reductions have finite enabled-step checks and a reviewed source-level argument; the larger reduced renewal graph is not an enumeration of its larger unreduced counterpart. |
| Delivery ↔ transactions/retention | C3 couples chosen source checkpoint/outbox data to receiver replay. `DeliveryRetention` turns required users and late enrollment into real root acquisition, transfer, cancellation and deletion. C6 uses restored committed intents with both deduplicating and ambiguous sinks. Transport receipt, durable custody and application completion remain different facts. |
| Epoch execution ↔ derived delivery | `DerivedDelivery` consumes independently produced interpreter outputs, actual chosen outcomes, stable logical IDs and canonical content. It does not inject pre-equal outputs. Route replacement and source/recipient replay preserve the same contribution; raw and transformed branches have independent semantic observations. |
| Capacity ↔ completion/recovery | C7 `CompletionUnderPressure`, `DeliveryPressure`, `BootstrapUnderPressure` and `SOSUnderPressure` obtain actual leases and backend registry operations before progressing. Physical use remains charged through callback retirement. The bounded controllers serialize some service stages; arbitrary recipe sizes or crash combinations are not inferred from a one-operation bundle. |

The mandatory histories C1–C9 are distributed deliberately: C1 and C8 are in
[JOURNAL](JOURNAL.md); C2, C6 and C9 in [EXECUTION](EXECUTION.md); C3 in
[DELIVERY](DELIVERY.md); C4 in material, delivery and pending-cut transfer; C5 and
C7 in [RUNTIME](RUNTIME.md). Each uses the preceding provider's actual records or
bytes. Their tables give the exact configurations. Authored service cuts remain
restrictions on the explored joined histories, even where the component families
explore wider local interleavings.

## Consequences for the design

The [architectural account](ARCHITECTURE.md) classifies the findings. The core
contention design survives this campaign: each shard releases its reservation
when it durably fixes the position; reads create retention obligations rather
than arbitrating over the entire read set. The useful common machinery is durable
owner replay, stable identity, explicit obligations and physical-operation
accounting. The evidence does not justify combining different acknowledgements
or obligations into one generic protocol.

Several implementation obligations became concrete. Recovery must regenerate
grant evidence, discovery must revisit a changed namespace, and copy resumption
must use learned storage incarnations. A transferred pending read needs its future
source obligation as well as its fallback. Reordered source notices must not undo
a complete authoritative resolution. Execution must use captured logical inputs
and journaled versions; a replay cannot consume its own installed output as its
original input. These clarify existing contracts or repair model/adaptor defects;
they do not silently amend BRIEF or PHYSICAL.

There are accepted limits rather than universal solutions. Ordered reservation
queues can transmit a WAN wait through a broad waiter. Eligible-first removes that
particular bridge but admits starvation under continuing narrow arrivals; the
small policy model witnesses both. Late source discovery can fail after history
collection. Historical reads are a checked candidate read-only entry adapter;
they consume already retained old material or fail, rather than recreate lost
history. Approved native execution trusts its program/runtime: asynchronous
audit detects a breach but cannot repair already committed divergence. The early
pre-persistence path remains optional and explicitly lossy; its range/incarnation
bookkeeping must earn its latency benefit experimentally. A sink without its own
deduplication contract can leave an external effect Unknown.

## What these results establish

A completed positive TLC check exhausts its configured reachable graph and checks
its stated invariants and conditional temporal properties. A deliberate defect
must fail the intended property. A witness establishes existence of its named
successful or boundary history, not correctness of every history. Exact source,
configuration, runner and tool identities keep those claims tied to the checked
model. Completion has TLC's usual fingerprint-collision qualification, whose
estimates remain in the raw logs; it is not a deductive proof. The evidence
collector additionally compares raw diagnostics and parsed
dependencies; legacy receipts did not authenticate earlier edits to their logs.

The external premises remain those named in PLAN: atomic durable records under
the modeled storage faults, authentic message identities, symbolic hash equality,
approved deterministic program/runtime behavior, sound application declarations,
enforced platform operations and the explicit deployment fence for PITR. Service
and material-survival assumptions belong to the specific progress checks. Broader
parameter sizes, arbitrary compositions of failures, TLAPS proofs and refinement
of future implementation code are not conclusions of this finite campaign.

Model-checking time is not a database-performance estimate. Selected receipts come
from different hosts and load conditions; their summed elapsed time is not a
measured clean full-suite wall clock. The retained [worker comparison](../../workbench/tools/tlc/evidence/20260927-workers/README.md)
tests unchanged graphs on modest machines. Larger attempted graphs that stopped
remain diagnostic comparisons, never substitutes for completed required cases.
