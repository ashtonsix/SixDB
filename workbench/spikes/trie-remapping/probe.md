# Executable placement and routing probe

This first probe is an in-memory, single-threaded access-method study. It
compares natural-key routing with local ordered remapping and different payload
orders. It is not a SixDB tree implementation, a Calico reproduction, or a
claim that a complete database architecture has been selected.

## Run and iterate

From the repository root on Linux (prefix with `orb -m ubuntu` from this Mac):

```sh
python3 workbench/spikes/trie-remapping/run.py
python3 workbench/spikes/trie-remapping/run.py --sanitize \
  --select '^d05/deps/[^/]+/(points|churn)$' --repetitions 5
```

The default selects two cases, two profiles, and point/churn phases. One command
refreshes editor configuration, builds the selected targets incrementally,
checks correctness, accounts for the selected histories, times pinned sequential
repetitions, and generates tables with a source-stable receipt. `--sanitize`
adds an independent ASan/UBSan build/check before the optimized measurements.
`--select` matches `case/profile/method/phase`, not a benchmark-name suffix.
An empty selection, oracle mismatch, missing repetition, or changed source
fails the run. The shared core is compiled independently of both drivers.

Run outputs stay under ignored `build/experiments/trie-remapping/`. The runner
prints the existing retention command; selected compact evidence can regenerate
its tables with `analyze.py EVIDENCE_DIRECTORY`. Full runs, including binaries,
logs, and measured source, belong in the verified S3 bundles described in
[artifact storage](../../tools/artifacts.md).

## Representations

All rows have a unique ordered `uint64_t` key and a stable fixture row ID. The
all-ones key is reserved as a gap-search sentinel. Columns are uncompressed
SoA arrays of 64-bit integers. Natural trie nodes compress unary paths, store
sorted sparse child labels, and add a 256-pointer dispatch array above 48
children. A leaf contained in one aligned 256-key interval uses an occupancy
bitmap and popcount rank for point lookup. Other terminals binary-search their
stored full keys. Columns remain packed by occupied rank. Natural containers reserve 25% growth
slack by default, capped at their representation limit; `--natural-slack 0`
is an explicit no-reserve control. Subsequent vector growth is included in
timed mutations. The recorded runner configuration names the selected slack.

| Method | Placement and routing |
| --- | --- |
| `natural` | Natural keys; terminate at at most 64 rows, or up to 256 when one aligned byte interval suffices |
| `natural256` | Same, with a 256-row general terminal limit |
| `natural1024` | Same, with a 1,024-row general terminal limit |
| `packed` | Ordered blocks with rows packed at the start; insertion shifts keys, columns, and affected physical locators |
| `gapped` | Ordered physical slots and corresponding payload gaps; use a gap between neighbours or shift toward the nearest gap |
| `gapped_tail` | Like `gapped`, but extend consecutively at the right edge; when only interior gaps remain, pack that block toward the front before continuing |
| `ranked` | The same gapped physical keys, with columns packed by occupied rank |
| `blocked` | Logical order between blocks; arbitrary occupied slots within each block, with a sorted local slot permutation |
| `indirect` | A paged ordered key/locator directory over a separately allocated payload pool; payload slots need not follow logical order |

Remapped methods share a sorted, inline fence-key directory for logical lookup.
Its block references resolve through an array standing in for a prefix-to-
location map. Each block has 256 slot positions and starts with at most 192
rows. A full block splits into two logical ranges. Physical codes are
`block_label << 8 | slot`; consecutive groups of 256 labels therefore span
65,536 codes. The probe does not materialize a full 16-bit segment container or
measure Calico-style QHash resolution, batched descent, or dual-arity navigation.
It tests the local placement and mapping seams those mechanisms would consume.

Initially, block labels have slack. A split uses the next free label or shifts
intervening labels toward a free label to the right, expanding the namespace
if necessary. This simple one-sided policy is not an optimal label allocator.
Relabelling a block repairs dependent physical locators without copying its
column buffers. `indirect` splits only its directory and retains payload slots.
Physical allocation has gaps independently of logical key spacing. `gapped`
and `ranked` fill search-key gaps with the next occupied key, allowing ordinary
binary search followed by occupancy correction. Splits redistribute gaps;
this is not a complete packed-memory-array density hierarchy. `gapped_tail` observes the
current insertion position; it does not use future trace information.

Natural keys survive column-rank shifts, so their secondary physical references
remain the logical key and are resolved by another natural-trie lookup. The
remapped methods can resolve valid physical references directly. Natural
terminals retain full key/ID entries even where key bytes could be implicit;
these buffers are not as compressed as Calico Keyset. Root fence routing is
one flat directory at these sizes, not an arbitrarily deep B+-tree baseline.

## Dependencies and histories

