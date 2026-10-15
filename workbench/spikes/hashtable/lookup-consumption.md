# Reducing the common bucket lookup work

The native selector and mask comparisons separate a large compiler dependency
from a remaining container cost. The improved K40/W2 map still loses ordinary
independent hits substantially. At A9/U128, 6,133 of 6,144 keys are primary,
11 are secondary and none are linked. Downstream routing is therefore unlikely
to explain the remaining gap in that uniform-hit history.

The [captured Zen5 scan32 primary-hit path](evidence/selector-native/structural-paths.json), with its first fingerprint candidate
correct, has 46 instructions from query reconstruction through value accumulation,
versus 34 for ordinary Abseil, excluding padding. This selected static path is
not an instruction average, uop count or cycle model. It includes two MULX
operations rather than one, occupancy loading/testing/masking, and separate
suffix/value addresses and loads. Both paths are fully fused. Granite has the
same structural concerns. These observations motivate two distinct experiments. The inactive-fingerprint
control is implemented behind `HT_BUCKET_SENTINEL`; the row consumer is now
implemented behind `HT_RECORD_CONSUME`. Both remain experimental controls.

## Inactive fingerprint encoding

Retain the dense occupied prefix and its writer-maintained count. Initialize
unused physical fingerprint bytes to 255, and restore that byte in the vacated
last slot after swap-last deletion. Search masks always exclude bytes outside
the static physical capacity. For a query fingerprint other than 255, all matches
are occupied, so lookup need not load occupancy or dynamically clip its SIMD mask.
For a 255 query, use the existing count-bounded path. With fewer than eight logical
fingerprint bits in the current physical eight-bit plane, that query is impossible.

This is an encoding of unused slots, not tombstones or a change to collision
routing. A real 255 fingerprint remains valid; count distinguishes its live
entries from the unused prefix tail. Descendant accounting remains independent,
so a primary vacancy still cannot hide a secondary or linked key. Future packed
fingerprints cannot assume a spare out-of-domain byte: the sentinel and its
query fallback must follow the actual physical width.

The complete cost includes initialization, append, erase-last, swap-last,
clear/reuse, clone and migration. Damage must include the vacated fingerprint
store before writing; clear/reuse must invalidate previously live fingerprints,
and erase-last gains a store. Refusal must preserve the encoding. Headless views still
borrow caller-provided count and storage. The owner supplies the invariant and
stable initialized read window. Exact guarded tails and wider read permission
remain distinct, with masks applied before any suffix/payload access.

Compare unchanged layout/hash/access paths with and without this encoding,
using a qualified full-count mask control equally on x86 to avoid reintroducing
the known compiler confound. Include every fingerprint (especially 255), empty
and full buckets, deletion/reuse, forced downstream entries and aged hit/miss
histories. Sets, maps and multimaps retain the same representation rules.

`inactive_check.cpp` checks exact physical bytes and before-write damage,
including refused erase-last, guarded exact tails, every fingerprint, private
clear/reuse and all eight key domains and container kinds with downstream
traffic. The existing byte-width histories also run with the control enabled,
covering promotion, cloned owners, bag spills and retained publication.

`occupancy-native.sh` compares count and inactive encodings at identical physical
recipes for K16/W2 maps, K40 sets, K40/W2 maps, K40/W8 multimaps and K64/W16 maps.
All sixteen geometry arms and both ordinary/exact-key Abseil controls remain.
The complete driver now adds hits and deleted-key misses after half the keys are
erased, reinsertion, and reads after reuse; reinsertion is charged in lifecycle.
These are added to construction, initial reads, dependent map/bag reads,
replacement/append, deletion and release. Full per-key byte/multiplicity and
hit/miss oracles run after deletion and reuse; checksums are additional guards.
Construction memory is captured before insertion. Reused multimap reads contain
half singleton keys and half two-value keys, rather than restoring the initial
singleton history. Sets still report ordered traversal,
not validated serial latency. The multimap control starts with one value and
appends a second; it is not a fanout-policy selection.

Both encodings use scan32 on capable x86 and the existing NEON windows. They
also use the paired full-count source control in both query production and
seeded masking on x86. That common producer work is charged to Abseil too;
these are new matched inputs, not directly interchangeable with the preceding
cohort's ratios. Native machine code must still verify the actual lookup paths.
`occupancy-run.py` interleaves fresh processes, reverses the second pass, retains
all operation results and checks identical phase inventories and independent
byte-plane fit. The v2 native reports are complete on V2, Zen5 AVX2 and Granite
AVX-512: 140,400 phase rows from 2,160 fresh processes. Frozen-source report
replay, complete schedules and equal count/inactive inventories pass on every
host. All 30 timed binaries now pass complete call-boundary qualification across
6,480 read regions and 1,080 updates, with no unresolved/truncated regions. The
[cohort evidence](evidence/occupancy-native/README.md) owns its status.

