# One or two inline 64-bit multimap values

Two inline values substantially reduce the cost of the second append and of
reading two-value bags. They move the spill boundary to the third append; they
do not remove the large short-bag mutation deficit. Retaining 128-byte buckets
also reduces key capacity enough to create substantial key overflow. Roomier
buckets recover much of that lookup cost at greater initial storage. This
experiment does **not** select an inline policy, and covers **64-bit values only**.

The three native panels retain every case, arm, repetition and phase:
[Zen5 AVX2](../inline-zen5/), [Granite AVX-512](../inline-granite/), and
[Neoverse V2 NEON](../inline-neon/). Each has 56,376 rows from 696 processes;
the complete cohort has 169,128 rows. All standalone owner, recipe, reservation,
inline-boundary and retained-reader checks passed before timing. Native timing
does not include MVCC publication or multiple writers.

## Matched representations

Arms 0/1 use one inline value; 2/3 use two inline values in the same 128-byte
primary/secondary units; 4/5 use two inline values with roomier units. Even arms
compose SeriesPack/TuplePack operations; odd arms use direct bucket-field
operations with the same owner, routing and chains. Arms 6/7 are ordinary-key
and exact-key Abseil maps with an InlinedVector per multimap key. That baseline
actually holds two 64-bit values inline on these targets.

Roomier units are calculated independently for each tier as
`round_up_to_64(128 + 8 * one_inline_exact_slots)`. Two values expand payload
from 8 to 16 bytes and tag from 1 to 2 bits. Candidate read permission is
whole-bucket, with inactive neighbors masked; admitted StreamPack append and
loop-local reductions are common. Set/map arms 0/2/4 and 1/3/5 are duplicate
representation controls. Every per-repetition pair has identical requested
bytes, metadata, slots, population and operation counts; all six set/map arms
also have identical inventories. These checks do not imply identical generated
code or process placement.

The mixed workload covers K8 through K64, all three container kinds, and
bucket exponents A6/A10 except K8/A6 only. Its initial bags have one value,
except every third key has two. One update appends one value to every bag.
Separate uniform initial fanouts 1, 2, 3, 4, 8, 32 and 128 cover multimap
K16/K40/K64 at both exponents. They remain separate strata in the reports.
Cardinality is fixed from the explicitly retained legacy slot-count control;
formats are not compared at their own equal occupancy. Actual current slots
are arbitrary exact fits; only bucket counts are powers of two.

## Results

The following are medians of per-case ratios for the **roomier composed
two-inline** arm. Lower is better. Mixed rows contain 15 multimap cases;
uniform-fanout rows contain six. They are descriptive summaries, not workload
weights or statistical estimates across independent machines.

| Comparison | Zen5 | Granite | V2 |
|---|---:|---:|---:|
| Mixed append / one-inline | 0.809 | 0.815 | 0.793 |
| Mixed append / ordinary Abseil | 4.677 | 4.742 | 5.730 |
| Mixed hit / ordinary Abseil | 1.067 | 1.068 | 0.818 |
| Mixed serial / ordinary Abseil | 1.239 | 1.232 | 1.063 |
| Uniform 1 → 2 append / one-inline | 0.208 | 0.198 | 0.197 |
| Uniform 2 → 3 append / one-inline | 4.492 | 4.697 | 3.531 |

The roomier mixed arm wins independent hits in 4/15 Zen5, 5/15 Granite and
14/15 V2 cases against ordinary Abseil. It loses all 15 mixed append and
phase-summed lifecycle cases on every platform. It loses all 15 serial cases
on both x86 targets and 12/15 on V2. The broad performance target is still unmet.

The same-128-byte two-inline arm has mixed append ratios 0.924/0.924/0.824
against one-inline, but mixed hit ratios 1.678/1.680/1.161. Median build-time
key-overflow fraction rises to 18.2%. This is key overflow in the hash table,
separate from spilling a bag's values.

K40/A10 mixed bags make the physical trade concrete (7,168 keys):

| Representation | Primary / secondary slots | Key-overflow rows | Initial logical bytes | After append |
|---|---:|---:|---:|---:|
| One inline | 9 / 8 | 24 | 242,406 | 434,730 |
| Two inline, same 128 B | 5 / 5 | 2,054 | 214,894 | 316,458 |
| Two inline, roomy 256 / 192 B | 11 / 8 | 0 | 274,736 | 376,300 |

