# Experimental Loom callers

The [MVCC study](../README.md) owns these candidate Object-facing contracts.
[loom.cpp](loom.cpp) retains real lifetime and cancellation histories from the
rejected implementation; the production Loom API remains Stage 1.

[Orbital Objects](../baseline/include/orbital/object.h) supply selected versions,
physical descriptions and retained Views. A named `BorrowedContext` serves
uninterrupted ready calls; end its borrowed access before publication, suspension
or Store destruction, and before replacing a borrowed alternative. For discovery
across waits, keep a `ReadContext` in the stable operation frame. Later Objects
and Parts still use its logical cut. A selected descriptor keeps its original
interpretation when a physical alternative is adopted or replaced.

When future coherent discovery is finished, retain only the resolved access:
`RetainedSelection` permits later Parts of that exact descriptor;
`RetainedView` owns one Part and its interpretation dependencies. Independent
Part backing permits unrelated Parts to retire; an umbrella backing owner keeps
its whole allocation group alive. Prepare named Ikea bindings and native bodies
while these owners are stable, and release native access before its owner.
Keep catalogue operations and retained handles on their owning Lane. A detached
Task ends caller interest; outstanding physical use keeps its own retained access
until completion and retirement.

For an accepted fill into private storage, its physical record keeps the Builder
and WriteView valid until every use of the captured writable span ends. Detach
does not revoke that span. On owner-Lane completion, close the writer, then
discard cancelled work or freeze and prepare the original Draft. Publication
still requires its exact basis; late filled bytes grant no authority to replace
a newer result.

The [Object caller check](loom.cpp) exercises direct ready reads,
Signal retries with publication and adoption of an authored equivalent
Representation during a wait, narrow retention, foreign native use after detach,
delayed private fills with current/cancelled/stale completion, and selective Ring
cancellation with stable retained frames. The fill histories check disjoint
backing, independently decoded published bytes and final zero Arena charges.
