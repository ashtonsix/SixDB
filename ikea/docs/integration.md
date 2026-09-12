# Integrating with owners

An Ikea call performs local work over borrowed storage. The owner makes that
work part of a larger operation: acquiring bytes, keeping bindings alive,
retaining progress across waits, and publishing data with valid summaries.
The [SeriesPack](../examples/seriespack/integration.cpp) and
[TuplePack](../examples/tuplepack/integration.cpp) adapters demonstrate an
in-place mutation with explicit retained state.

| Owner | Responsibility |
| --- | --- |
| Engine | Field semantics, segment schema and representation choices; summary validity and publication policy |
| Loom | Buffer leases, queueing, prefetch scheduling, cancellation and retained work |
| Orbital | OS-facing services, including the intended UFFD page-COW version mechanism |
| Ikea | Admitted operations, composition, physical descriptions and issued-byte coverage |

In-place and private-copy mutation are both valid. Orbital’s UFFD COW mechanism
can preserve page versions during in-place writes. Maintenance may separately
need current old values to compute a replacement delta. The owner’s isolation
and versioning protocol determines which data editions readers can observe.

![The owner acquires and prepares work, invokes Ikea, then either retains a completed frontier across a wait or coordinates publication.](images/mutation-lifetime.svg)

*The teaching adapter returns from Ikea at a complete work frontier. The owner
can then suspend or publish while retaining responsibility for the dirty state.*

## Bind once, invoke bounded work

The SeriesPack adapter moves the only readable lease into a stable-address work
object, modeling owner-provided exclusion. That object holds the named view
borrowed by its operation. Each step completes a 64-row range and returns.
Its chunk size is a scheduling choice, independent of physical tile size and
native instruction grain.

At binding, admit storage placement and retain its proof while the mapping stays
valid. Before each invocation, supply stable input/selection, disjoint command
metadata, enough effect capacity and any observation resources. Checked calls
validate the command; trusted entries reuse established proofs. Neither acquires
isolation. Kernels do not allocate, wait or call the scheduler.

## Retain live state across suspension

After a call returns, completed writes and their effects are real. Retain the
lease, named views, prepared bindings and endpoints, mapping/version generation,
input and selection, next original coordinates, unfinished summaries, journal
and plan. A resumed call must not depend on a terminated CPS stage’s stack.

Prefetch identifies a future access; it supplies neither a lease nor proof of
readiness. On an asynchronous reply, the owner validates generation and mapping
before continuing or rebinding. It must also release resources carried by stale
replies. An OS write-protection fault preserves machine state through the OS;
explicit Loom suspension occurs at the returned work frontier.

Selections retain original coordinates. Keep predicate evidence alongside the
selection when needed: an inactive row only means this operation excludes it.

## Consume effects and publish

Three descriptions serve different parts of the owner protocol:

| Description | Meaning | Owner use |
| --- | --- | --- |
| Logical destinations | Codes or bit windows selected for replacement | Apply field semantics |
| Issued byte coverage | Physical stores, including preserved neighbors | Protect and account for actual writes |
| Maintenance dependencies | Values needed by a summary, including untouched fields | Maintain or invalidate evidence before visibility |

`<ikea/effects.h>` journals actual source views and plane-relative byte spans.
Resolve those identities to owner/partition offsets before destroying the views.
Coverage hooks run before their associated local write group. A bulk call may
contain several groups; the journal supplies neither beforeimages nor a
transaction-wide pre-notification barrier. Hooks must have admitted capacity
and cannot fail or suspend mid-group.

SeriesPack’s `sum_change` supplies a modulo-u64 replacement delta.
TuplePack’s [observation](tuplepack/extending.md#observe-the-dependencies-of-the-summary)
can read before-values, after-values, both or neither. Its bracket surrounds all
child writes within one window and repeats across a range. The law determines
which dependencies make the summary valid.

Private contributions can survive with the retained operation. Persistent
summary writes need separately admitted resources and byte coverage. Before
data becomes visible, readers must have exact summaries, compatible corrections,
conservative evidence or a valid bypass; an ephemeral repair queue alone leaves
a correctness gap.

The SeriesPack adapter consumes mapped coverage and its delta before returning
the lease to readers. TuplePack models publication of a completed prefix with
summary bypass; its example leaves real lease acquisition and publication to
owner code. A real owner coordinates visibility with concurrency, durability,
recovery and multi-participant rules.

## Cancellation and failures

| Point reached | Meaning of stopping |
| --- | --- |
| Before any mutation | Return an unused lease or discard a private candidate |
| Checked call rejects | That call leaves data, summary and effects unchanged; prior successful calls remain real |
| After in-place writes | Retain dirty state; owner resolves commit, rollback or another admitted continuation |
| Publication submitted | Outcome depends on the owner protocol; cancellation may arrive after commit |

Dropping a dirty lease does not undo writes or contributions. After dirty
cancellation, the SeriesPack example elects to finish the operation; TuplePack
stops after two of four rows and models publishing that prefix with bypass.
A private candidate may instead be discarded before publication under its
ownership rules.

Report command errors with the original range, supplied extents/effect capacity
and optional binding diagnostic. Allocation, queue failure, stale mappings and
publication conflict belong to the enclosing driver or owner.
