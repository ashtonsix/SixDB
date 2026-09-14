# A connected worked investigation

Proposed cases, 2026-09-14. These are candidate definitions and decision arguments,
not new benchmark results. The [design](design.md) explains the model;
[the experiment plan](experiments.md) identifies the comparisons that would test
it. Earlier measured fixtures remain in [findings](findings.md).

## One record, several ways to finish its operations

Take a record containing 56 bytes of exact fixed information R, one infrequently
projected byte X, and a variable-length string S. R includes the string length
and tail locator. The initial representation stores an eight-byte string prefix
inline, producing a 65B fixed region. Short strings use an explicit length and
an agreed unused-prefix convention. A tail holds the remaining string bytes.
All candidates must reconstruct the same values and preserve the same predicates.

The supplied workload includes string equality with a projection, known-row
projection of R, full reconstruction including X and S, and partial replacement
of individual fields. It supplies predicate parameters, request order, joint
projection frequencies and mutation frequencies. Its objective might minimize
complete-operation time under an allocated-byte limit and an update-cost limit.
This is enough to consume an objective without inventing a production workload
learner or assigning a synthetic bonus to co-location.

There are two distinct kinds of choice. We may rearrange information already
stored, or additionally maintain a compact filter. Compare both under the same
logical outputs, but account for the extra information and its maintenance.

| Candidate | Fixed region and other streams | Why keep it in the search? | New work or cost to examine |
| --- | --- | --- | --- |
| Initial I8 | R56 + X1 + prefix8 = 65B; remaining tail | Eight prefix bytes before tail refinement, direct X | Dense rows often span lines; a 65B record needs a composition of TuplePack units |
| Short prefix I7 | R56 + X1 + prefix7 = 64B; byte 7 moves into tail | Dense 64B core without padding | Queries distinguishable at byte 7 now need tail refinement |
| Relocated X | R56 + prefix8 = 64B; a dense X plane; tail unchanged | Preserve eight-byte rejection while slimming the common core | X projections need another stream; total payload is still 65B before tail |
| Separate prefix | R56 + X1 = 57B; dense prefix8 plane; tail | Scan prefixes without bringing in R or X | Successful matches and full projections gather multiple streams |
| Padded initial | Same information as I8 in an explicitly larger stride | Alignment or adjacency may repay wasted space | Larger allocation, cache/translation pressure and initialization |

The dense X plane is directly addressable from the row ordinal. Replacing it
with a sparse optional/patch plane is another candidate, not a free improvement:
it needs absence semantics and a locator or rank mechanism. Its address path and
mutation obligations differ. Adjacent and separate allocation choices are also
not implied by the table's plane boundaries.

For the original **63B** example, use a separate fixture with 55 fixed bytes and
an eight-byte prefix. A ninth prefix byte produces 64B and may replace later
refinement; padding that same fixture adds no evidence. The two fixtures should
not be conflated: reducing 65B to 64B and increasing 63B to 64B have different
information consequences. The first measurements already showed both directions
can matter at the distinguishing string byte.

### Follow a query, not just the record boundary

For a known-row R projection, skip any filter and prefix comparison. Relocating X
can reduce the demanded core, provided the chosen R accessor does not issue
loads reaching the omitted region. For a string predicate scan, a separate
prefix can be useful even if its full-row reconstruction is worse. If the prefix
matches, load enough metadata and tail to decide equality, then obtain the
requested projection. Share already acquired R fields and decoded state.

Now admit an optional dense tag/flags filter, as in three-array. It precedes
exact comparison for some scans and random negative probes. The same filter
must also be added to an appropriate monolithic control: otherwise a filter's
benefit gets misreported as a packing win. Its program can be bypassed for known
rows and for regimes where filtering adds work without avoiding enough demand.
Three-array measured exactly this distinction between selective and less
selective scans and random probes.

### A small trace that joins evidence, packets and updates

Make one fragment of R concrete: A8 and B4 encode a 12-bit counter
`u = A + 256*B`; C4 encodes `kind`. Here C is the nibble in that fragment, while
X is the independent cold byte. The query is:

```text
for rows where kind = 1 OR S = "abcdefghZ":
    return (row_id, (u + 1) modulo 4096)
```

Compare I7 and relocated-X. Both have a 64B core. I7 keeps X there and stores
seven prefix bytes; relocated-X stores eight prefix bytes and puts X elsewhere.
Every string below is nine bytes long, so length alone rejects none. Groups G0
and G1 each contain four consecutive rows and are the refinement execution grain.

