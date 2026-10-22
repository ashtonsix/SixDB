# Memory descriptors

[Orbital research map: Allocation](../../notebook/orbital/reference/README.md#allocation)

Can arena-selected palettes and one-byte trampolines make independently growing
reservations cheap to locate, retain and reclaim?

## Read by topic

| Topic | Existing document |
| --- | --- |
| Original descriptor geometry | [Starting brief](BRIEF.md) |
| Addressing, placement, lifetime and native access results | [Findings](FINDINGS.md) |
| Allocation mechanisms, page ownership and retirement choices | [Allocation study](ALLOCATION.md) |
| Group sizes, packing and allocator comparisons | [Grouping extension](EXTENSION.md) |
| Occupied sets and allocation/free performance | [Tuning](TUNING.md) |
| Independent growth, spare capacity and retained raw reads | [Virtual growth](VIRTUAL.md) |

These documents distinguish measured results, modeled costs and open choices.
Each report links its own retained evidence.

## Prototype and source

The finite prototype implements palettes A/B, sixteen identities per grid and
sixteen homes per class. Small A cohorts share a fixed page; larger cohorts can
have private page ranges. Two control-page layouts compare dense trampoline
planes with a cache-line record containing trampolines and nearby metadata.
The side metadata is illustrative; it does not assign module interfaces.

```text
descriptor ─► trampoline byte ─► arithmetic address ─► current home
retained raw read ──────────────────────────────────► previous home
```

The original `arena` probe separates descriptor retirement from raw-read
completion and gates mutation through an experimental owner registry. The tuned
`grouped` allocator instead requires quiescence before release. These and the
virtual-growth comparison are separate implementations. The historical
[object/view composition](../orbital-mvcc/history/objects.md) used its own page-sized
memfd homes; it did not call these allocators.

| Material | Question |
| --- | --- |
| [arena.h](arena.h), [arena.cpp](arena.cpp), [probe.cpp](probe.cpp) | Original geometry and lifetime reference: mapping, relocation, captured old backing, native windows and COW. Latched operations and delayed-reader tests do not establish general concurrent acquisition or ABA safety. |
| [bench.cpp](bench.cpp) | Native scan, point, dependent and joint metadata access, with an optional concurrent metadata writer. |
| [placement.py](placement.py) | New-grid selection, page sharing and within-grid compaction over deterministic histories. |
| [small-packing.py](small-packing.py) | Small homes interleaved within affinity groups or across the control-page population. |
| [lifecycle.py](lifecycle.py) | Bounded interleavings and failure witnesses for acquisition, retirement, uniqueness and publication. |
| [grouped.h](grouped.h), [grouped.cpp](grouped.cpp), [probe](grouped_probe.cpp), [benchmark](grouped_bench.cpp) | Tuned finite-allocation candidate: packing widths 16–1,024 and prepared alloc/free. Serialized; no retained old homes, relocation or concurrent acquisition. Includes modeled 16 KiB granules, not measurements on 16 KiB hardware. |
| [allocation_bench.cpp](allocation_bench.cpp), [allocator_libraries.cpp](allocator_libraries.cpp) | Local/shared pools, regions and explicit allocator-library competitors. |
| [occupancy.cpp](occupancy.cpp), [tuning_bench.cpp](tuning_bench.cpp) | Occupied masks/lists and matched allocation/churn/accounting comparisons. |
| [virtual-growth.cpp](virtual-growth.cpp) | Serialized growth comparison with retained mappings. Movable candidates update a test owner index; they cannot update copied direct descriptors. Virtual placement remains open. |
| [packing-sweep.py](packing-sweep.py), [retirement-policies.py](retirement-policies.py) | Independent packing/ownership/protection choices and retirement progress. |

`grouped` page liveness counts its occupied homes. Retained or pending homes
would need accounting before reuse or trim; trim also allocates scratch, so
pressure-safe reclamation is unproved. These guarantees are not supplied merely
by sharing descriptor vocabulary with the other probes.

## Run

From Linux at the repository root (prefix commands with `orb -m ubuntu` on Mac):

```sh
cmake --preset dev -DSIXDB_SPIKES=memory-descriptors
cmake --build --preset dev --target memory_descriptors_probe memory_descriptors_bench \
  memory_descriptors_grouped_probe memory_descriptors_grouped_bench memory_descriptors_allocation \
  memory_descriptors_occupancy_probe memory_descriptors_occupancy_bench memory_descriptors_virtual_growth
ctest --test-dir build/clang/dev -R memory_descriptors --output-on-failure
python3 -B workbench/spikes/memory-descriptors/placement.py
python3 -B workbench/spikes/memory-descriptors/small-packing.py --output build/small-packing.json
python3 -B workbench/spikes/memory-descriptors/lifecycle.py
python3 -B workbench/spikes/memory-descriptors/packing-sweep.py --output build/packing-sweep.json
python3 -B workbench/spikes/memory-descriptors/retirement-policies.py --output build/retirement-policies.json
```

The [worker script](run.sh) runs two orders and five repetitions, with and without
a second-core metadata writer. Use the [worker helper](../../tools/workers.md)
with `--machine zen5 --instance-type c8a.large`. The
[summary script](summarize.py) preserves order effects; retained CSVs keep every
candidate and repetition. Model JSONs retain counts and counterexamples.
The [extension worker](extend.sh) downloads hash-pinned mimalloc/snmalloc sources
through [allocator-deps.py](allocator-deps.py), then runs both orders and three
repetitions. Library competitors are optional for local builds; pass their
source directory as `SIXDB_DESCRIPTOR_ALLOCATOR_DEPS`. The extension's
[summary](extension-summary.py) retains every candidate and order effect.
The [tuning worker](tune.sh) recovers the frozen implementation by artifact hash;
[tuning-summary.py](tuning-summary.py) checks coverage, logical outcomes and
footprints across both orders.
