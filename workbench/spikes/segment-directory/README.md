# Segment directory

**What shape should each directory layer take so point access, ordered
traversal and edits stay bounded as an index grows?**

Started 22 October 2026 to take over `engine::directory`. The chosen structure
has **graduated** into [`engine::directory`](../../../engine/README.md#segment-directory),
on Orbital Objects with its own material. This spike keeps the evidence, the
counter model and the controls that were rejected. The graduated code
searches its planes with `ikea::boundlist` and drops this spike's in-node
summaries, which showed no measurable time benefit. This spike covers only
the directory structure: how each layer is laid out, searched and maintained.
Segment codecs, aggregates, consumers and peers are out of scope. The
[previous directory spike](../directory/README.md) keeps that wider evidence.

## Starting point

Ashton's brief: five layers over overlapping 16-bit physical-key infixes; plain
tables at L0/L1; hashtables at L2…J (partitioned at J ≥ 3); balanced,
partitioned boundlists with dirent slots above J; 64-byte dirents. Physical
placement preserves logical order.

What the earlier work established, and how it changes the start:

- **The flat root does not scale.** The previous spike gives every layer one
  root Object listing partitions, with 64-byte rows. One fence-moving birth
  rewrote 765 KB at 1M Segments (`/mnt/project-files/directory-structure-review-20261022.md`).
  The control below reproduces this: 721 KB.
- **Natural partitions bound how many partitions there are, not how big they
  get.** At L3, natural24 is one partition, so a 64-byte edit copied 8.65 MB at
  65K Segments ([CONCENTRATION.md](../directory/CONCENTRATION.md)). Clustered
  L4 keys behave the same way.
- **Dirents inside the hash** make ordered scans probe once per entry, and
  value edits clone the hash ([REVIEW-20261022.md](../directory/REVIEW-20261022.md)).
  That spike's suggested fix, a key-to-rank hash plus a tiled dirent plane,
  was never measured.
- **No-op and mixed edits** were fixed in the previous spike after the review.
  Here they publish nothing by construction, and the check asserts it.
- **Calico** ([TRIE.md](../../../../calico/design/TRIE.md)) addressed nodes by
  prefix through one hash and walked successors in address order. It shows
  that relocation is cheap when location lives in one place. Its
  early-terminal and varlen handling is not reused.
- **Copy volume alone does not choose the grain.** The previous spike's
  16-dirent tiles cut copied bytes but slowed some complete cycles
  ([FINDINGS.md](../directory/FINDINGS.md)). The counters here rank layouts.
  They don't select one without native timings.

## The structure under test

Every layer above L1 is a tree of bounded nodes ordered by physical prefix:
- **Leaves** hold a prefix plane, a dirent plane in 16-dirent Parts, and, above
  J, the min/max logical fence planes.
- **Interior rows** hold a physical separator, a logical separator above J, and
  the child's Object name. There is no 64-byte row.
- **The root keeps a fixed name.** When it overflows the tree gets deeper, so
  no Object grows without bound.
- **Bulk build fills nodes to 75%.** Nodes merge or redistribute below 25%.
- **Separators are loose.** A separator changes only when an edit would violate
  it, which happens only on a birth or a fence change beside a boundary. Child
  Objects keep stable names, so a new leaf version leaves its parent alone.

Three treatments for layers at or below J, where the logical prefix is the
physical one. Hash pages use linear hashing over a hash of the prefix, so page
size stays bounded however keys cluster, and page names are computed.

| Variant | Dirents live in | Point access |
| --- | --- | --- |
| `tree` | ordered leaves | tree descent |
| `tree-hash` | ordered leaves | hash page (prefix → leaf), then the leaf |
| `tree-inlineN` | hash pages of about N entries (closest to the brief) | hash page only; leaves hold just prefixes for order |

The controls reproduce the previous spike's structures: `flat` has full
leaves, one unbounded root and 64-byte exact-bound rows; `naturalB` has one
inline-dirent hash per `prefix >> B`.

## Counters

[`store.h`](store.h) models Orbital Objects: stable names, versions made of
immutable Parts, and unchanged Parts shared with the previous version. Edits
count new Part bytes and published or erased Objects. Reads count Object visits
and distinct 64-byte lines touched by the real search code, with each Part
line-aligned. Every line is counted as a miss, so caching of upper levels is
not modelled. Edits are single and applied in
sequence from the bulk-built state, so the first births see build slack.
K = 12. "Dense" means half the prefixes of one region of 2N; "spread" is
uniform over the layer's domain.

## Results at 1M Segments

From `run.sh`. Bytes are means per edit; lines and Objects are means per
operation. Dense and spread agree for the tree variants, so one row is shown.

**Above J (L4, J = 0, logical fences):**

| Variant | B/Segment | find | logical seek | scan / entry | value | birth | death |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| flat (old) | 92.8 | 16.1 ln, 2 obj | 21.2 ln, 2 obj | 1.45 ln | 8,192 B | **720,830 B** | 23,465 B |
| tree | 92.3 | 17.0 ln, 4 obj | 23.7 ln, 4 obj | 1.45 ln | 1,027 B | 6,313 B | 6,266 B |
| tree, untiled | 92.3 | 17.0 ln, 4 obj | 23.7 ln, 4 obj | 1.45 ln | 6,147 B | 8,940 B | 8,756 B |
| tree, leaf 64 | 92.7 | 16.3 ln, 4 obj | 22.9 ln, 4 obj | 1.47 ln | 1,024 B | 3,438 B | 3,310 B |
| tree, leaf 256 | 92.2 | 14.6 ln, 3 obj | 21.2 ln, 3 obj | 1.45 ln | 1,024 B | 12,071 B | 12,030 B |

The tree's edit cost holds steady from 4K to 1M Segments: 6.3–6.5 KB per
birth. Flat goes from 7.7 KB to 721 KB. Reads pay two extra Object visits at
1M but touch about the same number of lines.

**At or below J (L3, J = 4; L4 is the same within a few percent):**

| Variant | B/Segment | find | scan / entry | value | birth | death |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| natural24 (old), spread or dense | 148 | 3.5 ln, 1 obj | 2.5 ln | **151 MB** | **155 MB** | 155 MB |
| natural16 (old), spread | 148 | 3.5 ln, 1 obj | 2.2 ln | 590 KB | 606 KB | 606 KB |
| natural16 (old), dense | 148 | 3.1 ln, 1 obj | 2.0 ln | 4.7 MB | 4.8 MB | 4.8 MB |
| tree | 68.2 | 17.0 ln, 4 obj | 1.08 ln | 1,025 B | 4,005 B | 3,963 B |
| tree-hash | 100.3 | 6.9 ln, 2 obj | 1.08 ln | 1,025 B | 10,887 B | 10,852 B |
| tree-inline24 | 145 | 3.5 ln, 1 obj | 3.5 ln, 1 obj | 3,856 B | 4,487 B | 4,176 B |
| tree-inline64 | 147 | 3.5 ln, 1 obj | 3.6 ln, 1 obj | 9,171 B | 9,732 B | 9,527 B |

At L4 with spread keys, natural24 behaves like natural16 at L3 above (590 KB
per edit). With dense L4 keys it is the 151 MB row.

These tables predate in-node summaries. Every 16th separator or prefix now sits
in the node's header line, and every 16th key in a small Part. That cuts tree
point lookups from 17.0 to 12.0 lines at 1M, and logical seeks from 23.7 to
21.6. Edits grow by about 100 bytes because the header changes with each birth.

## Read timings

[`image.h`](image.h) flattens each variant's built layout into one aligned
arena. Every Object is a 64-byte record in one name table holding its Part
offsets, so a visit is one table probe, and every variant pays for visits the
same way. That is a stand-in for Orbital's name resolution, not a measurement
of it. The lean views in `image.h` read the published bytes directly, and are
checked against the model before timing. Edits aren't timed, because the
model's writer decodes and re-encodes vectors, which is nothing like a
production writer.

Graviton4 (`c8g.2xlarge`), one pinned CPU, medians of seven alternating
repetitions, ns per operation ([evidence](evidence/reads-neoverse-v2-20261022/README.md)).
"Dependent" chains each lookup on the previous result; "independent" lets them
overlap.

**At or below J, L3 spread** (dense and L4 agree within ~10%):

| Variant | Segments | point, dependent | point, independent | seek + read | scan / entry |
| --- | ---: | ---: | ---: | ---: | ---: |
| tree | 64K | 113 | 69 | 73 | **2.8** |
| tree-hash | 64K | 121 | 58 | 77 | 3.0 |
| tree-inline24 | 64K | 72 | 26 | 122 | 25.9 |
| natural16 (old) | 64K | 50 | 16 | 72 | 8.1 |
| tree | 1M | 298 | 177 | 183 | **4.2** |
| tree-hash | 1M | 435 | 184 | 192 | 4.9 |
| tree-inline24 | 1M | 240 | 68 | 302 | 68.2 |
| natural16 (old) | 1M | 145 | 36 | 195 | 20.1 |
| tree | 4M | 434 | 231 | 247 | **4.4** |
| tree-hash | 4M | 626 | 278 | 258 | 4.5 |
| tree-inline24 | 4M | 350 | 93 | 488 | 92.5 |
| natural16 (old) | 4M | 168 | 38 | 300 | 21.0 |

**Above J, L4 J = 0, spread:**

| Variant | Segments | point, dependent | logical seek + read | scan / entry |
| --- | ---: | ---: | ---: | ---: |
| flat (old) | 1M | 277 | 333 | 4.6 |
| tree | 1M | 310 | 388 | 5.2 |
| tree, no summaries | 1M | 311 | 368 | 5.2 |
| flat (old) | 4M | 413 | 494 | 5.1 |
| tree | 4M | 443 | 581 | 5.4 |
| tree, no summaries | 4M | 455 | 526 | 5.4 |

## Through Orbital Objects

A reader since removed (recoverable from commit `64cab102`) published each
built layout as Objects through `engine::material`, with one grouped allocation per Object and one
Definition per distinct Part geometry. The same views then read through a
`Store::Scope`: one `Scope::bind` per visit and `Selection::part` per Part.
Graviton4, `c8g.4xlarge`, ns per operation
([evidence](evidence/orbital-reads-neoverse-v2-20261022/README.md)):

| L3, J = 4, spread | Segments | point, dependent | point, independent | seek + read | scan / entry |
| --- | ---: | ---: | ---: | ---: | ---: |
| tree | 1M | 520 | 508 | 516 | **8.3** |
| tree-inline24 | 1M | 583 | 298 | 1,241 | 303 |
| natural16 (old) | 1M | 183 | 96 | 376 | 31 |
| tree | 4M | 772 | 751 | 751 | **8.9** |
| tree-inline24 | 4M | 764 | 402 | 1,642 | 420 |

| L4, J = 0, spread | Segments | point, dependent | point, independent | logical seek + read | scan / entry |
| --- | ---: | ---: | ---: | ---: | ---: |
| flat (old) | 1M | 440 | 387 | 583 | 7.3 |
| tree, 128-row interior | 1M | 543 | 521 | 749 | 8.2 |
| tree, 512-row interior | 1M | 508 | 484 | 712 | 8.2 |
| flat (old) | 4M | 750 | 650 | 898 | 8.5 |
| tree, 128-row interior | 4M | 831 | 814 | 1,076 | 9.6 |
| tree, 512-row interior | 4M | 806 | 767 | 1,003 | 9.5 |

- **The decision holds and gets stronger.** Under Orbital, inline hash pages
  lose their dependent-point advantage: they are 12% slower than the tree at 1M
  and level with it at 4M. They keep a 1.7–1.9× edge on independent points but
  scan 36–47× slower.
- **Each Orbital visit costs more than a flat-table probe,** and independent
  tree lookups barely overlap: 508 ns independent against 520 dependent at 1M,
  where the Image overlaps them to 227 against 416. One `bind` per visit
  appears to keep lookups from overlapping, but this is not diagnosed. It
  is worth raising with Orbital, because it limits every multi-visit structure.
- **Wider interior nodes reclaim a visit.** With 512-row interior nodes the
  tree is three Objects deep up to about 14M Segments, so reads get 3–7% faster.
  Mean edit cost is unchanged; the largest separator rewrite grows from 4.6 to
  8 KB. 512 is now the default. The counter tables above used 128.
- **The deepening root's read cost is 7–15% under Orbital on dependent points**
  (flat 440 vs tree 508 at 1M; 750 vs 806 at 4M), plus 18–25% on independent
  points. That is the price of bounded root edits (721 KB → 6.3 KB per birth).
  Holding upper levels as direct references rather than names would recover
  most of it, if Orbital can offer that.

## Edit grain beyond single edits

From the `scenario-*` rows in `run.sh`. Each batch is one publication, charged
against the versions it replaces. Mixed batches are one-third births, deaths
and value edits at uniform positions. Churn alternates random births and
deaths for N/2 operations. Appends go past the current maximum.

| L4, J = 0, dense | Segments | B/edit, batch 1 | batch 256 | batch 4096 | churn | append, single | append, batch 256 | fill after appends |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| flat (old) | 64K | 9,384 | 6,152 | 1,504 | 8,252 | 50,210 | 327 | 0.83 |
| tree, halves split | 1M | 4,577 | 4,544 | 4,087 | 6,355 | 3,415 | 116 | 0.68 |
| tree | 1M | 4,577 | 4,544 | 4,087 | 6,355 | 2,904 | 111 | 0.75 |
| tree, L3 J = 4 | 1M | 2,991 | 2,979 | 2,635 | 3,966 | 932 | 76 | 0.75 |

- **Uniform batches barely help at 1M.** A batch of 4,096 touches about 3,400
  of 11,000 leaves, so it saves only 11%. At 64K Segments the same batch cuts
  per-edit bytes by 3.4×. Batching pays when edits cluster, not by count alone.
- **Appends are where batching and the split rule matter.** Batches of 256 cost
  111 B per append. Single appends with half splits leave the left leaf at 50%
  forever, so fill decays to 0.68. The rightmost node now splits off
  build-occupancy pieces instead, which holds fill at 0.75 and saves 15% of
  bytes. The flat root's 50 KB per append is the root rewrite again.
- **Births and deaths shift the dirent tiles after them.** That shift is most
  of a churn edit's 6.3 KB, against 1 KB for a value edit. Slotted dirents
  (stable slots plus a 1-byte rank permutation) would cut it, at the cost of
  one indirection on scans. It's an option, not yet measured.
- **Fixed: merges now happen only on shrink.** A leaf left small by an append
  split could merge on a later identical replacement, which then published
  something. Only nodes that shrank now merge, and the no-op check covers it.

## What this shows

1. **A bounded, deepening root removes the flat root's growth for about 10%
   on reads.** Births drop from 721 KB to 6.3 KB at 1M Segments and stay
   there. Two extra Object visits cost 7–12% on point reads and 15–18% on
   logical seeks at 1M and 4M.
2. **Slack and loose separators keep most edits to one leaf.** After a 75%
   build, the first births don't split, and births mostly leave the parent
   alone.
3. **Object visits, not lines, set read time.** Natural partitions are the
   fastest point path, most likely because there are only a few hundred of
   them, so their name-table entries stay cached (inferred, not profiled). In-node summaries cut line counts by a third
   but made no measurable difference to time: adjacent lines in one node are
   cheap. Line counters overstate the cost inside a node and understate the
   cost of visiting an Object.
4. **The prefix→leaf hash index loses.** It is 45% slower than plain descent on
   dependent points, because the descent's upper levels are hot and a hash
   page is not. It also costs 2.7× more per birth. Dropped.
5. **Your design (dirents inline in bounded hash pages) and dirents in leaves
   trade point speed for scan speed.** Inline is 1.2× faster on dependent
   points and 2.5–2.6× faster on independent points. Leaves are 16–21× faster
   on scans and 1.6–2× faster on seek+read, edit 1–4 KB against ~4 KB, and
   take 68 B/Segment against 145. Bounding inline pages by hash bits removes
   natural24's 151 MB edits either way.
6. **Hash layers don't need fences.** Leaving them out saves 24 of 92 bytes per
   Segment at K = 12.

## Decision

On 22 October Ashton chose **ordered leaves at every layer above L1**. At or
below J the tree has no fences and routes by prefix alone; above J it adds
logical fences and separators. The accepted cost is 2.5–2.6× slower independent
point lookups at the hash layers than inline hash pages give, and about 1.2×
slower dependent ones. In return, scans run 16–21× faster, seek+read is
faster, edits are smaller, and one partition format serves every layer. The
`tree-hash` and `tree-inlineN` variants stay as controls.

## Open next

- Visit cost dominates. Ask Orbital why independent `Scope::bind` lookups
  don't overlap, and whether a reader can hold direct references to upper
  directory levels across a Scope.
- Slotted dirents for births and deaths, if churn-heavy layers matter.
- Time `engine::directory`'s edit path (capture with retained Parts and one
  packet per batch) once callers exist. Validation beyond the oracle checks
  waits on more of the architecture being connected.

