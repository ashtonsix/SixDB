# Prefix addressing after Calico

Retrospective, 2026-09-13. Inspected Calico at
`ac83c82b9a0c8d76bea92829e471ebc0d98b1978` and SixDB's retained
[trie-remapping investigation](../spikes/trie-remapping/README.md).
This is an assessment and a candidate adaptation, not a selected Engine
interface or a new measurement campaign. Ashton's follow-up adds lookup by
the prefix of a friendly box enclosing a remapped segment, and explicitly
separates prefix addressing from the choice of hash-map implementation.

**Prefix addressing was a useful Calico idea. SixDB should retain derivable
region names and separation from byte location, while making their scope
explicit.** Natural-key prefixes can remain excellent routing coordinates;
remapped physical prefixes need their own mapping domain and edition. Neither
kind has to be the permanent identity of a row or independently movable block.

Calico combined several choices in its [trie contract](../../../calico/design/TRIE.md):

| Choice | Retrospective judgment |
| --- | --- |
| Derive a target segment name from key bytes without reading every parent | Retain where the active representation makes that derivation valid |
| Resolve object identity separately from its byte location | Retain, with snapshot and residency lifetimes distinguished |
| Use a prefix as both routing coordinate and hierarchical grouping | Retain within a declared coordinate domain; materialize useful groups |
| Derive sibling container names and relocate their bytes independently | Retain the capability; prefix arithmetic is one implementation |
| Choose a prefix-to-location map implementation | Independent implementation choice; QHash is one historical example |
| Materialize overlapping 16-bit windows at every ordinary byte depth | Reconsider independently of addressing |
| Give every column the occupied-key rank order | Keep as a layout option, with its insertion cost charged |
| Make all physical prefixes mean logical byte prefixes | Restrict to natural regions; local remapping breaks this equivalence |

