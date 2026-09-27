# Can an existing chosen operation establish its prepared root?

This study compares an explicit root Begin with deriving that same Begin from
an existing chosen operation under the same logical owner. It keeps the root's
Register/Abort decision, holder persistence and physical lifetime rules. It is
an eligible path, not a replacement for generic root acquisition.

The first pilots support the narrow saving: one fewer chosen Begin record for
the prepared acquisition, with the same successful value or justified failure.
The shared provider's full actor-cache-loss recovery correction is being checked
separately; final study evidence must use that corrected source. Pilot receipts
are diagnostics, not a final recommendation.

## What is actually joined

The maintained [PreparedRootFusion model](../../../orbital/spec/PreparedRootFusion.tla)
uses TxKernel, RecoveryKernel, DurableLog,
ViewsKernel, program interpreter and transaction oracle. It does not copy those
kernels. One actual read-only transaction journals its execution profile, fixes
its position and journals a read bound. Its profile includes an immutable prepared
recipe, declared holder, rights and termination owner. The bound's captured
context carries those recorded bytes.

In the explicit path, consuming the bound starts ordinary root acquisition,
whose Begin is another chosen record. In the fused path, the same chosen bound
applies the stable Begin projection immediately. Both paths subsequently obtain
actual durable holder evidence, choose Register or Abort and return material
through the unchanged provider. The root reconstructs an actual base/patch/code
recipe. The application expects zero from the retained initial value; a missing
interpreter cannot silently substitute a cached or default value.

One owner consumes the real mixed journal in index order. Recovery erases the
transaction fold and driver as well as the root actor's acquisition caches,
then uses an actual barrier-correlated snapshot. It recreates the Begin projection
at its original bound position before replaying later registration or terminal
records. The explicit path likewise rediscovers an acquisition whose transient
request disappeared. Holder state, reply suppression and submitted physical work
belong to another process and survive this cut. Recovery must obtain new correlated
material evidence, not keep an old reply in an unjournaled owner cache.

The selected fault cuts are before acquisition sends, after a real Hold reply
has been emitted and lost, after Register, and after Abort. Acquisition cancellation
is a request to abandon the still-unregistered attempt; it is **not** an illegal
pre-position transaction cancellation applied after position selection. Register
may win the race. When Abort wins, its chosen failure reaches the transaction and
the ordinary outcome/decision path publishes failure.

One logical root supplies main and two checker views. Each reads actual material;
the checker reports use those observations. The shared ViewsKernel also keeps a
cancelled borrow alive through logical root release and completes it afterward.
This is a local copied view's lifetime, not a claim that releasing a remote root
always permits collection while an unfetched remote page is still needed.

## What the comparison establishes

The same independent expected-value, transaction, receipt-provenance, physical
closure and authorized-termination properties apply to both paths. SavedRound
counts actual chosen root Begin entries: one for the explicit path and zero for
the fused path. Both cancellation outcomes have reachability cases. This is not
a direct trace-refinement theorem between the two complete executions, and TLC
state counts are not performance measurements.

The removed operation lies between the required read-bound record and Hold
requests. Thus an implementation can remove that additional durable append and
its acknowledgement dependency. Batching and placement determine the real latency
saving; this study measures neither network round trips nor microseconds. It
adds no adoption certificate or hidden durable index in place of Begin.

The projection is only valid when the chosen command and retained prefix fully
determine the acquisition and its discovery/termination information. An unresolved
future recipe or later-selected holder set is not supplied by a read bound merely
because it contains a cut. Existing pending-cut retention and generic cross-owner
acquisition remain separate maintained families.

## Bounds and limits

This is an authored service schedule with explicit fault/cancellation crossings:
one read-only transaction, one logical owner, one prepared recipe and one surviving
holder, one owner/driver loss, and three local views. Normal service takes actual
kernel transitions; it is not an unrestricted product of network schedules.

The declared prepared package remains available until acquisition has succeeded
or failed. The study does not model another owner retiring its original source
during preparation. It tests holder protection/terminal collection after acquisition,
not initial package discovery or physical-store destruction. The provider's separate
families own those obligations.

A selected crash is required to occur at its authored cut. Fair service and
cancellation apply afterward; they do not assume that missing material becomes
available. There is no state constraint or retry-count search cutoff. Ordinary
receipt attempts are bounded by this finite workload; post-loss query correlation
and replies come from the shared recovery provider.

The independent controls omit replayed projection/discovery, alter the recorded
descriptor, or retire a cancelled physical borrower early. They must expose the
named failure rather than merely end with a successful TLC process.

## Running

From the repository root on Linux (prefix with orb -m ubuntu from macOS):

~~~sh
python3 workbench/spikes/orbital-root-fusion/run.py \
  --output build/orbital-root-fusion/run --timeout 120
~~~

Use repeated --case arguments to select names in cases.json. The runner delegates
to orbital/spec/suite.py, which captures the canonical models and configurations
once before checking. The full 31-case comparison catalog remains here; the lean
root-fusion-cases.json selection participates in aggregate maintained verification.
There are no copied shared kernels or competing model sources. Each receipt
retains exact inputs, diagnostics and completed/negative/witness/incomplete status.

The provisional implementation direction is to reuse an existing complete
same-owner obligation record. Removing Register/Abort globally is a different
proposal: it loses the durable acquisition-success distinction and changes
abandonment, source-custody withdrawal and recovery availability.