Profiles are `keys` (no columns), `narrow` (one column), `wide` (16 columns,
128 payload bytes/row), `deps` (wide plus four physical-reference sets and
count/sum summaries), and `ids` (the same, with stable IDs in the four sets and
one ID-to-physical-location table). Column values are deterministic bounded
positive integers derived from row ID and column number.

Secondary-reference sets are real arrays of locator cells, indexed by fixture
row ID. Repair already knows the cell address; secondary-key search, tree
maintenance for indexed values, and realistic index-order scattering are
excluded. This is an optimistic repair model. The stable-ID arm pays for its
extra table, its updates, and its read indirection. These are internal row
identities, not a proposed SQL PK contract or stale-locator recovery protocol.

Summaries belong to each materialized data/directory block and the collection.
They count rows and sum column zero. A logical insert/delete updates the local
and collection totals; a split redistributes local totals; a block relabel
preserves its total. The indirect arm's local totals cover ordered directory
ranges, not arbitrary physical-pool prefixes. Other interior summaries,
min/max, dirty buffers, snapshots, WAL, replication, and device writes are
unmeasured. In particular, no missing deep trie nodes are assigned summaries.

| Case | Initial population and distribution |
| --- | --- |
| `d01`, `d05`, `d20`, `d50`, `d90` | 655, 3,277, 13,107, 32,768, 58,982 distinct keys sampled from `[0,65536)` |
| `fixed_dense`, `fixed_sparse` | 4,096 keys in universes of 8,192 and 1,048,576 positions |
| `spread` | 4,096 dispersed 64-bit keys |
| `collision` | 4,096 keys sharing only 16 distinct high 16-bit prefixes, with random 48-bit tails |
| `hot` | 4,096 keys in the 16-bit universe; new inserts concentrate around the middle while deletions are dispersed |
| `moving` | Like `hot`, but the insertion interval moves from the first to the third quarter halfway through |
| `append` | 4,096 keys in the 16-bit universe; new keys extend beyond it while deletions are dispersed |

The density sweep changes population; the fixed-population pair separately
changes the universe. Neither establishes a cache-residency tier. Collision
means a shared truncated prefix, never duplicate full PKs. These are fixed-width
keys, not variable-length strings. Normal histories contain 512 delete/insert
pairs; `hot`, `moving`, and `append` contain 2,048 and exercise repeated splits. Deletion
and insertion use distinct row identities. No payload-only update is timed.

## Timers and accounting

- `points`: 2,048 logical lookups, exactly half hits and half absent keys in the
  initial state. Collision-case misses are sampled within the same 16 prefix
  domains as hits. Hits read every configured column and form a checked digest.
- `secondary`: 2,048 initial live-row lookups through the reference sets, in a
  deterministic stride. This excludes searching by the secondary attribute.
- `scan16`, `scan256`: 256 lower-bound requests, each reading up to 16 or 256
  rows in logical order and summing column zero (keys in the key-only profile).
  These are projected scans, not full-row reconstruction or SQL execution.
- `scan256_after`: apply the entire mutation history outside the scan timer,
  preserving the resulting physical layout; then scan using a final-state
  query set with half live keys and half absent keys in the case's key domain. Its oracle comes from
  independently replaying the logical rows. Comparands share these requests;
  before/after phases do not isolate placement from changing logical data.
- `churn`: each pair deletes one existing key and inserts a new row, then reads
  the inserted row and another live key. Every eighth pair includes a 16-row
  scan, and every fourth includes a reference lookup when configured. The timer
  includes actual allocation, redistribution, splits, fence changes, reference
  repairs, and summary maintenance occurring within the history.
- `rebuild`: materialize the natural baseline's current rows/columns, then build
  the requested layout and dependency state while the source remains alive.
  Report time per row. This measures a full conversion/rebuild, not an atomic
  mapping publication or an incremental conversion. Source construction,
  subsequent destruction, and reclamation are outside the timer.

Initial construction, trace generation, and oracle computation are outside
point/scan/churn timers. Each churn repetition starts from the same initial
state. Thus these are finite histories including their maintenance, not a
long-run equilibrium or a claimed insert-latency percentile. Contiguous projections use the same simple summation loop; gaps and
permutations use explicit occupancy/gather loops without per-row layout dispatch.
Counter-enabled
and timed implementations share code; counters compile out of the timed one.
Point/scan/churn digests are checked after the timed interval.

Movement counters count known buffer shifts/growth, payload relocation and
reference repairs in churn. They exclude initial construction and temporary
materialization copies during a split; zero payload relocation does not mean
zero total memory traffic. Rebuild rows/time and resulting footprint describe
conversion separately. Footprint columns report owned capacities and node/page
objects, excluding allocator overhead, the shared input trace, temporary
conversion buffers, and process RSS. The largest-move counter records split
population only. No counter is described as measured cache/coherence/device
traffic. A retained source and rebuilt target are checked independently; this
is not an MVCC, concurrent publication, or crash-recovery proof.
