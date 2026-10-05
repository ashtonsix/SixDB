# View residency and binding costs

This bounded study compares ways to bind, mutate, retain and release useful pages.
It complements the [TLB study](../tlb/README.md) and the owning views spike; it
does not choose Orbital's representation, publication or allocation contracts.

## Comparison

Each binding episode opens version 0, reads the first 64 bytes of each selected
page, acquires a draft, increments those bytes, accesses the old version if
requested, makes a second draft/version, and releases all active images.
Initial preparation is inside the episode. Full-page expected bytes, both
updates and old-reader isolation are checked independently.

| Method | Binding mechanism |
| --- | --- |
| `page_copy` | PROT_NONE reservation; per-page anonymous replacement, copy, rights call and set insertion |
| `pool_rebind` | Pooled reservation with the same per-page work, whole-window protection reset and anonymous restoration |
| `run_copy` | Reservation with contiguous replacement/copy runs and a ready bitmap |
| `file_lazy` | One private file mapping; useful access takes the faults |
| `file_runs` | Private file mappings for selected contiguous runs |
| `file_populate` | One private file mapping with MAP_POPULATE |
| `anon_eager` | Whole anonymous home explicitly cleared before selected-page copying |
| `retained_copy` | Already mapped anonymous RW home, selected-run memcpy, ready bitmap |
| `retained_take` | The same, with exclusive reuse of a published image if no reader still holds it |

Trusted published images have semantic read authority while their physical pages
remain RW. Taking requires sole image ownership; a held old reader forces a
copy. No untrusted caller is given this trusted capability. Page geometry is
shared across all methods: four pages, 256 dense pages, or 16 selected pages in
a 4096-page part, either contiguous or scattered. Reader modes are absent,
held, fresh historical mapping, already pinned source mapping, and one fresh
historical mapping every eight episodes.

**These are binding episodes, not durable publication timings.** Two sealed
immutable memfds containing versions 0 and 1 are constructed before the episode.
Version 1 is an independently generated expected source, not bytes published by
the measured mutation. Authority/version transitions are timed; creating new
recoverable material, journaling and syncing are outside this comparison. Source
population and pool preparation have a separate `seed_ns`; `seed_copied_bytes`
counts the source bytes transferred by pwrite. The wider spike measures the
actual publication path and supplies an end-to-end composition.
The source mappings are touched during seeding. File-method faults therefore
exercise resident memfd material and private COW, not storage reads.

## Resource and mask probes

`memory.cpp` keeps separate, small comparisons for:

- 1 MiB, 64 MiB and 1 GiB reservations with 1 MiB touched, including explicit
  whole-home preparation and THP advice. RSS, PSS and actual AnonHugePages are
  observed; advice does not promise a huge page.
- 32 or 256 independently pinned sparse immutable sources: each reserves 64 MiB
  but populates only 16 pages. FD construction, source writes, mapping and first
  reads are included. Retained sources/mappings consume backing resources, VA
  and VMAs; observed RSS prices residency here. Ordinary mappings do not lock
  pages into RAM, and this probe uses neither `mlock` nor `MAP_LOCKED`.
- Growth from 1 MiB to 16 MiB inside a 64 MiB reservation, copy with a live old
  reader, or potentially moving mremap after all captures are dropped. Pointer
  stability and preserved old contents are checked.
- Partial-page input/output projections: the same 512 input and 256 output bytes
  per iteration. Private variants expose eight RO and eight RW pages, zero hidden
  bytes, and import only the permitted output spans. Contiguous rights changes
  are compared with per-page calls. A fixed-mask variant clears only RW pages;
  its RO pages retain zeros outside the overwritten input spans.
- Retain, MADV_DONTNEED over touched runs or the whole reservation, and MADV_FREE
  over touched runs, for 16 or 256 touched pages in a 64 MiB home. Release,
  RSS immediately after release, and the next first read/write are separate.
  MADV_FREE may preserve bytes or return zeros; discarded bytes carry no logical
  state. This is reclamation mechanism evidence, not pressure-induced eviction.

Projection checks include an actual RO write fault, hidden-byte checks, exact
destination bytes and a worker overwriting its entire RW page. The importer
still admits only its output spans. Core dumps are disabled for intentional
faults. This demonstrates private projection and PTE rights, **not a native-code
sandbox**: these processes can still access unrelated host memory and syscalls.

## Reading results

The runner uses pinned Clang 21.1.8, Release `-O3 -g`, independent translation
units and ordinary memcpy. Six serial trials reverse case order, with 64 episodes
per cell and a fresh process for each cell. Timed processes are pinned individually;
the worker controller retains its CPU affinity. Three resource trials follow.
`summarize.py` checks coverage, useful-copy counts and stable-home VM operations,
then produces `comparisons.csv` and `memory-comparisons.csv`.

Pipeline phases are per episode. Logical operation/byte counts and minor faults
are totals over the cell; kernel COW copying is not an explicit memcpy count.
`pool_bytes` is reserved virtual capacity, not resident memory. Pipeline peak RSS
includes immutable-source population and transient seeding buffers; use resource
probe RSS/PSS deltas for the separate reservation questions. Sampled VMA counts
are not a guaranteed lifetime maximum. Resource timings exclude final teardown,
although their operation counts include the final unmap. Projection fault/resource
counts include its independent checks. Metadata reads between trim and reuse
are outside both timed phases.

Prepared and retained describe acquisition provenance. None of these trials
establishes an L2/LLC/DRAM temperature. Base-page tests request NOHUGEPAGE where
specified; resource probes report actual huge-page residency. Linux page size,
THP policy, overcommit/limits, allocator placement and CPU/kernel behavior all
matter. This serial study cannot quantify cross-core shootdowns or contention;
the TLB study measures those separately. Background reclamation can still disturb
other workers sharing the address space.

Run from the Linux repository root:

```sh
SIXDB_RESULTS=build/spikes/orbital-views/residency-results \
  bash workbench/spikes/orbital-views/residency/run.sh
```

On local AArch64, set `SIXDB_MARCH=armv8.2-a SIXDB_TUNE=generic`; the default worker
target is x86-64-v4/Zen5. `SIXDB_CPU` and `SIXDB_BUILD_JOBS` are optional. The runner
provisions nothing; worker provisioning and output retention belong to the
existing workbench tooling.

The simple candidates are a mapped anonymous home and batched ordinary copies
for trusted work, optional exclusive take when old versions remain recoverable,
and private sanitised projections for untrusted work. Large lazy reservations
can preserve growth addresses while touched coverage controls physical cost.
Keep reclamation off the acquisition/release critical path when retention fits;
batch reclamation using actual prepared/touched coverage. Avoid turning every
measured alternative into a production policy or assuming whole-part eager
preparation is worthwhile for sparse access.
