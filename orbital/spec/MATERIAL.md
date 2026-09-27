# Admission, retained material and recovery

These models implement the A and R obligations in [PLAN](PLAN.md). The selected
configurations are in `root-cases.json`; [JOURNAL](JOURNAL.md) covers actual journal
recovery, bootstrap discovery and emergency services, and [DELIVERY](DELIVERY.md)
covers delivery-driven retention. The final evidence index, rather than a scenario
name or this document, determines which exact source/configuration completed.

## Admission and its abstraction

`AdmissionKernel` separates the payload-producing endpoint from the admission
service and registered storage holders. `ProducerReset` loses the former; it is
not a crash of the journal/receipt consumer with all of that consumer's state
miraculously preserved. The journal provider owns durable registration, protection
generation and admitted-frontier records. A full `DurableLog` instance exercises
the actual submission, chosen-prefix and delivered-command interface.

A holder persists immutable body and interpretation separately, then persists
its protection-generation metadata. It can issue a receipt only for its complete
local package. The receipt names the actual holder, storage incarnation, key and
generation. Certification consumes received receipts from independent holders;
it never queries an observer's current survivor count. The independent checks
inspect physical bytes, receipt provenance and protection through the declared
number of domain destructions since the generation began. Destruction while
copying counts. Historical receipts do not restart that interval.

Generation renewal is an atomic durable-metadata primitive in this model. It does
not claim a second asynchronous storage runtime: an implementation must realize
it with the same ordered persistence, storage-incarnation and retirement rules
checked by the journal and physical-registry families. Body/decoder submission and
completion remain separate in the fine physical family. The coarse atomic family
chooses one durable new-package publication containing body, decoder and generation;
it does not claim to enumerate the fine family's generation-zero discovery interval.
Existing complete copies still renew through a separate metadata transition.
Certificate issuance/receipt, generation start, destruction and frontier delivery
remain distinct. Senders forward the latest generation they have actually learned;
old packets and receipts already in flight remain deliverable. This is an explicit
sender policy, not access to the authority's current generation.

Two recovery mechanisms are explicit. The holder's directory scan reports real
complete packages, including a package whose protection metadata is not ready.
A later local package/incarnation/generation change leaves a rescan obligation;
an earlier empty scan is not permanent absence evidence. After loss of the
payload-producing endpoint, new certification requires actually discovering the
key. Separately, a recovered storage endpoint asks its registered peers to resume
copies. A source learns the destination incarnation from that message. It does
not read another host's true storage counter, and losing a destination's volatile
inbox does not permanently suppress the immutable resend.

`AdmissionCold` covers a separate combined process/media loss: the cold holder
really loses authorization and peer knowledge. Its recovery request obtains an
actual barrier-correlated journal snapshot, and committed registration/protection
records reconstitute authorization messages. Reaching completion requires a new
receipt for the holder's current storage incarnation. An old valid certificate is
insufficient. Omitting authorization recovery stalls despite surviving payloads;
the solution reuses journal recovery rather than adding another agreement protocol.
The logical admission projection excludes journal-service barrier IDs while the
actual consumer still advances its real cursor and records their delivery.

`CertificateInterface` is the abstract machine that `AdmissionFrontiers` actually
explores. The concrete wrapper checks its projection initially and after every
transition. It retains registration, generation-begin/loss history, certificate
issuance and receipt, submitted/consumed command identities, promised keys and
frontiers. Body writes, network transit and journal choice may stutter; command
submission and logical delivery do not disappear. The already-established journal
service separately constrains actual chosen order. Abstract delivery allows more
pending-command orders, so it is a safety overapproximation.

The frontier consumer reads only a certificate's key and generation. The mapping
therefore erases its holder/storage proof **after** checking concrete evidence at
emission. Receipt evidence binds `holder` to the sending holder as well as its
actual complete local package; a relabelled sender fails the check. The abstract
machine is not merely another implementation refining a permissive common model.
Concrete projected steps belong to the exact transition relation explored by the
abstract consumer.

Abstract progress explicitly assumes available immutable inputs and eventual
certificate service. Safety refinement does not transfer that availability premise
through destruction. Concrete fair-service configurations separately check physical
completion/recovery, including producer disappearance. Unrestricted two-key cases
are distinct from `AdmissionHistories`: the latter deliberately schedules named
hole, repair, disappearance and lost-copy cuts while executing the real kernel.
They are seam witnesses, not arbitrary product proofs. `AdmissionEvidence` also
reaches a certificate whose two historic copies never coexisted, and complete loss
after exceeding the generation's allowance. Those are deliberate boundary witnesses,
not reported protection failures. The histories' independent heartbeat
continues in the missing-resumption control, so unrelated activity cannot stand in
for target completion.

