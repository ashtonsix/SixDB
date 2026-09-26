# Physical runtime source audit

Read-only source review, 2026-09-26. These are mechanisms and counterexamples
worth mining, not inherited SixDB requirements. Implementation status below
describes the inspected sources; it does not establish production readiness.
Platform observations belong in this spike's evidence, not in this source map.

## S1 — Original direction and its limits

Start with the [Calico reference map](../../notebook/calico.md),
[Orbital mining list](../../../orbital/MINING.md), and the
[early narrative](../../../orbital/stale-drafts/BRIEF.md), especially its
sandboxing and ordinary-memory paragraphs. The latter proposes restricted
io_uring rings, UFFD, unique-reference COW elision and remapping container
growth. Its syscall policy and performance expectations are proposals, not
current contracts. [Consurgent's script](../../../../consurgent/pitch/SCRIPT.md)
explains the original alloc/free/persist programming model; it is a pitch,
not an implementation coverage map.

The important distinction for the new design is between durable logical
objects and their replaceable physical projections. Ordinary pointer access
does not remove the obligations to authorize, retain, materialize and retire
the referenced version.

## S2 — Fault interception, COW and access control

[fault.h](../../../../calico/xmem/include/xmem/fault.h), its opening path
catalogue and `service_minor`/`service_wp`, implements lazy shmem mappings and
WP-triggered copying to a fresh backing offset. The handler populates through
an unregistered alias, avoiding recursive interception through that alias.
Claim enforcement is separately described as `PROT_READ` plus `mprotect`,
assuming trusted native actors. Remote fetch is a hook, not a completed remote
fault service; facade lazy-open integration remains unfinished in the
[backlog](../../../../calico/xmem/BACKLOG.md).

UFFD is fault interception and resolution machinery, not a complete
confidentiality boundary. A permitted object sharing a mapped page with
forbidden bytes needs an additional boundary or different representation.
The [kernel documentation](https://www.kernel.org/doc/html/latest/admin-guide/mm/userfaultfd.html)
also requires feature checks for the memory type and registration mode.
The prototype opens UFFD without `UFFD_USER_MODE_ONLY`; that asks for broader
fault-handling permission than a userspace-only backend needs.

Several handler error paths increment a counter and issue `UFFDIO_WAKE`
without resolving the mapping or protection. Waking alone does not establish
terminal progress: the instruction can fault again. `ResolveFn` cannot report
failure, and the default materializer supplies zeros. Reusing this seam for
retained data requires distinguishing valid zero content from unavailable or
corrupt content.

A dedicated handler is useful but insufficient for deadlock freedom. A worker
can fault while holding a resolver-needed lock; resolution can allocate or
wait for completion on an exhausted pool. Those are compositional risks, not
observed failures in this audit. Test bounded fault storms and failure paths,
with control/completion work independently runnable.

## S3 — Residency, references and historical obligations

[serving_image.h](../../../../calico/xmem/include/xmem/serving_image.h) explicitly
separates the mutable discardable image from recovery evidence. Its verified
immutable checkpoint adapter is a different source type.
[pins.h](../../../../calico/xmem/include/xmem/pins.h) implements local frontier
and exceptional range/version pins. In
[xmem.h](../../../../calico/xmem/include/xmem/xmem.h), `OpenSpec` accepts only
the currently mapped frontier; it does not implement arbitrary historical
reopening. The [backlog](../../../../calico/xmem/BACKLOG.md) leaves distributed
retirement and physical-region reopening unfinished, despite the broader
architecture described in [DESIGN §§11–12](../../../../calico/xmem/DESIGN.md).

Local reference uniqueness can help establish physical exclusivity. It does
not establish that an older snapshot, replay or remote reader no longer needs
the previous logical version. Conversely, preserving an old version does not
require keeping every physical projection resident. Eliding a copy needs both
exclusive physical use, including outstanding I/O, and retained means to serve
every outstanding logical obligation. `MemfdBacking::punch_hole` in
[arena.h](../../../../calico/xmem/include/xmem/arena.h) deliberately leaves that
eligibility proof to its caller; reads of punched backing otherwise see zeros.

## S4 — Runtime hardening and protection keys

[omachine SCOPE](../../../../calico/omachine/SCOPE.md) labels Wasmtime hardening,
Firecracker, cgroups and io_uring as candidates; its
[backlog](../../../../calico/omachine/BACKLOG.md) does not supply a completed
extension runtime. Historical CIDR trust, actor addressing and directive ABI
choices are not carried forward.

WASM isolation and deterministic execution are separate properties.
[Wasmtime's determinism guide](https://docs.wasmtime.dev/examples-deterministic-wasm-execution.html)
identifies host imports, NaNs, relaxed SIMD and resource-dependent growth as
controls. Scheduling interruption is also distinct from a deterministic
program-visible limit. A versioned runtime profile must cover these choices;
host capabilities must still validate the referenced object and operation.

[Linux protection-key documentation](https://www.kernel.org/doc/html/latest/core-api/protection-keys.html)
describes thread-local, user-writable permissions and small key namespaces:
16 x86 keys and 8 arm64 POE keys. Arbitrary native instructions cannot be
confined simply by setting a restrictive key register. Furthermore, io_uring
kernel workers use default key-register state, not the submitting thread's
view. Keys are a possible fast transition within controlled execution, not a
replacement for capability checks, actual mapping permissions or asynchronous
buffer ownership. Hardware and kernel availability require probing.

## S5 — Restricted I/O is not automatically object-scoped

The [liburing restriction interface](https://man7.org/linux/man-pages/man3/io_uring_register_restrictions.3.html)
limits opcodes, registration operations and SQE flags. Restrictions are
installed before enabling a disabled ring. This supports the early brief's
broker-provisioned ring idea, but does not itself validate object identity,
version or allowed byte offsets.

Counterexample: an extension receives the shared serving-image FD in an
immutable registered-file slot. Its CPU view exposes one authorized object,
but an allowed file-read operation can name another object's offset. Either
the underlying resource must match the capability's granularity or a trusted
broker must validate finer-grained requests. Keeping ordinary syscalls on a
restricted path does not resolve this difference. Ring access also needs
independent buffer lifetime and direction checks; a protection-key setting is
not that proof.

## S6 — Thread, IPC and asynchronous retirement

[omachine CONTRACT](../../../../calico/omachine/CONTRACT.md), shared transaction
handles and directive execution, contains useful participant-drain and
worker-loss cases. Native/WASM mapping enforcement and buffer ABI details
remain open. Its same-actor, same-machine partitioned working view is not a
proof for arbitrary extension sharing.

The more concrete reusable lifecycle is in
[Loom DISK](../../../../calico/loom/spec/DISK.md) and
[disk.h](../../../../calico/loom/include/loom/disk.h): admission charges queued,
running and completed-but-undrained operations; cancellation is advisory;
shutdown joins and drains before destruction. [Loom IO](../../../../calico/loom/spec/IO.md)
separates source incarnation from operation generation, preventing an old
completion from publishing under a reused identity. Residency holds and
semantic version pins are explicitly different.

Apply that distinction to shared buffers, IPC and DMA: cancellation, timeout
or revocation of new access does not prove that existing access has ended.
Retirement follows completion/quiescence, and must remain bounded when the
consumer disappears. Microsoft documents the same cancellation distinction
for [CancelIoEx](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex).

## S7 — Portable semantics, optional platform acceleration

Explicit asynchronous loading, explicit copies and read-only shared views
provide a baseline worth comparing with Linux fault-driven execution.
[Windows mapping views](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-mapviewoffile)
support read-only and private COW access, but modified private pages are not
automatically durable versions and COW views incur commit charge.
[Apple's Mach VM interface](https://github.com/apple-oss-distributions/xnu/blob/main/osfmk/mach/mach_vm.defs)
provides mapping, copying and protection primitives. Neither source establishes
equivalent UFFD delegation, sandboxing or performance. Platform capability
acceptance is narrower evidence than successful fault handling, confinement
or safe teardown under races.

## S8 — Allocation/cache hint: search limitation

The exact remembered cache-residence-aware alloc/free hint note was **not
located**. Searches covered current SixDB Orbital/notebook files, Calico
xmem/runtime/design sources, and Consurgent pitch/archived notes, followed by
a broader allocation/free plus cache/hint search across Markdown in the three
checkouts. This was not an exhaustive Git-history search.

Closest sources are [xmem DESIGN §2](../../../../calico/xmem/DESIGN.md), LIFO
exact-run reuse retaining prior bytes, and `alloc` in
[xmem.h](../../../../calico/xmem/include/xmem/xmem.h), which calls reuse
“cache-hot by construction.” [alloc_reuse_bench.cpp](../../../../calico/xmem/tools/alloc_reuse_bench.cpp)
tests immediate reuse, not guaranteed residence under competing workloads.
[Consurgent's older design](../../../../consurgent/archive/notes-v0/design.md)
similarly overstates protection-key handover as avoiding main memory.

Treat locality as an optimization hypothesis: hints can guide reuse without
promising that cache lines remain resident. Such hints must not change
semantic lifetime, replay retention or the need to clear old bytes when
crossing confidentiality boundaries. Do not substitute these nearby sources
for the still-missing exact note.
