# Reading and prior art

Targeted reading, 2026-09-08. This is a starting mechanism map, not an exhaustive
survey or novelty claim. Applications to SixDB are proposals in this spike;
published and Calico performance results do not establish SixDB crossovers.

## Calico starting points

The user supplied Linux paths under `/home/ashtonsix/calico/workbench/prototypes/`.
In this macOS workspace they are accessible through the sibling `calico`
checkout. Links below use that relationship. These prototype directories are
currently untracked in that checkout, so its Git HEAD is not a source revision
for them. Their own reports and evidence archives are the starting provenance;
this opening review has not rerun or independently audited their campaigns.

**Compact row filters** — [README](../../../../calico/workbench/prototypes/compact-row-filters/README.md),
[report](../../../../calico/workbench/prototypes/compact-row-filters/REPORT.md),
[filter implementation](../../../../calico/workbench/prototypes/compact-row-filters/filter.h),
and [probability/range model](../../../../calico/workbench/prototypes/compact-row-filters/model.py).

The fixed schema has eight uint32 fields. Four bytes per row encode either
shared masks or eight dedicated four-bit tags; tags are also packed into
separate field streams. The report favors dedicated tags for this scalar
workload, with selective conjunctions benefiting and many cheap single-field
queries preferring direct access. A one-mismatch case loses the apparent
benefit predicted by counting all query predicates. Identical candidates in
different physical orders have different refinement costs. Buffered refinement,
exact-column controls, and single-field replacement costs are useful comparands.

Limits: equality scans, a fixed four-byte budget, synthetic data, single-thread
updates, and Apple M1 Pro/NEON timings. Its range model checks conservative bin
covers, not a native progressive range index. It does not establish Boolean
planner composition, block rollups, snapshots, or publication safety. The
shared-mask result does not settle variable-cardinality sets or sparse schemas.

**Three-array** — [README](../../../../calico/workbench/prototypes/three-array/README.md),
[report](../../../../calico/workbench/prototypes/three-array/REPORT.md), and
[storage implementation](../../../../calico/workbench/prototypes/three-array/storage.h).

The summary array contains key/string fingerprints and flags, independently of
a hot core and cold payload. Adding the same filter to native rows supplies
much of the selective-scan benefit; core packing has separate trade-offs.
The key-width comparison uses 4/8/16 fingerprint bits while keeping the
32-bit summary allocation fixed. Broad sequential scans can prefer bypass,
while random candidate probes retain a different useful region. Known-row
retrieval bypasses the summary. String equality is tested, not substring search.

Limits: the string hash is explicitly weak for structured collisions, payload
and patch choices are synthetic, and the update protocol excludes concurrency,
MVCC, logging, and reclamation. Neither its three arrays nor its recommended
core width are inherited SixDB requirements.

**Bytepack, progressive work** — [README](../../../../calico/workbench/prototypes/bytepack/README.md),
[memo](../../../../calico/workbench/prototypes/bytepack/MEMO.md),
[progressive operations](../../../../calico/workbench/prototypes/bytepack/progressive.h),
[plane format](../../../../calico/workbench/prototypes/bytepack/planes.h), and
[contextual findings](../../../../calico/workbench/prototypes/bytepack/evidence/context.md).

Unsigned full values use significance-ordered planes, including leading bytes
and wider lower groups. Range evaluation retains separate low/high boundary
equality masks and reads later groups only while relevant boundary ties remain.
That demonstrates the distinction between surviving the range and still needing
refinement. Masked materialization and subsequent operations test whether saved
decoding matters to the whole request. Selected-row reconstruction materially
narrows some wins against a full-cell reconstruction control.

Limits: 256-row cells, unsigned semantics, external nullity/identity, no summary
maintenance, and exploratory local ARM measurements. The contextual quantile
query is chosen separately per cell, an acknowledged limitation: this spike
must use a shared logical query across heterogeneous blocks. Plane-major
traversal, batching thresholds, and calling protocols remain candidates.

## Primary references

**Li and Patel — BitWeaving: Fast Scans for Main Memory Data Processing
(SIGMOD 2013), sections 4–5.** The vertical scheme uses early pruning of
unresolved comparisons and passes filter bit vectors between Boolean clauses.
It also groups bits physically to make avoided work translate into better
memory access. This directly motivates separate experiments for predicate
composition, group occupancy, and plane placement. It operates on value codes;
it does not make hashed signatures exact or resolve SixDB mutation semantics.
[Author-hosted extended paper](https://quickstep.cs.wisc.edu/pubs/bitweaving-extended.pdf)

**Feng, Lo, Kao, and Xu — ByteSlice: Pushing the Envelop of Main Memory Data
Processing with a New Storage Layout (SIGMOD 2015), section 3.1.** Byte-oriented
value slices use SIMD comparisons and stop when a segment has no unresolved
prefix ties. Its analysis connects the number of values processed together
with early stopping. This is a close control for byte planes derived from
existing data, and a reason to count nonempty groups rather than only rows.
Its uniform-bit model and hardware measurements are not width-selection rules
for mixed predicate signatures.
[Paper](https://www.cs.cmu.edu/~15721-f24/papers/ByteSlice.pdf)

**Sidirourgos and Kersten — Column Imprints: A Secondary Index Structure
(SIGMOD 2013), sections 1 and 3.** An imprint records which histogram bins
occur in a cache line of column values; compatible presence vectors can be
combined, and repeated imprints compressed. This is a direct comparison for
range-aware block occupancy and a stronger alternative to simple min/max when
holes matter. A single-column presence vector does not retain the row
correlation needed to decide arbitrary multi-column conjunctions.
[Paper](https://15721.courses.cs.cmu.edu/spring2023/papers/04-olapindexes/p893-sidirourgos.pdf)

**PostgreSQL `pg_trgm`, index support.** The documented GiST operator class
approximates trigram sets with bitmap signatures and exposes a signature-length
trade-off between index size and precision. LIKE/regexp index searches use
extractable trigrams; patterns without extractable trigrams degenerate to a
full-index scan. This motivates text-feature occupancy, pattern extraction,
and direct/inverted alternatives as explicit controls. A GiST text index is
not evidence for a tiny multi-field row plane or its planner interface.
[Official documentation, index support](https://www.postgresql.org/docs/18/pgtrgm.html#PGTRGM-INDEX)

These readings suggest three independent axes: progressive access to existing
value bits; auxiliary row evidence for expensive or multi-field filters; and
block summaries that avoid reading either. The [experiments](experiments.md)
separate them before testing combinations.
