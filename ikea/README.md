# Ikea

Ikea supplies the data containers and computational parts used to build SixDB.
A column, hash table or index can use these parts without bringing along a query
planner, scheduler or storage engine. Engine composes them into database work;
they are also useful in standalone data structures and experiments.

The central idea is to make useful operations composable. Reading packed values,
comparing them and accumulating a result can share one execution region, where
intermediate values can remain in registers. Ordinary calls that read or write
arrays are useful too. The choice of execution should preserve the meaning of
the data and allow its costs to be measured against alternatives.

**SeriesPack is the first implemented component.** It stores fixed-width unsigned
integers in packed arrays and supports point reads and updates as well as bulk
processing of the same bytes. A caller chooses what those integers mean: they
might be column values, dictionary IDs, fingerprints or compact metadata.
TuplePack and StreamPack currently reserve names for future work.

[Start with SeriesPack](seriespack.md). That introduction follows one small array
from ordinary C++ use through its encoded bytes, scalar/SIMD reads and composition.
It is the main reading path for a new Ikea contributor.

## The boundary with the rest of SixDB

SeriesPack operates on storage supplied by its caller. The surrounding structure
owns record membership, synchronization and the lifetime of those bytes. In
SixDB, Engine supplies database meaning and visibility, Loom arranges work and
buffer residency, and Orbital supplies the underlying machine services. A packed
array operation does not acquire a buffer, perform I/O or publish a transaction.

This division lets point updates and analytical operations reuse a representation
while leaving database coordination outside the small computational parts.
Calico's `frame`, `qhash`, `keyset` and `kmath` are prior work; SixDB implementations
are developed afresh from their lessons and new experiments.

## Build and run

From the repository root on Linux (prefix with `orb -m ubuntu` from this Mac):

```sh
cmake --preset dev
cmake --build --preset dev --target ikea_examples
./build/clang/dev/ikea/ikea_hello
```

The examples are independent programs in `build/clang/dev/ikea/`:

| Source | Program | What to try |
| --- | --- | --- |
| [Basics](examples/seriespack_basics.cpp) | `ikea_hello` | Construct an array, update selected positions and inspect write coverage |
| [Query](examples/seriespack_query.cpp) | `ikea_seriespack_query` | Compare materialized and native consumption, including a partial final tile |
| [Two arrays](examples/seriespack_join.cpp) | `ikea_seriespack_join` | Filter a tag array and sum matching counters in another placement |

The query and two-array programs use native operations when the build enables a
supported ISA; their scalar paths also run on a baseline build. Consumers link
`ikea::seriespack`; no Workbench spike selection is required.
[Building SixDB](../BUILDING.md) owns toolchain settings.

After the introduction, [extending SeriesPack](seriespack/extending.md) shows how
to add a computation and find the relevant implementation and checks.
[Recurring benchmarks](../workbench/benchmarks/seriespack/README.md) provide the
performance entry point; their linked investigations retain the evidence.