The existing SixDB decision is **at most `2^16` segment-local positions**,
recorded after the remapping probe in the
[integration proposal](../spikes/ikea-composition/semantics-and-integration.md#segment-and-record-slice).
It does not select a fixed logical interval, full-capacity allocation, rank
alignment, or a segment at every byte depth. This retrospective preserves that
decision. Navigation in 8/16-bit steps is another separable choice.

**What prefix naming actually bought.** Its distinctive advantage over an
opaque page ID is that a query can construct useful names before loading
parents: an expected leaf, an arithmetic LCA in a natural-key domain, demanded
descendants, or corresponding column containers. This creates opportunities
for direct entry, batched resolution and independent prefetch. Calico's
[frontier resolver](../../../calico/arbor/include/arbor/navigate.h) implements
optimistic target resolution followed by ancestor search when necessary.
It does not establish universal one- or two-probe database lookup.

Separating names from locations was also sound: replacing a container's bytes
need not change its incoming logical references. That benefit comes from
indirection, however, and does not require prefix identities. The
[Bw-tree mapping table, section II.B](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/bw-tree-icde2013-final.pdf)
provides the same separation with logical page IDs. Conversely, a conventional
tree does not intrinsically require rewriting an entire subtree when one node
moves. Calico's “one QHash update” describes the location binding, not the
complete relocation transaction: allocation, retained versions, ordered
navigation, reclamation and durable publication still have costs.

Prefix topology also gives a common vocabulary for clustering and summaries.
Its utility depends on what the key scheme groups together and how bytes are
placed. Prefix names alone neither make adjacent objects physically adjacent
nor ensure good compression. Ancestor summaries are valuable because they can
avoid reading descendants; this does not require storing a summary at every
possible prefix depth. Calico's independent
[container identities](../../../calico/design/CONTAINERS.md#rank-and-containers)
are worth preserving even when their local value layout changes.

**Prefix addressing and QHash are separate choices.** Prefix addressing chooses
which key names an object and which lookup targets a caller can derive. Flat,
bucketed, clustered or other maps can implement that lookup. QHash's prefix/
suffix decomposition, canonical blob and particular dependent accesses are not
requirements of prefix naming. Its [report](../../../calico/qhash/REPORT.md)
is evidence about that implementation, and cannot decide the architectural
question in either direction. The relevant comparison gives prefix addressing
a suitable resolver and compares the complete access path against alternatives.

The architecture was less complete than its compact description implied.
Calico withdrew ordinary stratum elision because of branch complexity and miss
ambiguity. Early terminals subsequently needed explicit authority and suffix
identity. Its [Arbor specification](../../../calico/arbor/SPEC.md) and
[backlog](../../../calico/arbor/BACKLOG.md) distinguish the tested relocation
fixture from unfinished production overflow/compaction and mutable recovery.
Distributed ownership remained open. These are limits on the retrospective
claim; they do not by themselves refute prefix addressing.

**Remapping changes the meaning of an address.** For a logical key
`K = P || suffix`, a fully ordered local mapping can produce
`T_v(K) = P || f_v(suffix)`. Above the boundary, `P` still describes a natural
logical prefix. Below it, the bits describe placement under mapping edition
`v`. Moving the bytes behind an unchanged physical prefix and changing
`f_v` are different operations. Only the former is solved by a location-map
update. The [coordinate analysis](../spikes/trie-remapping/design.md#keep-the-coordinates-distinct)
separates logical key, optional stable row identity, physical key, local slot,
occupied rank and storage address.

The amount of preserved order determines how much prefix algebra survives:

| Local representation | Meaning of physical grouping |
| --- | --- |
| Natural keys | Logical byte-prefix membership; natural prefix/LCA arithmetic applies |
| Fully ordered remapping | Contiguous logical populations, provided region ranges are also ordered; logical endpoints need exact lower-bound translation |
| Ordered blocks, flexible slots inside | Whole blocks retain logical range order; arbitrary slot-prefix subsets generally do not represent contiguous logical ranges |

An empty gap in a remapped array is insertion capacity, not evidence that a
corresponding logical-key interval is absent. A full physical slot group does
not prove a logical byte range is saturated. Structural membership, query
evidence and complements must refer to their actual universe, including visible
rows and unused slots. Slot-set intersections require a common mapping domain
and compatible edition; equal numeric slots in independently remapped indexes
do not establish equal records. Ordered scans within a disordered block need
its ordering directory or permutation.

**Payload placement is as consequential as routing.** At 5% occupancy in the
probe's wide-column/dependency profile, gapped keys with rank-packed columns
still moved 12,720,896 counted payload bytes over 512 mutation cycles. Gapping
the columns too reduced that to 69,120, with mixed-cycle time falling from
1,116 to 336 ns. Natural routing remained competitive with keys alone, and
confirmation point queries favoured natural over gapped at both 5% and 20%.
See the [findings](../spikes/trie-remapping/FINDINGS.md#gaps-in-keys-must-reach-the-payload).

The later comparison keeps fully ordered gaps and block-local disorder in
play. Tail-aware gaps rescued the append history, but interior hotspots still
favoured avoiding payload relocation. Larger natural terminals removed most
tiny nodes in the collision case and scanned well, while widening packed-column
shifts. The first choice should therefore include improved natural terminals
with independently chosen payload layouts. A sparse trie does not imply that
key remapping is the necessary remedy.

These are single-threaded, resident ARM64 Linux VM measurements with fixed
histories and uncompressed columns. The probe uses 256-slot blocks and an array
standing in for prefix-to-location lookup. It does **not** measure mixed-region
discovery, Calico QHash navigation, full 16-bit containers, WAL, replication or
online conversion. It informs placement choices, but cannot decide whether
prefix addressing beats another complete access method.

**A candidate SixDB adaptation.** Keep natural routing wherever it is useful,
with explicit authority at terminal or remap boundaries. A region descriptor
could supply logical bounds, direct/remapped mode, mapping edition and access
to its local representation. Queries must discover that authority before
interpreting a deeper miss as logical absence. Known natural regions can still
use direct prefix targets. A 16-bit hop crossing a remap boundary needs the
translated bits and cannot bypass the descriptor's meaning.

An ancestor descriptor or a searchable directory over logical fences are
candidates for discovering regions. [Wormhole, sections 2.2–2.3](https://wuxb45.github.io/papers/wormhole.pdf)
is relevant precedent: it indexes leaf-range anchors with prefix metadata, and
explains why a failed prefix match need not mean that a record is absent.
Applying a related routing mechanism to SixDB's natural/remapped regions is a
proposal; its maintenance and lookup costs still need measurement.

**Ashton's enclosing-box option.** Interpret a friendly box here as a prefix
interval containing the segment's logical ownership range. A map keyed by that
box prefix could locate its segment descriptor, which supplies exact fences
and the suffix-to-slot mapping. The enclosure is over logical keys, so finding
it does not first require knowing a remapped physical key. Local slot changes
can leave this locator intact. Hash-map implementation is independent.

The lookup is then conceptually `logical key -> enclosing-box candidate ->
range-checked segment -> local slot`. Query keys supply candidate prefixes;
the directory still has to discover the relevant prefix length. A known depth,
ancestor search, or auxiliary directory metadata could do that. Arbitrarily
sparse prefix entries do not support binary search over lengths merely because
they are prefixes: that needs an appropriate monotonic presence invariant or
extra routing information.

Enclosures can contain keys owned by neighbouring segments. For example,
consider these half-open ranges in a four-bit key universe:

| Segment ownership | Smallest binary prefix enclosure |
| --- | --- |
| `[0,6)` | `0xxx`, or `[0,8)` |
| `[6,10)` | `xxxx`, or `[0,16)` |
| `[10,16)` | `1xxx`, or `[8,16)` |

Key 6 matches the deeper `0xxx` box, but belongs to the middle segment. A
longest-prefix hit must therefore check exact ownership and, on rejection,
continue to another candidate such as the enclosing ancestor. A rejected
candidate is not an absence proof. This also means nested locator boxes need
not denote nested segment populations: the segments in the example are
disjoint. Summary ancestry cannot be inferred from locator-box ancestry alone.

There is a useful qualification about duplicate names. If arbitrary bit
prefixes are allowed and each disjoint contiguous ownership range uses its
*smallest* binary enclosure, two ranges cannot share that enclosure: both
would have to cross its midpoint and would overlap. Nested enclosures and
false candidates remain possible. With byte-aligned prefixes, coarser preferred
boxes, or several segments intentionally sharing a box, duplicate names are
possible; an entry can then hold a small fence directory or identify a region
containing several segments. For example, separate byte-key ranges within
`0x12xx` can have that same shortest byte-aligned enclosure.

I would keep the enclosing-box option as a serious candidate. It preserves
derivable natural-prefix entry while letting segment density and local slots
follow records. Compare tight binary boxes, byte-aligned boxes with local
fences, and stable boxes spanning several segments. The trade-off is extra
candidate work versus locator churn: a tight enclosure changes when ownership
crosses its boundary; a looser stable enclosure may need more discrimination.
Derive or maintain the enclosure from ownership fences rather than recomputing
it from current occupied min/max on every deletion. A new key inside an owned
range then does not change the name merely by becoming its new extreme.

Inside a region, keep logical routing coordinates separate from container
identity. A natural prefix can serve both roles when stable. When block labels
are reassigned, a scoped stable block/container ID could avoid renaming all
its dependent references. A stable region ID alone does not stabilize every
block or row beneath it. Physical prefixes remain useful for local routing,
grouping and batched lookup even when stable identities are separate.

Resolve those identities through the storage/buffer machinery, then use compact
bound handles or lease-protected pointers during execution. This need not be
three mandatory directory lookups: descriptors can hold resolved local handles,
dense IDs may index arrays, and common prefix resolution can be amortized.
Prefix lookup should use an appropriate map and compete with those choices at
the seam that actually needs it. Name independence from byte placement does not prohibit transient
native pointers while their owning lease remains valid.

For secondary references, compare logical keys, stable row IDs and validated
physical locators with recoverable identity. Do not require a new row-ID table
solely because containers move. In the probe, stable IDs reduced four reference
repairs per moved row to one locator repair but barely changed some cycle
times; payload movement remained. Generation checks can detect stale slots,
but need a fallback identity or retained mapping to find the row again.

Keep range summaries at useful logical routing groups and local summaries in
their declared slot domains. Relabelling a block with unchanged membership
need not change its aggregate; splitting it does. Pending physical-location
deltas must be drained, translated, or applied through their retained mapping
edition. An online conversion must publish a compatible mapping, membership,
column/index and required-summary view while old readers retain theirs.
Neither prefix identities nor stable IDs supply that publication protocol.

For distributed operation, route logical ownership to a shard and resolve local
container placement there. Avoid making every physical-prefix lookup a global
directory operation. Logical ownership, mapping edition, content version and
the lifetime of resident addresses are distinct facts. This fits the provisional
[Engine](../../engine/README.md), [Loom](../../loom/README.md) and
[Orbital](../../orbital/README.md) division without selecting a wire format or
replication policy. Portable persistent names do not require identical local
physical mappings on all replicas; the replication contract must decide that.

The unresolved comparison is a mixed-region read/write path that charges
discovery, resolution and payload maintenance together. Holding payload layouts
constant while comparing enclosing-prefix lookup with compact child/block
handles would isolate the addressing question; varying payload layout
separately would expose interaction. Include candidate-prefix counts, rejected
enclosures, directory footprint and name changes at splits, alongside the
natural, fully ordered and block-order placement arms. This is a discriminating
experiment suggestion, not a required sequence or an adoption gate. The current
evidence supports selective carry-forward and leaves the resolver and local
ordering policy open.
