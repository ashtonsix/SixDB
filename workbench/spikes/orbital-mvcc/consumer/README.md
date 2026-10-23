# Versioned fixed-record consumer

Disposable performance scaffolding for the rejected Orbital Objects/Views
implementation, retained as an experimental baseline. Its seams and ownership
choices are candidates for measured revision; this is not a supported API.
Fixed records use the existing sparse-key workload's key, next-key, 20-bit value and 32-bit tag. There is no Engine/index/container design
or row-membership policy here. Insert/split are omitted, so Stage 1 insertion
tail deficits are outside this consumer's comparison.

`data.h` declares immutable schema/count controls, owned interpretation/Part
shapes, cold qualification and construction/conversion/private fixed-row update
over explicitly supplied bytes. `native.h` defines the inline homogeneous read
bodies. Neither supplies lifetime protection or authority. The experimental
adapter must acquire those through Orbital and keep them through every body.
`check_data.cpp` qualifies this layer only; baseline selection/publication
histories require the separate real adapter checks.

Rows occupy one TuplePack Part with 23-byte units: key bytes 0–7, next 8–15,
value 16–18 (20 bits, high nibble spare), tag 19–22. Each field therefore has a
bounded scalar transfer. Four tight headless Local SeriesPack columns occupy one
Part per field with widths 64,64,20,32. Final tiles are fully initialized.
Both encodings preserve all logical fields and record order. Point lookup reads
the encoded key using binary search over sorted records; it keeps no decoded
key directory. Codecs can represent arbitrary key order, while lookup/scan
require the consumer's sorted input.

Conversion uses bounded 128-row chunks and existing Ikea constructors. This
chunk advances every column payload by a multiple of 64 bytes, preserving the
required base alignment. No whole-input eight-u64 record vector or diagnostic
sentinel clearing enters conversion. Kernel counters describe actual issued
coverage and construction/decoding work; baseline allocation, capture,
publication and retirement costs belong to the enclosing callable operation.

`consumer.h` integrates the actual Orbital API. `construct` and `convert` create
real Builders, acquire exclusive WriteViews, run the codecs, end all writer
scopes and freeze an immutable Representation. `update` resolves at most 64 keys
against one selected basis, copies its initialized Parts and replaces only
next/value/tag. It can derive another private result from the same Draft version.
It copies every Part, including the unchanged key column; `Costs::copied_bytes`
and `copied_spans` keep that cost visible. This is a fixed-Object comparison,
with no prescribed copy grain for future containers.

The caller uses actual `Draft::put`, `Store::prepare`, `Store::install` and
`Prepared::retire` to publish; same-version conversion uses `Store::adopt`.
The helpers themselves perform no publication. Read binding consumes actual
Selections, Representations or a complete Tuple View, checks the retained
interpretation on the stack and introduces no independent Hold or cache record.
Cold qualification checks Tuple unit/transfer shapes, Series placement and the
supplied control's row count once. The 24-byte parent description retains the
LE64 logical count; each eight-byte SeriesPack description is only a shared law
(width, head count and storage geometry). Counts and tight tile strides come from
this parent recipe. The child law is identical across different row counts. Its Tuple cursor is one 8 B byte pointer.
The immutable schema reference and row count belong to a shared 16 B control;
opening the cursor forms a 24 B complete native Tuple body. Each column cursor
is also 8 B: the four-column closure uses 32 B plus one shared 8 B count, forming
a 40 B native column body. The generic conversion/history `ReadData` occupies
56 B, down from 88 B, and delegates to these same bodies. These are actual
AArch64 sizes, excluding backing storage and Orbital lifetime protection.

`open()` is a trusted native entry: callers must use the same or equivalent
immutable control admitted at qualification and keep schema/backing protection
live. A cursor carries no count association, retention handle or allocation.
Copied/moved bodies borrow no former cursor or control address. Native Tuple
calls rebuild stationary local views/endpoints from the admission proof; lookup
prepares only key access before a hit, then reads the payload. Column bodies use
the existing typed dense source/decoder. Orbital Selections and complete Tuple
Views qualify these cursors through the actual adapter; a single Part View
discovers no siblings. Mapped bytes use the same cold and native codec bodies.

Complete caller assembly still matters. The qualification probe using the
matched harness's shared read/chain/scan dispatcher inlines Tuple lookup but
outlines the combined operation, reserving a 432 B stack frame for its scan
scratch even on point/chain calls. Shrinking persistent cursor state does not
remove that caller cost. The Tuple probe wrappers add 128 B (560 B combined on
point/chain); the column-chain wrapper adds 144 B (576 B combined). These probe
frames exclude enclosing selection/binding and any deeper scan call. This is
code-generation evidence, not a throughput gain.

Conversion scratch bounds are 4,256 B toward rows and 2,048 B toward columns.
The column bound includes conservative SeriesPack construction effect capacity
for whole-byte/residual fields per tile, even when issued physical coverage
coalesces. These are explicit adapter buffers, excluding native stack/plan frames
and allocator overhead. Metadata Shape preparation and Orbital descriptor copies
are part of the complete helper call. Allocation counters describe successful
Builder body requests; failed physical work and idle inventory belong to actual
Pool statistics. Complete request timing must include the enclosing selection,
publication and retirement work, which is outside these helper calls.

Build only the needed targets in the separate consumer directory:

```sh
orb -m ubuntu cmake -S . -B build/orbital-mvcc/consumer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=orbital-mvcc \
  -DSIXDB_MARCH=armv8-a+simd -DSIXDB_TUNE=neoverse-v2
orb -m ubuntu taskset -c 2,3 cmake --build build/orbital-mvcc/consumer \
  --target orbital_versioned_data_check orbital_versioned_check -j2
orb -m ubuntu taskset -c 2 ctest --test-dir build/orbital-mvcc/consumer \
  -R '^orbital_versioned_(data_check|check)$' --output-on-failure
```

The pre-relocation baseline passed both checks natively and with ASan/UBSan,
leak detection and stack-use-after-return instrumentation on the pinned Linux
compiler. The data check covers 36
format/count cases and independent field/wire oracles, six copied/moved generic
bindings across an ended stack frame and exact-sized misaligned column refusals.
Six thin-cursor cases exercise copied/moved cursors and bodies after local frames
end; 41 thin qualification refusals cover count, format, metadata, extent and
alignment. `check.cpp` covers 19 actual baseline histories: coherent old/new
multi-Object and Part discovery,
bidirectional conversion including empty Parts, exact-version adoption,
sequential same-Draft updates, retained Part independence, initialized spare
preservation, Builder/publication rights and private failure/refusal/retry. Thin
native reads cover actual old/new selections, direct Tuple Views, empty and tile
boundaries, and retained Part access after its Store and read context end.
All allocation charges drain to zero. These checks use resident access; pending
physical access is not simulated. Loom owns suspension/cancellation composition.

The original consumer required no new Ikea production change. The current retained
checks now use Ikea's shared schema framing and byte storage interfaces; historical
measurements and frozen source recipes retain their original qualification. It uses existing
caller-owned views, prepared controls, bounded constructors and source effects;
Orbital supplies ownership, coherent discovery and publication authority. Empty
conversion did expose and help qualify Orbital's zero-byte Builder Part support.
This baseline demonstrates one fixed consumer seam; it does not choose future
container membership, split policy or copy grain.

No versioned performance result is claimed here. The matched harness in
[performance](../performance/) includes selection and publication at its complete
request boundary. Local qualification receipts and source/binary hashes live in
ignored `build/orbital-stage2/consumer/qualification/`; these qualify the
pre-relocation baseline, not later experimental changes.
