#include "orbital/object.h"

namespace orbital::object {
/* Selection / try_view
   Borrow Object/Representation/Part from the protected ReadContext, or select
   private candidates from Draft. Validate membership, cut/position and rights.
   Keep selected state while borrowed hierarchy/owner metadata is still used;
   hierarchy steps do not retain again. Resolved cold work can drop that context
   after capturing material, cut/geometry/authority and needed owner metadata.
   Immutable reads use the caller's buffer cache, without a Consumer hop.
   Catalog/qualification/staging state runs at its owner lane; Draft.lane does
   not require all native computation to run on that thread.

   acquire / Lock retirement
   One LockSet can serve several execution lanes. Its private owned LockIndex
   synchronizes conflict checks, membership, revocation and final removal; the
   index/synchronization algorithm remains open. Membership borrows live Lock
   records; each Lock retains the Set, without a reciprocal retention cycle.
   Copy scope/qualification into reserved Lock storage before publishing it.
   Conflict is returned to the owner; no foreign-lane Signal or actor gate is
   exposed. Keep granted authority stable through actual native accesses; after
   joining them, close/remove under Set synchronization before Lock storage dies.
   No shared metadata lock belongs in the qualified native loop. Immutable
   reads can omit Lock when their protected qualification is sufficient.

   Resolve a compatible Buffer by object/replica/part/material. Qualify exact
   footprint, initialization, padding and alignment once, then register the actual
   View before exposing its base/extent. Registration keeps exact backing alive.
   Owned Allocation backing has Pool.lane == Buffer.lane on the direct path.
   View borrows that Allocation while Buffer registration prevents eviction,
   replacement or rebind. Source backing borrows the actual built-in MemoryResource
   Hold and selected extent. Neither branch needs another physical capture.
   Neither branch can replace backing before registered native uses finish.
   Already qualified joined native work shares that scope. View is
   the protected native memory, not a ticket for another guard/window pair.
   try_view uses a closed View; false/error opens nothing and queues/allocates
   nothing. Native code uses ordinary spans/pointers within the qualified region.
   An unchanged Part can reuse its buffer across unrelated published epochs.

   BufferCache / Draft buffers / evict
   The read cache owns a private index; reserve metadata outside ready access.
   resident borrows its dense Buffer array for owner-local inspection.
   Draft owns a separate private index, so copied/mutated draft backing can never
   answer an immutable Material-key lookup. Private qualification names that Draft
   and its authority; no second cache parameter or dirty/disposable flag is needed.
   No private buffer enters the read cache without immutable material identity.
   evict accepts only a read cache member with valid reconstruction and no
   registered native users. Drop backing, clear prepared coverage and advance
   placement generation without changing material. Pending ViewRequest metadata
   can survive; actual Requests keep old captured backing and late results cannot
   populate a replacement. A released Source alias may remain retained elsewhere.
   With no pending preparations, remove the index entry to return metadata capacity;
   otherwise keep it indexed through preparation/native handoff. Removal invalidates
   resident's borrowed span: select one victim, evict, then inspect again. Independent
   retained metadata can outlive removal but does not regain a native-use registration.
   Loom chooses victims. Released backing can remain charged reusable inventory;
   Arena refunds only confirmed discard or retirement after physical users end.

   begin(ViewRequest) / advance
   Cold begin qualifies selection once and captures exact material, selected cut/
   transaction position, logical size, footprint, Buffer identity and authority
   generation before returning. Error leaves preparation closed. Source/Buffer/
   Lock references protect pending identity; no borrowed hierarchy pointer crosses
   a wait. A resolved read can close its ReadContext after copying any additional
   owner interpretation/selection metadata needed later. A larger query retains
   its context for further lookups or borrowed metadata; writes keep private
   Draft and authority alive. Borrowed footprints outlive preparation/native use.
   Reserve request/range/result capacity before unreleasable pins.
   Registration protects Buffer's immutable material/identity, not its backing.
   It alone acquires no physical target. Cache eviction retains pending indexed
   identity while evicting/replacing backing when actual users permit. Replacement clears
   prepared coverage and advances placement generation; material identity stays
   unchanged. Metadata final retirement still waits for references/registration.

   advance invokes the material's resolved owner populate function for missing
   useful ranges. Exact compatible immutable memory can be adopted through
   bind_native, without a Request, new target or byte copy. Other material uses
   its captured services and owner reconstruction; no generic factory is needed.
   When useful work needs a target, claim its real memory/footprint
   and snapshot that placement generation before submission can physically access
   it. Check the owner's actual transfer write footprint, including preserving
   stores and padding, against registered Views and other pending population.
   Delay or split conflicting writes before submission; a Hold and matching
   generation protect placement, not concurrent access to the same bytes.
   Requests own actual target Holds; preaccept pressure can still retain those
   captured allocations. This is not a claim that a provider capacity wait is free.
   Independent requests on that target can start together. Interpret exact completed
   coverage against the captured target, generation and authority, then register
   the actual View before dropping preparation registration. A late completion Message
   never fills a replacement buffer; partial failure grants no unproven bytes.
   If placement changed, retire/cancel old interest without releasing actual uses
   early. Retry the same immutable material/cut against current backing after
   old request slots retire or independently reserved slots become available.
   Placement retry never substitutes a newer logical revision.
   Closing acquisition precedes testing exclusive copy/COW/take eligibility.
   Failure preserves captured identity until cancellation. Native registration
   keeps exact backing through release: owned Allocation or retained Source.
   Detach task wait interest before cancelling preparation. Accepted Requests
   retain their own source/target through actual completion and completion-message handling.

   bind_native
   Owner code first proves exact native interpretation/encoding, not merely that
   bytes are in memory. borrow_memory validates the actual memory backend/extent.
   Source extent begins at Part byte zero; validate logical size, initialized
   requested coverage, base/typed-offset alignment and readable padding. Reject
   writable footprints; immutable aliasing cannot escalate authority. Change
   backing only when no native use would be invalidated, then retain Source and
   advance placement generation/coverage coherently. The existing Source Hold
   already protects its addresses/padding, even if its Pool lives elsewhere.
   Fragmented/transformed/incompatible material still reconstructs into owned
   backing. Nothing requires all Sources to be native or all Parts to be copied.

   retain_view / release
   Only escape needs a pooled RetainedView. Copy exact read/write ranges, retain
   Buffer/Lock, snapshot authority generation, and register
   RetainedView.view before releasing the native source. Failure leaves the source
   unchanged and retained View closed. The retained View has no dependency on the
   former task/stack. Actual use can run elsewhere within its captured rights;
   lane state registration/retirement still runs on the Buffer's assigned thread.
   The retained Buffer and registration preserve either backing without a
   per-View Arena Ref. Actual Port Requests and MemoryResource capture independent
   strong Holds because their lifetimes need not retain the same Buffer.
   View is nonmovable while intrusive registration names it. Its destructor
   closes an unreleased view; explicit release supplies an access hint and makes
   later destruction empty. Buffer/registration determines whether View is open.
   Release unregisters the View; final registration removal may retire its
   Buffer/backing. No separate allocation claim needs release.
   RetainedView's retirement callback keeps the retained View alive until its lane has
   finished unregistering, even if the final Ref died on another thread.
   It cannot run the View destructor on a foreign thread. RetainedView retires
   its View before Buffer/Lock/record storage after final physical use.
   A backend cannot retire it early.

   Native changes / capture_native / stage
   Physical footprints include readable padding and preserving stores; disjoint
   logical bits can still share a native word. Already qualified, conflict-safe
   native regions can run ordinary threads within a stable owner scope. Keep
   their View, footprint and authority alive and unchanged; join actual accesses
   before owner mutation, registration/authority changes, capture or freeze.
   RetainedView is only for escaping lifetime; an Executor supplies isolation,
   not permission to parallelize ordinary native work.
   The owner closes all mutable aliases; capture_native validates that
   frozen coverage under a supplied SourceId. Owned backing retains its real Hold
   in the supplied MemoryResource. Source backing retains/slices its existing
   immutable Resource, adding Source.extent.offset to the captured range; it
   needs neither a new foreign Pool claim nor the supplied memory record.
   It allocates no record/backing and copies no body. If direct capture
   is ineligible, the owner explicitly copies into independent backing and calls
   port::retain_memory. A Hold alone cannot freeze bytes, nor may a read-only
   handle freeze another owner's mutable data. Failure leaves View unchanged.
   Captured Source offsets start at its captured range, not the allocation base.
   Stage owner reconstruction with metadata, interpretation, exact source ranges
   and unchanged dependencies. Empty body damage can change logical data.
   capture owns copied metadata/descriptors and retained sources/dependencies.
   Its Populate is resolved compiled owner code; an unloadable interpreter must
   retain its code in the recipe closure. No universal patch format is imposed.

   Draft / edit / hierarchy/state / grow
   Journal begins Draft with private admitted input and complete predecessor
   State. The assigned execution lane acquires its own resources after any
   synchronized handoff; beginning never touches a foreign Pool/cache. This
   grants private execution, not publication authority. Share immutable directory
   paths; copy changed records/paths using reserved storage. Directory geometry
   remains private and compiled once. state copies borrowed logical/physical
   metadata into owned nodes before its source expires. Before rare backing growth,
   derive the maximum registered physical extent from actual read/write footprints,
   including their promised readable padding. Capture that extent with one temporary
   allocation Hold, call existing grow, then release the Hold. This forbids relocation
   or clobbering protected padding while allowing disjoint in-place append. With no
   registered users, grow needs no extra Hold. Source-backed immutable aliases cannot
   grow or acquire write permission. Establish logical initialization with the owner.
   Resource failure preserves the admitted obligation. Later batch events see
   earlier private work. Partial stores/imports need owner repair or discard,
   never blind replay from a later cursor.
   Draft buffer identity is scoped by this Draft-owned index, not the original
   immutable Material identity. Sharing the recipe does not share writable bytes.

   freeze / discard
   On Draft.lane, join actual writers, close acquisition/authority and clear borrowed
   Lock.writer pointers. Freeze coherent membership, metadata/material closure
   into Draft.frozen and seal the result. Failure preserves staged work/basis.
   Success exposes no visible head and waits for no redundant derived writeback.
   Retained physical uses may survive; they retain bytes/Lock, not open write
   permission or a stale Draft pointer. Draft cannot move while borrowed.
   A synchronized handoff can deliver const sealed Draft and its results/updates
   to another publication lane. Preserve their storage until Journal finishes
   preparing; the old execution lane performs any remaining physical retirement.
   Journal validates exact admitted input and predecessor and publishes one
   State. State holds the object Directory directly; unchanged index nodes can be
   shared across epochs without a separate retained wrapper. An empty frozen directory
   is valid: sealed and complete closure, not a non-null root, establish readiness.
   Before dropping the private index, end native borrowed Views; escaped retained Views
   retain their own Buffer/Lock. Discard can abandon retained physical work but
   cannot reuse its backing early or leave a Lock pointing at dead Draft.
   Clear sealed before clearing private edits/buffers/frozen result, so discard
   cannot look like a sealed empty-object result. Invalidate old private access.
   Preserve the exact admitted input and predecessor for repair/retry; never
   reacquire a newer basis. Destroying
   Draft ends those references, not the caller's obligation to account for input.
*/
} // namespace orbital::object
