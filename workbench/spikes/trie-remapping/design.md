# Making physical radix geometry follow the records

Initial reasoning, 2026-09-08. These are hypotheses and design alternatives.
Ashton's reported Calico experience motivates the question; this investigation
has not reproduced those workload results.

## What Calico already separates

Inspected sibling Calico at `ac83c82b9a0c8d76bea92829e471ebc0d98b1978`:

- [Trie contract](../../../../calico/design/TRIE.md) and
  [prefix algebra](../../../../calico/arbor/include/arbor/prefix.h): prefixes
  name segments; a location map resolves storage offsets. A 16-bit hop skips
  reading a byte stratum. At this revision, ordinary intervening strata still
  exist; earlier structural elision was withdrawn. SixDB's choice remains open.
- [Container contract](../../../../calico/design/CONTAINERS.md): record columns
  use occupied-position rank, and sibling containers relocate independently.
- [Store seam](../../../../calico/arbor/include/arbor/store.h): changing a
  segment's byte location preserves its prefix identity; mutation references
  also carry the edition they were planned against.
- [Data model](../../../../calico/design/DATA-MODEL.md): early termination needs
  enough suffix identity to distinguish records. Its general multiplicity
  contract is broader than the implemented sparse-postings terminal.

The proposed remap changes which prefix names a record. That is an additional
kind of movement beyond replacing the bytes behind an existing prefix.

## Keep the coordinates distinct

| Coordinate | Meaning | What can invalidate it? |
| --- | --- | --- |
| Logical key `K` | The key under the collection's comparison and uniqueness rules | A logical key update; delete/reinsert may denote a different row |
| Stable row identity `R`, if needed | The continuing record independent of its key or placement | Retirement/reuse policy; it need not be a separately stored ID if `K` suffices |
| Physical key `T_v(K)` | The trie path under mapping edition `v` | Relabelling, splitting, changing the remap boundary |
| Segment position `s` | The local physical-key coordinate, initially 16 bits | Local relabelling or movement into another segment |
| Occupied rank `r` | Number of occupied positions before `s` in a particular edition | An earlier insertion/deletion, even if `s` stays unchanged |
| Storage address | Where a particular container edition's bytes live | Allocation, compaction, eviction, migration |

For one remapped region with common logical prefix `P`, write
`K = P || suffix`, `T_v(K) = P || f_v(suffix)`. For distinct ordered records,
the candidate requires `K1 < K2 => T_v(K1) < T_v(K2)`. Direct regions use the
identity mapping. Exact logical suffixes must remain recoverable unless the
mapping is demonstrably invertible; a slot number or fingerprint alone is not
enough for equality or inverse lookup.

For example, suffixes `10, 1000, 1000000` might receive local positions
`0x2000, 0x6000, 0xa000`. Inserting `500` at `0x4000` preserves their labels.
It still changes the occupied ranks of `1000` and `1000000`. The example
illustrates ordering and gaps, not a proposed spacing policy.

Below the boundary, a physical prefix describes an ordered population range,
not necessarily a logical byte prefix. A logical range must be translated
using lower bounds, including when its endpoints are absent. Prefix/LCA
arithmetic on the original bytes cannot blindly stand in for this translation.
Disjoint remap regions need unambiguous ownership; independent ordered local
maps do not establish global order unless their ranges are ordered too.

## Where lookup and addresses live

A plausible logical lookup has three steps: discover the region and mapping
edition from the preserved prefix; locate the logical suffix within that
region; resolve the resulting physical segment and record position. A region
descriptor can hold the direct/remapped tag, logical bounds, and a reference
to its mapping representation. A small map could be inline there; larger
maps need their own container. These are possible placements, not new APIs.

Start with exact ordered suffix search and an occupancy/rank mechanism. For
small populations compare a linear scan; for larger ones compare a searchable
dense suffix directory pointing to slots against search in the gapped array.
A gapped array is not automatically binary-searchable: its gaps need defined
search semantics or a rank/select route. A compact predictor plus exact
correction is a later option. Charge directory maintenance and all extra
dependent reads; a second full logical index could consume the savings.