At the same K40/W2 A9/U128 control (6,144 keys, half Abseil's logical memory),
the inactive encoding gives the following ratios. Lower is better. These are
complete queries in a finite warmed panel, not established cache-residence tiers.

| Target | Hit / count encoding | Hit / ordinary Abseil | Dependent read / ordinary Abseil |
| --- | ---: | ---: | ---: |
| V2 NEON | 0.841 | 1.159 | 1.094 |
| Zen5 AVX2 | 1.013 | 1.489 | 1.214 |
| Granite AVX-512 | 1.033 | 1.195 | 1.089 |

Other geometries show gains, washes and regressions. This does not establish a
universal hit improvement or hot serial parity. Short-multimap lifetime and
crowded wide-map losses remain large; every geometry and operation stays in the
report. The encoding remains an experimental choice rather than a default.

The [selected x86 hit paths](evidence/occupancy-native/diagnostic/occupancy-v2-arm1-hit-review.json)
confirm the intended dependency change. Non-255 queries omit the occupancy
load/test and supply static capacity to the SIMD mask clipping instruction;
255 queries retain the actual-count fallback. Both encodings fuse lookup and
value consumption without calls. All three K40 masks (query producer and both
permutations) use full count-register definitions. Inactive adds a fingerprint
branch and fallback code, while count retains an early empty-bucket path. The
mixed timings therefore do not establish that removing count always reduces
total executed work or costs fewer cycles.

## Consume the suffix and value from one row load

K40/A9/U128 fits twenty rows containing a three-byte suffix and two-byte value
in the same 125 used bytes as the current planes. With the admitted stable unit,
an eight-byte row load fits even the final row at offset 120. This requires
whole-unit read admission; an exact five-byte row view cannot authorize it. The existing
TuplePack transfer supplies both suffix verification and the returned value;
the independent direct arm uses the same bounded physical window.

The new `hashtable_design_consumption_*` controls compare plane layout, row
layout with rank-returning lookup, and row layout with an immediate match
consumer, plus independent direct field access. The latter passes already loaded
value bits into the caller instead of issuing another load. Keep one search
implementation and retain a general path for records that do not fit the fused
transfer. `find_if` owns the common selector walk and tier routing is shared by
rank-only mutation and immediate read consumption. Eligible contiguous rows
contain at most eight suffix/payload bytes; exact access reads their exact
length, while widening needs whole-unit admission for every rank. Tags retain
their existing physical extraction, including separate bit planes. Borrowed material must not escape the immediate callback or survive
mutation/relocation. This investigates caller composition, not a new owning
primitive or a fixed word-map interface.

Suffix and value loads currently derive their addresses from the same candidate
rank; successful-branch speculation can overlap them. The proposal therefore
targets issued loads/address work and locality, not an unavoidable second serial
memory stage. Compare complete reads and mutations, exact tails, direct versus
composed kernels and relevant value/tag widths before attributing a benefit.

These experiments preserve the current reversible permutation and independent
tier entropy. They do not reopen previously rejected cheap hashes, establish
attack resistance, select growth policy or discharge the remaining multimap,
worker, batch, lifetime and residence requirements.

The row consumer preserves the temporary-record callback contract across fused,
fallback, legacy and overflow paths. Guarded checks include the actual K40/A9
twenty-slot layout at 127/128/129 bytes: the 128-byte unit admits an eight-byte
load at the last row's offset120, while the 127-byte unit falls back to five
bytes for every rank. All four access permissions and independent direct access
check full payloads and tags, collisions, every occupied count and no callback
on a miss. Updated Release and ASan/UBSan checks pass.

The first local panel retains 11,232 phase rows from 288 processes, four
variants, both cardinalities and all geometry arms. Logical histories and
inventories agree. At K40/W2 A9/U128, plane/row/immediate-row/direct hits take
3.694/3.359/3.518/3.261 ns; dependent reads take
17.428/16.643/16.446/15.682 ns. The panel is mixed, including regressions and
substantial variation in short mutation phases. These ARM/OrbStack diagnostics
do not select a layout or establish target-native gains. Exact local binaries,
source hashes, compile commands, checks and the complete report are under
`build/hashtable-design/consumption-local`; no row consumer is adopted.
Occupancy native work uses its earlier frozen v2 source, which does not contain
the row-consumer change.
