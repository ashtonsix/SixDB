# Coupled multimap storage policies

A multimap record reserves an inline byte budget. Larger budgets defer external
storage, but enlarge every bucket record and can send more keys downstream.
External storage adds a second choice: shared StreamPack link blocks versus
one growable Orbital allocation per bag. The link owner's block capacity also
affects allocation frequency, unused capacity, directory bytes and the amount
of storage covered by whole-block damage. These choices need joint assessment.

The live spike now fuses its mutation continuations and directly patches an
existing inline multimap's appended bytes and discriminator. Map replacement
keeps the fused whole-value path: native x86 partial-write controls regressed
relative to fusion alone. The combined implementation passes all eleven width
checks and the all-tier damage/retained-publication test. The earlier
[native mutation receipt](evidence/mutation-native/README.md) qualifies its
individual controls; it does not measure this newly combined implementation.
The later [coupled native receipt](evidence/bag-policy-native/README.md) measures
the combined implementation and the policies below.

`HT_BAG_POLICIES` selects a separate complete-operation panel in
`value_width_bench.cpp`. It keeps the ordinary typed and exact-key Abseil
baselines, independent full-byte oracles, dependent rank cycle and nine phases
from the width driver. These are executable physical controls, not proposed
production parameters:

| Arm | Inline bytes | Bucket bytes | Links/block | External owner |
| --- | ---: | ---: | ---: | --- |
| 0 | 8 | 128 | 16 | StreamPack |
| 1 | 16 | 192 | 16 | StreamPack |
| 2 | 24 | 256 | 16 | StreamPack |
| 3 | 32 | 256 | 16 | StreamPack |
| 4 | 32 | 320 | 16 | StreamPack |
| 5 | 16 | 192 | 64 | StreamPack |
| 6 | 16 | 192 | 256 | StreamPack |
| 7 | 16 | 192 | — | Grown buffer |
| 8 | Abseil's typed capacity | — | — | Ordinary Abseil |
| 9 | Abseil's typed capacity | — | — | Exact-key Abseil |

The solver derives fitting arbitrary slot counts for each geometry. The panel
holds K40, A6/A10, the secondary bucket ratio and cardinality fixed. It includes
W2/W8/W16, uniform fanouts on either side of inline/link boundaries, and a
deterministic mixed history with expected proportions 75% singleton, 15% pair,
6% four values, 3% sixteen and 1% sixty-four. Every key receives one subsequent
append, and half are erased before release. Uniform histories remain necessary
because the mixed distribution can hide a bad boundary. This finite panel
does not choose geometry through growth or qualify other key domains.

The link owner now admits a bounded power-of-two block capacity up to StreamPack's
256 ranks; its default is unchanged at 16. Allocation still starts at one link
and grows through Orbital. No alternative allocation layer is introduced. The
inline grammar now permits multiples of eight through 64 bytes, with the tag
separate from that budget; only the listed budgets are measured in this original
panel. The wide controls below removed its earlier internal tagged-record limit.

Release and ASan/UBSan checks cover the larger inline budgets and 16/64/256-rank
blocks, including rank 255, directory relocation, refusal/retry, clone/reuse,
packed tags, all hierarchy tiers and publication. The actual ObjectStore
retained-reader mutation check also passes. Physical bag-clone checks do not
by themselves establish ObjectStore copy cost.

The first local screen completed 1,000 fresh processes and 27,000 phase rows.
All independent contents, point/dependent reads, updates and erases passed.
The exhaustive report checks the randomized forward/reverse process schedule,
every operation count, baseline capacity and tier population. **OrbStack timings
are diagnostic, not native qualification.** Larger shared blocks materially
change some mixed-write results, while larger inline budgets also worsen
memory and reads in some cases. Those outcomes motivate native measurement
and allocation/damage attribution; they do not establish a selected policy.

