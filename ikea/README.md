# Ikea

Ikea supplies composable data containers, codecs and computational kernels for
SixDB. Its parts serve records, indexes, strings and standalone structures such
as hash tables and sketches. Engine chooses field semantics and representations;
Ikea operates over storage supplied by its caller.

| Choose a primitive | Physical model | Start here |
| --- | --- | --- |
| SeriesPack | Packed unsigned arrays, 1–64 bits per value; tiled payload and optional head planes | [Use](docs/seriespack/usage.md) · [Physical tour](docs/seriespack/representation.md) · [Compose](docs/seriespack/extending.md) · [Reference](docs/seriespack/reference.md) |
| TuplePack | Fixed-layout units containing byte-contained unsigned codes; ordered projections into row packets | [Use](docs/tuplepack/usage.md) · [Compose](docs/tuplepack/extending.md) · [Reference](docs/tuplepack/reference.md) |
| Bec256 | Headless 256-position bitsets; caller retains population, exact body length and location | [Use and compose](docs/bec256/usage.md) · [Representation](docs/bec256/representation.md) |

These modules support construction, reads and replacement, native composition
and source-qualified byte effects. StreamPack is reserved and has no operations.

Modules keep logical interpretation, physical placement and execution distinct.
Prepared operations reuse admitted mappings; Bec256 sources retain an admitted
body association. Checked writes validate the new command before executing it. Changing a
child's layout or owner can preserve the parent's logical contract. Different
segments can retain different representations.

![Logical expression, physical placement and execution recipe meet at preparation. Repeated calls reuse the binding and validate new checked commands. Different segments can realize the same field contract.](docs/images/architecture.svg)

The [owner integration guide](docs/integration.md) explains how Engine, Loom and
Orbital retain borrowed state and coordinate visibility across bounded Ikea calls.
[Extending and changing Ikea](docs/source.md) covers authoring principles, source
boundaries and validation. Each module's use and composition guides link directly
to its executable examples.

## Build and run

Use the [pinned Linux toolchain](../BUILDING.md). Prefix these commands with
`orb -m ubuntu` from the macOS workspace. For an AVX2 build:

```sh
cmake -S . -B build/ikea/avx2 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DSIXDB_MARCH=x86-64-v3 -DSIXDB_TUNE=zen5
cmake --build build/ikea/avx2 --target ikea_validate -j2
```

For ARM, use a separate directory with `-DSIXDB_MARCH=armv8-a+simd` and
`-DSIXDB_TUNE=neoverse-v2`. Run binaries only on compatible CPUs. ISA-specific
requirements live in the [SeriesPack](docs/seriespack/reference.md#execution-profiles)
and [TuplePack](docs/tuplepack/reference.md#routes-and-native-execution) references,
and the [Bec256 guide](docs/bec256/usage.md#compose-native-values).

Link `ikea::seriespack`, `ikea::tuplepack` or `ikea::bec256` and include the
corresponding `<ikea/seriespack.h>`, `<ikea/tuplepack.h>` or `<ikea/bec256.h>`.
`ikea_validate` runs the modules' correctness checks, independent wire checks
and examples. It does not time benchmarks. The [SeriesPack](../workbench/benchmarks/seriespack/README.md)
and [TuplePack](../workbench/benchmarks/tuplepack/README.md) benchmark guides own
measurement instructions, target profiles and retained comparisons.
The [Bec256 routine suite](../workbench/benchmarks/bec256/README.md) measures codec
boundaries and native pair consumers; its comparative research and larger-bitset
metadata investigation live in the
[composition study](../workbench/spikes/bec256-composition/README.md).
