# Publication and retained material

This experiment asks where change-proportional publication moves its costs.
It supplements the [views spike](../README.md) and
[Calico/Ikea review](../prior-publication.md). Shared pages, blocks and afterimages
are generic material comparands, not a selected universal Orbital delta format.
Unit-specific damage, capture, encoding, reconstruction and meaningful content
checks belong with Ikea/owner code. Engine binds interpretation, descriptors,
indexes, summaries and isolation; Orbital preserves qualified recovery material
and source/outcome lifetimes and supplies transport; Loom budgets and schedules
bulk work.

`probe.cpp` checks exact bytes against an independent sequential byte-array
oracle. It retains every tested cut through later writes and compaction,
including overlaps, page crossings, empty effects and untouched guards. The
benchmark checks its latest and an older cut outside the timed work.

The coupled proof appends replayable afterimages to a real file and calls
`fdatasync` before advancing admission. A consumer constructs the admitted
material head while a bounded derived sink remains pending. A fresh `exec`
recovers epoch 3 before any sink succeeds; epochs 4–12 can follow. Failed,
duplicate, late and reversed sink completions do not establish the consumer
frontier. A partial epoch 13 does not become visible. Exact submitted cuts pin
material until their I/O tickets retire.
Epoch qualification here belongs to one serialized owner; cross-owner identity
and incarnation are outside the model. Reopen checks cover process restart and
an explicitly incomplete tail, not power-loss/sector-atomicity guarantees or
directory-entry durability.

This is **afterimage authority**, not an implementation of the design notes'
semantic intent interpreter. `Sink` models retained full-cut requests, capacity
and completion qualification; it does not itself write a physical sink. The
root-owned [native composition probe](../systems/README.md) connects actual
Ikea stores, damage, summary and this mechanism. Its ordinary working-image
semantics must not be inferred from the isolated material timings here.

## Comparands

All methods receive the same completed effects, capture owned immutable bytes,
publish the same cuts, and preserve the same retained-reader semantics.

| Method | Publication work | Debt or amplification |
| --- | --- | --- |
| Full snapshot | Copy whole preceding material, then apply effects | Whole-part copy even for one small change |
| Shared pages, flat index | Copy the page-pointer vector and changed 4 KiB pages | Metadata still scales with part size |
| Shared pages, two-level index | Copy root and changed 64-page groups; copy changed pages | 4 KiB copying for a dispersed byte store |
| Shared 256 B blocks | Copy changed groups/page headers/blocks | More pointers, allocations and initial metadata |
| Retained patch chain | Retain immutable afterimages and prior cut | Lookup, replay and history debt persist even without old readers |
| Patch chain, collapse every 8 | Periodically reconstruct a full base | Collapse work; old cuts can keep earlier chains/bases live |

Fully overwritten shared pages/blocks are formed directly from their new
bytes; they do not first copy an obsolete predecessor. Partial overwrites must
preserve untouched bytes. Individual allocations use the ordinary allocator;
this is not a comparison of tuned slab allocators or packed index formats.

Raw capture and same-epoch coalescing are paired. Coalescing preserves the last
store to each byte and merges overlapping/adjacent ranges. It reads only
reported store coverage, rather than scanning the whole part to discover a
diff. Coverage can exceed actual bit differences. Broader afterimages are safe
here because each epoch has one serialized owner; this does not justify replay
over independently newer preserved neighbours.

Certification uses the **same content computation** for every method:
256 B block hashes → 4 KiB page hashes → 64-page group hashes → root. A separate
full-snapshot cell recomputes every hash. Incremental cells reuse established
metadata and rehash covered blocks, changed pages/groups and the root vector.
The root vector still scales with part size; this is a two-level experiment,
not a fully logarithmic production tree. Initial certification always visits
every byte and is reported separately. A patch chain with no content metadata
must reconstruct material to produce that same root; a cheap hash of its new
records would answer a different question.

This certifies equality of a **byte cut**, not every unit's logical content.
[Bec256](../../../../ikea/src/bec256/codec.cpp) empty and full sets both have
zero-byte bodies; population metadata gives them opposite meanings. Its
[prepared encoding](../../../../ikea/include/ikea/bec256/codec.h) writes an exact
body while the owner coordinates population, length, location and summaries.
Existing Ikea effects describe stores, not a full delta/recovery API. A unit's
content check must bind the interpretation it requires.

The FNV-64 function represents byte/metadata work, not a chosen collision/security
primitive. Material roots use host byte ordering. Exact independent byte checks,
rather than hash equality alone, validate fixtures.

## Recovery and pressure

