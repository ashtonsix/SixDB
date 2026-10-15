# Multimap inline vectors

> Prior lifetime comparisons need corrected reruns after removing inter-phase validation allocation; see the [measurement correction](BENCHMARK-VALIDATION.md). Allocation counts and logical/backing-byte observations remain separate.

A multimap stores one key with a sequence of fixed-width values. The sequence
keeps a small number of values inside the bucket record. Once full, it moves
those values into external storage. Duplicates and append order survive growth;
whole-key deletion releases its values. Individual-value deletion remains an
unimplemented operation to assess before graduation.

The inline budget must depend on value width and bucket packing. Reserving two
values is inexpensive at W2 and substantial at W64. Larger records reduce bucket
capacity and can push keys into secondary/overflow storage; an append win alone
cannot select the budget. The starting comparisons hold bucket geometry and
inline bytes constant when isolating external storage, then vary budgets.

We resume the existing bucket hierarchy. Candidate external owners are:

- Existing geometrically linked StreamPack slots, with the insertion hint.
- One growable Orbital allocation per spilled bag, the existing buffer control.
- A contiguous vector in a single StreamPack slot, moving to a larger size family
  on growth and returning the old slot for reuse. This shares allocation/directory
  costs across bags while removing intermediate read hops for ordinary fanouts.

The third control reuses the existing StreamPack block owner. It is a storage
policy experiment, not another allocation abstraction. A replacement is acquired
before changing the old bag; cancellation preserves its contents. Commit resolves
both addresses after any Orbital relocation, copies the initialized prefix,
appends, then retires the old slot in the same nonsuspending private mutation.
Its before-write events include both physical blocks and the free list. Actual
ObjectStore whole-material cloning is a lifetime control, not fine-grained COW.

The pooled-vector control keeps the existing maximum slot family. Beyond that capacity
it uses the existing segmented continuation. This explicitly bounds the initial
contiguous-vector experiment; it does not select a production long-bag format.
A fully contiguous large Orbital buffer remains a contender. No value-count cap
or failure is introduced merely by reaching the largest StreamPack slot.

Expected trade-offs, to measure: copying on vector growth versus repeated chain
hops; smaller live payload versus retired-family high-water storage; fewer owners
versus broader shared-block damage. Inline vectors do not automatically win.
Per-bag buffers remain important because they reclaim independently and can grow
beyond a StreamPack slot. A cached direct pointer/length descriptor could remove
one read dependency, but its wider bucket footprint, relocation and retained-
version fixups must be included before implementing that option.

First complete-operation comparison: ordinary and exact-key Abseil, existing
chains, per-bag buffers and pooled vectors; W2/W8/W32/W64; different inline budgets;
uniform fanouts around spill/growth boundaries and mixed fanouts. Report reads,
dependent reads, appends, erase/reuse, lifecycle and requested/backing/owner bytes
separately. Native NEON, AVX2 and AVX-512 measurements qualify performance; local
runs check complete bytes/multiplicity and expose code generation. Follow with
long skew/churn, per-value erase, cold Loom multimap consumers, retained-reader
costs and growth/worker integration. Those remain owed, not assumed by this panel.

The first local W2/W8/W32/W64 panel completes 520 fresh-process histories and
6,760 phase rows, with full byte/multiplicity checks. It includes mixed fanout,
first spill, later geometric boundaries, 64 values, erasure and reuse. These
OrbStack timings are development diagnostics with substantial variability, not
native qualification or a policy recommendation. The stable storage comparison
is useful: at W64/fanout64, 256-slot linked blocks request-plus-metadata consume
49,666,082 B; pooled vectors consume 25,925,506 B; ordinary Abseil 25,828,928 B.
The vector's 64-to-65 append now pays for copying and growth instead of adding
a small new link. This can lose badly even where read hops and storage improve.
The raw local panel is in `build/hashtable-design/vector-local-broad`.

