# Durable objects and the machine underneath

This is a design proposal, informed by [source mining](SOURCES.md),
[worked cases](SCENARIOS.md) and the small [probes](README.md). It is not a
replacement for the transaction protocol or a detailed runtime interface.
The question is how ordinary programs can manipulate durable objects without
turning OS pages, local scheduling or SQL concepts into Orbital's consistency model.

## An object is durable before it is mapped

An object has a stable logical identity, an application-defined kind, and logical
versions established by admitted outcomes. Its owner supplies interpretation,
scope/overlap rules and ways to reconstruct or convert those versions. Orbital
tracks their authority, availability and lifetimes without knowing what the
object means. A tree, document, model, queue or simulation field can use the same
services. A shard, transaction, object, physical extent and page need not coincide.

The identity of a mutable object is distinct from the identity of one version or
one encoded artifact. Object discovery resolves names to current authority and
usable representations; it must also retain what old plans need after movement.
This need not be a central directory lookup on each access. Representation
descriptors can be cached subject to their identity and lifetime conditions.

A version may have compressed storage, a resident decoded form, and differently
laid-out replicas. Its reconstruction closure includes required bases, admitted
operations or accepted results, dictionaries, code and source versions. A compact
reference is useful only while this closure remains available. An object should
be independently useful to load, verify, convert or recover; a giant name whose
small reads always reconstruct the whole shard is a poor physical choice, even
if its conflict rules are fine-grained. The application chooses decomposition.

Logical allocation changes the application state and must follow deterministic
rules. Scratch addresses, backing-frame choice and cache-reuse order are local
physical decisions. Letting an allocator's free-list order choose durable IDs,
iteration order or exact extension outputs would mix those two responsibilities.

## Bind a view, then use ordinary memory

An open view binds object/version or transaction position, representation,
permitted access and a lifetime. It projects durable state into local virtual
memory. Code uses ordinary loads and stores within that view; an API call per
load is unnecessary. The application supplies logical read coverage independently
of page faults, potentially once for the whole invocation. Writable views contain
tentative state; making a page writable or flushing it does not commit a transaction.

A useful implementation supports three preparation choices for this same view:

| Preparation | Good case | Principal cost or failure |
| --- | --- | --- |
| Populate and hold the complete view | Small objects, bounded hot operations, strict no-fault regions | Overfetch and resident capacity; unsuitable for objects larger than the budget |
| Populate bounded windows or owner-described access sets ahead of execution | Scans, known gathers, phases with safe suspension points | Predictions can waste bandwidth/cache; access descriptions and scheduling have cost |
| Populate on first touch | Sparse or unpredictable pointer access, compatibility with ordinary programs | The OS thread parks; fault service and dependent I/O need capacity independent of that thread |

Use the first two where the plan knows enough; keep first-touch materialisation
as a genuine supported path. Mixing them must not change the observed version.
An I/O completion identifies the source version and mapping incarnation; a late
completion cannot fill a reused address with stale content. Decode and verify
the required reconstruction dependencies before exposing usable bytes.

Physical access is not logical observation. A fault on a page containing `x`
does not detect a later load of `y` on that page. Native code with an enforceable
access contract can use a narrow binding; unrestricted access to a projected
page requires every readable byte to be valid for the context. An untrusted
extension must not receive unauthorized neighboring bytes. Isolation-compatible
layout, a separate projection or copying a permitted slice may be necessary.
This is a physical-granularity cost, not a reason to widen every transaction's
logical dependencies to full pages.

Pointers stay usable only while their binding remains valid. Address-space
reservation can let an active projection grow without moving existing bytes.
It does not solve arbitrary internal pointers after restart, cross-process
transfer, shrink or relocation. The object owner supplies offsets, relocation
or an appropriate address convention for durable references. Representation
changes do not retarget an old view's pointers beneath its user.

## Avoid copies when their saved work outweighs reconstruction

