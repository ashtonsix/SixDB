# SOS: stop admission and preserve whatever can escape

2026-09-26. Proposed emergency behavior for severe health deterioration. **Pause
admission altogether and opportunistically export surviving state or fragments
along available authorized flows.** Start preservation without waiting for a
quorum, global cut, complete manifest or successor grant. This is distinct from
[authority handoff](HANDOFF.md) and [bootable recovery](PITR.md); exported evidence
may make either possible later, but export itself establishes neither.

The purpose is to preserve durable obligations, possibly chosen history and the
evidence needed to recover or explain them before further infrastructure
disappears. Waste and duplicate
copies are acceptable when they buy useful survival. SOS is not the response to
every latency anomaly: [health investigation](DETECTION.md) supplies the evidence
for this exceptional stop. No fixed trigger threshold or additional consensus
mechanism is selected here.

## Stop locally; state exactly what is known globally

A process entering SOS closes every local path that can admit new ordinary work
or create new authoritative protocol state. This covers client/producer ingress,
new event processing, witness acceptance of new journal proposals, allocation and
observation admissions, transaction decisions, and ordinary control events such
as membership or domain changes. Renaming a new transaction decision a “control
message” does not bypass the pause. New reads requiring observation registration
are also admissions; scanning retained bytes for export is not.

Already issued I/O and previously transmitted messages can complete after the
gate closes. Preserve their results and identify the local gate boundary and
known in-flight operations; do not claim that the instant of local stop is an
atomic global snapshot. An event may have been accepted or chosen elsewhere
without this process learning it. A pending decision is exported as pending or
unknown, not forced to abort so the inventory looks finished.

| Available coordination | What the stop establishes |
| --- | --- |
| One reachable process closes its admission gate | That process stops creating new admissions through the gated paths. Hidden processes and earlier in-flight accepts may still establish outcomes. |
| Several processes independently stop | Preserve each process's exact evidence and boundary. A collection of claims is not automatically a global stop or a common cut. |
| The existing authority can durably agree a pause and exclude further admission | A stronger boundary is available under that protocol's rules. Export its proof and the accepted suffix; it still does not prove that every payload or replay dependency was exported. |
| Old authority cannot establish a global pause | Every reachable process still stops locally and exports immediately. Global cessation, the latest chosen suffix and successor authority remain unresolved. |

Preparing a durable agreed pause through ordinary admission is a **pre-SOS**
option while a quorum is still usable. Once SOS begins, close the local paths
immediately and export; do not delay cessation to propose or await that pause.
Already issued messages may still yield valid pause/choice evidence, which can
be copied with its precise boundary. SOS export does not reopen the journal for
a pause record, a terminal handoff control decision, a synthetic checkpoint
epoch, or an export completion event. Any independently valid stop/revocation
evidence already produced can be copied.
A separately authorized move from SOS into handoff/recovery can invoke that
control path; ordinary admission remains stopped until its authority permits
resumption. Copying never waits for that transition.

The local stop must survive restart. Use a persistent safety latch checked before
enabling any admission path; it is local fail-closed state, not an agreed epoch
or a grant of new authority. If local persistence is failing, close the gate
immediately, report that stop persistence is unconfirmed, and preserve a
fail-closed startup restriction through the surviving supervisor/bootstrap path
where possible. A process that cannot establish such a restriction must not
claim its future restart is fenced. Export does not wait for this repair.

SOS exit is a separate authorized action that reconciles the stop with current
authority and the required recovery frontier. A healthy ping, elapsed timeout,
successful export receipt, process restart or “cancel evacuation” message must
not silently clear the latch. A local SOS latch does not select the next leader
or configuration and does not replace an existing authority revocation.

## Keep copying after admission has stopped

Reading retained storage/memory, enumerating evidence, checking hashes,
transmitting copies and recording local export receipts remain permitted.
Receiving and durably retaining copies is also permitted. These activities do
not allocate a serial position, admit an event, decide a transaction, publish an
application result or advance consensus authority. A receiver's storage receipt
acknowledges a copy, not commitment of the object it contains.

