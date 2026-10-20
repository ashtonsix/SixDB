# Maintained directory pages

The directory tracks segments, logical bounds, representations and Part
locators. This experiment compares its structural maintenance through supplied
segment-version changes. The fixture's records and summaries produce those
changes and independently check reads; they are not the directory's entries or
an Engine storage implementation. This driver is part of the directory spike,
separate from the proposed Engine-versus-LMDB driver.

## Compared storage recipes

`repartition-roster` is the earlier occupied-page control. Pages contain at
most 64 occupied dirents. Structural changes repartition the complete per-layer
roster; StreamPack overflow blocks use its global occupied ranks. A single
segment birth/death can therefore move many page and overflow coordinates.

`stable-pages` keeps an ordered set of page lower fences. The first fence is
zero, so insertion before the first live segment needs no new namespace rule.
A fence can remain after its original segment disappears. It is a directory
storage boundary, not a replacement identity for any segment. All supplied
segment identities remain index/layer/physical-prefix qualified.

An overfull page splits near its midpoint. Occupancy below one quarter prompts
a sibling merge if the combined contents fit; otherwise the pair redistributes
around its midpoint. The experiment uses 16- and 64-entry pages in checks and
64 in measurements. These thresholds are a tested control, not a selected
Engine policy. A batch may touch several pages, with work charged to those
pages and their affected siblings. Bulk initial construction can split several
times; it is outside update timing.

Each leaf directory page also owns one StreamPack overflow block. Its anchor
is the block coordinate and occupied local rank selects the segment's extra
Part locators. At most 256 dirents fit this recipe's block. Splits, merges and
redistribution repair ranks inside the affected pages, while other pages keep
their coordinates. The first two layers have at most 1/256 segments and retain
the single bounded-roster recipe. The existing global-block control remains
available for comparison.

Overflow rebuilding can be skipped when segment membership, coordinates and
extra locator tokens are unchanged. A representation/Part-count change must
still replace the affected block. Empty extra-locator lists retain their local
rank when other segments in that block still need overflow. Old readers select
the old directory and block versions under their original Orbital Scope.

The actual Ikea hash owner is rebuilt for an affected shared-prefix page,
including growth, shrinkage and spill changes through the history. Unaffected
hash pages remain shared. This bounds one hash reconstruction by page capacity;
it does not establish an independently mutable in-place hash protocol.

The directory's outer page roster remains flat. Its traversal and descriptor
copy cost are included. This experiment establishes maintained leaf partitions
and bounded overflow repair, not population-independent complete updates.
Root scaling remains an implementation question in the coverage audit.

## Workload and measurement boundary

Each initial directory population is recorded explicitly as `segments_L0`
through `segments_L4`. The two shapes at 4,096 layer-4 segments are:

| J | L0 | L1 | L2 | L3 | L4 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 0 | 1 | 1 | 1 | 32 | 4,096 |
| 2 | 1 | 32 | 4,096 | 4,096 | 4,096 |

The smaller cases start with 256 layer-4 segments. The J=0 producer has equal
logical keys spanning 17 adjacent segments; the J=2 producer has a distinct
two-byte logical/physical prefix per leaf segment. Interior gaps admit new
segment prefixes while preserving logical order. Each burst has 96 edits.

| Trace | Directory input/change pressure |
| --- | --- |
| `segment-rewrite` | Replaces five segment versions per edit without changing the segment population. This deliberately stresses descriptor/locator refresh across layers; it is not a claim that ordinary relabeling requires five rewritten summaries. |
| `segment-insert` | Adds an interior leaf prefix, with corresponding supplied upper-segment versions; grows occupied pages and shared-prefix hash pages. |
| `segment-remove` | Removes a consecutive run of leaf segments; drives underflow, redistribution, merge and overflow-rank repair. |
| `mixed-reads` | Interleaves segment birth/death, value/position changes and four directory-to-Part point reads plus one equal-key range per edit. |
| `representation-parts` | Changes one selected layer-4 segment's codec/Part recipe from four auxiliary Parts to none; other segment versions stay unchanged. |

`ns_per_operation` is `dirent_ns + directory_ns`, divided by edits. It includes
forming/replacing cached dirents, page balancing, fence/rank repair, directory
and overflow images, hash construction and actual native directory captures.
It excludes record-model work and preparation of the supplied segment bodies.
The initial populated directory, trace/oracle construction and old reader are
prepared outside timing. Each arm gets its own fresh Runtime, Worker, Pool and
Arena, also outside timing; the burst itself exercises that backend's history.
Arm order alternates across trials.

Coupled Orbital `prepare_ns`/`install_ns` are reported separately: their packet
contains both supplied segment versions and directory changes. They are not
claimed as isolated directory publication costs. `fixture_ns_per_operation`
retains the complete producer/edit/read cycle as a diagnostic, not directory
latency. The mixed trace's reads are charged there, separately from the primary
directory mutation timer. All reads are checked against the precomputed oracle.

`copied_bytes`, `allocated_bytes`, `directory_captures` and
`journal_record_bytes` count directory/overflow work only. Fixture-wide native
bytes/captures remain separately named. Captures count constructed
representations, excluding tombstones. Inverse-record bytes include headers and
padding but exclude retained map nodes, pointer-vector storage and unused arena
capacity. Neither counter is RSS. `segment_changes` counts supplied segment
versions and deletions; `dirent_changes` counts unique changed dirents per edit,
including overflow coordinate changes. Traversal counters count actual visited
entries, with balancing separate from builder and overflow visits; they are not
modeled cycles or physical writes.

