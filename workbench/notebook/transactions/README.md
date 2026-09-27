# Transaction workloads worth keeping

These are the application questions from the retired Orbital scenario workbench,
kept for Engine, Loom and Orbital to revisit together. They are a repertoire,
not a coverage checklist or another specification of the transaction protocol.

- [Read situations](reads.md): 21 cases, distinguishing coherent reports,
  current-state decisions, historical products, discovery and extension reads.
- [Write situations](writes.md): 17 families, including broad transformations,
  sparse effects, indexes, cascades and private construction followed by publication.
- [Worked ELT histories](elt.md): executable examples and explicit gaps for MERGE,
  unique races, delete/reinsert, expanding cascades, moving destinations, index
  visibility and snapshot-plus-CDC activation.

The detailed documents are dated research records. Their model names and claims
refer to the captured scenario implementations; current protocol direction belongs
to [Orbital's brief](../../../orbital/BRIEF.md), and maintained experiments to the
[native simulator](../../simulator/README.md). Archived source links preserve the
original context without keeping those old runtimes active.

Three counterexamples remain especially useful. A narrow eventual write can
require broad advance evidence coverage when its destination is not yet known.
A global index can introduce predicate waits outside the primary row's owner.
Replacing a published generation from an old snapshot can lose live destination
corrections unless that replacement is the intended application semantics.

Recovery questions stay in [Orbital's mining guide](../../../orbital/MINING.md#failure-recovery-and-retained-evidence):
revoking old authority, preserving possibly chosen suffixes, exporting evidence
under distress, and retaining enough dependencies to reopen. Neither the old
tiny probes nor the current prepared-authority model settles those protocols.
The [retirement record](../retired-spikes.md) gives complete source recovery.