A separate default-off `HT_LINK_RESERVE_BLOCK` control asks Orbital for a whole
block's reservation headroom at first allocation, while exposing only the first
slot. It changes neither slot format nor initialized bytes. It may avoid later
relocation/copying, but can change finite-versus-Window allocation and page slack.
Both chains and vectors receive the control, including the table's key-overflow
links. Per-bag buffer policy itself is unchanged, but buffer-table key overflow
also receives the control; Abseil is entirely unchanged. This carries back the separated experiment's reservation
lesson without adding an allocator. Actual growth/relocation counts, footprint
and complete lifetime must decide whether it helps. No default is changed.

The local reservation comparison passes 336 complete histories across W8/W64,
mixed and uniform fanouts 1/3/64, seven arms and three repetitions. It now records
untimed Pool allocations, growths, relocations, mapped/reserved/idle bytes and
allocator metadata at each mutation frontier. These are Pool inventories, not
process RSS; zero baseline Pool counters mean Abseil does not use Orbital, not
that its allocation is free. Full reserved-vector behavior checks also pass.

Reservation removes the observed relocations, but does not consistently improve
local timings. In the W64 mixed 256-slot vector arm, append growths remain 166,
relocations fall from 105 to zero, and mapped Pool bytes after update fall from
6,209,536 to 4,845,568. Vector growth still copies the live prefix and constructs
a full new slot. In the W64/fanout64 vector arm, 64-to-65 append has 192 shared
block grows; reservation removes 25 relocations. It cannot remove the 6,144
individual vector resizes. Native timings and complete histories will determine
the recommendation. Do not equate fewer relocations with a measured speedup.

## Native V2 result

The [first native panel](evidence/vectors-neon/panel/summary.csv) completes
780 histories and 10,140 phase rows, W2/W8/W32/W64, three repetitions. Its
[reservation controls](evidence/vectors-neon/reserved/summary.csv) add 360
histories and 4,680 rows at W8/W64. All complete-operation oracles and vector,
reserved-vector and ObjectStore publication checks pass. These are warmed
6,144-key histories with complete value consumption; they do not establish each
request's servicing cache level. The native default fanouts cover inline spill,
selected growth boundaries and 64 values, not every external doubling boundary.

The comparison below holds bucket geometry and inline budget fixed between
chains and pooled vectors, both using 256 slots per block. Memory is requested
plus owner metadata, before / after one append to every key. Hit is reciprocal
throughput; serial is an answer-dependent read; lifecycle is the charged mutation
history per initial key. All times are ns; full owner/backing inventories and
ordinary/exact-key baselines remain in the retained rows.

| Case / owner | Logical B, before / after append | Hit | Serial | Append | Lifecycle |
| --- | ---: | ---: | ---: | ---: | ---: |
| W8 mixed, chains | 390,172 / 449,988 | 8.61 | 24.69 | 22.41 | 116.10 |
| W8 mixed, pooled vector | 325,730 / 570,974 | 7.72 | 23.26 | 34.08 | 122.53 |
| W8 mixed, ordinary Abseil | 341,248 / 440,832 | 8.13 | 18.80 | 48.64 | 121.34 |
| W64 fanout64, chains | 49,666,082 / 49,666,082 | 174.67 | 187.74 | 52.08 | 4,035.70 |
| W64 fanout64, pooled vector | 25,925,506 / 76,295,346 | 144.86 | 149.92 | 2,576.14 | 5,628.20 |
| W64 fanout64, ordinary Abseil | 25,828,928 / 50,994,752 | 118.96 | 125.71 | 310.60 | 2,371.58 |

The pooled vector improves initial long-bag storage and reads over chains, but
its old size-family slots remain assigned and reusable after resizing. In this
uniform 64-to-65 history that retains the old capacity as well as the new
128-value slots. Reserving whole shared blocks removes relocation, but the W64
append still costs 2,433.99 ns and retains 76,295,346 logical bytes. This is a
reclamation/growth-policy issue as well as copying and initialization work.
Do not promote this implementation as a general inline-vector replacement.

