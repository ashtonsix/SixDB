#include "orbital/candidate/port.h"
#include "orbital/worker.h"

namespace orbital::candidate::port {
/* Endpoint creation / retain_memory
   Typed file/blob/network APIs retain their resources and qualify identity+extent.
   They return Source or Destination directly; a universal opaque locator format
   and backend factory dispatch add no useful common semantics here. Source must
   preserve exact immutable bytes; a raw allocation Hold alone cannot freeze
   writable aliases. The memory owner must close/freeze them or provide a copy.
   retain_memory fills a closed supplied MemoryResource, moves in the physical
   Hold and uses the shared compiled memory backend. It neither allocates record
   storage nor retains a destroyable caller frame. Pool/storage ownership and
   retirement are supplied before capture; failure leaves the record closed.
   Resource.native points at this known MemoryResource; the memory backend uses
   an ordinary typed cast. Member destruction retires bytes and Resource before
   the record's own physical storage.
   Its Source extent is [0, Hold.footprint.size); native byte offsets start at
   Hold.address, never the Allocation base.
   Resource retains its shared Backend independently of requests;
   a Source need not own an FD, thread or backend. Destination is a distinct
   writable capability. Native backend casts stay inside its qualified transfer
   implementation, not a universal registry.

   borrow_memory
   Recognize the actual built-in memory backend before reading Resource.native;
   reject other services without dispatch, I/O or allocation. Validate the frozen
   Source extent against MemoryResource.bytes. Return its existing physical Hold,
   borrowed while Source remains retained. This grants immutable native access,
   not write authority or a new lifetime. Encoded/transformed material still needs
   its owner reconstruction; memory residency alone does not prove native layout.

   read / write
   Capture Source for a read or Destination for a write in the endpoint variant;
   dispatch through its Resource.backend. The active alternative also determines
   transfer direction; no second facade can reinterpret that endpoint.
   Route completion Messages to the explicit caller lane, regardless of backend
   execution thread; resumed calls validate that lane. Validate endpoint/range bounds,
   including offsets from Hold.address rather than Allocation.address. Validate
   persistence support, idle Request and supplied
   range/result storage. Validate already-owned ranges in place; copy borrowed
   ranges into Request storage before returning. Capture endpoint
   and actual memory use once, then mark prepared and call advance. A resumed
   operation uses that same captured endpoint/ranges. An inline completion can finish without
   parking. Pending submit returns the same finished Signal for Loom to observe.
   Request/range storage is independent pooled storage or retained caller storage;
   no backend receives a destroyable task's borrowed spans or Signal.
   advance calls submit only before acceptance: done means accepted, waiting
   means preaccept pressure, failed means rejection without physical effects.
   A provider pressure Signal targets the Request caller lane, bridging shared
   backend pressure when necessary. Refusal leaves finished idle. After acceptance
   set it pending unless inline completion already made it terminal; then only
   inspect/wait on finished, never invoke submit again. If cancel_requested is
   already true while finished is idle, never submit; report cancellation and
   release preparation only after retained pressure notifications retire.

   cancel_interest / finish / completion-message handling
   Detach waiter interest; request backend cancellation without freeing the endpoint.
   Backend writes each range's known transferred prefix, then finish after all
   physical access ends. Define every used slot on failure/cancel too, with zero
   for no known prefix. A count of bytes elsewhere is not qualified coverage.
   Publish actual status/ranges before handling the completion Message. Inline completion on
   the already assigned caller thread can handle directly; another thread posts
   the retained Message. Request identity/endpoint stay frozen through cleanup.
   The caller handles that Message once, then completes its Signal, independently
   of the backend's execution thread.
   Even a failed/partial result is inspectable; it cannot qualify unread bytes.
   Object owner validates its captured buffer generation and exact request.
   Backend-native wire IDs reject late/duplicate replies after retirement. Endpoint/storage
   retirement follows physical completion AND message use, not logical cancel.
   Partial writes can have effects; durability includes only the reported guarantee,
   not implicit root/namespace/checkpoint completion. Never publish an epoch here.
*/
} // namespace orbital::candidate::port
