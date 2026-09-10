# Ikea

Composition-ready parts, primarily data containers, codecs, value operations
and kernel stages. Intended for Engine and for standalone structures such as
hash tables, heaps and sketches.

Successor to Calico's `frame`, `qhash`, `keyset`, and `kmath`. Those modules are
sources of mechanisms and evidence; their original representations and
constraints are subject to recalibration.

Initial scope is SeriesPack, with TuplePack and StreamPack awaiting their own
spikes. The [implementation sketch](implementation.md) and
[SeriesPack specification](seriespack.md) are proposals; the code currently
contains only namespace stubs, a compiled library skeleton and a hello example.

The working approach separates value meaning, physical layout, placement and
execution. Native bodies should serve useful inline and compiled compositions;
their local byte-access and effect contracts let enclosing operations handle
ownership, scheduling and database publication. Exact interfaces remain open.

The [composition investigation](../workbench/spikes/ikea-composition/README.md)
owns the conceptual design and the completed block probes that informed it.
The exploratory implementations remain evidence in Workbench.

## Starting layout

| Location | Purpose |
| --- | --- |
| [include/ikea](include/ikea/) | Small public entry headers; native/composition headers can develop beside them |
| [src/seriespack](src/seriespack/) | Independently compiled SeriesPack implementation |
| [examples/hello.cpp](examples/hello.cpp) | Minimal consumer of the module's headers and library target |

From the repository root on Linux (prefix with `orb -m ubuntu` from the Mac):

```sh
cmake --preset dev
cmake --build --preset dev --target ikea_hello
./build/clang/dev/ikea/ikea_hello
```

Consumers link `ikea::seriespack`; Workbench spike selection is unnecessary.
See [Building](../BUILDING.md) for the shared toolchain and build settings.