The boundary must be discoverable without probing every failed natural-key
depth. An ancestor descriptor or a prefix-to-region directory can provide it;
both have a lookup/update price. Optimistic direct probes may still be useful,
but a miss alone cannot establish absence in a remapped region. Eight/sixteen-
bit navigation remains possible on physical prefixes after translation;
retaining its geometry does not guarantee the old logical-key shortcut cost.

Overlapping 16-bit strata make the boundary placement significant: a hop
straddling it must obtain translated suffix bits before constructing its
target prefix. Decide whether mapping mode belongs to a whole segment or a
child region, and where that tag is available to either navigation arity.
One remapped child should not force unrelated dense siblings to translate.

A physical lookup can skip logical search only when its mapping edition is
known to be valid. A cached locator could contain a region/segment identity,
slot, and generation, with `K` or `R` available for recovery. Generation checks
detect staleness; they do not locate a moved record. Whole-region generations
are simple but invalidate unrelated locators on a local move. Finer tokens or
pinned editions change that cost and must also prevent slot-reuse ABA.

Keep byte addresses in the storage resolver rather than copying them into
every secondary index. Separately compare secondary references that store:

| Reference | Read cost and movement consequence |
| --- | --- |
| Logical key | Repeat logical lookup; physical remapping does not rewrite the reference, but PK updates can |
| Stable row ID | Resolve an ID-to-location mapping; moving records updates that mapping, whose memory and contention must be counted |
| Physical locator plus recoverable identity | Fast validated hits; stale misses need a defined fallback and retained identity bytes |
| Physical locator alone | All durable references must be repaired, or forwarding/history must remain reachable |

A stable segment ID only solves segment relocation. A split changes which
segment owns some records; it does not grant stable record identity by itself.

## Movement and splitting

With finite ordered labels, repeated insertion between the same neighbours
eventually exhausts the gap. Fixed labels, permanent order, bounded capacity,
and unlimited such insertion cannot all be retained. The choices include local
shifting/relabel, wider labels, redistribution, splitting, or indirection.

Separate movement events in both the model and accounting:

| Event | Work that can be hidden by calling everything a move |
| --- | --- |
| Relocate bytes | Update container location; keep key/row identity |
| Fill an existing gap | Add key/payload; update rank metadata and possibly packed columns |
| Relabel within a region | Repair mapping, changed physical memberships, locators and affected summaries |
| Split or merge | Move ownership, install logical fences/routing, repair traversal and references |
| Move the mapping boundary | Convert the region and potentially rekey descendant metadata |

A useful local design bounds relabelling to a segment or small group. If a
segment splits at logical separator `q`, readers need a rule routing keys
below/above `q`, even when `q` is not a natural byte-prefix boundary. Forcing
every split back onto a logical radix boundary could recreate the original
skew. A range directory can itself use trie/keyset kernels, but it remains
ordered routing metadata whose size, search, and mutations count.

Two split families are worth distinguishing: preserve a region namespace and
add ordered child ranges, or redistribute into a wider physical suffix space.
The first needs fence routing; the second can change many physical prefixes.
Giving segments ordered labels with slack moves the label-exhaustion problem
up a level. Giving them stable unordered IDs separates location from order,
but requires an ordered directory. A single physical trie does not erase
these choices. Initially remap terminal regions only; recursive remapping and
reversible direct/remapped conversion can follow if that restriction loses.

Publish routing, membership, columns, and identity changes consistently with
the visible database state. Old readers need the appropriate mapping edition
along with old data, not current routing into an old payload. A copy/publish
model can explore this before choosing locks, deltas, or copy-on-write. Record
retained bytes, forward hops, and reclamation work. Distributed ownership and
transaction locks need logical range/row identities that survive maintenance;
otherwise a split can change conflict detection without changing the data.

## What should contain the gaps?

| Layout | Advantage to test | Cost to expose |
| --- | --- | --- |
| Ordered physical slots with matching payload gaps | Local insertion into spare space can preserve neighbouring payload positions | Slack across columns; shifting, occupancy checks, compression effects |
| Ordered physical slots with dense rank-aligned columns | Retains Calico-style packed scans | Label slack does not spare columns from rank insertions/repacking |
| Ordered directory over payload slots | Move small references instead of wide rows | Indirection, gathers, permutation maintenance and poorer ordered scan locality |

