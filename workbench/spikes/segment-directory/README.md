# Segment directory

**What shape should each directory layer take so point access, ordered
traversal and edits stay bounded as an index grows?**

Started 22 October 2026 to take over `engine::directory`. This spike covers only
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

## Counters, not timings

[`store.h`](store.h) models Orbital Objects: stable names, versions made of
immutable Parts, and unchanged Parts shared with the previous version. Edits
count new Part bytes and published or erased Objects. Reads count Object visits
and distinct 64-byte lines touched by the real search code, with each Part
line-aligned. Every line is counted as a miss, so caching of upper levels is
not modelled. There are no clocks here. Edits are single and applied in
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

## What this shows

1. **A bounded, deepening root removes the flat root's growth.** Births drop
   from 721 KB to 6.3 KB at 1M Segments, and the drop holds at any
   population. The cost is a deeper descent: 4 Objects instead of 2 at 1M,
   with the same line count. Upper levels are small and hot, so those visits
   are probably cheap, but that is an assumption until timed.
2. **Slack and loose separators keep most edits to one leaf.** After a 75%
   build, the first births don't split. Births mostly don't touch the parent.
3. **Bounding hash pages by hash bits fixes the population problem, with
   dirents inline or not.** `tree-inline24` keeps the brief's one-Object,
   ~3.5-line point access and edits ~4 KB regardless of clustering. That is
   40,000× less than natural24 at L3. The trade-off is in ordered scans, which
   pay a hash probe and an Object visit per entry (3.5 lines vs 1.08).
4. **Leaves with a hash index are the middle option.** They need 2 Objects and
   ~7 lines per point lookup, scan at 1.08 lines, and keep value edits at one
   1 KB tile. Births cost 2.7× more than the plain tree because the hash page
   and leaf splits rewrite pages. Dropping the hash gives the cheapest edits and
   smallest footprint (68 B/Segment), but 17 lines and 4 Objects per lookup at 1M.
5. **Hash layers don't need fences.** Leaving them out saves 24 of 92
   bytes per Segment at K = 12. Physical order is logical order here, so
   seeks use the prefix directly. Nothing in this model needs suffix pruning.
6. **Interior search is line-hungry.** At 96 rows per node, a binary search
   over 4-byte separators touches about four lines per node. An in-node
   summary line, or Eytzinger order, would likely halve descent lines for
   every tree variant. It's the cheapest next improvement.

## Open next

- Time the three treatments at or below J natively on point, scan and
  edit loops, with upper levels warm. The counters can't say whether one extra
  Object visit costs more than scanning 2.4 extra lines per entry.
- Add in-node search summaries and re-measure descent.
- Batched edits, steady churn, and runs of births at one hot spot
  (append-heavy). Edits here are single and uniformly placed.
- Bind the chosen layout to `engine::material` Objects and Scopes. The
  counter model maps one Part to one material Part.

## Reproduce

On Linux from the SixDB root (prefix with `orb -m ubuntu` on macOS):

```sh
cmake --preset dev -B build/segment-directory -DSIXDB_SPIKES=segment-directory
cmake --build build/segment-directory --target segment_directory
build/segment-directory/workbench/spikes/segment-directory/segment_directory --check
workbench/spikes/segment-directory/run.sh > build/segment-directory/counters.txt
```

`--check` compares 56 configurations against an ordered oracle. Each runs
12,000 random births, deaths, fence changes, value edits and identical
replacements, with small capacities so trees reach 3–5 levels, drain and
regrow. It validates separators, hash pages, find, prefix and logical seeks,
and that identical replacements publish nothing. Deliberately broken separator
upkeep, exact-bound rows and hash-page moves each fail the check. The full
matrix takes about two minutes. A single row is
`segment_directory run LAYER J COUNT dense|spread VARIANT...`.