`bag_policy_events.cpp` separately observes the mixed A10 histories for arms
1/5/6. Its complete-byte and point oracles pass; all 27 phase inventories match
the 162 corresponding local timed rows exactly, including requested storage,
owned capacity and metadata. For W8 updates, 16/64/256-link blocks cause
308/115/36 bag-allocation growth events, while the sum of issued block-damage
extents rises from 2,427,998 to 9,564,998 to 33,681,834 bytes. Larger blocks
therefore reduce allocation operations and broaden coarse damage. These are
untimed logical counts: extents can overlap repeatedly, and neither copied
bytes nor unique dirty bytes nor the timing contribution has been measured.
The retained-version trade-off remains part of policy selection.

The native panel completes 135,000 phase rows in 3,000 fresh processes on Zen5,
Granite Rapids and Neoverse V2. Nine timed ELFs qualify all 720 read/update
regions; all checks and full-byte oracles pass. Reports retain every case,
negative result and separate pass median. The following ranges are descriptive
across those three hosts, for the mixed A10 history with B16/U192 held fixed:

| Value bytes | 16 links/block update / Abseil | 64 links/block | 256 links/block | 256-link lifecycle / Abseil |
| ---: | ---: | ---: | ---: | ---: |
| 2 | 4.80–5.41 | 1.45–1.79 | 1.35–1.69 | 1.14–1.30 |
| 8 | 1.39–1.44 | 1.23–1.32 | 0.44–0.51 | 1.20–1.40 |
| 16 | 2.35–2.41 | 1.72–1.88 | 1.29–1.50 | 1.53–1.71 |

The unchanged W8 owner uses 1.14 times Abseil's initial logical memory with
256-link blocks versus 1.02 with 16-link blocks. W16 uses 1.42 versus 1.22.
The W8 B24/U256 control also wins the mixed update at 0.44–0.47 times Abseil,
but begins at 1.22 times its logical memory and loses lifecycle on every host.
Grown buffers improve some read cases, yet their mixed-update/lifecycle costs
are worse than the stronger linked controls. Thus neither a larger inline
budget nor a larger block is a universal improvement. Uniform spill boundaries
remain in the retained reports; these mixed rows cannot select a preset.

The same untimed event inventories reproduce on all three native hosts and
match all 270 corresponding timed rows per host. Their allocation counts support
the proposed allocation mechanism; they do not apportion nanoseconds. ObjectStore
currently publishes complete materials and does not consume these byte journals,
so damage sums cannot stand in for measured retained-version copying. Geometry
selection, long churn and actual retained-owner costs still need resolving.

`values-run.py --bag-policies --passes 2` records exact binary identities and
all processes. `bag-policy-report.py` retains every arm/phase comparison,
separate pass medians, requested-plus-owner bytes and owned-capacity ranges.
`bag-policy-native.sh` runs the same panel after the policy and publication
checks. `bag-events-report.py` checks the separate event histories against those
timed inventories. Assembly and source identity must qualify the native comparisons.
Logical memory is not RSS; phase-summed lifecycle excludes Pool setup/drain,
queries and verification. Retained-version cost, compaction, long churn,
full residence coverage and policy across growth remain outstanding.

## Wide inline values

The 64-byte user-value bound is independent of the encoded multimap record:
64 payload bytes plus one discriminator need 65 bytes in key overflow. That
extra byte is now supported by the existing chunked transfer; it does not
require a 65-byte TuplePack field. This removes the accidental restriction that
forced even singleton 64-byte values into external storage.

`HT_WIDE_BAG_POLICIES` compares W32/W64 with 8/16/32/64-byte budgets. Byte units
range from 96 to 320, with bucket-count shifts chosen alongside the budget.
The eleven candidate arms include 16/256-rank StreamPack blocks and grown
buffers, beside ordinary and exact-key Abseil with one inline value. The solver
derives arbitrary fitting slot counts. These are coupled geometry controls;
only the matched 64-byte-budget/320-byte-unit arms isolate external-owner policy.
Cardinality stays 384 or 6,144 irrespective of the chosen geometry.