The shared-loss family uses `AdmissionCompact`, which changes only permanently
obsolete send-suppression bookkeeping. `AdmissionQuotient` retains the current
authorization, source/destination storage identities, local copy receipt identity
and producer submission identity. Those identities advance monotonically; a reset
of a copy's generation also changes its storage incarnation. Discarded flags can
therefore never suppress a future eligible send. In-flight envelopes, their exact
multiplicity, old receipts and complete certificate proofs are unchanged.

`AdmissionCorrespondence` checks equality of normalized enabled transitions,
including their tags, emissions and next states, with transitions taken from the
normalized input. It also checks each actual queued receive and journal application.
This is an enabled-step correspondence, rather than two implementations satisfying
a permissive common service. Fine complete boundaries cover storage loss, generation
renewal and multiple keys; a mutant discarding a still-current flag enables a new
send and fails the correspondence. Independent review checked every read of the
suppression fields and the monotonicity argument.

The checked correspondence bounds are one key/one generation/one loss with split
persistence, one key/two generations/no loss with atomic publication, and two
keys/one generation/no loss. The observer-bridge probe uses one key/one generation
without loss, including invalid histories. Applying the reductions to the larger
two-generation/two-loss graph relies on the reviewed source-level argument; TLC
has not enumerated that larger unreduced graph. Retained envelope multiplicity
means distinct immutable protocol envelopes, not arbitrary duplicate wire packets.

The observations, generation/loss intervals and completion predicate do not inspect
those fields. Each real send retains its new live flag in the normalized post-state;
other meaningful transitions change retained state, and input removes the actual
queued envelope. The reduction does not hide service actions as stuttering or create
a new resend cycle, which supports the same finite action-family fairness. This
argument must be revisited if future reset semantics regress knowledge without
changing identity. The atomic-publication policy described above is a separate
choice; this correspondence does not retrospectively equate it to split persistence.

`AdmissionCapabilities` adds a second representation reduction for the renewal
family. It validates each original certificate proof against actual received and
issued evidence before retaining only the certificate's immutable key/generation.
The observer latches any invalid emission; no protocol transition may consult that
observer. Actual receipts, storage, losses and messages other than the certificate
body remain unchanged. The same enabled-step correspondence checks this reduction.
An independent probe checks that the old combined refinement/stored-proof observer
and the new refinement/sticky-emission observer agree even on invalid one-copy
histories; its deliberate bypass fails. Passing the invalid-history agreement case
means both observers reject, not that one-copy protection is valid. Unique issuance
per key/generation currently makes message normalization injective; reissue semantics
would require revisiting this argument. Fairness is unchanged because retained
certificate identity still changes on issuance and actual input envelopes retire.

`AdmissionBypass` holds a real L-stream hole open while actual transaction execution
finishes. Updating and checked read-only cases use the transaction kernel and serial
oracle. The missing L input is not a reservation or eligibility condition for those
independently submitted C transactions. A deliberately shared admission gate fails
the target progress property despite continuing unrelated service.

## The deliberately early channel

`EarlyDispatch` journals disjoint immutable event ranges assigned to a named driver
incarnation. Claims and grants are local volatile state. A crash really discards
them; the replacement obtains an actual journal barrier snapshot. Knowing an old
allocation does not authorize the replacement to use a grant naming the old driver.
Already-issued attempts can arrive after that driver's death, and a separately
surviving old driver can continue its own grant. The observer counts application
by original processor/event, not by the new driver or range label.

Completion counts authoritative allocations owed by each live driver, including
fresh grants that have not arrived yet. It cannot finish merely because the driver
has not learned of its work. Controls reassign an old event to a fresh range or let
a new incarnation use the old grant; the latter must cause an actual duplicate
application. The effect-before-replacement control names a fault cut after the
first real application, avoiding an unrelated missing-event dead end masking that
safety violation. Progress is through a requested one-time replacement of a finite
workload, not a theorem about arbitrary failures or uninterrupted workloads.

This capability accepts missed early effects. Durable allocation is amortized over
a range; no per-event synchronous persistence is added to its pre-persistence path.
Its value is avoiding that latency for explicitly lossy processing. Its costs are
range management, retiring ambiguous unused work, and strict incarnation discipline.
The normal committed outbox remains a separate capability with a different promise.
TLC establishes neither the size of the latency saving nor whether a particular
workload should select the early channel.

## Root ownership and actual bytes

`RecoveryKernel` journals Begin before asking holders to reserve a recipe. Holders
persist their reservations before replying; the owner registers a live root only
after actual replies. Begin/Register/Abort/Release folds are idempotent and terminal
outcomes reject delayed requests. Owner recovery consumes the actual prefix of its
matching barrier query, not an arbitrary cached snapshot.