In that case, Zen5 independent hit is 4.955/12.109/4.660 ns and append is
76.115/74.098/62.008 ns respectively; ordinary Abseil is 4.470/13.851 ns.
The roomier representation consumes more initial logical memory, while the
same-size representation saves memory but pays for increased key overflow.

Uniform long-bag append wins are retained, including negative read results.
For example, roomier V2 fanout32 append is 0.396 times Abseil but hit/serial are
3.449/3.136 times Abseil. These append points coincide with the contiguous
baseline's growth boundaries and are not evidence of broad bag-operation
parity. All exact-key baseline and direct-field results remain in each host's
`comparison.csv` and `ranges.csv`; no best-arm minimum is selected.

Set and map controls also retain their losses: composed mixed set/map hit
medians are 1.674/1.623 times Abseil on Zen5, 1.634/1.573 on Granite, and
1.832/1.767 on V2. There are no independent-hit wins for these fixed arms.
This bag experiment does not resolve the general table lookup deficit.

## Native call boundaries

The audit resolves 1,800 hit/miss/serial/update/erase regions per ELF using
frozen-source DWARF caller locations, including 108/108/123 outlined timing
wrappers on Zen5/Granite/V2. Some MM miss paths have compiler-cloned ending
clock calls; every reachable end must independently resolve to the same source
phase. No phase comes from a clock ordinal or lambda number.

All 3,240 native read regions have fused per-key and per-value consumers.
Where the compiler outlines a complete read loop, the sole outer helper call
is verified outside a CFG cycle, and the helper contains no per-key/per-value
calls. No indirect calls or unresolved branch targets remain in the audited
regions. This is a call-boundary result, not a timing or branch-frequency model.

K40/A10 MM update retains a per-key call to the outlined index-mutation lambda
inside `promoted_table::try_set`, for the one-inline, same-128 and roomier
composed arms. Its nested proposal/reservation path is retained in
`mutation-paths.json` beside each host's evidence. The shorter inline path and
spill path share that compiled owner entry. Reachable allocation and failure
calls do not imply that each append executes them; no frame-size or instruction
count is presented as a cost attribution. Detailed selected disassembly is
regenerable from the retained ELF.

## Measurement and recovery limits

- Each case/arm gets a fresh process, shuffled with seed 127; each uniform
  fanout has its own seed `127 + fanout`. Nine repetitions construct fresh
  owners and Pools. Process placement, temporal drift and Abseil seed changes
  remain; these are not nine independent machine trials.
- Complete read loops warm up before timing. Independent point oracles check
  misses and every full-width value. Serial multimap XORs all values and proves
  the entire key cycle. Build is reported per key, including construction of
  its complete initial bag, not per appended value.
- Lifecycle is the sum of separately timed construct/build/update/erase/release
  phases. Oracles, inventories, reads and Pool setup/drain are outside that sum.
- Requested storage plus metadata is the logical inventory. Owned backing is
  reported separately; baseline backing zero means unknown. Inventories are
  after build, update and erase; population fields always describe build.
  No transient peak, RSS, cache-residence, DRAM throughput or prefetch claim is
  established by these byte totals. K8 bypasses the low-occupancy list policy.
- ISA and microarchitecture vary together across these hosts. This is not an
  ISA-only comparison, a concurrent-writer comparison, or a policy for values
  narrower/wider than 64 bits. Later live width-generic source is not part of
  this immutable experiment.

Frozen source manifest:
`7597501343a4ce7d7726a7dc005007e4515de6924c1f5fafe36da27b65744a38`.
Source archive SHA-256:
`818463d3b0cff9994d8439f86aa407f785cee06f32f0e252d3f4f1c19a759a00`.
Driver SHA-256:
`96ab1264e3b123364ffcb3fc0488c12b654ae7ab5e5daec083f72ba4c22e8598`.
Group: `20261013T204030Z-4f6b5cdf`.

[context.json](context.json) links the three exact worker archives and ELF
identities. Each sibling host directory contains the raw CSV panels, checks,
shuffled schedules, artifact recovery reference, comparison and codegen
summary. `compare.py` revalidates all raw rows, paired inventories, shuffled
orders and archived summary reproduction. `audit-dwarf.py`,
`codegen-summary.py` and `mutation-paths.py` reproduce the native audit; commands
are in each host's `context.json`. Full binaries, disassembly, compile commands
and the original source archive remain recoverable through `artifact.json`.