The local K40 panel covers uniform fanouts 1/2/3/8/32 and the existing mixed
distribution. Its 624 processes and 40,560 rows include 13 phases: the original
nine plus reads after erasure, reuse and reads after reuse. Complete byte and
multiplicity oracles check each mutation phase. Initial tier populations remain
explicitly named `build_*`; they are not later-phase inventories. Reported
memory is requested storage plus owner metadata, with owned capacity separate;
it is not measured RSS or a retained-version peak.

The local result does not select a preset. A two-value W32 inline budget makes
singleton-to-pair append cheap, but shifts first spill to pair-to-triple. W64
inline singletons reduce the cost of reading external-only singletons and their
initial memory, while first-spill and complete-lifetime losses remain. Larger
blocks help some mutation histories but carry their own slack and damage costs.
`wide-bag-native.sh` extends this comparison to K16/K40/K64 on native hosts.
All three native jobs have completed and their reports have been revalidated:
each has 1,872 processes/121,680 surface rows and a 672-process/43,680-row matched
zero-source panel. The Zen5 native audit now binds all eight ELFs to the timed
receipts and checks complete W32/W64 reductions, dependent walks and mutation
transfers in the selected controls. Other-host generated-code qualification
remains outstanding; these completed runs do not select a policy.

Fourteen Release checks cover 64-byte inline records across every key domain,
W1/W32/W57/W63 edges and both link/buffer owners. Selected W32/W64 histories also
pass ASan/UBSan. New actual ObjectStore histories enroll readers before inline
bags spill and before spilled bags are erased/reused, then bind both old scopes
late and check independently accumulated complete contents. Hierarchy histories
prove use of all three key tiers. They use the existing whole-version cloning
control, not a byte-damage COW implementation.

A K64 row check independently encodes its 8-byte key plus 65-byte record, fills
the final row of a link adjacent to another live link, and checks cancellation,
reuse and swap deletion. Its neighbor sentinel detects writes; it is not a
guard-page read proof. Direct tag replacement in that check verifies byte
position/preservation; integrated table histories exercise the mutation path.

### Caller scratch and copies

The first W64 ELF's audited independent/dependent readers are call-free inside
their loops, and the complete payload reduction feeds the next dependent key.
Linked acquisition reserves a 32,928-byte frame for its largest-family scratch
array, although family-zero first spill zeroes/copies only 262 bytes. These must
not be confused. Full-record staging and replacement remain visible in external
appends even when only an eight-byte head/hint changes.

The default-off `HT_LINK_ZERO_SOURCE` control supplies identical initialized
slot bytes from one immutable 65,535-byte program-data object, through the same
StreamPack append operation. No allocation owner or wire format changes. A
672-process local paired panel interleaves old/new ELFs and reverses both case
and variant order; every logical/physical inventory agrees. Timing changes are
mixed and the unchanged Abseil controls also vary substantially. No speedup or
default adoption is established. The separate native comparison has been
collected alongside the wide panel; native code-generation review is pending.

The exact local paired-binary audit finds all 30 selected hit, dependent-read
and update bodies equivalent after resolving function, global and string
relocations. Their acquisition descendants change: admitted bag acquisition
has a 176-byte frame and zeroes the destination directly; ordinary key-overflow
acquisition has a 240-byte frame but retains generic StreamPack dispatch and
payload copying. The unchanged operation bodies and smaller frames establish
code-generation behavior, not a measured speedup.

Software task-clock profiles of the first ELF include untimed verification.
Only the actual update helper/callsite intervals can identify update samples;
reader helpers also serve untimed verification. The initial 4 KiB DWARF capture
does not unwind reliably through the large acquisition frame. A 64 KiB attempt
still has truncated callers and reports lost samples. These traces identify
sampled paths, not reliable wall-time shares or native performance causes.

