# Initial Ikea implementation sketch

Provisional, 2026-09-10. Intended for the implementing engineer. Start with
**SeriesPack**, with **TuplePack** and **StreamPack** reserved for subsequent
spikes. The [SeriesPack specification](seriespack.md) is the concrete first
contract. The [composition design](../workbench/spikes/ikea-composition/design.md)
and [integration proposal](../workbench/spikes/ikea-composition/semantics-and-integration.md)
inform the seams; their open questions do not block starting this scope.

## Scope and source layout

The current [scaffold](README.md#starting-layout) has three namespace headers,
one compiled SeriesPack stub and a hello example. The fuller layout below is
proposed; the listed packing operations and native implementations do not exist yet:

```text
ikea/
  README.md, implementation.md, seriespack.md
  CMakeLists.txt
  include/ikea/
    seriespack.h                  # small public entry: descriptions, views, operations
    seriespack/
      layout.h                    # wire geometry, resolved presets, size/access facts
      view.h                      # borrowed const/writable placements; no allocator
      operations.h                # shared read/write/head composition and declarations
      native_avx2.h               # explicit native bodies for inline consumers
      native_avx512.h
      native_neon.h
      detail/                     # packet/tail helpers and generated shuffle indices
    tuplepack.h                   # at most a namespace/scope stub pending its spike
    streampack.h                  # at most a namespace/scope stub pending its spike
  src/seriespack/
    bind.cpp                     # checked dynamic attachment and operation selection
    scalar.cpp                   # exact production fallback and boundary handling
    avx2.cpp, avx512.cpp, neon.cpp # shared compiled endpoints for enabled targets
  test/seriespack/
    wire.cpp, access.cpp, composition.cpp
  bench/seriespack/
    ...                          # focused API/boundary comparisons using Workbench tools
```

Keep the layout/view headers independent of native intrinsics, Loom, Engine,
and graph implementation headers. Native headers may include reusable private
helpers where useful; they do not implement a new ISA-neutral instruction API.
Move machinery into a shared Ikea header only after there are real sibling
consumers. In particular, do not build a general type registry, graph recorder,
resource context or driver framework as a prerequisite for SeriesPack.

LocalPack and ScanPack become internal packet/tail mechanisms. They are useful
names in the implementation and reflection, but do not need independent public
modules or CMake targets. A statically described SeriesPack must expose their
work without adding dispatch, headers, allocation or intermediate arrays.

TuplePack and StreamPack have names and intended consumers, not selected wire
formats. Their stubs should point to their eventual investigations and expose
no invented packing API. Heterogeneous tuples, variable-length framing and
their relationship to strings/row storage remain questions for those spikes.
The current headers reserve those namespaces without selecting packing APIs.

## Two usable paths through the same implementation

**Static composition:** an author supplies the width, head split and resolved
layout in the type/constant description; placement is attached separately.
Inline read/body/tail/join functions feed a native consumer. Target-specific
code sees its actual register types. A recordable operation expansion delegates
to those same operations rather than duplicating the algorithm for reflection.

**Bound operations:** a caller supplies a runtime description and admitted
storage. Compiled binding checks the shape, extents, target and operation once,
then selects an endpoint. The hot call contains only work required by that
endpoint. It must not traverse a `Shape`, reconstruct through a u64 array, or
redispatch width at each value. Erasure at this boundary does not dictate the
representation of intermediate values inside a region.

Initially provide direct scalar/native use and shared compiled operations.
Keep native bodies reusable by compatible continuation wrappers, but do not
copy the probes' fixed graph matcher or require a CPS hop after every child.
The three execution buckets remain the direction; the general CPS ABI and
many-recipe economics remain open. A manually fused implementation must retain
the same value, access and effect contracts as its ordinary expansion.

## Minimal integration seams

- SeriesPack handles unsigned bit values, coordinates within its supplied
  array, physical placement and admitted byte effects. Signed/float meaning,
  order transforms, validity and delta reconstruction compose around it.
- Its views borrow storage. An enclosing owner/binding keeps the bytes alive;
  no reference count, allocation or lease acquisition belongs in a native body.
- Writers expose footprints that the enclosing adapter maps to MVCC spans.
  Summary consumers reuse semantic old/new values in the enclosing composition;
  SeriesPack does not infer record changes from its byte writes.
- A kernel invocation is nonsuspending. A driver can split work between admitted
  regions, acquire Loom buffers, prefetch/rotate, and retain state as needed.
  It must account for resources and effects before parking.
- Engine defines segment structure and publication. SeriesPack's lengths use
  ordinary checked host-sized arithmetic and are not capped at `2^16`.

These should be local descriptions and a few concrete adapters, not a service
locator passed through all kernels. An attached primitive remains useful in a
standalone hashmap or sketch with none of the database lifecycle machinery.

## Build, verification and adoption

The scaffold provides an ordinary `ikea_seriespack` library target, aliased as
`ikea::seriespack`, using `sixdb_target`. Consumers link it directly, including
Workbench spikes; selecting the composition probe must not be necessary.
Follow [BUILDING.md](../BUILDING.md): independent TUs, pinned Linux toolchain,
incremental objects, and the configured ISA/tuning. The target-specific paths
above do not imply building every ISA into every executable. A later deployment
dispatcher can compose supported compiled targets outside the hot body.

Factor code by wire/byte expansion and operation family, with deliberate native
specializations. Do not create a TU per width, array length, head count, preset,
summary consumer and scheduling policy. Extent and stride normally remain loop
or address parameters; known values can specialize when useful. Compile shared
validation and broad endpoints once; expose only useful inline kernels.

The old probes remain owned evidence in Workbench. Carry over mechanisms and
independent test cases selectively; production code must not include from a
probe, import its test harness, or inherit its fixed 256-value endpoint. The
spec's [curation decisions](seriespack.md#carry-forward-and-borderline-decisions)
identify what to reuse and what to leave behind.

Wire/access checks and a few real compositions should accompany implementation.
Include a static SeriesPack-versus-direct-body generated-code comparison, native
read reuse, a strided embedding and an exact partial-array boundary. Measure
supported target paths against matched contracts; wider widths and new presets
have geometry evidence, not established throughput. Use existing Workbench run
and evidence helpers rather than starting another measurement framework.
