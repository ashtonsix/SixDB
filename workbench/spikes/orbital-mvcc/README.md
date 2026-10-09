# Orbital MVCC

How can coherent versioned access, representation changes and short native calls
fit together below LMDB's latency? Performance, metadata ownership and interfaces
must be measured and changed together, while preserving the actual lifetime and
publication behavior. Engine and Ikea's future multi-block containers remain
outside this investigation.

The first Stage 2 implementation was withdrawn from the modules on 9 October
2026. Its performance comparison came after implementation and left material
deficits, so it is a **rejected experimental baseline**, not an architecture to
finish optimizing. Its source is retained here to expose those choices to
comparison. The original cut is `7f73c818548c9c52b53818f2eb5af3c1d2451124`;
the older measured cut and exact source digest remain with each captured run.

| Area | Current material |
| --- | --- |
| Baseline Objects | [Behavior](baseline/objects.md), [structure](baseline/structure.md), [header](baseline/include/orbital/object.h), [implementation](baseline/src/internal.h), [histories](baseline/test/object.cpp) |
| Actual Loom composition | [Caller lifetimes](callers/README.md) and [executable histories](callers/loom.cpp) |
| Disposable format consumer | [SeriesPack columns ↔ TuplePack rows](consumer/README.md), with four Parts ↔ one Part |
| Complete-operation comparison | [LMDB controls and runner](performance/README.md), [retained baseline findings](performance/FINDINGS.md) |
| Metadata alternatives | `metadata/orbital/`, `metadata/consumer/`, `metadata/loom/`: packet census, ordinary ABI probes and candidates in progress |

## Questions driving the next candidates

The census distinguishes persistent records from arguments, returned values and
stable caller frames. For each, charge real ownership and shared state as well
as packet size. A pointer to another allocation is not eliminated metadata.
Actual pinned-compiler AArch64 and x86-64 returns matter: triviality, result
wrappers and the complete caller can outweigh nominal size. Ordinary calls are
the starting point; special calling conventions cannot stand in for a simpler
interface.

Compare genuinely different layouts and ownership arrangements, including
8-, 16- and 32-byte version candidates where their semantics permit. Include
slab/chunk headers, unused capacity, sparse publication, alternative
Representations, retirement debt and dependent loads. Keep resolved Part access
cheap without making coherent later discovery incorrect or hiding retention.

Each candidate needs complete-operation feedback early enough to change its
seams: the same encoded data, legal native body, read cut, lifetime, work and
budget under Orbital and LMDB. Pair a small discriminating loop with broader
checks across object/part counts, retained history, churn and cache footprints.
Record throughput and adequately sampled latency separately; keep platform and
unequal adapter costs explicit. The [baseline findings](performance/FINDINGS.md)
identify cases to challenge, not performance conclusions for new candidates.

The tests remain falsifiers: coherent later discovery, absence, exact retained
interpretation, conversion without logical publication, refused preparation,
single-use authority, and actual late physical completion after cancellation or
replacement. Keep these meaningful outcomes while revisiting their internal
construction. The [Orbital specifications](../../../orbital/spec/README.md)
own the wider correctness obligations; this single-owner study does not implement
Journal replay, consensus, missing-byte I/O or Stage 3 worker distribution.

## Build the experimental baseline

```sh
orb -m ubuntu cmake -S . -B build/orbital-mvcc/native -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=orbital-mvcc
orb -m ubuntu cmake --build build/orbital-mvcc/native --target orbital_mvcc_validate
```

The module targets `orbital_validate` and `loom_validate` cover Stage 1 only.
The old Object/consumer check names exist only when this spike is selected.
`orbital_mvcc_baseline` exports its experimental `<orbital/object.h>` include
directory locally; the production Orbital include tree contains no Objects API.
The existing candidate protocol sketch remains separately compileable.

Selected historical evidence retains its original bytes and provenance. Moving
it here does not turn the old machine, source or timing into a new measurement.