`wide-bag-run.py report` validates source-case inventories, independent byte
fits, exact binary hashes and the forward/reverse process schedule. Its
`pair-report` additionally checks interleaving, reversed variant order and exact
agreement of every non-timing output field. Exact local ELFs, raw rows, source
snapshots and selected disassembly remain under `build/hashtable-design/` pending
selection with the native evidence.

### External append without full-record replacement

`HT_EXTERNAL_BAG_PATCH` is a separate default-off caller control. Once a bag is
external, only its eight-byte head/insertion-hint descriptor is live in the
index payload. A scoped existing-row continuation copies that prefix, reserves
one value from the independent bag owner, then commits the value. It patches
the descriptor only if it changes; stable descriptors issue no index write or
index damage. The discriminator and inactive payload bytes remain untouched.
First spill and absent-key insertion retain their original preparation path.
This changes neither layout nor ownership and adds no production primitive API.

The existing-row result distinguishes decline from handled success or refusal.
Decline has no effects. A refused reservation can retain bag capacity/structural
changes but cannot fall through to a second preparation, change reachable
values, patch the index or publish cardinality. Copied inputs remain valid
through bag relocation, observer space is admitted before preparation, and no
failure is possible between bag commit and the optional index patch. Observers
are synchronous and cannot reenter these private owners.

Release checks cover complete W32/W64 linked and buffer histories, including
actual retained-reader publication. `external_patch_check.cpp` independently
checks eight-byte patches with poisoned inactive payload and neighboring packed
tags across all key domains and all three tiers. It also forces real allocation
refusal/retry in both bag owners, checks packet refusal and repeated-key prefix
semantics, and verifies omission of index damage for stable buffer descriptors.
All three checks also pass ASan/UBSan. A separate W2 history covers all eight
key domains with the control enabled in both Release and ASan/UBSan, and the
existing mutation/damage check remains green in Release.

The matched local comparison completes 672 interleaved processes and 43,680
rows, with identical non-timing inventories. For the 256-link/320-byte-unit
control at 6,144 K40 keys, W32 fanouts 3/8 and W64 fanouts 2/8 have update ratios
of 0.31/0.32 and 0.48/0.38 versus the original caller. Both passes favor those
changes. Mixed-history lifecycle ratios are 0.81 for W32 and 0.87 for W64, but
remain 1.18/1.71 times ordinary Abseil. W64 singleton first spill remains slow;
W32 singleton inline append varies between passes. Unchanged Abseil timings
also vary. These OrbStack observations motivate native measurement and do not
establish adoption, broad parity or a causal allocation-time breakdown.

The source snapshot is `build/hashtable-design/external-patch-source`, with
exact paired ELFs/receipts in `build/hashtable-design/wide-inline-external-pair`.
`external-bag-native.sh` measures the same complete operations beside both
baselines. All three native jobs completed successfully, were collected, and
their paired reports independently revalidated. At 6,144 K40 keys in the
64-byte-budget/320-byte-unit/256-link control, already-external W32 fanout-eight
update ratios are 0.54/0.59/0.45 versus the old caller on Zen5/Granite/NEON.
W64 singleton first-spill ratios are 1.17/1.10/0.78, and remain 5.67/5.78/2.62
times their corresponding ordinary Abseil control. Mixed-history W64 lifetime
remains 2.11/2.17/2.31 times Abseil. These warmed fixed-geometry results leave
the control off; they are not cache-residence or whole-surface conclusions.

The exact local emitted-path review confirms direct input commit and conditional
eight-byte descriptor patching, with refusal returning without fallback. It also
finds larger update bodies. External readers already defer inactive payload
loads to the inline arm, so an unconditional full-record read is not an
explanation for their remaining losses. Native first-spill inspection is
complete: the selected primary-hit route adds three conditional branches and
reads the tag twice, while the downstream first-spill acquire helper is
instruction-identical after relocations. Its larger update body retains the old
value staging and complete-record replacement. This establishes emitted overhead,
not the fraction of the timing regression it causes. Other-host code-generation
qualification remains outstanding.

