# Bucket capacity, fingerprints and exact lookup

An executable bounded consumer of SeriesPack Local fingerprints, TuplePack
20-bit keys / 24-bit values and an eight-byte metadata tuple. The actual table
uses head buckets plus linked overflow blocks, with a 32-bit next-block index
and a 16-bit occupancy mask. This fixture selects a concrete hash/overflow
policy so N and b have observable consequences; it is not an Engine table design.

For fixed logical key counts, compare N=8/9/16, b=7/8/12 in the combinations
listed in `main`, gap-free or padded bucket strides, and a global separate
fingerprint plane. Both complete Local tiles are allocated for N=9. Head count
targets 50% or 90% of N slots under uniform hashing. A skew arm routes into one
eighth of the allocated heads while preserving the same logical key count; it
retains that unused allocation. Actual head/block/overflow counts are recorded.

H1 uses hash bits above bit 15 for bucket assignment; H2 uses the low b bits.
Exact candidate-key comparisons still establish identity. Queries are successful,
unsuccessful, alternating hit/miss, or value updates to existing keys. Construction
and binding precede timing. Insertion/erase and overflow growth are not timed in
this first screen. The keys fit 20 bits, including the disjoint miss range.

Three recipes expose different capabilities:

| Recipe | Fingerprints | Keys, values and metadata |
| --- | --- | --- |
| `raw` | Explicit scalar implementation of Local wire | Explicit byte loads/stores; raw writes omit ordinary coverage |
| `rawfp_tuplepack` | Same scalar Local reader | Retained ordinary TuplePack readers/writers, including mutation effects |
| `seriespack_tuplepack` | Ordinary bound SeriesPack range decoder | Same ordinary TuplePack operations |

The Local scalar implementation is cross-checked against the ordinary decoder
on admitted placements. Fingerprint false matches, exact-key checks and overflow
visits are counted outside timing. `conditional_visits` in CSV is blocks visited
per query batch, not cache lines or measured traffic. The ordinary fingerprint
recipe materializes a small N-value range before exact lookup; the raw scalar
recipe can stop extracting on a true key. That is a whole-consumer choice, not
a matched isolated kernel comparison.

TuplePack reuses N slot views spanning all blocks, each with bucket stride and
its own entry offset; shared plans expose key/value projections. Metadata uses
one view spanning all blocks. This avoids per-query allocation or rebinding.
Bindings and byte owners remain stable throughout the run. Updates verify exact
new values and preserve keys/fingerprints; publication is outside this model.

## The repeated two-tile placement gap

A single SeriesPack view can express N=8 combined buckets with tile stride equal
to bucket stride. A global separate fingerprint plane can also express N=9/16,
padding each logical bucket to a complete 16-position tile pair.

Combined two-tile buckets instead need
`origin + floor(tile/2)*bucket_stride + (tile%2)*b`. Current views have one constant
tile stride, and composition does not provide this grouped placement. Binding
each bucket is also rejected for unaligned payload origins under current 64B
origin admission. `--check` retains concrete rejected N=9/16 dense cases.
Ikea ARCHITECT confirmed this gap; trusted admission does not waive the rule.

Aligned 128B-stride buckets **could** use an individual admitted view/binding per
bucket. That alternative is not measured here. The ordinary SeriesPack arm in
this experiment only covers the single-view placements, so compare
`rawfp_tuplepack` across split/combined layouts to isolate placement without
silently substituting fingerprint recipes. The raw-format arms retain the
consumer requirement; the search must not infer that padding is mandatory.

All byte buffers begin at 128B alignment. Recorded `allocated_bytes` counts
their actual rounded capacities, excluding query/construction fixtures and
prepared C++ bindings. Slot maps, descriptors and recipe state are shared per
table instance. `--check` tests hit/miss/overflow/update semantics, Local wire
agreement and the alignment counterexample. Local ASan/UBSan passed before the
initial screen. The [shared runner](../run.py) owns hardware/source receipts and
sequential timing; these repeated traces are not a cold-cache claim.