The baseline is private mutation state. Linux UFFD write-protection can create
a new page on its first write. Fresh construction needs no copy of nonexistent
old content. A privately owned existing frame may also be reused destructively
when all these obligations are met:

- No other mapping or active user requires its old bytes. Include hashing,
  persistence, DMA and sends, not just readers counted by an object handle.
- New access cannot race the exclusivity decision. Late old readers receive
  another correct view, potentially reconstructed from retained material.
- A suitable base plus retained history/accepted results can reconstruct every
  still-promised old version and the state required after abort or crash.
- The replay and temporary-memory costs fit the selected resource policy. A
  prediction that readers are unlikely is a cost hint, never a safety proof.

This changes when copies and reconstruction occur; it does not change the serial
position or publication rule. A cold old snapshot can incur a large replay bill.
If reconstruction is expensive or old reads frequent, retaining a beforeimage
can be preferable. A local file used as a serving image remains disposable;
writing it back does not establish database durability.

Two nonconflicting logical updates can share a physical page. Installing or
undoing their whole-page images independently is incorrect. Choose separate
working representations and owner-defined effect application/reconstruction,
or arrange short local execution over a compatible representation. Encodings
with wide stores need corresponding physical exclusion even when logical
effects differ. Orbital must not infer byte patches from page dirtiness or
replace application semantics with page-sized transaction locks.

A local mapping/publication latch may protect a short physical transition.
It must not remain held while a transaction waits for a predecessor, remote
input or verification. Nor may an application lock held across a page fault
be required by the service that resolves that fault. Mutable projections can
be discarded and rebuilt; they are not the sole repository of authoritative
history. Shared in-place mutation with general undo/merge adds a different
mechanism and is not the initial recommendation.

## Where the modules attach

The useful boundaries describe what each party owes, rather than prescribe one
large universal descriptor. They can compile down to direct calls and small
bound records on the ordinary path.

| Attachment | Application / Engine supplies | Orbital supplies | Loom supplies |
| --- | --- | --- | --- |
| Object discovery and interpretation | Object kind/version handlers, identity/reference conventions, representation and reconstruction dependencies | Authority and retained-version resolution, physical holders and backing/mapping services | Placement and residency decisions among usable choices |
| Transaction preparation and execution | Effects, complete observation coverage, overlap rules, deterministic operations and atomic publication meaning | Admission, position, pending outcomes and recoverable decision machinery | Resources and scheduling for enabled work and continuations |
| Plan/analyser binding | Legal alternatives, applicability conditions, access sets or discovery steps, safe stopping points, output growth bounds where known | Available services, identities and capability limits | Resource feasibility, placement, work grain, prefetch and measured cost feedback |
| Memory | Read/write context, permissible physical access and durable allocation meaning | Reserve/map/protect/populate/reconstruct/remap/release mechanisms | Resident budgets, holds, eviction, lookahead and allocation policy |
| I/O and messaging | Logical destination, purpose, permissible data and completion requirement | Endpoint/handle creation, submission/completion/cancellation mechanics, identity and authority checks | When to issue work, batching, queues, credits and completion placement |
| Execution contexts | Callable entry, resource requirements and deterministic versus external inputs | Discover/create/retire threads and isolated processes/VMs, stacks, mappings and platform operations | Worker roles, rings, spin locks, affinity, task assignment and wait policy |
| Diagnostics and recovery | Application replay/interpretation handlers and semantic outcome checks | Actual backend progress/errors, recoverable history and physical incident evidence | Attribution, bounded service capacity and prioritization of repair versus foreground work |

Engine owns its analysers and retained plans. Loom binds legal plan choices to
current resources. Orbital exposes capabilities and observations and invokes
the required application handlers; it does not become a SQL optimiser. A
physical hint may change cost or scheduling but cannot alter agreed behavior.
If a plan substitution changes read/effect coverage, that is a semantic change
which must satisfy the existing protocol, not just an allocator or routing tweak.

