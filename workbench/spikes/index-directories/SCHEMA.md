# Schema lookup and selected segment migration

The `schema` driver measures the historical-definition tail and explicit
representation migration in the existing five-layer directory. Every segment
still has one representation and is identified by its layer and physical
prefix. Migration changes its encoding and directory interpretation while
preserving its logical contents. Other segments, including other segments at
that same layer, can continue using older definitions.

## Implemented recipe

The schema is an actual Orbital Object selected under the reader's Scope.
Definition IDs index a contiguous array; the dirent's 32-bit ID uses four of
the existing sixteen metadata bytes. An ID of 100,000 needs seventeen bits, so
a bare 16-bit field would not cover this experiment's historical population.
This experiment keeps IDs stable and appends definitions, including definitions
not yet used by any segment. That is an experimental policy, not a general
SixDB schema-version contract.

Each definition occupies 24 bytes: six 32-bit fields for ID, key width,
allowed-layer mask, summary codec, summary chunk size and auxiliary-Part count.
There is also an encoded StreamPack overflow layout. The lookup checks ID and
layer admission, then the reader cross-checks key width, summary geometry and
Part count against the selected segment. Actual Object versions and snapshot
selection remain Orbital's responsibility outside the 64-byte dirent.

The default three definitions admit every layer. Generated historical
definitions admit two layers; twenty appended test definitions each admit one
layer. Those twenty provide four representations per layer:

| Representation | Summary codec | Occupied-rank chunk | Auxiliary Parts |
| --- | --- | ---: | ---: |
| A | Ordinary integer fields | 64 | 4 |
| B | Bitwise-complemented min/max/sum | 64 | 4 |
| C | Complemented fields | 256 | 1 |
| D | Ordinary fields | One complete column | 0 |

Auxiliary payloads remain fixture tags, not implemented filters, sketches,
histograms or every other possible structure. The differing counts exercise
actual Part directories and StreamPack overflow shapes. They do not qualify
an arbitrary representation converter or an arbitrary logical-key codec.

`migrate` validates all requested segments, IDs, layer permissions and duplicate
targets before doing work. It captures selected segments and the affected
directory/overflow Objects, then publishes them together through the existing
Orbital Store. Unchanged logical bounds share their original Parts. An identical
target ID is a no-op. The subsequent [failure experiment](FAILURES.md) wraps
editor changes in an undo log and checks construction/refusal/abort retry. The
measurements below precede that writer change; invalid-request refusal alone
does not prove it.

## Two migration controls

`model-image` reconstructs every segment column from the private author model,
then compares/shares unchanged native Parts during capture. This preserves the
simple implementation as a measured control.

`source-transcode` reads the old summary recipe from the selected schema and
transcodes the selected native summary Parts into the requested codec/chunks.
It borrows unchanged positions, logical bounds, keys and tags directly from the
same protected segment. It supplies every newly placed trailing Part, including
auxiliary tags, so a former summary ordinal cannot accidentally become an
auxiliary source. The independent record-derived oracle checks both controls.

Codec-only migration alternates A/B. Part-layout migration alternates C/D,
changing Part count and overflow lengths/ranks. Raw addresses and compact
indirect memory cells are separate matched arms. Indirect cells can be reused
when their Object/ordinal set is unchanged; differing Part counts create a new
set. A representation change still updates the dirent even when cell addresses
are stable. The [locator study](LOCATORS.md) owns its snapshot/lifetime argument.

## Checks and measurement boundary

The correctness panel crosses J=0/2, raw/compact locators, flat/occupied-page
directories and both migration controls. It migrates selected segments at all
five layers while confirming that every unrelated segment realization stays
unchanged. Old readers retain the original three-definition schema; later
readers retain new interpretations across further migrations and value edits.
The checks include nullable integer count/min/max/sum, logical order, pending
reads, return to historical representations, invalid-batch refusal and no-op
publication. A separate history starts without overflow, introduces it by
migration, and removes the last overflow Object while both old states remain
readable.

The performance matrix uses 4,096 records, J=0/2, four schema populations
(32, 256, 4,096 and 100,000 definitions), and all four locator/conversion arms.
J=0 has logical duplicates spanning sparse physical segments; J=2 has sixteen
populated shared prefixes. The selected leaf has roughly 256 records. Each
migration burst performs 32 conversions of that leaf, retaining readers through
the burst. It measures a repeated selected-segment conversion, not a migration
scheduler's throughput across a whole index.

Lookup controls distinguish a new Scope/schema bind per lookup from repeated
lookups under an already protected schema. Hot-tail IDs use four current
definitions; wide IDs sample all definitions admitted at that layer. Complete
directory-to-summary reads consume a mixture of historic and migrated segments.
Arm and lookup-case order rotate across trials. Schema extension includes
definition construction, native capture and publication; the current contiguous
schema copies its full new definition column.