Existing admitted computations need not finish before export. They may remain
pending; any continued local calculation does not license a new decision or
admission. Receiving valid evidence of an earlier choice is also different from
making a new choice. Preserve that evidence without reopening the acceptance
path, and give preservation the resources it needs.

The exporter needs a small runnable path with reserved CPU, memory, storage I/O
and network capacity. It must not invoke a blocked application program, wait for
its transaction's results to become visible, obtain a new ordinary control event,
or finish handoff before sending bytes. A minimal metadata reader and chunk copier should
work even when ordinary consumers cannot decode or execute the state. Preserve
raw evidence when higher-level interpretation is unavailable, marking its
format, source and verification limits.

Use whatever pre-authorized destination and network path survives: failover-pool
storage, consumer peers, another region or an independent archive. “Broadcast”
describes broad opportunistic dissemination; it does not authorize plaintext
publication or untrusted recipients. Prefer authenticated destinations outside
the threatened failure set, but a useful copy reachable now need not wait for
the ideal destination. Record its actual domain and durability rather than
counting every copy as independent protection.

Start with available fragments and an incremental inventory. Send later additions,
corrections and discovered gaps under their own identities. Neither sender nor
receiver waits for an all-shard manifest, a finished checkpoint, a complete
dependency walk or agreement that the region has stopped. An inventory can
explicitly say that enumeration or a log scan was interrupted.

## What to export

Aim for the complete recoverable state, but send any independently useful part
while the opportunity exists. Preserve the distinction between raw local claims,
durable accepts, chosen evidence, verified outputs and physical copies.

| Evidence | Required distinctions |
| --- | --- |
| Exact journal and accepted suffix | Include record bytes, slot/epoch identities, terms/ballots, payload references, accept state and local durability evidence. Export beyond the last locally known commit frontier: a later accept may already have been chosen without a delivered notification. |
| Authority and revocation records | Include configurations, transitions, promises/votes, ownership/incarnations, locally issued stop/revocation records and any already-established grants. Distinguish their original scope and validity; don't infer a successor from an export. |
| Producer state and unadmitted durable tails | Include stable stream/event IDs, registration, contiguous/gapped LSN ranges, payloads, holder receipts and accepted-submission obligations. Persisted work awaiting admission is not discarded or promoted to admitted input. |
| Ordering and transaction state | Include allocation queues, observation bounds and sufficient monotone position floors, including those left by completed observers; provisional/exact positions, effect/domain definitions and mappings, announcements, pending outcomes, staged results, continuations and installation state. |
| Objects and reconstruction dependencies | Include checkpoint roots, object/chunk bytes, base/delta ancestry, dictionaries, codecs, runtime/extension artifacts and captured inputs or accepted output evidence. A fragment can escape before its dictionary or proof does. |
| Key access and identity metadata | Include encryption/key-version identifiers, access references and required trust/identity metadata. Do not export plaintext keys or broaden access. Existing authorized encrypted envelopes may be retained as artifacts under their original access rules. |
| Client and external-effect evidence | Include stable request/result identities, known acknowledgements, deduplication/outbox state, processor invocation evidence and connector offsets. Exporting them does not reissue an effect or decide an unknown delivery outcome. |
| Inventory and holder receipts | Include source lineage/process incarnation, artifact identities, lengths/digests, chunk offsets, dependency references, holders, retention promises and known missing/unreadable ranges. Qualify whether a receipt proves receipt in memory, durable storage, or an ongoing retention obligation. |

Do not spend the last network window constructing a single ideal export order.
Small irreplaceable metadata, decisions, authority records and dependency roots
often deserve an early lane; bulk payload transfer can run concurrently wherever
resources allow. A missing dictionary or the only remaining payload copy can be
more valuable than another metadata duplicate. Send known unique endangered
fragments early and use holder receipts to reduce avoidable duplication without
waiting for a globally current directory.

