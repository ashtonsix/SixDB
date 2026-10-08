#include <loom/worker.h>
#include <orbital/object.h>

/* Future cache and placement policy; Worker/Task/Ring mechanics live in Loom.

  A stable caller-owned BufferCache uses the Worker's Lane. Reserve its metadata
  outside ready access; changing C++ ownership does not change cache policy.
  Loom selects victims; Object verifies immutable material, current generation,
  cache membership, pending preparations and actual uses before eviction.
  Private Draft buffers never answer published reads. Eviction is not a budget
  refund: Sources/Requests and final returns can retain physical storage.

  Reserve output and completion capacity before unreleasable input. Include old
  backing, replacement and scratch in peak occupancy; shared aliases count once.
  Routes and CPU/cache policy are bound outside native regions. A placement
  proposal revalidates capacity, backing and authority at the actual owners.
  Transfer credit lasts through required ACK/reconciliation. Workload pressure
  must never refuse its reserved completion or memory-return Messages.

  Concrete operations own reusable typed Ring banks, allocated from the Worker
  Pool. No generic registry or cache policy belongs in their tiny switches.
  Close/discard operation banks and drain cached backing, preparations and Views
  before calling actual Worker shutdown. Stop external posting, or transfer
  independently retained work with its stable Lane instead of destroying it.
*/