### Scoped first spill

The next default-off `HT_BAG_SPILL_PATCH` control handles a full inline row in
the existing-row continuation. It copies only live inline values, reserves one
external group, commits the old and new values from separate stable inputs,
then patches the eight-byte descriptor and discriminator. Inactive index payload
bytes retain their prior contents. There is no concatenated value proposal or
full-record replacement. Failed preparation can retain an empty bag owner or
extra capacity, but must publish no index write, reachable value or caller delta.

The split-input reservation shares the existing allocation and publication
boundary in both linked and grown-buffer owners. Aggregate prefix transfer
widths are admitted when constructing the table, before any bag owner exists;
this matters for narrow non-word widths. The control does not change first-link
capacity, bucket geometry or a production packing API. `spill-bag-native.sh`
compares complete operations against the unchanged caller and both Abseil
controls. Seven Release checks pass, including W2 across all eight key domains,
W1/W3/W32/W57/W64 histories and forced first-spill refusal/retry in every key
tier for both storage owners. Four ASan/UBSan checks cover W3/W32/W64 publication
histories and the all-tier pressure/retry check.

The exact local W64 linked update audit finds that the old-record fallback and
128-byte concatenated proposal disappear. The selected update body shrinks from
5,660 to 4,240 bytes and its frame from 1,008 to 256 bytes; the downstream link
acquire retains its 32,928-byte frame. Both complete 64-byte values reach the
bag and all three index tiers receive exactly the descriptor/tag stores. This
is an exclusive private-owner mutation: the emitted count store precedes the
last payload store, so these instructions are not an atomic publication scheme.

The local comparison completes 672 processes/43,680 rows with matching output
inventories. In the K40/6,144-key/256-link control, W32 pair-to-triple update is
0.66 times the old caller and 1.83 times Abseil. W64 singleton-to-pair update
varies substantially between passes; both pass ratios favor the change, but
the pooled result still costs 3.25 times Abseil. Already-external fanout-eight
updates improve strongly, while read deficits remain. Unchanged baseline
timings also vary. This VM evidence supports native investigation, not adoption.

The composition lesson so far is above the packing kernels: finishing an
existing-row operation in its scoped continuation avoids normalizing it through
a complete decoded-record proposal. Handling refusal distinctly from declining
the continuation prevents a second preparation attempt. The generated-code
savings include control flow and register allocation, not just the split copy.
Frozen local inputs and results are under `build/hashtable-design/` in
`spill-patch-source-v2`, `wide-inline-spill-pair` and `spill-local-codegen`.

All three native runs have completed and their paired reports are independently
revalidated. The [cohort reference](evidence/spill-native/cohort.json) binds the
full archived inputs, exact ELFs and quoted rows. Control flags are **0/0** and
changed flags **1/1**: this measures the combined external/first-spill change,
not an incremental comparison with the earlier external-only ELF.

For the K40/6,144-key/64-byte-budget/320-byte-unit/256-link control:

| Host | W64 first-spill update / old caller | / Abseil | W32 mixed lifetime / Abseil | W64 mixed lifetime / Abseil |
| --- | ---: | ---: | ---: | ---: |
| Zen5 AVX2 | 0.80 | 4.13 | 1.35 | 1.66 |
| Granite AVX-512 | 0.87 | 4.35 | 1.53 | 1.96 |
| Neoverse V2 NEON | 0.87 | 2.91 | 1.07 | 1.33 |

Both pass ratios favor these first-spill changes. The remaining losses are
substantial; some unchanged-baseline timings also vary. NEON W32 singleton
inline update regresses about 11% versus the old caller despite remaining faster
than Abseil. Read costs have not been repaired. These warmed controls therefore
do not establish a competitive policy or native cache/DRAM coverage. Selected
native emitted-path audits remain in progress and neither flag is adopted.