Recovery opens the retained seed plus complete contiguous afterimage frames.
Fixed headers and descriptor/checksum tables are validated while indexing;
payload blocks are read and checked on access. Corrupt metadata fails at open;
corrupt payload fails when accessed. A lazy open has not verified every byte.
No intent is regenerated from a workload, and no caller-provided current image
is needed to replay.

The lazy-start comparison indexes **the entire retained history**, then either
replays intersecting records forward or visits them in reverse until the
requested bytes are covered. Eager hydration uses those same paths for the
whole cut. Reverse coverage can skip overwritten payload, but adds per-byte
coverage bookkeeping and still searches record descriptors. It is not a
persisted interval index, bounded restart root, or proof that all recovery
should be deferred. Timings use buffered reads from recently written files;
they are not cold disk, direct I/O or remote-storage latency measurements.

Pressure cases retain only the latest cut, an old reader plus two stalled sink
requests, or every cut. They report material before and after those readers and
requests retire. This distinguishes externally pinned material from a current
patch chain's own reconstruction dependencies. Pending sink bytes are a
declared admission charge, not an estimate of unique shared backing bytes.
The proof refuses a third request before adding it to the bounded queue; it
does not claim that an unbounded authoritative journal is free. Reader cuts,
pending captures, reconstruction bases and recovery records all affect safe
retirement. Journal compaction/reader-floor policy remains outside this probe.

## Run and read

From the repository root on Linux:

```sh
SIXDB_RESULTS=build/publication-run \
  bash workbench/spikes/orbital-views/publication/run.sh
```

The standalone build uses pinned Clang 21.1.8. Default targeting is Zen5;
set `SIXDB_MARCH=armv8.2-a SIXDB_TUNE=generic` for the local ARM VM and prefix
Linux commands with `orb -m ubuntu` from macOS. The runner pins one CPU and
runs three forward/reverse pairs sequentially. Every case is reversed, not
just the size ordering. `PUBLICATION_TRIALS`, `PUBLICATION_EPOCHS` and
`PUBLICATION_MAX_BYTES` support a shorter run. Outputs remain under the chosen
ignored build/results directory; workers use the ordinary S3 artifact path.

Each trial flushes completed material groups and recovery cells. The summary
rejects missing/duplicate pairs, nonfinite measurements and counter drift.
It retains medians and observed trial ranges; these are not confidence bounds.

- `material.csv`: complete capture/coalesce/publication time, latest and retained
  64 B reads, payload/metadata/hash work, initial seed work and material size.
  Publication timings exclude fixture generation and journal I/O. Read timings
  include a 64 B checksum to make results observable. All `epochs + 1` cuts are
  retained in this timing comparison.
- `recovery.csv`: metadata-only open, first/warm 64 B reads, whole hydration and
  subsequent reads of the hydrated bytes, source payload and descriptor visits,
  and actual journal file size. Journal writing is setup; real write batching
  and sync costs belong to the sibling [systems probes](../systems/README.md).
- `pressure.csv`: requested live/peak material bytes and release after reader
  and sink retirement. Counts include retained data, captured records and index
  objects; exclude allocator/control-block overhead, spare capacity, transient
  scratch/read buffers, recovery indexes and the producer's working image.
  Copy/hash counts are explicit software work, not measured cache/DRAM traffic
  or physical device write amplification. Timings include scratch work.

The matrix spans 4 KiB, 64 KiB, 1 MiB and 16 MiB parts, 64 B sparse writes,
half-part dense writes, 128 dispersed byte stores, 64 overlapping 64 B stores,
and 64 adjacent 64 B stores. The shape, grain, retained epochs, certification
requirement and read pressure must accompany any winning timing.

## What the experiment already rules out

Whole-part copying, whole-vector metadata copying and whole-state hash recompute
are distinct costs. Removing one does not remove the others. For example, 128
dispersed byte stores over 1 MiB touch 512 KiB of 4 KiB pages, versus 32 KiB of
256 B blocks; the finer grain pays more metadata and allocations. Neither grain
is a universal winner.

A chain can retain a growing history even with only one latest reader. A
collapse can release that debt for the current cut while old readers and I/O
retain older bases. Coalescing 64 overlapping 64 B stores reduces 4096 B and
64 records to 79 B and one record, but sorting and copying still have a front
cost. It needs downstream savings to justify that cost.

Carry forward known coverage, completed capture frontiers, reuse of unchanged
material/metadata and visible reconstruction debt. The data unit determines
valid changes; the complete costs determine batching and material grain.
Qualified recovery input and consumer publication must remain usable while a
derived physical sink catches up.