Holder metadata, imports and deletions use the shared `ViewsKernel` backend
registry. Physical completion changes the actual ledger/copies in that same step;
callback retirement occurs later. Process reset clears loaded metadata, closes
admission to the registry, drains submitted work, then reloads durable holds before
serving or collecting. A logical root's terminal state is not completion until
holder tombstones are durable and backend debt is empty. Immutable physical-copy
IDs identify deletion targets independently of logical recipes.

The material fixture contains actual base bytes, an interpreter token and an
ordered patch suffix. `MaterialCore` reconstructs the bytes; no input says simply
"the recipe is available." The fixture's array/patch meaning belongs to the
application, while Orbital carries opaque tokens, dependencies and context.
`RecipeDependencies` separately rejects a cyclic declaration with no physical
foundation while allowing a grounded dependency chain to construct real bytes.

`MaterialTransfer` starts with two independent root owners and an empty destination.
It journals the destination Begin before importing actual source-held snapshot and
tail bytes. A lost hold/registration-custody reply, process death while an import
is active, real backend completion/drain and source GC all occur through the shared
kernels. A conversion variant builds a flat snapshot by reconstructing the held
source recipe; this changes representation while preserving the cut and bytes.
Only a received same-cut/context successor custody receipt permits old release.
The destination stays live at the end: this checks one transfer, not arbitrary
chains of transfers or subsequent destination retirement.