The third layout can preserve ordered physical trie keys while mapping them
again to unordered payload slots. That is another coordinate, with a real
cost. Alternatively make physical keys unordered, but then logical scans and
range summaries must follow the ordering directory rather than physical order.

Full order is a candidate, not a requirement. Intermediate choices include
ordered logical ranges assigned to physical blocks with arbitrary placement
inside each block, or bounded displacement from a predicted position. Define
whether a prediction has a guaranteed search bound or is only a hint with an
exact fallback. Inserting rows changes ranks and distributions, so maintaining
such a bound has a cost too. Compare saved movement with boundary-block scans,
range fragmentation, weaker pruning, lost compression, and explicit sorting
when a consumer needs logical order. Physical-prefix summaries under partial
order need logical bounds before they can safely prune logical ranges.

Gap policy should follow insertion position, not numeric distance between
logical keys. Compare uniform spacing by rank, slack per small block with
progressively larger redistribution, and extra slack at an append/hot frontier.
The last needs a moving-hotspot case: yesterday's prediction can waste space.
Allow local fullness despite global spare capacity, and include delete/reinsert
churn. Distinguish free slots from tombstones retained for older readers.

Keep three sizes independent: the physical position universe, live population,
and actual encoded/allocated bytes. A 65,536-position universe need not allocate
65,536 full rows. It may permit smaller active blocks within the same keyset
kernel. Conversely, an eagerly allocated occupancy bitmap alone is 8 KiB.
Smaller position universes are a comparand if metadata floor or redistribution
cost is excessive. There is no reason yet to require a remapped segment to
span exactly 65,536 *logical* key positions: its logical fences can be arbitrary
while its physical universe stays fixed.

## Which collisions disappear?

Distinct logical keys sharing a truncated prefix can receive distinct slots;
that removes the need for multi-record slots merely to accommodate truncation.
For a first model, use one record identity per physical slot. Keep three other
cases separate: equal secondary values need an identity tie-breaker or postings;
hash collisions need original-key verification; versions of one row need a
visibility representation. A SQL primary key remains subject to its uniqueness
rule. If logical keys here are non-unique internal keys, order `(K, R)` instead.
None of these requires retaining Calico's universal inline-head/overflow-tail
shape, but none disappears just because the physical mapping is injective.

## Other consequences worth keeping visible

- **Summaries and pending deltas.** A pure remap leaves logical query answers
  unchanged, but physical-prefix membership can change. Summaries below the
  boundary may need redistribution; above it, an unchanged logical region's
  total can remain unchanged. The earlier [dirty-buffer idea](../aggregate-maintenance/shared-dirty-buffer.md)
  cannot replay old physical locations through a new mapping unqualified.
  Compare draining before conversion, generation-bound replay, or identity-
  addressed deltas. Include this work rather than inventing logical updates.
- **Keysets and vector execution.** Boolean operations on slot sets are valid
  only in the same mapping/alignment domain and compatible edition. Different
  secondary keyspaces cannot intersect independently assigned slots directly.
  Compression, dictionary locality, gathers, and filter-result reuse may change.
- **Choosing the boundary.** Occupancy alone misses key length, residual
  entropy, comparisons, number of columns, write locality and hot readers.
  Compare explicit boundaries before adding adaptive policy. Later account for
  conversion cost and use separate enter/exit conditions to avoid oscillation.
- **Determinism.** Fixed floating-point flags do not make layout choices
  independent of insertion order. Decide whether replaying a committed history
  must reproduce mappings, or identical visible data must have canonical bytes.
  Workload-driven gap placement, learned models and background split scheduling
  interact differently with those contracts.
- **ELT and distribution.** Bulk loading can establish spacing cheaply, but
  incremental ingest and changing distributions determine its lifetime. Define
  whether IDs/mappings are shard-local and how shard movement affects handles;
  a local layout conversion should not silently require collection-wide repair.

The [experiments](experiments.md) start with the lookup/placement seam. The
production questions remain visible without making their answers prerequisites
for measuring that seam.