| Row / group | kind | S | u | Does the query return it? |
| --- | --- | --- | --- | --- |
| 0 / G0 | 1 | `xxxxxxxxx` | 255 | Yes, independently of S |
| 1 / G0 | 0 | `abcdefgXZ` | 12 | No: byte 7 differs |
| 2 / G0 | 0 | `abcdefghY` | 30 | No: byte 8 differs |
| 3 / G0 | 0 | `abcdefghZ` | 511 | Yes, after exact string comparison |
| 4 / G1 | 1 | `yyyyyyyyy` | 4095 | Yes, independently of S |
| 5 / G1 | 0 | `abcdefgQZ` | 60 | No: byte 7 differs |
| 6 / G1 | 0 | `abcdefgRZ` | 70 | No: byte 7 differs |
| 7 / G1 | 0 | `abcdefgSZ` | 80 | No: byte 7 differs |

Kind evidence first establishes L = {0,4}, U = all eight rows. I7's prefix cannot
settle any other row, so its unresolved set is {1,2,3,5,6,7}. Relocated-X rejects
{1,5,6,7} with byte 7 and has unresolved set {2,3}. Both finish with exact matches
{0,3,4} and return counters {256,512,0}. An eight-byte prefix still cannot certify
row 3: byte 8 must be checked.

Thus relocated-X needs tail refinement only in G0; I7 needs both groups. Within
G0 it also reduces refined rows from three to two. These are exact demand counts
for this trace, not cache-line counts or timing predictions. If the two groups'
tail bytes share a fetched line, omitting G1 does not save that line. Their actual
allocation, access spans and start phase determine the physical saving. Tail
addresses come from R's locators, so acquiring a prefix separately does not make
those addresses ready. X is never projected by this query.

Two executable approaches over either image are now visible. A staged program
reads kind, performs unresolved string work, then reads AB for {0,3,4}. An eager
program reads four rows of ABC with groups `{2,1}`: four AB pairs form the first
eight packet bytes and C follows. It obtains kind and counters together, computes
the candidate increments and retains those results across exact refinement. The
latter can share acquisition and avoid a later AB read, but computes unused
results and keeps more state live. The shorter refinement path of relocated-X
changes how long that state is needed. This is an interaction between evidence,
representation and program; neither a 64B-width score nor an isolated packet
benchmark resolves it. Current Ikea makes both ordered projections possible;
its grouping measurements make the runtime comparison necessary.

For an explicit subsequent counter replacement, write row 3's returned value 512:
A changes from 255 to 0 and B from 1 to 2, while C must remain 0. The logical
counter changes; its encoding updates both the A byte and the B bits in the
shared B/C byte, preserving C. The actual issued spans come from the chosen writer. Changing only S in row 3 to `abcdefgXZ` instead makes the next query reject
that row. In I7 the distinguishing byte remains in the tail; in relocated-X it
changes the inline prefix. Any derived string tag or relevant summary also needs
repair. A counter-only change has no such string-evidence dependency.

In a later full-record workload, every row needs X and S. Prefix rejection no
longer omits tails, and relocated-X additionally needs its X stream. Either old
image can use a full-projection program and bypass filtering without conversion.
Whether future records should use I7, I8 or another family now requires pricing
that new demand mix. The same trace has exposed why a read advantage can change
with demand, and why rebinding and re-encoding are distinct responses.

### Resolve physical feasibility before assigning hardware costs

Resolve admitted Ikea layouts, including composed TuplePack units for regions
wider than 64B, code boundaries, SeriesPack tiles where used, strides, final-tile
occupancy and descriptors. Then resolve the programs' maps and issued spans.
An ideal bit sum or a 64B decoded packet is not a physical record contract.

Placement then changes where those programs obtain bytes: prefix adjacent to
core, separate dense prefix stream, aligned core, or padded row stride. Retain
core-only and conditional/full-tail programs. Distinguish an extension whose
address is known from the row ID from one whose locator must be decoded from R.
At low concurrency that dependent step may dominate; with independent requests
it may overlap. The existing 96/128B spatial study supplies motivation and an
arithmetic-address control, not a timing estimate for this locator-dependent case.

### Follow changes back through the same design

For each operation, record logical destinations, physical effects and repair
dependencies separately:

| Change | Logical destination | Effects and dependent work to price |
| --- | --- | --- |
| Replace counter u | u | Write codes A/B while preserving C; account for issued loads/stores and the owner's effect handling |
| Replace string S | S | Write its length/prefix/locator/tail as required; refresh derived tags and summaries; rebuild or reuse tail storage |
| Change cold X | X | Direct dense-plane update, or sparse presence/rank/payload work for the sparse alternative |
| Change a predictive reference, if that family is admitted | One logical source field | Re-encode dependent residuals according to the selected dependency graph |
| Relocate a row without changing values | No logical-value destination | Change the physical mapping; repair locators and affected rollup membership; no logical aggregate delta |

Three-array's shared cold payload is particularly instructive: a small patch
change can require rebuilding a payload that also contains the string. Separating
patches and strings is a candidate with different space and pointer costs. The
probability of touching cold state depends on the whole projection: eight
independent fields with 1% patch probability produce about 7.7% patched rows for
that projection. Real joint distributions should replace independence when known.

A predictive residual is a different transformation from a sparse exception.
It stays on the normal exact reconstruction path: a predicted target needs its
residual and raw references even when there are no PFOR exceptions. A full
projection may already have those references; an isolated target projection may
introduce extra block reads. Updating a reference must preserve dependent logical
targets by repairing their residuals. That conditional sharing and repair connect
PFOR to this cost problem, separately from borrowing its training methodology.

### Carry the choice across regions and time

Create independent containers in two known key regions. Give them different
string-length/prefix distributions, joint patch distributions and workload mixes.
Do not infer local behavior from the pooled mean. Train structural families on
earlier containers; fit prefix length, common widths, plane arrangement and
operation choices with a bounded budget on a new container. Compare with a fixed
layout, a collection-wide winner and an exhaustive finite local reference.

Then change the workload in a later window. The candidate actions are:

1. Keep the image and binding.
2. Rebind an existing image to another admitted program, including filter bypass
   or packet/traversal choices where applicable.
3. Encode only new containers with the newly preferred representation.
4. Convert selected old containers under explicit transition assumptions.

Retain one reader using an old image while new images use another representation.
Their actual descriptors and compound mappings must remain sufficient after the
advice changes. Price layout dispatch and any legal grouping of mixed-layout work.
Migration requires conversion, repair, overlapping images and owner costs; a
modeled benefit cannot establish an unimplemented publication mechanism.

The useful result is a **decision account**: what evidence made a plane useful,
what program exploited it, which local property changed the preferred member,
and which adaptation action repaid its cost. No candidate above is declared the
winner in advance.

## A nested bucket tests whether the account generalizes

The hash-map example is not just another way to seek a 64B boundary. It tests
whether a data unit can contain tunable children whose parameters change the
operation itself. A bucket holds N fingerprints in SeriesPack, N key/value tuples
in a TuplePack array, metadata including an overflow-rope reference, and any
specified alignment gaps. Fingerprint width b, N and placement are joint choices.

A negative lookup reads enough metadata/fingerprints to identify possible matches,
checks exact keys, and may follow overflow. A successful value projection also
reads values. A key-only negative path need not decode values. Increasing b can
reduce false exact comparisons while changing codec work and tile occupation.
Increasing N can reduce overflow while enlarging the fingerprint scan and entry
region. Slack is useful only if a different fingerprint width is actually admitted
by the codec and improves the complete lookup under the space constraint.

The first bucket measurements are a concrete warning: fewer false candidates
from wider fingerprints did not always repay decoding, and the verdict changed
between scalar and ordinary SeriesPack consumers. Search should therefore retain
`(N, b, child representation, placement, operation)` combinations, with occupancy
and rope behavior obtained from the actual table or a declared model. A formula
for independent false tags does not determine overflow or exact-key costs.

Bec256 adds a stronger test of nested metadata interpretation. A directory may
store lengths and checkpoints instead of every absolute address. A sequential
program can retain prefix state; a point program may reconstruct it. Population
and length evidence can remove body reads while making directory work a larger
fraction of the operation. Length-changing updates repair later addressing state.
The bucket need not adopt Bec256's format: the transferable requirement is that
metadata's interpretation, traversal state and maintenance accompany its bytes
into the cost comparison.

Together these cases distinguish a useful analyser from a packer. The record
case exercises evidence, relocation, exceptions and history. The bucket case
exercises capacity and compositional address discovery. Both require valid
representations, executable programs, conditional demand and lifetime costs.
