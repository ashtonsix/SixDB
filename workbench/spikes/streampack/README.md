# StreamPack

StreamPack is a headless packing primitive for variable-sized byte values.
A production block holds up to **256** slots, each reserving up to 65,535 bytes.
The comparative research retains 128/512 controls; they are not production options.
Caller-owned sidecars locate values and describe used lengths and tombstones;
the enclosing collection owns allocations, logical references and version visibility.
Strings, blobs, optional fields and mixed field groups can share this model.

Production code now lives in [Ikea StreamPack](../../../ikea/docs/streampack/usage.md).
This directory retains the comparative spike and exercises the production module
through native consumer benchmarks and real Loom/Orbital owners.
The [implementation qualification](IMPLEMENTATION.md) tracks that handoff.

The [implementation recommendation](RECOMMENDATION.md) distills the decisions
from the whole spike. Start with the [design](DESIGN.md) for terminology, examples, operations and
ownership. The [investigation](PLAN.md) explains the alternatives, prior art and
coverage. [Results](RESULTS.md) contains timings, limitations and recoverable evidence.

The research comparisons found that compact widths earn large-population read and
reverse-lookup wins, and now run through the complete MVCC caller. Direct geometry
remains useful for ordinary point edits and selected reads. Presets remain
conditional on the consumer; the results include losses as well as wins. String
prefixes and encoding belong to the schema; full string optimization is later work.

Metadata placement now has explicit NEON, AVX2 and AVX-512 execution controls,
with automatic loop and SLP vectorization disabled and native disassembly checked
before timing. The earlier compiler-generated field comparisons remain withdrawn.
[Results](RESULTS.md) separates serial service time, overlapping requests, field
throughput, full projection and private owner growth.

## Code map

| Purpose | Files |
| --- | --- |
| Production primitive qualification | `implementation_bench.cpp`, `implementation-native.sh` |
| Production Loom and Orbital callers | `implementation_loom*`, `implementation_orbital*` |
| Borrowed research geometry, mutation and exact byte search | `direct.h`, `grouped.h`, `prefix.h`, `byte_search.h` |
| Independent values, owners, bounds, aliases and damage checks | `check.cpp`, `geometry.cpp` |
| Real Orbital Parts, damage reconstruction and MVCC | `material.h`, `material.cpp`, `publication_check.cpp`, `widths_publication_check.cpp` |
| Cross-block rewrite, remapping and holder repair | `rewrite.h`, `rewrite.cpp`, `rewrite_check.cpp` |
| Isolated population and mutable operation measurements | `population_fixture.h`, `footprint.cpp`, `mutable_population.cpp` |
| Schema-sized fields, placement, padding and SeriesPack/TuplePack reuse | `layout.h`, `layout_tuple_group.h`, `layout_hybrid.h`, `layout.cpp`, `layout-*-control.sh` |
| Explicit ISA fields, Width prefixes and Direct endpoint batches | `layout_native.h`, `prefix.h`, `layout-explicit-control.sh`, `layout-direct-control.sh` |
| Fixed directories, partial occupancy and private growth | `layout_growth.cpp` |
| Publication, retained readers and allocation policy | `lifecycle.cpp`, `sealing.cpp` |
| Genuine Loom Ring versus direct and manually staged callers | `ring.cpp` |
| External-prefix and shared-string composition checks | `substring_check.cpp` and its helpers |

## Build and check

Use the pinned Linux toolchain in [BUILDING.md](../../../BUILDING.md). Prefix
commands with `orb -m ubuntu` from the macOS/OrbStack workspace.

```sh
python3 workbench/spikes/streampack/references.py
cmake -S . -B build/clang/streampack-fresh -G Ninja -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=streampack
cmake --build build/clang/streampack-fresh --target streampack_model_check streampack_geometry streampack_publication_check streampack_widths_publication_check streampack_substring_check streampack_rewrite_check streampack_layout streampack_layout_fields streampack_layout_growth streampack_layout_explicit streampack_layout_explicit_bulk streampack_layout_direct
ctest --test-dir build/clang/streampack-fresh -R '^streampack_.*_check$' --output-on-failure
```

`references.py` verifies pinned DuckDB and StringZilla source archives in ignored
build storage. Ordinary primitive checks do not require those libraries; the
substring competitor and descriptor benchmarks do. Native runners record compiler,
hardware and captured-source identities. Full binaries and diagnostics are kept
in recoverable artifacts rather than the source tree.
