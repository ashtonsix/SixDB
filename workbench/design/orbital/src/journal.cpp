#include <orbital/journal.h>

/* Candidate implementation boundaries, not runtime bodies.

Retention / batching / admission:
  Retain complete input, decoder/code/profile and declared dependencies. Matching
  retries keep identity; conflicts fail and retired IDs stay retired.
  Concrete ready memory append uses reserved owned event storage. Stronger
  provider callbacks implement actual storage/checkpoint operations, not batching
  or voting semantics. Storage-evidence spans belong to retained event/log storage.
  reserve(MemoryLog, events, producers) prepares container and private index
  capacities at its lane before ready append. Failed reservation preserves
  retained records and identity boundaries; append checks capacity before effects.
  form_epoch is ordinary Journal code: choose eligible retained input within
  byte/work/wait budgets and freeze canonical order/value in reserved storage.
  Local ordering admits that batch directly. Witnessed ordering validates actual
  configured acceptance proof using captured routes and retained request slots.
  A local storage acknowledgment is not an ordering certificate.

  Witness owns its Promise vote state. Journal code enforces ballot/prefix rules,
  persists before qualified response and recovers the compatible accepted prefix.
  Journal freezes complete framing, identity and recovery closure as an immutable
  Source in AdmissionRequest/WitnessWrite before retention. retain_record captures that
  Source and its actual Request/backend, not the semantic observer. The provider
  retains/indexes the exact record, preserving retries, retirement boundaries and
  its configured storage evidence guarantee. record_evidence returns actual physical
  retention/storage evidence for the matching SourceId and finished Request; its spans live
  in retained Log/request storage. Journal validates that evidence and actual
  protocol facts before qualifying admission/vote response. A Destination write
  alone supplies neither log framing nor storage evidence/protocol authority.
  Accepted tail lives in Witness-owned or immutable
  Log-owned storage. Copy/retain response snapshots before later votes or request
  retirement can invalidate them; never keep a span into a retiring WitnessWrite.
  The real Log/Port backend handles storage/transport; no semantic provider table
  chooses what promise/accept means. A local Promise is not chosen history.
  Authority handoff transfers certified history before successor voting.

  AdmittedBatch can be created only by qualified direct admission or validated
  asynchronous admission; its frozen input cannot be relabelled by a caller.
  A ready provider returns unavailable before effects when it cannot complete.
  Other failures need reconciliation, not automatic fallback/repeated submission.
  Accepted async work captures its resources before returning and posts retained
  immutable evidence to the caller lane. Same-thread completion may handle
  directly. Only the caller Message handler validates operation/value/incarnation and
  completes its Signal; backend threads never mutate task/wait storage.
  Capacity waits before acceptance preserve idle state; accepted retries observe
  the same submission and terminal facts.

Private execution / prepare / publish:
  On Consumer.lane, begin checks AdmittedBatch against the completed predecessor.
  Fill private Draft.input_/predecessor_ only after all begin checks succeed;
  failure leaves fresh storage unbegun. Retain frozen input and the complete
  selected State once. Assign Draft.lane without touching that lane's resources
  or registrations; a synchronized handoff precedes work on another lane.
  Draft has no execution cursor or durable continuation facade.
  Engine/Ikea owns computation and real maintenance. A simple embedded owner
  stays local. Already qualified, conflict-safe native regions can run ordinary
  threads within a stable owner scope; join actual accesses before changing
  catalog/authority/registration, capturing material or freezing. This requires
  neither an isolated Executor nor a RetainedView when the scope outlives
  and joins every use. Owner metadata still changes only at its assigned lane.

  Object freezes private Draft on its execution lane before Journal consumes it.
  Cross-lane delivery establishes happens-before and keeps Draft/results/update
  storage immutable/alive through preparation. Validate private admitted input,
  sealed/frozen closure and exact predecessor/current Consumer basis; equality of execution
  and publication lane is not a validity condition.
  Prepare the actual next State with objects/results/cut and optional obligations,
  including every fallible capacity/dependency/install reservation. No extra
  publication token or visible object head is needed. Wait/failure preserves
  input, basis and sealed candidate. Share unchanged directory nodes directly;
  empty object directories are valid, not an unfinished freeze.
  If prepare yields, resume its checks. After final successful preflight, next
  stays unchanged and publish runs in the same non-suspending Consumer-lane visit.
  Assert the exact prepared basis again before install; exchange one complete
  State with no new allocation, I/O or failure after installation begins.
  Derived transfers may trail; late completion Messages never certify a newer epoch.
  Discard closes private edits while retaining the same input and predecessor;
  repair/retry must not acquire a newer State or erase admitted work.

Read enrollment/acquisition:
  Enroll caller-reserved Reader once with ReaderDomain. This is actual safe-
  acquisition/subscription state, not a per-point actor. Reuse it on its assigned
  caller thread. Prepare metadata before publishing registration; no fallible
  work follows publication. Failure leaves the fresh Reader unenrolled with no
  notification queued. ReaderDomain's reclamation mechanism remains private/open.
  Latest-ready read opens one native ReadContext on that Reader, lending immutable
  State without incrementing a shared State reference counter. Reader protection
  keeps State and its complete hierarchy alive until native context release.
  One native context per Reader; another acquisition conflicts until it closes or
  is retained. Moving a context transfers its protection and empties the source;
  destroying/replacing it closes native protection on the caller lane. No
  publication notification or Consumer queue trip is required on the ready path.

  retain(context) captures the same State through the protected domain record,
  then drops native protection and empties the Reader's active slot. It neither
  copies state nor allocates; the retained context can outlive the Reader or cross
  lanes. Never derive ownership by casting the readonly selected pointer.
  Caller-lane close/retain precedes Reader leave, reenrollment or storage reuse.
  Resolved cold work can instead capture material/qualification/owner metadata
  and close the context before waiting. Owner-borrowed metadata still requires
  its context until copied or no longer used. Same-lane result observation can
  retain Consumer.completed directly; that optional path is not native point
  acquisition. Head publication/restore preserves every protected old State.
  Pending exact cuts/bounds subscribe and recheck domain generation. Publication
  posts a coalesced retained Reader Message to its caller lane; it pulses changed.
  Recheck qualification on wake, not only Signal. Known unavailable history is a
  terminal error. Task detach removes only its WaitLink; leave unsubscribes the
  Reader. Already queued notifications retain Reader storage and cannot revive interest.

Full fold/checkpoint/restore:
  Reserve participants in a common order; durably fix one position at each, then
  release local reservations before compute. Engine declares scoped bounds,
  including absence/range changes. Full epochs retain logical continuations
  while independent work completes; a parked physical task is not durable state.
  Resolve every declared output, preserve future-source/fallback obligations and
  replay original bounds/profile excluding already-published own output.
  Discarding physical work never erases admitted events or batch positions.

  Checkpoint state, owner encoding/code, producer/dedup cursors and unresolved
  transactions/sources/outbox/audits with complete reconstruction closure before
  adoption. Preserve accepted suffix, promised old cuts and actual physical uses.
  Restore actual learned barrier history; metadata readability, usable bytes,
  write authority and redundancy are different readiness outcomes. Reclaim only
  after recovery closure, readers and physical uses release.
  Prefix retirement keeps identity boundaries without an unbounded result cache.
  Processor commits source progress/outbox together, preserving origin/ordinal.
  Non-deduplicating lost replies remain unknown effects. Separate early-loss
  grants are durable disjoint ranges, never reissued by replacement.
*/