## Reproduce

On Linux from the SixDB root (prefix with `orb -m ubuntu` on macOS):

```sh
cmake --preset dev -B build/segment-directory -DSIXDB_SPIKES=segment-directory
cmake --build build/segment-directory --target segment_directory segment_directory_bench
build/segment-directory/workbench/spikes/segment-directory/segment_directory --check
workbench/spikes/segment-directory/run.sh > build/segment-directory/counters.txt
python3 workbench/tools/worker.py run workbench/spikes/segment-directory/bench.sh \
  --machine neoverse-v2 --instance-type c8g.4xlarge --env SIXDB_BUILD_JOBS=8
```

`--check` compares 92 configurations against an ordered oracle. Each runs
12,000 random single births, deaths, fence changes, value edits and identical
replacements, then 150 batches of up to 96 edits, some of which empty the
layer. Capacities are small, so trees reach 3–5 levels, drain and
regrow, and some cases use nodes wide enough for summaries. It validates
separators, hash pages, find, prefix and logical seeks, and that identical
replacements publish nothing. Deliberately broken separator upkeep,
exact-bound rows and hash-page moves each fail the check. The counter matrix
takes about two minutes. Single rows
are `segment_directory run LAYER J COUNT dense|spread VARIANT...` and
`segment_directory_bench LAYER J COUNT dense|spread QUERIES REPS VARIANT...`.
