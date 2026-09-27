# Design changes following the verification review

The fixed-position transaction design remains the centre: reserve outputs, fix a
position, release each local reservation, and compute from retained inputs. The
review changes the waiting policy, simplifies eligible retention acquisition and
makes recovery requests repeatable. [BRIEF](../BRIEF.md) carries the resulting
requirements; this note explains the decisions and which evidence they replace.

## Queue policy

A waiting request protects its scopes once its older conflicting requests have
released their reservations. Before then, eligible younger requests can pass.
Keep each request's original local order until fixation or cancellation. Actual
holders always exclude conflicting grants. This needs no timer, global age,
revocation protocol or contention retry.

The rule addresses the motivating bridge: X holds x, B waits for x/z, and C needs
only z. C can proceed while X holds x. Once X releases, B prevents new conflicting
arrivals and waits only for younger reservations already admitted. Conditional
progress follows from a finite older conflict prefix and eventual release of
that prefix and the already-held younger cohort. It is not a latency bound.

That last cohort is the accepted cost. If it includes a younger WAN holder Y on
another part of B's envelope, B's drain can delay later z-only work. The native
study measures both sides of this reversal, not only an aggregate percentile.
Actual broad reservations still delay every overlapping writer, and enabled work
can compete for shared CPU, network and storage. Native policy work is counted;
its CPU price has not been calibrated.

Ordered queues remain the fair but convoy-prone comparator. Eligible-first admits
continuing-arrival starvation. Protecting only the oldest live request is simpler,
but lets an unrelated old holder disable protection for an independent broad
queue. The [formal comparison](../../workbench/spikes/orbital-reservation-policy/README.md)
and [420 native histories](../../workbench/spikes/orbital-reservation-policy/NATIVE.md)
support selecting older-conflicts-drain provisionally.

Each agreed local record must also settle the grant queue in canonical enqueue
order before exposing logical state and outputs. Otherwise deferring grants past
a later release can choose another winner. Normal execution, replay and direct
folding now share that closure. Full cached reply sequences include grants that a
record enables for other transactions. Policy interpretation is fixed for the
lineage; these models do not change the interpretation of old journal prefixes.

## Recover the obligation, then resume its ordinary work

A coordinator drives records owned by a replicated journal. Root owners likewise
recover their chosen definitions and decisions. Losing an owner's in-memory
replies must not lose the obligation or reset a surviving holder's state.
Acquisition, material fetch and custody use correlated ordinary requests;
idempotent effects and repeated responses are separate concerns. Once acquisition
success is recorded, serving a complete surviving copy does not require collecting
all original acknowledgements again.

The original root-owner reset retained consumed replies and cleared surviving
holder suppression. It therefore established less than a real cold restart. The
corrected boundaries erase the owner's caches, replay an actual barrier snapshot,
and retain each surviving peer's state. Dedicated histories lose consumed hold,
material, close and custody facts, including either side of custody transfer.
Finite post-fault retry histories and unrestricted repeated-request progress have
different evidence; [MATERIAL](MATERIAL.md) states the exact service assumptions.

A transferred read may still await an earlier writer. Its successor receives the
fallback and responsibility for the eventual qualifying result or no-effect
resolution, as well as currently available bytes. Existing pending-cut joins
check this obligation. Recovery discovery must revisit changing inventories and
resume interrupted copies under stable identities.

## Remove a redundant record when its meaning is already present

An existing chosen operation can establish a prepared retention obligation when
that operation and its retained prefix completely determine the obligation under
the same owner. Normal folding and replay derive the same acquisition from that
record. This removes a separate Begin append and its acknowledgement dependency.
It does not add another certificate or durable index in place of the append.

The [prepared-root comparison](../../workbench/spikes/orbital-root-fusion/README.md)
uses actual transaction, journal, holder and physical-view kernels. It keeps the
durable success/abandonment distinction. Removing Register/Abort globally would
lose recoverable acquisition evidence and change custody withdrawal and failure
availability. Unknown future recipes, later-selected holders and different owners
still need their own explicit acquisition path.

Shared journal folding, stable identities, recovery correlation and physical
operation accounting are useful implementation machinery. Their meanings stay
separate: local reservation release, custody acceptance, application completion
and physical retirement discharge different obligations.

## Evidence replacement

The [historical selection](evidence/baseline-before-design/README.md) preserves the
pre-revision sources and 485 accepted cases. It is explicitly incomplete: the
larger admission-renewal graph was unfinished, and the cold-root-owner gap was
found afterward. It is a comparison baseline, not an earlier completed proof.

Changing TxKernel invalidates 213 baseline source closures; changing RecoveryKernel
invalidates 118, with 55 in common. Their union is 276. This includes epoch folding,
transaction/journal recovery, retained reads, physical views, delivery and capacity
joins wherever they import those actual kernels. The retained ordered/eligible
projection explicitly selects its old policies; it does not silently acquire new
meaning from the default change. Additional queue, actual-Tx binding, cold-owner,
repeated-request and prepared-root cases exercise the new boundaries.

The final collector matches parsed dependencies, exact configuration, checker and
TLC pin, then rechecks raw outcomes. An unchanged leaf's receipt remains usable;
a changed imported kernel requires replacement even if that particular fixture
does not reach the changed branch. Later admission representation work has its
own correspondence checks and invalidation. Final dispositions and exact inputs
belong in [RESULTS](RESULTS.md), not in a manually maintained claim of completion
here.