Copying must not destroy the source. Suspend relevant reclamation/eviction or pin
the material being exported. A receipt for one fragment does not release the
source's entire retention obligation. Maintain bounded buffers and prioritize
evidence rather than overwrite uncopied bytes to complete a larger batch. A
local export pin protects only the surviving local copy; it cannot guarantee
survival of the machine or prove remote durability.

## Receiver behavior and later reconciliation

Receivers authenticate the source and authorize the transfer. Bind chunks to
their source lineage, process/storage incarnation, artifact identity, range and
digest, and retain original signed/authenticated authority evidence where present.
Verify byte integrity as far as the available metadata permits. A source's new
digest of raw bytes supports exact copying, not proof that those bytes were an
authoritative object. Preserve that distinction in the inventory.

Deduplicate identical fragments idempotently and retain conflicting evidence
with its provenance. Conflicting values for a journal slot, different reported
durability states, or disagreement about a decision are reconciliation inputs.
Do not choose the newest wall-clock report, the largest term, the highest scalar
frontier or whichever copy arrived last. A newer term can contain relevant
promises without making every attached value chosen; valid older chosen evidence
can still govern installation in the original lineage.

Track holes, unavailable dependencies and interrupted enumeration separately.
“Not received” or “not found by this scan” is not “destroyed” and is not proof an
event never existed. A receipt for epoch 100 does not fill a payload gap at 90.
Receiving all items in an incomplete inventory is not completion of recovery.
Receiver-generated inventories and receipts must themselves be copyable without
an ordinary source-shard admission or a complete global manifest.

Later, the owning recovery protocol can combine fragments and establish a
consistent cut, valid authority and the restart closure in [PITR](PITR.md). It
may establish some outcomes while leaving others unknown, recover an older cut,
or show why a requested target is impossible. Until then describe the result as
preserved evidence, not a writable recovered database. SOS does not weaken the
ordinary requirements for consensus, publication, external-effect fencing or
operator-selected exclusion of source history.

## A useful escape can be very small

A witness knows commit through epoch 56 but has durably accepted epoch 57. The
network can move only a few kilobytes before its region becomes unreachable.
It closes local admission and exports the exact accepted record, ballot and
configuration, its stop status, referenced payload identities, known holder
receipts and an inventory marked incomplete. It need not obtain a second witness
or copy the referenced payload before sending this package.

That fragment cannot establish that 57 was chosen, that every old witness stopped,
or that the database can boot. It can nevertheless prevent later recovery from
treating 56 as a proven final suffix, identify the holders to search, and combine
with another survivor's evidence to recover the choice. If enough transport also
exists for payload chunks, send them without waiting for that interpretation.

## Bounded histories to exercise

| History | Required observation |
| --- | --- |
| One witness enters SOS while the other two remain hidden and may choose another epoch | Local export starts immediately; neither the pause claim nor last-known frontier is advertised as global cessation. |
| A quorum establishes a durable pause, then blob storage and handoff control fail | Read/copy/dissemination continues without another ordinary epoch or successor grant. |
| Local latch persistence stalls and in-flight accepts complete after gate closure | Admissions stop immediately; evidence reports the late completions and unconfirmed persistent stop; no invented atomic boundary. |
| Only accepted-suffix metadata escapes | Preserve its usefulness and unresolved status; do not classify the package as bootable recovery or discard it for being incomplete. |
| Two receivers get overlapping chunks, conflicting slot records and incomplete inventories | Verify/deduplicate exact copies, retain conflict provenance and holes, and defer authority interpretation to recovery. |
| Source restarts or gets a healthy response after export | Admission remains stopped until the separate authorized exit establishes the applicable authority and recovery conditions. |
| A pending transaction cannot decide and an extension cannot finish verification | Export pending state and raw/verified evidence as distinguished facts; no forced outcome, verification bypass or application-execution dependency in the exporter. |

These are proposed behaviors and authored histories. No exporter implementation,
failure detector threshold, timing bound, global-pause proof or new consensus
protocol is supplied by this note.
