# Compact small-table state: exact mapping, complete owner controls

Owned source: `workbench/spikes/hashtable-design/compact_hash.h` and
`compact_state.cpp`. No root hash/small header or CMake change. This is a scalar
state experiment; no compact packet lowering or new hash family is selected.

The capture preserves the existing permutation exactly. At A=0 the bucket seed
is dead. At C>0 the cached exact selector is dead. At K=8,A=0, the masked input
XOR and constant selector XOR compose into a single stored byte. Other A=0
widths retain the full selector seed and masked input word (16 B). General A>0
geometries retain 24 B. Only multimap needs a separate value count: set's is zero,
map's equals its distinct-key count. The caller seed mechanism, full-width seed
derivation, pool/allocation owner, count, actual bag owner, and clone lifetime
remain present. This does not establish security of the underlying hash.

## What ran

Pinned Linux Clang 21.1.8, O3, autovectorization disabled, ARMv8.2+crypto generic
on local Apple/OrbStack CPU1. This is not a V2/V3/x86 native result. Exact compile
and link argv are in `release-commands.json`; all captured source hashes in
`release-source.json`; source itself is under `capture/`. Relevant hashes:

- compact_state.cpp: `0aa29c39e759bf7bdc744917bf680c9d4fff09a4841345484270af9928571bf9`
- compact_hash.h: `c5d6018329a5f1ba55f14d9bb14119f0a115828263e9e90ad6de6687ebbab955`
- captured small.h: `be3b5b8a22c594704bc010a291ff763bdd1a0c17dff3f99e3757b7e27b841f87`
- captured hash.h: `a14a9ce17ef8c3e8c8dbd20b7235b520f0c36e8a1df6d36a05d4057f19ce65b5`

Commands: `release check`; `taskset -c 1 release bench 9`. A second identical
nine-repetition run checks the unstable local lifecycle observations. Final
phase-correct files: `local.csv`, `repeat.csv`, `summary.csv`, `summary.txt`.
Each CSV has 15,120 rows (1,680 cells x nine repetitions). Arm order rotates.
`release-check.log` passes. Earlier O1 check also passed. No state-specific ASan
or native-target capture has run.

The five arms are root original, copied body with full state, the same copied
body with compact state, ordinary Abseil, exact-key Abseil. Both Abseil timed
owners use actual ordinary stateless allocators: 16 B, not the larger counting
wrapper. Separate untimed counted copies are checked for identical contents
and storage capacity and report allocation requests.

Every K=8..64 by8, all three kinds, n=0/1/4/16/64, one or64 owners. Lifecycle
time includes the owner vector, owner construction, population, two mutation
rounds, four mixed read rounds, consuming every value, and destruction. MM starts
with three equal values per key and appends two different values; duplicates
are observable. Allocation pool setup/drain, input/expected preparation and
inventory are outside the timer. Individual lifecycle operations are not
homogeneous: use elapsed time, not a claim that one MM value equals one map op.

Serial has 4,096 answer-dependent steps. Maps use returned values as next keys;
MM XORs its three values to the successor and consumes every value. Sets use a
common 24 B query node with membership-selected next index, half misses. This
is a complete caller loop, not isolated table latency. n=1 map/MM is a degenerate
single-successor control; ordinary Abseil has an actual no-hash single-element
path. Its very low timing must not be generalized to nontrivial dependence.

Memory is phase-correct: lifecycle reports final five-value MM state and charges
the owner array's 24 B vector object; serial reports initial three-value state
and its one owner, with no vector header. `requested` is live requested payload;
`owned_capacity` is actual Orbital allocation capacity for the candidate and
unknown/zero for Abseil. Compare requested+metadata consistently; do not call
that RSS or allocator usable bytes. Common pool/input storage is excluded.
MM bag directory/owner capacity is included in metadata. Only n=0 has no payload.

## Checks and code generation

All 288 legal (K,A) geometries have exact split and cross-inverse checks over
32 full-entropy seeds; K8 is exhaustive, wider K has 512 mixed/extreme samples
including high bits outside the key domain. Original, full, compact owners
match an independent std::map<vector> oracle through insert/repeated update,
MM duplicates, dense swap erase, counts/deltas, retained clone, zero-budget
publication refusal and clear. Timed digests are checked independently.

