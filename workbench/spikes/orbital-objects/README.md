# Orbital objects and OS services

Explore durable objects as memory, physical protection, resource progress,
allocation and the Orbital/Loom/Engine boundary. This is preliminary design
and focused prototyping before detailed design and the high-fidelity simulator.

- [Design proposal](DESIGN.md): object/view separation, mapping choices, COW
  elision, module attachment points, allocator and thread/IPC ownership.
- [Worked scenarios](SCENARIOS.md): HTAP, ELT and other applications, including
  late readers, shared pages, conversion and resource exhaustion.
- [Source audit](SOURCES.md): mining material, implementation discrepancies and
  primary OS/runtime references.
- [Retained evidence](evidence/README.md): exact inputs, source hashes and limits.

The initial recommendation is ordinary memory within a version-bound view,
bounded preparation with first-touch fallback, and private mutation projections
with safe reuse of exclusively owned frames. Loom chooses resources and
scheduling; Orbital provides the backing, protection, I/O and lifecycle services.
Application meaning and legal transformations remain above both.

## What the first probes actually establish

[probe.py](probe.py) is a deterministic design probe using authored workloads.
It does not emulate a kernel, CPU, consensus implementation or real network.
It retains logical counts rather than invented microsecond predictions:

- **24 small installation histories** vary two disjoint logical updates on one
  page, resolution order and commit/abort decisions. Naive whole-page installation
  is incorrect in **5** histories. The serial reference constructs the correct
  result from resolved scoped effects; it is not an independently tested replay
  implementation. The owner supplies the effects in this probe.
- **90 mapping cases** compare complete population, one-page demand faults,
  aligned eight-page fetch windows and exact owner-described access windows,
  with capacities of 16, 64 and 256 pages. Eight access patterns include scans,
  known sparse gathers, pointer chasing and repeated hot data.
- **48 copy/reconstruction comparisons** expose old-reader frequency, aborted
  pages and replay depth. They count eager copied pages versus reconstructed
  pages/operations. They do not identify a universal crossover or model shared
  replay caches, concurrent readers, fault-handler CPU or retention capacity.
- Eight exhaustive small resource models vary independent fault-service capacity,
  spare reconstruction memory and a resolver lock held by a parked caller. Only
  the combination with capacity, memory and no lock cycle completes; a dedicated
  handler alone does not suffice. Further counterexamples show retained-output
  deadlock and premature cancellation reuse. A coherence toy compares compact and owner-separated scratch allocations;
  its transfer counts are not measured cache misses or a selected allocator.

For a sequential 256-page scan in a 16-page cache, demand access incurs 256
faults/batches; eight-page windows need 32. Exact known windows have zero demand
faults, although the same 256 pages still load. Full-object population refuses
because the requested pin does not fit. For 32 accesses spaced eight pages
apart, the same eight-page guessing policy loads 256 pages versus 32 on demand.
This supports having more than one preparation policy, not a fixed window size.

No overlap or timing is modeled: a batch count alone cannot predict throughput
or tail latency. Exact windows receive application knowledge which a dependent
pointer chase lacks. Small-object eager loading remains a valid comparator;
objects larger than the resident budget need a bounded path.

The Linux [capability probe](capabilities.cpp) was built with the pinned Clang
21.1.8. On the local aarch64 OrbStack guest it verified that a write through a
read-only mapping traps, queried usermode UFFD features, and successfully
registered a NOP-only restriction on a disabled io_uring instance. Missing,
minor and WP support on shmem were advertised. It does **not** execute UFFD
materialisation/COW, submit ring operations, test MPK/POE, prove sandboxing or
measure production performance.

The separate [view probe](views.cpp) executes real UFFD missing-page supply,
minor-fault mapping and write-protect COW. All three paths passed locally. COW
preserved both an existing old reader and a reader first opened after the
write. A fourth case destructively reuses an exclusive projection, reconstructs
an old view from a toy base/operation, then discards the tentative view on abort.
Its reconstruction recipe is held in process memory; it tests the mapping
separation, not durable logging or crash recovery. Each case runs in a child
with a five-second timeout; unsupported optional paths are reported explicitly.
These one-page tests establish no contention, throughput or race coverage.

## Reproduce

From Linux at the repository root; prefix commands with `orb -m ubuntu` from
the Mac:

```sh
python3 workbench/spikes/orbital-objects/probe.py
cmake -S . -B build/orbital-objects -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++-21 -DSIXDB_SPIKES=orbital-objects
cmake --build build/orbital-objects --target orbital_objects_capabilities orbital_objects_views
build/orbital-objects/workbench/spikes/orbital-objects/orbital_objects_capabilities
build/orbital-objects/workbench/spikes/orbital-objects/orbital_objects_views
```

`probe.py --output PATH` writes the full model results. The native probe creates
temporary anonymous memory, a short-lived child and kernel descriptors; its
child intentionally faults, with core dumps disabled. It changes no machine
configuration and treats unavailable optional facilities as observations.

## Experiments still needed to choose implementations

The next useful discriminator extends the real projected-object path: population and
COW/reuse, a late old reader, partial overwrite, abort and delayed backend use,
all under finite memory. Compare full preparation, bounded lookahead and demand
faults at the same logical read/write workload. Include a slow page source and
blocked worker/allocator cases. Retain both reader and independent-writer tails,
copied/replayed bytes, admitted/refused/unfinished work and recovery dependencies.

RO WASM entry/exit needs a separate embedding probe with unauthorized neighbor
bytes, nested host calls and asynchronous I/O. Allocator hints need a mixed
foreground/bulk measurement against ordinary allocators and simple per-owner
reuse, including stale hints and memory footprint. These are open experiments,
not a checklist completed by capability detection or this small simulator.
