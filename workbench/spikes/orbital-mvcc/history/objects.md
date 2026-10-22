# Durable object composition findings

Historical comparison, retired October 22, 2026. Current interfaces belong in
[Orbital material](../../../../orbital/docs/material.md); current consequences
run in the [Object integration checks](../../../benchmarks/retired-drivers.md#current-integration-checks).
The findings below retain their original workload and limits. [Recovery](README.md#exact-recovery)
pins the complete source; the standalone Linux probes are described at the end.
The [original brief](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/BRIEF.md)
records the alternatives and intended guarantees.

The useful boundary is a prepared object operation built from independently
retained state and physical bytes. Share stable material, replace changed
material when a borrower needs its old bytes, and permit exclusive reuse through
the same interface. The native experiment supports this direction; it does not
select production types or a transaction protocol.

## What the caller establishes

The [caller](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/composition_fixture.h) uses a directory and approximate signatures
to select three bodies from two pages, rejects a false positive, and publishes
a 64-byte output and an eight-byte summary. Independently formed expected bytes
check both the result and the surrounding representation. All strategies execute
ordinary loads and stores and install those actual stores.

Four obligations are useful to distinguish:

| Obligation | What survives |
| --- | --- |
| Retained state | The intended logical position and material needed to obtain it, independently of resident bindings. |
| Prepared view | Selected native windows, with their exposure and stability promises. |
| Physical use | The captured backing and byte window until the last CPU/backend borrower finishes. |
| Working change | Issued writes and their eventual publication, retry or abort disposition. |

An operation facade can compose these. Scratch and output services need physical
uses without a durable identity. Closing an object context stops acquisition but
does not release another user's captured bytes. A generation detects changed
bindings; it does not identify historical state.

The [probes](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/composition_probe.cpp) expose several consequences:

- A read-only projection is itself a physical user. Keeping its allocation alive
  did not prevent reuse from changing still-readable aliases; retaining its
  stability window does. A narrow backend borrow can share that record, but then
  keeps the broader projection window alive and may force a disjoint write to copy.
- Native padding belongs to the promised window. A 47-byte reader permits an
  append beyond byte 47; a reader admitted for 64 bytes requires those extra
  bytes to remain stable too. Permission and stability are separate requirements.
- Disjoint writers sharing a page need range-aware installation and undo.
  Rebasing actual issued stores preserves intervening publications; replacing or
  restoring an entire stale page does not. An overtaken rebase fails without
  losing staged work and can retry or abort.
- Slow preparation retains selected material and a weak identity check, rather
  than the whole resident image. Source I/O runs outside the owner latch;
  unrelated resident operations progress, and late completion cannot bind into
  a replacement image.

For synchronous abort, this prototype saves and charges narrow local undo
slices before allowing reuse. This is a policy choice, not a requirement for
every Orbital binding: another policy could revoke contaminated access and
reconstruct later, while preserving the recovery obligation. Volatile undo does
not solve process loss. Cache occupancy and undo share the total budget; active
foreground use also leaves a service reserve. A full cache can deny undo even
when a cached replacement fits. Automatic cache eviction is not implemented.

## Native comparison

[Retained evidence](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/evidence/zen5-composition/README.md), October 2, 2026:
AMD EPYC 9R45 on a fresh c8a.large, Clang 21.1.8, 4 KiB pages, one pinned CPU.
Each cell is the median of six **trial mean** times, three repetitions in each
case order. These are neither latency percentiles nor throughput under contention.

The timed operation includes the caller, actual output validation, memory
publication and retirement. Preparation of the initial resident pages is outside
timing. The eight-page cache is prewarmed equally and remains charged.

| History, with prepared cache | Private image | Shared backing | Exclusive reuse |
| --- | ---: | ---: | ---: |
| Resident inputs | 1.79 µs | 1.51 µs | 1.28 µs |
| Captured old changed output | 1.80 µs | 1.51 µs | 1.44 µs |
| Late old changed output | 2.19 µs | 1.91 µs | 1.68 µs |
| Cached-file inputs and local socket output | 3.24 µs | 2.94 µs | 2.86 µs |

Resident application-copy work is 16 KiB, 8 KiB and 72 bytes respectively;
initialization zeros add another 16 KiB, 8 KiB and zero. Active backing peaks at
32, 24 and 16 KiB. With the fixed cache, all retain 32 KiB total backing, and
reuse additionally charges its 72-byte undo. These figures exclude metadata
heap, the snapshot file's cache and external buffers; they are not RSS.

An old output borrower makes reuse replace that page, increasing copy work to
4,104 bytes and active backing to 20 KiB. Its advantage over shared backing
then shrinks to about 0.07 µs. All strategies fetch a complete historical page
for late old opens: this comparison does not exploit an old-image resident cache.

Without prepared caching, resident means are 17.12, 11.30 and 6.07 µs.
Mapping, unmapping and hole-punching on each operation outweigh the copying
differences. Cached free pages remain real capacity, not reclaimed memory.
The prepared native filter alone takes 8.80 ns; it excludes output construction,
acquisition and publication. The gap points toward acquiring useful execution
windows together, then performing ordinary native work without repeated grants.

## Scope and remaining uncertainty

4,677 checks pass locally and on Zen5; local ASan/UBSan passes too. Coverage
includes delayed actual sends, context replacement, mixed-page protection traps,
stale acquisition, publication races, source failure and capacity denial partway
through preparation, and cleanup after mapped-projection construction failure.

The substrate uses page-sized memfd homes, not the tuned descriptor slot
allocator. Protected projections are correctness-tested; the timed caller is
trusted to obey declared write ranges. The file/socket case uses cached `pread`
and same-thread AF_UNIX send/receive; the delayed-send probe pauses before an
ordinary copying syscall. Neither measures remote transport or kernel zero-copy.

Historical material consists of two full snapshots synced before measurement.
Publication installs memory under supplied outcomes. There is no measured durable
commit, delta replay, replicated admission or process-loss recovery here.

Moving source I/O outside latches establishes progress under an injected delay,
not size-independent interference. Undo copying/restoration and whole-image
metadata work still occur under owner locks; the pool serializes mapping and
initialization. The next useful refinement is an operation-scoped acquisition
of selected bindings and backend uses, with independently retained reconstruction
material. Its metadata cost and integration with descriptor allocation need
measurement before adopting a wider type hierarchy.

## Standalone Linux fault-path probes

The retired tree also contains two independent runnable specimens:

- [views.cpp](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/views.cpp)
  actually supplies a missing shmem page, maps a minor fault and performs
  write-protect COW with an existing and late old reader. Its fourth case
  reuses an exclusive projection, reconstructs from an in-memory recipe and
  aborts. These are bounded native fault paths, not durable reconstruction.
- [capabilities.cpp](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/capabilities.cpp)
  queries available UFFD features, checks a read-only protection trap and
  accepts a NOP-only restriction on disabled io_uring. It does not exercise
  the advertised fault paths or establish sandboxing.

[Native observations](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/evidence/README.md)
retain kernel/platform qualifications and exact source/output identities.
Current production and the later embedded-view work do not supply UFFD.
Recover either specimen independently, from the repository root:

```sh
mkdir -p build/recovered/object-fault-probes
git show 35e63b8416d377f216575d6f8d97668b9bce9ab3:workbench/spikes/orbital-objects/views.cpp \
  > build/recovered/object-fault-probes/views.cpp
git show 35e63b8416d377f216575d6f8d97668b9bce9ab3:workbench/spikes/orbital-objects/capabilities.cpp \
  > build/recovered/object-fault-probes/capabilities.cpp
```

The [original build instructions](https://github.com/ashtonsix/SixDB/blob/35e63b8416d377f216575d6f8d97668b9bce9ab3/workbench/spikes/orbital-objects/README.md#prototype-and-checks)
and complete checkpoint recover the composition separately. Missing optional
kernel facilities remain unsupported outcomes, rather than successful tests.