`release.asm` and `codegen.json` contain the final executable. 480 timed
populate/update-consume/serial/lifecycle roots have zero indirect calls (untimed
counted-inventory instantiations excluded). Set/map operation wrappers and
ordinary Abseil hot bodies fuse. MM search/admission fuses; its inner
`apply(...)::lambda()::operator()` proposal/commit body remains one direct call
per mutation in original/full/compact alike. This is not an outlined outer
apply or a per-slot search call. MM chain reads retain direct links::each calls;
no claim of entirely call-free MM is made.

K8 map serial's original/full/compact functions contain 168/168/166 static
instructions; K40 contains 212/212/211. These whole-function counts are not
cycles. In K8, original already combines the two constant XOR seeds once before
the loop. Compact replaces those entry loads/XOR with one byte load. The loop
still has the same XOR -> selector compare/mask -> index -> value load. For K40,
required selector multiplication and suffix check remain. This explains why
saving stored state need not shorten the dependent path. The ordinary Abseil
single-element branch loads key once, compares the current key, and loads the
fixed value address; its no-hash path is real, not a missing baseline call.

## Observations and recommendation

| Owner | Original | Compact | Ordinary/exact Abseil |
|---|---:|---:|---:|
| K8 set/map | 88 B | 48 B | 16 B |
| K16..64 set/map | 88 B | 64 B | 16 B |
| K8 MM | 96 B | 64 B | 16 B |
| K16..64 MM | 96 B | 80 B | 16 B |

No payload bytes or placements changed. This is useful metadata simplification,
not a repair for current tiny-table operation costs. Across n>=4, all eight K,
64-owner lifecycle, geometric means of full/compact per-cell median times are
1.008/1.016 for sets in the two runs and 1.006/1.011 for maps. Serial ratios are
0.986/0.988 for sets and 1.001/1.001 for maps. MM lifecycle changed from0.946 to
1.010 on the repeat; serial1.005/0.999. Do not promote a speed claim from that
local instability. Set individual cells also vary more than the aggregate.

For K40 n16, one owner's final requested+metadata (including the common24 B
vector header) is320 ->296 B for map vs576 B ordinary Abseil; set192 ->168 B vs
328 B ordinary/235 B exact-key Abseil. At n1, compact remains93 B set/101 B map
vs40/56 B Abseil, before candidate pool rounding. MM n1 five-value state is526
->510 B vs136 B baseline: the extra bag/directory ownership dominates a16 B
hash-state saving. At n64 MM the compact state is8368 B vs8336 B baseline.
These actual tiny-state controls reject treating packed payload alone as the
memory result.

Take the exact dead-state/count removal as a simple encoding improvement if the
parent integrates it, with equivalent packet and retained-reader checks. Keep
its speed claim at neutral. There is no reason to carry the copied state_table
implementation forward as a second owner API.

A representation can potentially own immutable entropy/hash context once and
pass a typed reference alongside the headless allocation view, allowing a bound
caller to hoist it without a stored per-table indirection. That is a different
ownership experiment, not a free deletion of bytes: charge context once, retain
it with every reader, preserve caller counts and allocation ownership, and use
shared seed only when these tables are genuinely one representation/generation.
Do not silently make unrelated same-schema tables share entropy. At one owner,
externalizing state may only move bytes. No shared-context implementation or
measurement is claimed here.

## Retention and recovery

This selected evidence retains both nine-repetition CSVs, summary, checks, source
identity, exact commands and host context. The artifact helper's full bundle also
contains the final executable/object/disassembly, captured spike sources, analysis
script, and dependency headers/static libraries. Fetch artifact.json with
`workbench/tools/artifacts.py fetch` into ignored storage to recover them.

The spike-source capture preceded compilation. External dependency headers and
static libraries were collected at retention; their recorded mtimes all precede
the driver object, but their content was not hashed at original execution.
`dependencies.json` preserves this distinction. The archived measured binary and
assembly are authoritative. Toolchain/system libraries are not embedded.
`rebuild.py` relocates the recorded commands to recovered files; that recovery
build recipe has not been executed. This retention does not certify an unseen
fresh build as equivalent to the measured binary.
