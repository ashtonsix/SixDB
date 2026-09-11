# Partial native materialization and original coordinates

An admitted native read can cover more rows than the requested output without
changing the shared `composition::materialize` authoring core. The output sink
must map and store only selected original positions. This separates a physical
read window, a logical request and a prefilter; none substitutes for another's
admission. It gives the physical materializer a way to investigate native edges
after its [scalar traversals](../../seriespack-range-execution/expressions.md) left large losses.

## The checked composition

The ignored AVX2 probe uses eight-row native reads for Local7, Local23/H16,
Striped12 and Striped28/H16, then widens at a u32/u64 sink. It consumes the
actual authored expression, including independently placed body/tail/head
replacements and projected heads. It reads complete admitted windows, including
storage padding where needed; only requested logical positions become active.

For each output-vector portion, clipping happens on integer coordinates first:
`begin = max(region_begin, request.begin)` and
`end = min(region_end, request.end)`. An empty portion forms no output pointer.
The sink shifts values and selection together, addresses
`output + (begin - request.begin)`, and uses exact masked stores. It never forms
a pointer corresponding to an inactive region before the output allocation.
Sparse prefilter holes stay at their original output positions. Compressing
those holes would change this positional contract; a physical compress-store
can still implement an all-selected contiguous interval under a narrower sink
admission.

Complete encoded storage alone does not activate final padding rows. The logical
prefilter is evaluated only for requested rows, and the sink leaves padding
inactive. The driver can skip a region whose prefilter is empty. Other native readers remain free to
read their admitted full windows for a nonempty partial mask; a mask supplies
no additional read permission or general fault-suppression guarantee.

## Evidence and limits

The pinned Clang 21.1.8 x86-64-v3 build passes 798,976 exact guarded-output
queries under QEMU AVX2, plus 408 requests with a runtime-empty prefilter and
every source plane inaccessible. There are 516 guarded source placements in
total, including dense and independently page-gapped strides at both guard
edges, every logical origin, boundary lengths and partial final tiles.
The main count includes 262,528 projected-head queries with unused payload and
opposite heads protected, and 11,392 substituted-tail queries whose original
body is inaccessible. u32/u64 output capacity is exact; sparse holes and bytes
outside the request retain canaries. The independent wire/oracle and guard
utilities come from the frozen scalar-run experiment.

The [provenance](evidence/partial-materialize-contract-20260910/provenance.json)
records all 18 source dependencies, flags, emulator, binary and exact counts.
A same-source relink reproduced the executed binary byte for byte. The
[recovery bundle](evidence/partial-materialize-contract-20260910/artifact.json)
retains source, native binary and callback assembly. The two inspected complete
callbacks have no inner calls or native-value stack staging, but save general
registers; the headed striped endpoint also spills scalar controls.

This is a semantic and code-generation result, with no hardware timing claim.
The deliberately simple predicate construction and masked-store sequence are
not performance recommendations. The physical task has its own optimized
all-selected native-edge candidate. Neither probe defines a universal traversal,
partial sink, CPS handoff or public admission API. No production driver changes
follow solely from these checks.