## Checks and evidence

The history check crosses J=0/2, raw/compact locators and page capacities 16/64.
An independent record oracle checks every layer after every edit, plus point
and equal-key range reads. It retains an older Scope throughout, deletes every
segment and recreates the directory. A maximum physical prefix/local position
and maximum logical key check the last fence without successor overflow.
Schema conversion changes auxiliary-Part counts while old interpretations remain
selected by the retained reader.

Dedicated construction/prepare/accepted-abort sweeps exercise page split, merge,
redistribution and first population. A second sweep refuses every actual
metadata/journal allocation on fresh fixtures with no local journal capacity.
Failed attempts must preserve the published cut, fixture rows and old/new reads;
the identical request must then succeed. The writer uses the allocation-free
reverse replay from the [failure experiment](FAILURES.md).

The initial local run `20261020T110836.445193Z` passed 160 splits, 40 merges and
56 redistributions, 1,251 injected failures and 5,044 actual allocator refusals.
Its headline timing was fixture-wide; it is a diagnostic precursor, not the
directory result. The revised local run `20261020T111748.121806Z` established the
directory phase boundary before the final counter labels were added.

The final source is worker group `20261020T111931Z-465c14b8`. Its C++ and runner
bytes match this implementation. The selected [Zen 5](evidence/maintenance-zen5/),
[Granite Rapids](evidence/maintenance-intel/) and
[Neoverse V2](evidence/maintenance-neon/) exports retain all 200 samples per
target, all 40 cases and counters. All three pass the same 160/40/56 structural
events and 1,251/5,044 failure sweeps. All ten local directory research checks
pass ASan/UBSan in 127.19 seconds; maintenance accounts for 70.33 seconds.
Receipts, raw check output, binaries and sources remain in the archives.

## Directory results

Five-trial median directory mutation time in microseconds, starting with 4,096
layer-4 segments and the per-layer populations above. Each cell is
`repartition-roster / stable-pages`. The mixed trace interleaves reads, but its
primary number still times directory mutation only.

| J | Segment event | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | --- | ---: | ---: | ---: |
| 0 | Rewrite | 551.5 / 25.2 | 1,035.1 / 45.1 | 873.5 / 44.8 |
| 0 | Insert | 862.6 / 63.6 | 1,571.3 / 94.8 | 1,444.9 / 94.0 |
| 0 | Remove | 821.1 / 40.9 | 1,442.3 / 68.9 | 1,309.8 / 64.3 |
| 0 | Mixed, with reads between edits | 578.9 / 40.5 | 1,033.4 / 60.6 | 934.3 / 58.5 |
| 0 | Representation/Parts | 24.1 / 15.4 | 38.1 / 23.2 | 39.4 / 25.5 |
| 2 | Rewrite | 2,960.9 / 72.7 | 4,457.6 / 86.4 | 4,751.0 / 90.6 |
| 2 | Insert | 4,811.6 / 166.6 | 6,771.8 / 213.4 | 8,363.5 / 214.8 |
| 2 | Remove | 4,486.1 / 86.2 | 6,218.0 / 136.5 | 7,699.4 / 139.5 |
| 2 | Mixed, with reads between edits | 3,013.4 / 92.0 | 4,407.2 / 121.3 | 5,042.9 / 124.4 |
| 2 | Representation/Parts | 34.6 / 24.1 | 44.6 / 26.7 | 46.3 / 29.1 |

The five-segment rewrite has exactly five changed dirents in both arms. At J=0,
builder traversal falls from 4,197 to 165 entries and overflow traversal from
4,131 to two; directory captures fall from 91 to ten. At J=2 the corresponding
figures are 12,513 to 417, 12,321 to 33, and 247 to ten. Native directory copying
is unchanged: 10,560/26,688 bytes per edit at J=0/2. Less copying alone therefore
cannot explain the difference. These are actual logical-work and native-copy
counters, not measured cache misses or durable writes.

Insertion also exposes rank repair. At J=0, five supplied segment changes create
an average 2,139 changed dirents in the global-roster control, versus 35.5 with
local ranks. At J=2 these are 6,408 and 96.5. Directory copying falls from about
264 to 18.7 KB and 786 to 49.5 KB respectively. Local repair is bounded by the
affected pages; the number of page events can grow with the input batch.

The single-segment representation/Part recipe has one changed dirent. Both arms
construct three directory/overflow representations. The affected overflow block
contains 256 versus 64 entries, and directory copying falls from 17,295 to
11,219 bytes per edit. This is a more modest gain than the broad-roster stress
case and is retained alongside it.

The tradeoff is occupancy and a larger outer roster. The 96 interior insertions
cause 58 leaf splits in each large per-layer directory: 64 initial pages become
122, holding 4,192 segments at about 54% capacity. The repartition control uses
66 pages. Extra page and StreamPack metadata, retained versions and backing
retention are not established by the update-byte counters. Their ownership
economics remain in the broader locator/colocation work.

The flat outer roster still shows through. On Zen 5, the J=0 single-segment
recipe costs 10.4 microseconds with 256 layer-4 segments and 15.4 with 4,096;
builder visits rise from 68 to 128 even though the leaf capacity stays 64. The
J=2 recipe rises from 10.5 to 24.1 microseconds with those same visited-entry
counts, in a much larger surrounding Object population. That second difference
is not attributed solely to the root. The results favor stable local page and
overflow repair over complete repartition, while leaving root scaling, memory
economics, arbitrary byte keys and wider resource histories open. No Engine
end-to-end or LMDB speedup is claimed.