## Allocator choice

The allocator should share Orbital's backing/protection mechanisms without
forcing scratch to become durable objects. Loom supplies execution locality,
budget and reuse policy. Allocation/free hints can express recent residence,
next consumer or locality domain, expected reuse and independent concurrent
writers; they are optional and may be wrong. Small allocations and frees should
be ordinary local operations against prepared arenas, not broker round trips.

Useful comparisons include per-owner arenas, bounded transfer/reuse caches,
global LIFO reuse, and compact packing. Reusing a recently freed address does
not prove cache residence. Separating independent writers can reduce false
sharing while increasing footprint; over-separation can evict more useful data.
The existing memory study shows that LLC domains matter even within one NUMA
node. Cross-domain reuse must honor data isolation and outstanding users before
any cache preference. No fixed cache-coloring scheme or hint vocabulary is
selected by the current counts.

Durable objects can use the same physical pools after their version and access
lifetimes permit reuse. Logical frees and durable IDs remain application state.
This keeps one source of backing and accounting without coupling scratch policy
to recovery or letting nondeterministic warmth affect logical state.

## Thread, IPC and fault progress

Orbital creates the OS resources; Loom decides their use. This includes kernel
I/O rings configured on Loom's request, shared memory, wait/wake facilities,
thread stacks and CPU/NUMA discovery. Loom owns userspace work/completion rings,
spin locks, batching and polling versus sleeping. Platform adapters report
capabilities and actual progress instead of growing a second scheduler.

Endpoints need an identity across thread/process restarts. Local transport can
pass a descriptor and leased memory through a Loom ring; a remote destination
uses encoded transport. A pointer is never a portable message identity. Both
paths preserve the same logical delivery obligation; local enqueue does not
establish remote durability or transaction completion. Buffer ownership, memory
ordering and the receiver's access rights are part of the local handoff.

Fault handling requires independently runnable service work, bounded resident
metadata needed to make progress, and provisioned space for an admitted
fault/reconstruction step. Merely creating
a handler thread is insufficient if its lock, I/O completion or allocation
depends on a parked worker. Do not park short interleaving slots for storage
waits. Where continuation extraction is unavailable, account for the blocked
OS thread; do not promise invisible coroutine suspension at an arbitrary load.

Fetch failure must not become a wake/refault loop or fabricated zeros. A managed
runtime can fail the invocation through its supported trap boundary. For native
code with no safe fault-unwind boundary, compare explicit preparation with
retiring and recovering its containing execution process. The latter spends
availability and warm state; a signal handler cannot safely invent C++ unwinding
through arbitrary locks and partially modified structures.

Admission must consider input, scratch, output and retained work together.
Bounded windows, spilling/reconstruction, releasing execution capacity while
waiting, and refusing an impossible resource request are alternatives. No
constant emergency reserve proves progress for an unbounded decoder or task.
An admitted logical obligation survives a local resource failure: reschedule
or recover it, or resolve it through the agreed outcome mechanism. Local OOM
must not independently choose a replicated transaction result.

Closing an endpoint stops new work. Destruction follows retirement of existing
workers, mappings and backend users; cancellation by itself does not retire
them. A completion carries enough incarnation information to reject stale
delivery without losing its accounting/cleanup obligation. This same lifetime
rule can cover paging, disks, sockets, IPC and extension buffers.

## What remains genuinely unsettled

The proposed common structure is small: durable identity/history, bounded views,
physical service operations and Loom-owned execution. The difficult remaining
choices are material rather than naming questions: representation-specific
effect application, historical reconstruction granularity, safe native fault
boundaries, RO WASM embedding with fallback, and the allocator's benefit on
mixed workloads. The small probes do not resolve their performance. Actual
fault-path experiments and focused mixed-workload simulations should decide
them before a detailed API or the high-fidelity simulator freezes assumptions.
