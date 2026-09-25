# SeriesPack validation

Build `ikea_validate` using an enabled native profile. It checks the frozen
reference hashes and runs the behavior checks and four executable guides.
`ikea_seriespack_descriptor_check` builds and runs separately from the expensive
format matrix; it exercises descriptor wire bytes, 64-bit counts/strides and
malformed-input boundaries. CTest lists and reruns the built checks.
`python3 ikea/test/headers.py BUILD --module seriespack` independently parses the
ordinary/author headers with that build's flags.

| Behavior | Files | Evidence provided |
| --- | --- | --- |
| Existing wire compatibility | [read/wire.cpp](read/wire.cpp), [reference](reference/README.md) | Independent encoded input; point, region and arbitrary-range outputs; 76 headless formats |
| Placement and composition | [read/composition.cpp](read/composition.cpp) | 206 width/head/geometry combinations, stride gaps, masks, scalar edges, actual-leaf substitution |
| Physical mutation | [mutation/cases.h](mutation/cases.h), `mutation/widths_*.cpp`, [verify.cpp](mutation/verify.cpp) | Final bytes against independent encoder, preserved neighbors/gaps, summaries, coverage and rejection |
| Nested mutation | [mutation/substitution.cpp](mutation/substitution.cpp) | Multiple owners, bulk traversal, points, construction, retired `PROT_NONE` storage and overlap diagnostics |
| Construction | [mutation/construction.cpp](mutation/construction.cpp) | Partial final tiles, zeroed owned slack, preserved siblings, empty and boundary extents |
| Descriptions | [format/descriptor.cpp](format/descriptor.cpp) | Independent expected bytes, large fields and exact lengths; invalid descriptions rejected |
| CPS | [execution/chain.cpp](execution/chain.cpp) | Widths, copied aligned tables, every supported depth/early exit, mutation completion |
| Ownership | [integration/ownership.cpp](integration/ownership.cpp) | Serialized waits, stale replies, cancellation, sealing, conflict, private-copy and in-place visibility obligations |

The 206-format matrix is validation coverage, not 206 named preset promises.
Geometry and head choices are explicit; three presets are the curated starting
surface. Width shards control compiler memory beneath the mutation behavior.

`IKEA_CHECK` failures report the source expression/location and enclosing format
and scenario scopes. Add `format_scope<F>` and a range/mask `scope` to new matrix
checks; shared verification then retains useful caller context. The diagnostic
support is compiled once and is outside timed kernels.

Tests do not establish a concurrent publication protocol or OS page-COW behavior.
Use an ASan/UBSan build for changes affecting extents, substitution or ownership;
keep it separate from measured builds. Examples teach ordinary successful use and
selected failures; exhaustive permutations belong here.
