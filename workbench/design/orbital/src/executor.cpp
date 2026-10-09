#include <orbital/candidate/executor.h>

/* Future isolated execution; native Lane/Thread/Message are implemented in Orbital.

prepare/start/verify:
  Capture immutable arguments, interpretation and projections independently of
  executor choice. Native ready regions need no Executor hop. WASM/Firecracker
  backends receive permitted ranges with host-enforced rights and padding.
  Validate owned descriptors in place or copy them into reserved tail/slab
  storage. Retain them, the Executor and every View through physical use and
  final completion Message release. Task detach cannot revoke those uses.

  Completion belongs to the captured caller Lane. Validate report context and
  interpretation before completing its Signal; never reenter a Task step.
  Compare the main interaction and selected outcome, not only checker pairs.
  Matching reports do not make new observations reproducible: agree and retain
  those observations as owner input before import. An asynchronous discrepancy
  after publication is an incident, not a retrospective abort.

  Owner/Ikea imports under the original authority and generations, with
  unit-specific index/summary maintenance. Never retarget late output to the
  current head. Failed import retains accepted output for repair or discard.
  Executor completion and verification do not publish Journal State.
*/
