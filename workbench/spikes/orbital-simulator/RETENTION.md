# Retention and progress experiment

The [checked old-cut composition](demanding_case.py) now exercises durable context
registration, private checker queries, continuing writes and recovery through the
existing transaction machinery. Reclamation and completion-capacity policies below
remain proposed. This is a prototype application binding, not a new Orbital contract.

## What the first composition taught us to separate

The existing transaction read eagerly captures values and journals its request.
Using it to initialize a delayed checker would turn a deferred first read into
prepared input. Journaling every checker query would also put unchecked query
choices into agreed history. The new binding instead registers the complete read
coverage at the transaction's actual fixed position, then serves private queries
without changing agreed ordering state. Both paths share a pure observation
function, including pending-output and replacement rules.

Two checker actors persist their invocation and complete report, obtaining their
values from the source after registration. A delayed first query can follow both
intervening writes and source recovery. The coordinator requires distinct matching
reports for the same context, source/code versions and profile; its durable outcome
must be exactly the checked outcome, including after recovery. The observer
independently checks the old snapshot and evidence, not just replica agreement.

The same case uses standalone and prepared-quorum admission. Explicit actor
factories select the fold and coordinator before boot; physical placement can
separate coordinator failure from leader failure. A small [scenario helper](scenario.py)
starts external offers and incident sequences relative to actual context/report
events and records missing triggers. These changes remove post-construction actor
replacement and guessed absolute fault times from this experiment.

Some awkwardness remains consequential. The coordinator adapter interposes on
phase names and recovery records; its construction still knows the transaction
implementation's internal state. Values come from logical retained history, with
synthetic buffer allowances at the checkers, rather than object mapping, physical
reconstruction or COW. History is never reclaimed. A missing checker leaves work
pending; a known mismatch also remains pending because agreed abort is not yet
composed. Safe nonpublication does not establish successful failure handling.
Those are distinct gaps to investigate before treating the case as complete.

[The 72 retained histories](evidence/checked-composition.json) include all requested
incident triggers and account for every offered obligation. All foreground writes
finish; the missing-checker, mismatch and tight retained-buffer controls leave
eighteen checked transactions unfinished. Longer runs continue source writes both
before and after the delayed checker's first read. The passing negative controls
also require pending-predecessor checks before an outcome exists and reconstruction
of the context in the source's new incarnation. Completed report retransmission
has no retirement acknowledgement yet, so these runs do not measure idle traffic
efficiency or a sustainable retention policy.

## Observed boundaries

The [replicated composition](replicated_contention.py) can accept one finite
transaction and announce its output, then run out of consumer memory before
fixing its position. At the fixture's 8 KiB budget it remains unfinished; with
12 KiB it completes. This is not merely years of unreclaimed history accumulating.
The model admits an obligation without securing a way to finish or agree its
failure. The byte thresholds are authored model allowances, not production sizing.

The [recovery footprint experiment](recovery_footprint.py) first completes twelve
writes, then resets a host and offers another write. Whole-journal recovery cannot
reopen under a 12 KB budget that supported the healthy history. Reading one
immutable journal record at a time reconstructs the same ordering-state hash,
preserves the versions and serves the new write. At 16/20 KB both work; the whole
read reopens faster in this model. [Evidence](evidence/recovery-footprint.json)
retains all eighteen histories and all offered work. The alternative bounds the
temporary read footprint, not the accumulated retained state. It depends on a
contiguous single-writer journal and a device reset cancelling old submissions;
it does not establish recovery from process-only restart or damaged journals.

## Fixed old cut with continuing writes

Hold one checked extension at an old snapshot while repeatedly overwriting a
fixed object. One checker finishes; the other pauses before its first read.
Continue writes inside the declared source coverage and on unrelated scopes.
The extension has a narrow output and the source reads have declared coverage.

A fixed old cut need not retain every intervening version. With bounded active
work, the fixture needs values visible at the retained cut, current values,
unresolved effects and their recovery/interpretation dependencies. Once actual
checkpointing and reclamation exist, retained space should settle despite
continued overwrites. Compare retaining source versions for deferred reads with
preparing and retaining the declared input snapshot. Both must account for
physical users and reconstructible state.

## What the existing probes do not establish

The traffic binding retains shard request leases, versions, responses,
coordinator receipts and journals indefinitely. It reserves coordinator state
before accepting input, but later participant metadata and decision persistence
can still fail for lack of capacity after outputs are announced. It has no
agreed failure path or checkpoint/delete operation. The baseline recovery loads
the whole journal in one charged scan, then allocates reconstructed state while
the scan buffer remains live; the record-at-a-time alternative isolates that
temporary footprint. An ever-growing history is neither a steady-state capacity
experiment nor proof that an admitted obligation can finish.

The object fixture acknowledges an in-memory context floor without persisting
the corresponding context obligation. Reopening restores history, not that
floor. Its checker and verification coordinator do not recover pending work.
Existing extension cases establish equality-gated publication without testing
that gate across recovery.

## Minimal composition and oracle

Persist the old-cut context, declared coverage, execution profile and required
checker set through the composed protocol. Preserve unfinished decisions and
ordering metadata in the checkpoint along with the data needed by open cuts.
Reclaim only after the replacement recovery basis is durable; cancellation alone
does not release bytes still borrowed by workers or I/O. Add bounded checkpoint
and journal reads rather than requiring the full durable history in memory.

Restart the source owner or coordinator after context acknowledgement and before
the delayed checker reads. Resume verification and install through the ordinary
recoverable decision. Repeat near memory/storage capacity, with lost responses
and with an explicitly agreed cancellation. A local timeout is not an agreed
abort. Keep all offered, refused and unfinished ordinary writes in the result.

An independent oracle reconstructs each required snapshot from admitted input,
checks reported transcripts and published effects, then verifies recovery using
only the durable material actually retained. It rejects early publication, lost
read bounds, missing interpretation dependencies, conflicting decisions and
premature retirement of borrowed bytes. Measure whether ordinary service and
retained space settle, and whether every admitted obligation can finish or
reach its agreed failure once its declared failure/recovery assumptions hold.

Distinguish three failures: unnecessary intermediate history is a reclamation
problem; inability to record a decision because admitted work exhausted all
completion capacity is an admission/progress problem; a missing owner or
required verifier needs recoverable authority and agreed resolution. Garbage
collection cannot settle that third obligation. The architectural question is
whether the existing brief's obligations compose with bounded service, without
requiring a global retention barrier or a new restriction on accepted work.