`PendingCutTransfer` extends this boundary to a reader waiting for a source that
has not resolved. The successor imports both real fallback bytes and the chosen
pending-source obligation before the old holder deletes its copies. After a
successor-controller reset, actual journal replay, grant and material replies
restore the same read. The source can then return a qualifying value or no effects;
a newer physical head cannot replace the requested cut. Crossings exercise a
result before import, after retirement, or sent to the old address. The six
healthy cases, four defect controls and four witnesses are detailed in
[JOURNAL](JOURNAL.md#pending-source-obligations-through-retention-owner-movement).
This is one authored transfer with a surviving transaction owner and material
holder, not an unrestricted product of failures in all providers.

`RetainedTransactions` attaches source announcements and root creation to the same
ordered local transaction fold; they are not reorderable network messages. Capturing
a cut protects its existing fallback/decoder and unresolved earlier contributions
before waiting. Result persistence and returned material remain asynchronous. A late
source returns the exact cut or produces real unavailable evidence followed by an
agreed failure. It never substitutes a newer value. The independent transaction
oracle checks actual reads, outcomes and failure provenance, including a minimal
writer/dynamic-reader case where history really is collected before discovery.

`RetentionRaceProbe` adds two named causal histories through those same kernels.
In the four-transaction history, one root captures the fallback while its earlier
writer is unresolved. A later replacement becomes the physical head and serves
another reader; the earlier writer then commits no effects. The original waiting
root must still return its captured fallback. Its late RMW installation must not
erase the newer replacement. The second history keeps a read whose invalidation
scope covers two keys blocked while an earlier contribution on the other key is
unresolved. Replacing the requested key alone cannot satisfy that obligation.

Milestone observations name the same root throughout, and successful-path checks
require the whole sequence. Controls drop pending coverage, substitute the latest
version, overwrite by physical arrival order, or narrow the provider's dependency
scope. The driver authors logical stages and delivers local facts/attachments in
the emitting step; persistence, collection and material replies still interleave.
These are precise composition witnesses, not a reduction of the unrestricted
four-transaction scheduling product. The smaller pair, checked read-only and late
source families retain their unrestricted scheduling. Exploratory larger products
that timed out supplied sizing information only; a fixture writing `-1` was not
treated as a no-effect outcome.

The root kernel keeps individual terminal records. `TerminalFloor` checks a compact
alternative against an independent uncompressed holder fold: only a contiguous
terminal prefix may become a floor. An unresolved hole cannot be skipped, and
recovery loads the durable floor before accepting delayed holds. It checks this
metadata substitution; it is not an implementation of a distributed garbage collector
or a proof that every future namespace can use one global counter.

## Requirement map and limits

| Requirements | Models and independent observations |
| --- | --- |
| A1 | `Admission-two-keys`, `Admission-independent-keys`, `Frontiers-small`, `Frontiers-growth`, `Frontiers-three-lsns`: `Contiguous`, `RefinesCertificateService` / `InterfaceRefinement`, `Completes`; `Frontiers-hole` and the three `Frontiers-reach-*` cases distinguish holes and independent progress. |
| A2 | `Admission-physical` and `Admission-journal`: `CertificateEvidence`, `ExactCopies`, physical-emission correspondence; `Admission-bad-one-copy`, `-decoder`, `-receipt-alias` test the evidence boundary. |
| A3 | `Admission-domain-loss`, `Admission-shared-loss-compact`, `Admission-renewal-capability`: `Protection` and `Completes`. `Admission-history-repair` / `-reach`, stale-renewal and the two `Admission-reach-*` boundary witnesses distinguish repair from historical evidence. `Admission-correspondence`, `-renewal`, `-keys`, `-live-send`, their three capability counterparts and `Admission-capability-one-copy` check both representation changes. The three `Admission-observer-probe-*` cases check the observer bridge and its bypass control. |
| A4 | `Admission-discovery-reset`: actual namespace discovery/rescan and `Completes`; `Admission-history-tail` / `-reach` and `Admission-no-resumption` expose discovery and retry. `Bypass-single`, `Bypass-readonly-check`, `Bypass-global-order`, `Bypass-reach` check `Serial`, `SourceContiguous`, `Completes` and `SourceIndependent` across a persistent L hole. |
| A5 | `Early-full`, `Early-overlap`: `AtMostOnce`, `DisjointRanges`, `Completes`; the three `Early-bad-*` and three `Early-reach-*` cases cover volatile loss, a fresh reassignment, delayed attempts and actual recovered grant ownership. |
| R1 | `Recovery-pilot`, `Recovery-two-holders`: `LiveRetained`, `ExactBytes`; `Recipes-chain`, `Recipes-cycle`, `Recipes-cyclic-declaration` and their witnesses check grounded bytes and decoder dependencies. `Transfer-conversion` checks a changed representation. `PendingCutTransfer` carries fallback plus future-result responsibility through owner movement and replay. Actual checker/head/read roots are joined in C5, listed in [RUNTIME](RUNTIME.md). |
| R2 | `Recovery-reset`, `Recovery-crossed-abort`, `Recovery-gc` and three `Recovery-bad-*` cases: `HeldExists`, `LiveRetained`, `NoResurrection`, `Completes`. `Recovery-reach-*` witnesses grant, recovery and retirement. `TerminalFloor-holes` / `-skip-unresolved` / `-forget-floor` and both witnesses check `RefinesUncompressed` and durable terminal-floor recovery. |
| R3, R6 | Actual checkpoint bytes, replay cursors, fenced normal/PITR reconstruction, external effects and restored-material milestones have their exact configs in [EXECUTION](EXECUTION.md). |
| R4 / C4 | `Transfer-crossed`, `Transfer-conversion`, `Transfer-hold-reply`: `HeldExists`, `LiveRetained`, `ExactBytes`, `TransferBeforeRelease`, `Completes`; `Transfer-early-release` and the three `Transfer-reach-*` cases test real source retirement, lost receipts and late physical writes. `PendingCutTransfer` additionally checks `CustodyControl`, `NoPrematureRead`, `SourceTerminal` and `Completes` across actual pending-tail transfer; its exact configurations are in [JOURNAL](JOURNAL.md). Delivery-driven retention configs are in [DELIVERY](DELIVERY.md). |
| R5 | `Admission-domain-loss`, `Admission-renewal-capability` and `Admission-history-resumption` destroy and restore actual packages independently of witness state. `Admission-cold-holder`, `Admission-cold-reach` and `Admission-cold-no-authority` check actual authorization recovery and new-incarnation copying. Bootstrap authority, scope readiness and redundancy cases are in [JOURNAL](JOURNAL.md). |
| R7–R8 | Exact SOS, empty-inventory/late-root discovery and counted emergency-service configs are in [JOURNAL](JOURNAL.md); `Admission-discovery-reset` supplies the source-tail case. |
| T12 | `Retained-pair`, `Retained-readonly-check`, `Retained-late-source`: `ExactMaterial`, `RetainedCoverage`, independent serial/publication/failure checks and `Completes`. Both `Retained-bad-*` controls and five `Retained-reach-*` cases distinguish exact bytes, pending-cut completion and published agreed unavailability. `Retention-race-chain`, `Retention-race-invalidation` and both `*-witness` cases check `CausalCompletion`, `ReplacementSurvives`, `CrossKeyBlocked` and actual completion; `Retention-race-drop-coverage`, `-latest-substitution`, `-last-arrival` and `-key-only` detect the corresponding defects. |

The physical loss model is whole-store destruction of bounded immutable packages,
not torn media, malicious storage or Byzantine receipts. The directory is a bounded
registered namespace, not a verified implementation of an arbitrary blob-store
listing API. Root acquisition families use one or two declared holders; owner and
holder restarts, old requests and pending physical operations have explicit finite
bounds. Fair progress requires surviving material, usable storage/message service
and adequate resources; the pool compositions test the modeled resource path rather
than silently deriving it from these unbounded-service leaf models.

Independent admission reviews challenged the original scan, receipt identity,
consumer-abstraction and volatile-claim assumptions; their concrete objections led
to the executable corrections above. The journal and runtime peers reviewed actual
R transfer, snapshot correlation, terminal floors and backend lifetime. Authored
history restrictions and atomic durable-record boundaries remain visible; neither
those reviews nor a large green case count establishes an unrestricted product or
arbitrary-size proof.
