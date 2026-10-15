# Bounded aggregate primary-hit inspection

Read-only inspection of the three completed aggregate jobs named in source-inputs.json.
Source capture: 8cd931e84ad5445796fbf8d0fabfe7de0176cd2888e2e3f9682bfd90c94ee451.
No repository implementation changes or native benchmark reruns.

## What was selected

Fuller K40/A8/B4/P128/Q128 one-search accumulation, and ordinary Abseil accumulation.
The .asm files are complete symbol extracts from retained final binaries. The .s
files select actual instruction ranges listed in mca-summary.json: load the next
record, first primary/group lookup, first fingerprint candidate is the exact key,
existing-value sum/store, caller occupancy delta and loop advance. No insertion,
false-positive candidate, secondary visit, chain visit, or one-time prologue.
Branches are preserved as instructions and their targets renamed .Ledge, with
blocks linearized in their assumed taken order. Nops are omitted. The selected
path is plausible, not a measured dynamic profile. Secondary/header population
is not request-frequency evidence, especially for skewed input.

## Model

Pinned Ubuntu LLVM 21.1.8 llvm-mca, 100 repeated paths; exact commands are in
mca-summary.json. CPU models: neoverse-v2, znver5, graniterapids. The latter models
report Zn4 and SPR resource names respectively. Default no-alias/cache-hit memory
model; not a model of real spill store-to-load forwarding or same-key update
chains. Branch prediction, frontend code placement, memory misses, first insertions
(1/16 of the repeated trace) and downstream visits are absent. This is neither
measured ns nor answer-dependent operation latency. Do not multiply these figures
by clock period to explain the native results, or subtract subpaths as additive
cost attribution.

| Model | Fuller Block RThroughput | Abseil Block RThroughput |
| --- | ---: | ---: |
| V2 | 10.5 | 7.8 |
| Zen5 | 10.0 | 8.3 |
| Granite | 10.2 | 7.7 |

## Observed instructions and geometry

Both primary-hit paths are fused. The fuller primary path executes no owning,
allocation, dispatch, prepared-field interpretation or callback call. Its two
independent folded products produce bucket and selector, versus one product in
ordinary Abseil. These two products are not a serial two-multiply mixer: address
and selector chains can overlap. Current occupancy must be loaded and masked
before extracting the candidate lane. Exact suffix comparison reads two bounded
pieces (16+8 bits); primary values are plain aligned 64-bit loads/stores at +48.

On V2, fuller addresses 15880/15884 produce the bucket fold and 1589c/158a0 the
selector fold. 158bc/158c4 load fingerprints/count; 158dc masks before lane
extraction. 15908/1590c read the suffix; 15924..1592c sum/store. The common loop
has no stack spills. The V2 Abseil sequence 10de4/10de8 computes one fold and
10e3c loads the full key before its value update.

On Zen5, fuller 1702b and 17042 are MULX for bucket/selector. Five stack stores
execute on the selected hit: accumulator, incoming value, original key, masked
key, and FP-plane address (16ffd,17009,1700e,17018,17069); two reloads execute at
16fd0 and16fda. Granite has the equivalent stores/reloads. Abseil has one key
spill in its selected common path. V2 does not share this penalty, so it cannot
be the general explanation. Nor does this inspection establish a particular
outlining/refactoring remedy.

Both fuller primary geometries in this receipt use 9 slots, 3-byte suffixes,
values beginning at byte48 in a 128-byte bucket. Thus primary value accesses
are aligned and never cross a 64-byte line. The selector and suffix planes fit
in line0; values for slots2..8 lie in line1. Abseil's 16-byte native key/value
rows keep a key and its value together. Secondary layouts differ and can have
unaligned values. Cacheline split penalties must not be attributed to these
primary values.

## Recommended next architecture experiment

A bound mutation cohort with an explicitly outlined primary-miss continuation,
retaining the exact hash, placement, capacities and physical layout. The cohort
captures primary/secondary bases and both immutable permutation recipes once.
Its hot primary match/value update is inlined; only a miss forms the continuation
state and calls the ordinary downstream/insert body. Pass the already computed
primary parts so the continuation neither rehashes nor searches primary twice.
No authoritative occupancy duplicate, allocation on a hit, escaping slot pointer,
or per-operation owner check. The source's constructor-established scratch
mutation authority remains the scope.

Compare original one-search accumulation, this bound consumer and ordinary
Abseil on the same distinct/repeated/skewed records; charge binding, misses,
insertions, refusal handling, context renewal, growth and owner release wherever
those operations are present. Include complete ingestion, not just prepopulated
update-only loops. Verify generated common paths to see whether context reloads
and x86 spills actually disappear. Do not infer removal from the source-level
CPS boundary. If those disappear but full-operation performance does not improve,
the boundary hypothesis is falsified; do not keep tuning around it.

A cohort is valid only while primary/secondary owners, geometry and hash recipes
are stable. A relocation/reseed ends it; the owner must rebind before continuing.
Overflow-only growth must preserve the same logical continuation contract and
must not leave escaped pointers into relocated StreamPack blocks. Combiner is
infallible, immediate and non-reentrant; key-level refusal preserves this key,
while already completed rows remain committed. Growth/rebind/continuation may
not silently retry a completed combination. These are narrow validity rules,
not a new reservation or ownership layer.

The causal evidence differs by target: x86 has actual hot stack traffic for
cold state; V2 instead reloads owner geometry per iteration and has no hot-loop
spills. Improvement is plausible, not established; the hash and metadata search
work remain. The retained timing panel is fixed geometry, so a new cohort result
without timed growth/rebinding still makes no online-growth performance claim.

## Separate selector discriminator

Do not mix the mutation-boundary change with the reviewed selector change.
A subsequent orthogonal comparison can retain the current bucket permutation
and replace only suffix-to-selector encoding with seeded multiply-add high bits.
Primary C24/F8 and C18/F8 admit a 32-bit intermediate (C+F-1 <=32). Small secondary
B4 has C28/F8 and needs >=35 bits, so use64 there; B10 secondary fits32. Preserve
independent selector entropy requirements, inverse and conditional-collision
checks. Placement stays identical while fingerprint candidate counts can change.
This can reduce selector work, not the bucket-address dependency. The two current
folds are independent and can overlap; it is incorrect to subtract one multiply
latency from measured operation time. C0 geometries already have a constant
selector, so this alone cannot establish broad all-K competitiveness.

Occupancy post-check and eager exact matching remain separate alternatives;
old exact-match failures are not erased by either proposal. No new layout sweep
or general hash-family search is recommended by this inspection.
