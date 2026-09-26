# Orbital Physical Design Brief

Orbital makes durable state usable through ordinary memory and supplies the OS
services beneath execution. Loom directs preparation, placement and scheduling.
This brief develops that boundary: prepare an efficient path for ordinary work,
and keep the services needed to finish it available when execution waits.
Linux is the primary implementation target; concrete interfaces and performance
remain to be established.

## Memory views

Implement the main brief's version-bound views with reserved virtual address
space, shared backing and UFFD for lazy materialisation and copy-on-write.
Loom selects complete preparation, bounded windows or first-touch loading.
Views prepared for their intended access need no fault-handler round trip.
Backing can grow within reserved space without moving existing addresses.
Pointers remain local to their view; restart, relocation and representation
conversion use the object's own reference or rebinding scheme.

Avoiding COW is a choice about when to preserve or reconstruct bytes. Fresh
construction needs no beforeimage. For reuse of existing storage, the main
brief's exclusivity rule includes active hashing, persistence and transmission,
as well as mapped readers. New access must not race that decision. Late old
readers may pay reconstruction costs; keeping a beforeimage can be cheaper.

Logical independence does not imply physically independent storage. Two writers
sharing a page need separate working representations or compatible
application-defined effect application; installing or undoing a whole page must
not erase another writer's changes. Mapping latches protect short transitions
and must not span transaction or remote waits. Bytes sealed for verification or
I/O remain stable until those users finish.

## Loom bindings and allocation

Orbital discovers machine capabilities and creates threads, stacks, backing,
endpoints and kernel I/O facilities. Loom owns worker roles, affinity, work rings,
spin locks, polling or sleeping, batching and when I/O begins. Bindings expose
capabilities, actual completion and failure, letting Loom apply Engine's legal
plans without a second task scheduler in Orbital. Small operations should use
prepared resources directly.

The same backing and accounting machinery can serve scratch arenas and eligible
object storage. Loom owns residency, eviction and allocation policy; Orbital
provides reservation, mapping, protection and reclamation. Allocation and free
accept advisory cache-residence and reuse hints to reduce false sharing and
unnecessary eviction. Their benefit must repay any larger footprint, and hints
may be wrong. Neither a freed address nor a NUMA label proves cache warmth.
Durable allocation identity follows application semantics; physical address
choice must not affect deterministic results.

## Progress and lifetimes

A page fault can park an OS thread. Its resolver needs independently runnable
service capacity, bounded resident metadata and enough memory and I/O to advance
the admitted work. It must not depend on a lock held by the faulting worker.
Known long waits should release execution slots through continuations; an
arbitrary memory load cannot be assumed to suspend a coroutine safely.

Loom budgets working inputs, scratch, outputs, retained state and reconstruction
together. Windows, spill and replay are ways to fit work within that budget.
Refuse new work before accepting obligations which cannot be sustained.
After admission, a local resource failure must preserve the obligation for
recovery or resolve it through the agreed transaction rules. It cannot silently
change the snapshot, substitute zero bytes or independently choose a replicated
outcome.

An unrecoverable fetch leaves the view inaccessible and fails execution at a
supported runtime boundary. Native code without a safe fault boundary needs
prepared access or an execution container that can be retired and recovered.
Cancellation requests likewise do not retire users: queued I/O, active workers
and undrained completions keep their buffers and accounting. Closing a context
stops new submissions; destruction waits for existing users. Incarnations let
late completions release old resources without corrupting replacement work.

## Protection and execution

Page-table protection is the baseline for read-only extension views. Only
permitted bytes may be exposed, so shared pages may require a separate projection
or copy. MPK/POE may make transitions faster inside a controlled WASM runtime;
they require a suitable embedding, including host calls and asynchronous users.
Untrusted native code retains process/VM isolation. Approval to omit synchronous
determinism checks does not grant memory or I/O authority.

The hardened WASM runtime's imports and program-visible failure behavior belong
to its versioned execution profile. Local resource pressure may pause execution;
it must not change agreed results. The main brief owns native verification and
external-effect rules.

A trusted service creates resources and grants workers only the capabilities
they need. Restricted io_uring rings and pre-established resources can support
frequent I/O; ordinary syscalls remain useful elsewhere. Ring restrictions do
not enforce object/version byte ranges, and CPU read permissions do not constrain
asynchronous I/O. Requests need validated host operations or resources whose
extent matches the permission. A shared backing file must not expose objects
outside an extension's permitted view.

## I/O and IPC

Use io_uring where it improves complete workloads. Polling threads, registered
buffers, direct I/O and zero-copy each spend capacity or extend resource
lifetimes; configure them with Loom's budget. Prepare backend buffers for their
access, or provide an explicitly supported kernel-fault path. Userspace fault
support alone is insufficient. Check completion lengths and errors, and
interpret successful writes under the backend's persistence contract.

Logical endpoints survive physical relocation. Local IPC can pass descriptors
and leased memory through Loom's rings; remote destinations use transport.
Both paths preserve access rights, logical delivery and duplicate handling.
Orbital supplies shared memory and wait/wake facilities; Loom owns physical
queue ordering and work distribution. Pointer values are not portable message
identities, and local enqueue does not establish transaction completion.

The network direction includes WireGuard over IPv6 ULA, custom UDP transport
and adaptive MTU, packet and FEC choices. DMA and NUMA-aware placement are
physical options whose costs and lifetimes belong in the same resource model.
Detailed transport policies remain open to the continuing networking design.

## Platform adaptation

Capability discovery and measured costs guide physical plan choices without
changing application semantics. A stale binding must retain valid dependencies
or be rebound before use. Interpretation and runtime versions needed for
recovery remain available independently of local compiled caches.

Windows and macOS preserve the same object, protection and retirement rules
using native memory protection and asynchronous I/O or bounded blocking adapters.
Explicit loading and copies can replace unsupported fault-driven techniques.
Lower performance must not weaken access rights or durability. Firecracker
requires a Linux execution facility, supplied locally, in a Linux VM or remotely
as deployment policy permits.

The remaining experiments must establish reconstruction granularity, progress
under mixed load, safe and fast WASM view transitions, allocation-hint benefit
and portable behavior. Existing feature probes and small mapping tests
demonstrate individual mechanisms, not their composed performance.