Schema-byte counters count actual native Part extents, including the overflow
recipe, separately for current and retained initial schemas. They exclude Object
and geometry metadata, author overrides, allocator rounding and scratch. Copied
bytes are native Image-to-Part copies, not all staging work. Migration counters
include directory dependencies and cell creation. The two controls can copy
the same native bytes while differing substantially in staging time. Timings
are per-trial burst averages and their medians, not service latency quantiles.

## Native results

Worker group `20261020T101048Z-c39fc7e0` captures the common source for
[Zen 5](evidence/schema-zen5/), [Granite Rapids](evidence/schema-intel/) and
[Neoverse V2](evidence/schema-neon/). Each passes the schema history check before
measuring. The retained panel includes all four definition populations, both J
values, every conversion/locator arm, five trials and counters. The C++ and
runner bytes match the implementation in `1407e479`. Native settings are Clang
21.1.8, Release `-O3 -g1`, both vectorizers enabled and one pinned CPU, with
`znver5`/`zen5`, `graniterapids`/`granite-rapids` and
`armv8-a+simd`/`neoverse-v2` respectively.

The following lookup controls use compact cells/native-source conversion,
J=0, and 16,384 lookups per trial. Values are five-trial medians in ns,
shown as 32 → 100,000 definitions:

| Target | Protected hot-tail lookup | Protected wide-ID lookup | New Scope/schema bind, wide IDs | Complete directory/summary read |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 | 3.61 → 3.62 | 3.57 → 5.18 | 11.39 → 15.94 | 132.6 → 130.3 |
| Granite Rapids | 4.43 → 4.44 | 4.44 → 6.05 | 12.39 → 20.46 | 131.3 → 130.0 |
| Neoverse V2 | 6.95 → 6.73 | 6.37 → 10.52 | 20.25 → 43.13 | 204.0 → 203.8 |

An unused historical tail does not require a scan during ID lookup. Wide-ID
access pays a locality cost, especially on ARM. These are warmed in-memory
controls over up to 2.4 MB of definitions, not above-LLC or remote-schema
measurements. The complete read uses only the definitions selected by the
actual mixed population; its historical-tail result is not a wide-ID result.

The current native schema payload grows from 776 to 2,400,008 bytes, including
the eight-byte overflow recipe. The retained initial three-definition payload
is another 80 bytes. Full-copy extension from three to 100,000 definitions takes
875/1,361/1,455 µs on Zen 5/Intel/ARM, versus 5.57/6.75/6.50 µs to reach 32.
That includes constructing the full column and publishing it. This is a bulk
extension measurement, not a measured marginal single-definition append.
The implementation nevertheless copies the entire array on every extension;
fast lookup does not make that writer cost bounded. A paged immutable schema is
a candidate if representation creation makes this cost material.

Selected-leaf migration at 100,000 definitions, J=0, in µs per conversion:

| Target / transition | Raw model image | Raw native source | Compact model image | Compact native source |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 / codec only | 19.49 | 6.99 | 18.28 | 5.66 |
| Zen 5 / Part layout | 20.22 | 7.22 | 20.43 | 7.52 |
| Granite Rapids / codec only | 26.31 | 8.48 | 24.30 | 6.59 |
| Granite Rapids / Part layout | 25.25 | 7.79 | 25.15 | 8.14 |
| Neoverse V2 / codec only | 32.34 | 9.74 | 29.29 | 7.77 |
| Neoverse V2 / Part layout | 30.22 | 8.75 | 30.64 | 9.08 |

J=2 shows the same conversion result: compact native-source codec/layout
migration is 5.42/6.95 µs on Zen 5, 6.23/7.72 on Intel and 7.21/8.71 on ARM.
These change one selected segment's representation; they are not five-layer
record replacements or LMDB-equivalent point updates.

For Zen 5 compact codec migration, native copied bytes are identical in the
model and source arms: 11,072 per conversion. Both publish three Objects and
create no locator cells. Source conversion reduces image construction from
11.68 to 1.23 µs and segment capture from 3.78 to 1.57 µs. It avoids staging and
comparing unchanged columns, which native-copy counts alone cannot expose.
For Part-layout changes both arms copy 13,296 bytes and publish four Objects;
compact cells add 74 requested bytes per conversion as the Part count changes.
Compact Part-layout migration is slightly slower than raw-source migration in
this panel. Preserve the extra ownership cost rather than assuming indirection
always wins.

All eight Release drivers passed captured run `20261020T101048.972299Z`.
All eight also passed ASan/UBSan in 28.46 seconds; the schema histories account
for 4.11 seconds. This establishes the tested synchronous publication and
retained-reader histories. Construction rollback was unqualified at that source
cut; the subsequent [failure study](FAILURES.md) supplies its editor histories.
Cross-worker concurrency, arbitrary conversion recipes and portable schema
reopen remain unqualified.
