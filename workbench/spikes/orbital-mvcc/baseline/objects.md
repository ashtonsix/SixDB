# Embedded Objects

[mvcc_baseline.h](include/orbital/mvcc_baseline.h) is the rejected Stage 2 baseline, retained for comparison in this spike.
It experiments with completed, single-owner state; its layouts and interfaces
are open to redesign together with measured complete-operation performance.
One Store belongs to an adopted Lane. All API calls, reference copies and releases
run there, including final release of escaped owners. The Lane outlives them.
There is no Journal, distributed decision, pending predecessor, IO residency or
cross-worker publication in this implementation.

An Object has stable identity and a local version. A Representation owns a frozen
physical description and Parts. A View borrows one Part's bytes, padding and
interpretation. Ikea or another format owner qualifies those bytes for native
work; Orbital does not decode schema, navigate records or prove that two formats
contain equivalent values.

## Read, then keep only what is needed

For uninterrupted work, create `auto context = store.borrow()`, resolve with
`select(context, id)`, then get a Part by ordinal or `PartId`. The returned
Selection fixes the descriptor. `Selection::part(index)` and View byte accessors
are compiler-visible leaves; qualify once around native work rather than binding
for each decoded value. No extra physical Hold is needed.

This borrow ends before logical publication, suspension or Store destruction.
An alternative borrow also ends before replacement of that alternative. A stale
context is rejected on the next selection; already borrowed Views cannot check
their own expired lifetime.

Use `store.read()` when later discovery must remain coherent:

```cpp
auto context = store.read();
if (!context) return std::unexpected(context.error());
auto first = select(*context, a);
if (!first) return std::unexpected(first.error());
auto part = first->part(0);
if (!part) return std::unexpected(part.error());
auto held = retain(*part);  // One resolved Part, including its interpretation.
// Other work may publish, convert or wait here.
auto second = select(*context, b); // Still the same completed cut.
// After discovery is done, closing context releases history protection.
// held.view() remains usable, even after the Store handle is gone.
```

A context also preserves absence, creation and deletion, including Objects not
previously looked up. Default descriptors are immutable history anchors.
Selecting a replaceable alternative captures that descriptor in the context;
this optional path may refuse its metadata enrollment. Replacing it cannot
revoke an existing Selection under that context.

`retain(View)` owns that Part's backing and shared interpretation dependencies.
`retain(Selection)` additionally preserves its exact descriptor for future Part
discovery. Once catalog history releases the version, this does not keep its
other physical descriptions alive. A supplied umbrella owner can still retain
several physical Parts; use separate backing owners when those lifetimes differ.

## Construct private results and publish once

`store.edit()` creates an unaccepted Draft at the current completed cut.
`draft.version()` supplies the proposed local version for its frozen results.
`draft.select(id)` reads staged changes first, otherwise its retained basis.
Private borrows end before Draft mutation, acceptance or destruction; retain
them explicitly if they must escape.

Two construction paths feed `draft.put(rep)`:

- `store.capture(spec, parts)` copies small descriptions and retains immutable
  external backing without copying encoded bodies. The caller guarantees that
  all mutable aliases have ended and that body, padding, code and dictionaries
  remain valid under the supplied owners.
- `store.build(spec, pool, plans)` allocates private Parts. Use scoped WriteViews
  to initialize them, close every writer, then `builder.freeze()`. Cached mutable
  spans must end with their writer, before abandonment or freeze. Freeze trusts
  initialization and format validity; it cannot revoke arbitrary C++ aliases.
  Abandonment revokes further `bytes()` access, while outstanding writers keep
  their actual storage alive. Zero-body Parts need no Allocation unless they
  expose readable padding; that padding receives zeroed backing.

`draft.erase(id)` stages absence. All changed Objects publish together:

```cpp
auto prepared = store.prepare(draft);
if (!prepared) return std::unexpected(prepared.error());
auto installed = store.install(*prepared);
if (!installed) return installed;
prepared->retire();
```

Preparation first verifies Store incarnation, exact predecessor and issued
version, then reserves all records and catalog entries. Refusal leaves the Draft
usable and has no logical effect. Successful preparation consumes the Draft and
accepts its complete immutable result. One accepted result may be pending per
Store. Installation checks that private occurrence and consumes it once; the
caller cannot supply a commit position or obtain authority from a Representation.
The owner visit links every change before advancing one completed cut. It does
not allocate, call user code, wait or destroy backing.

Explicit abort, or destruction of an uninstalled Prepared, terminates accepted
work with no logical effect. This embedded profile accepts finished results; it
promises no later retry of an admitted command. Private Draft destruction is
ordinary rollback. Journal admission and distributed outcome provenance will
need their own implementation rather than a caller-supplied epoch label here.

## Convert without a logical write

`store.adopt(selected, rep)` registers an alternative of that exact catalog
version. Object identity, version and logical metadata must match; the format
owner establishes semantic equivalence. Part count, physical format and Part IDs
may change. `select(context, id, true)` explicitly chooses the alternative.

Adoption never advances the completed cut or invalidates prepared logical work.
A delayed conversion of an old retained catalog version cannot replace the new
version. A version already removed from catalog history has no adoption target;
its retained immutable descriptor remains readable.

## Budgets and maintenance

Stable catalog entries reach immutable version histories. A sparse update builds
only changed records. Draft lookup scans its changed-Object vector, so building
a very large batch can take quadratic work; small batches do not scan the catalog.
A context protects versions newer than the oldest retained
cut and one qualifying anchor per Object. This conservative policy can retain
many intermediate versions behind one old context, and historical lookup can
walk that history. Close discovery contexts when resolved retention is enough.

The metadata limit counts requested bytes for records, index buckets, descriptors,
temporary validation, context captures and retirement debt. It includes the fixed
budget domain; malloc overhead, RSS and caller-supplied external owners are
excluded. Allocation/Arena separately account physical backing. Statistics expose
both the metadata charge and the oldest cut; exhausted budgets refuse work,
never invalidate readers.

Call `store.drain(budget)` between batches. It examines history/deletion debt and
destroys queued metadata records; unchanged Objects need no maintenance scan.
Prepared catalog entries stay pinned through their terminal disposition. Temporary
construction failures also become refundable through drain. Deleted entries are
removed once no context needs earlier presence or absence.

The budget bounds record steps, not elapsed time: opaque external-owner
destructors can take arbitrary time. During drain, their callbacks may reenter on
the Lane or release the public Store handle; the active call protects its catalog.
Nested calls and callback work have their own costs outside the outer budget.
Final Store/catalog teardown drains locally;
owners that outlive it release synchronously on the Lane. This is an explicit
single-owner policy, not a multi-worker reclamation service.