Chains already offer a useful W8 mixed exchange: faster append and slightly
lower lifecycle than ordinary Abseil, with a roughly six-ns serial penalty and
more initial logical bytes. Wide mixed bags still lose materially: W64 chains
cost 379.90 ns/lifecycle versus Abseil 259.98, with 2,329,738 versus 1,345,728
initial logical bytes. The narrow mixed win does not settle the value-width policy.

The independent Orbital-buffer control also exposes allocation friction. W64
64-to-65 append costs 11,688.68 ns, with 6,144 additional growths and 6,144
relocations for the 4,096-to-8,192-byte requests. That phase increases mapped
Pool bytes from 25,935,872 to 76,267,520; 25,202,688 B are idle afterwards.
This was reported to Orbital with the exact capture and caller. It is not a
reason to hide the cost behind a new allocation layer. The captured idle-Window
selector traverses a single list, rejecting undersized reservations; growing
this population leaves increasingly many undersized old Windows. That is a
concrete candidate mechanism, not a measured causal attribution yet.
Orbital confirmed this is supported caller use and is investigating that path.
For this clean population the list entails N(N−1)/2 = 18,871,296 rejected
undersized candidates, or 3,071.5 per append. Default Window reservation is
page-rounded body size; explicit headroom would avoid this relocation boundary,
but it is a caller policy trade-off and does not eliminate new-page preparation.
The allocator owns the search repair; no hashtable-specific allocator is added.
Orbital's independent local diagnostic now reproduces the scaling without the
hashtable: at 6,144 buffers, ordinary growth is 17.158 microseconds, an
attribution-only no-fit-search bypass is 4.393, and initial 16KiB virtual
headroom is 1.426 (medians of three cohort means). Full old/new bytes and balanced
retirement pass. These are OrbStack AArch64 measurements, not native V2
attribution; the bypass is legal only for that constructed no-fit trace and is
not a proposed production allocator. Orbital owns the reproduction and a possible
eligibility-indexed search/default-headroom comparison in
`build/window-growth-scaling-20261015/README.md`. Keep the independent-buffer
arm until that owner-level issue can be measured separately from table policy.

Whole-block reservation is not selected as a universal policy. It removes shared
block relocations and often lowers mapped bytes, but native lifetime gains are
mixed, unchanged baseline timings also vary between panels, and it leaves
per-bag buffer relocations intact. Targeted remaining growth boundaries and
cross-platform results qualify the choice; no production default changes here.

The [targeted native boundary follow-up](evidence/vector-boundaries-neon/panel/summary.csv)
adds 600 complete histories (7,800 phase rows), covering fanouts 4/5/8/9 at
W8/W32/W64 and W8/W64 reservation controls. It confirms that the cost depends
on the growth history, not only the final value count. A fresh W8/fanout5 pooled
vector population uses 641,314 logical bytes, while growing all existing
four-value bags to five retains 876,074 B. Fresh fanout9 uses 1,034,602 B; growing
eight to nine retains 1,465,938 B. Incrementally constructing each bag can reuse
old-family slots that a simultaneous population transition leaves behind.

At W8/fanout8, pooled vectors read in 14.65 ns versus chains 20.00 and Abseil
7.00; the next append costs 137.19 versus 16.76 and 84.92 ns. At W64/fanout8,
the corresponding reads are 42.13 / 46.27 / 27.91 ns and appends are
471.29 / 43.57 / 197.29 ns. Whole-block headroom does not remove this boundary.
The follow-up preserves first-spill and non-resizing controls rather than
reporting only the vector's favorable between-growth operation.

## Native Zen 5 result

The [Zen 5 AVX2 panel](evidence/vectors-zen5/panel/summary.csv) completes the same
780 histories plus 360 reservation controls; all three correctness/publication
checks pass. Candidate logical bytes match V2; ordinary/exact Abseil's owner
metadata adds eight bytes. The main trade-off survives this second platform:

| Case / owner | Hit ns | Serial ns | Append ns | Lifecycle ns/initial key |
| --- | ---: | ---: | ---: | ---: |
| W8 mixed, chains / pooled vector / Abseil | 6.00 / 5.30 / 4.93 | 14.89 / 14.02 / 11.14 | 15.81 / 24.05 / 35.37 | 85.96 / 89.84 / 86.20 |
| W64 mixed, chains / pooled vector / Abseil | 10.63 / 9.85 / 7.84 | 22.00 / 20.44 / 17.29 | 104.99 / 170.60 / 42.32 | 293.39 / 350.61 / 164.79 |
| W64 fanout64, chains / pooled vector / Abseil | 149.30 / 106.76 / 78.85 | 179.65 / 103.31 / 80.89 | 62.81 / 2,296.11 / 320.44 | 3,259.81 / 4,660.81 / 1,972.58 |

The chain control has already removed the old broad short-bag append deficit
for this W8 mixed history, with lifecycle parity and a few ns of read cost.
Wide mixed bags remain a substantial problem. The independent-buffer W64
64-to-65 append still costs 6,449.04 ns; the native allocator friction is not
exclusive to V2. Reserving shared blocks does not fix that buffer policy.
Neither native panel selects a general vector replacement. Unchanged baseline
timings also vary across the separate reservation cohort; its differences should
not all be causally attributed to reservation, especially small lifecycle gains.


## Complete-container allocator repair comparison

Orbital repaired idle-Window eligibility lookup independently of hashtable policy.
The matched [Zen5](evidence/vector-allocator-zen5/repaired/panel/summary.csv) and
[V2](evidence/vector-allocator-neon/repaired/panel/summary.csv) reruns rebuild all
Orbital translation units against its two changed private files. Each host runs
the original consumer first with the original allocator, then with the repair:
780 complete histories plus 360 reservation controls per side, three repetitions,
ordinary/exact Abseil and all correctness/publication checks. Source and repair
hashes are retained with each panel. Sequential panel order is a limitation for
small timing differences; the unchanged controls expose that drift.

For 6,144 K40 keys with W64 bags growing from 64 to 65 values:

| Host / owner | Append before → after, ns | Mutation lifecycle before → after, ns/initial key |
| --- | ---: | ---: |
| Zen5 independent buffer | 6,297.26 → 3,016.43 | 9,470.16 → 6,207.74 |
| Zen5 128-link-block chains | 57.94 → 59.59 | 3,092.67 → 3,060.66 |
| Zen5 128-slot-block vectors | 2,015.88 → 2,004.50 | 4,191.83 → 4,186.83 |
| Zen5 ordinary Abseil | 291.16 → 292.79 | 1,749.40 → 1,793.03 |
| V2 independent buffer | 11,417.19 → 3,908.70 | 15,999.02 → 8,478.32 |
| V2 128-link-block chains | 48.76 → 47.63 | 3,968.70 → 3,974.54 |
| V2 128-slot-block vectors | 2,473.52 → 2,501.62 | 5,508.46 → 5,509.36 |
| V2 ordinary Abseil | 306.48 → 314.90 | 2,334.29 → 2,326.17 |

The repair materially helps the intended buffer case without changing logical
bytes, grow count or relocation count. It does not cure bag-copy cost or make
buffers competitive at this boundary. The independent-buffer append inventory
still has 51,376,874 logical bytes; allocator Pool metadata rises from
3,839,632 to 4,233,040 B on Zen5 and 3,937,992 to 4,331,400 B on V2. This is
additional allocator metadata, not part of the table logical-byte figure.
Orbital reports +32 B per Window and +56 B per PoolState; its size-index nodes
are controller storage outside the existing Pool budget. Do not equate either
logical or Pool bytes with process RSS.

The repaired W8 mixed controls retain a useful chain result: Zen5 hit/serial/
append/lifecycle are 5.74/14.57/15.99/80.57 ns versus ordinary Abseil
4.93/11.10/35.04/84.37; V2 gives 8.57/24.89/21.09/111.35 versus
8.31/18.80/48.73/119.38. Before/after table logical bytes remain
390,172/449,988 for chains versus 341,256/440,840 for Abseil on Zen5 (eight
bytes less for Abseil on V2). These are trade-offs to preserve while improving
wide bags, rather than evidence for a wholesale external-owner replacement.
