# Native value-width comparison

The integrated byte-valued table now has native correctness coverage beyond
64-bit values and comparative timings for 2-, 8- and 16-byte values. The broad
performance target remains unmet. Narrow multimap values expose a useful
distinction: even an append that stays inline, without allocating or changing
key population, is still much slower than Abseil. Moving the spill boundary
alone cannot fix that path.

The complete panels are retained for [Zen5 AVX2](../value-widths-zen5/),
[Granite AVX-512](../value-widths-granite/) and
[Neoverse V2 NEON](../value-widths-neon/). Each contains 220 processes, nine
repetitions and nine phases: 17,820 rows per host, 53,460 total. No arm, fanout,
negative result or repetition is discarded. This is single-writer evidence.

## Representations and workload

All timed keys are K40; values occupy their actual 2, 8 or 16 bytes. Candidate
operations compose SeriesPack/TuplePack bucket fields and StreamPack linked
bags, using the byte-valued interface, admitted bag append and whole-bucket
read permission. The five arms are:

| Arm | Representation | Primary / secondary unit | MM inline values for W2 / W8 / W16 |
|---|---|---|---|
| 0 | 8-byte inline payload budget | 128 / 128 B | 4 / 1 / 0 |
| 1 | 16-byte inline payload budget | 128 / 128 B | 8 / 2 / 1 |
| 2 | 16-byte inline payload budget | 192 / 192 B | 8 / 2 / 1 |
| 3 | Ordinary-key typed Abseil | Native container | 8 / 2 / 1 |
| 4 | Exact-five-byte-key typed Abseil | Native container | 8 / 2 / 1 |

Map arms 0 and 1 have identical physical inventories and populations; both are
retained as code-placement controls. Abseil multimaps use
`InlinedVector<value_type,1>`; its actual capacity, shown above, exceeds one for
narrow values. W2/W8 use their ordinary unsigned type; W16 uses a 16-byte array.
The exact-key baseline is retained, not replaced by ordinary-key results.

There are 384 or 6,144 keys (`6 << A`, A6/A10). Maps start with one value/key.
MM initial fanouts are W2: 1/2/4/5/8/9/16/32; W8: 1/2/3/4/8/32;
W16: 1/2/3/8/32. Update replaces every map value or appends one MM value/key;
erase removes alternate keys. This samples both sides of inline and linked
growth boundaries. It does not represent a steady-state arrival distribution.

Every arm receives the same shuffled keys and complete value bytes. Independent
contents and point-query oracles check every value and all misses. Full-value
XOR digests produce the serial lookup's rank delta; the graph is a shuffled
cycle through every key, with each edge independently checked. Full hit, miss
and serial loops warm before timing. The reported serial cost includes digest
and next-rank arithmetic. Build is per key, including its complete initial bag.

## What the narrow values establish

For W2, A10, fanout 1, arm 0 appends a second value within its four-value inline
capacity. The source takes the inline branch, existing keys stay in place, and
requested/backing/metadata inventories are unchanged. The following are ratios
to ordinary typed Abseil, lower being better:

| Operation / inventory | Zen5 | Granite | V2 |
|---|---:|---:|---:|
| Hit | 1.076 | 0.992 | 0.960 |
| Serial | 1.113 | 1.124 | 1.044 |
| Append | 4.349 | 4.552 | 5.080 |
| Phase-summed lifecycle | 0.980 | 1.064 | 1.373 |
| Initial logical memory | 0.517 | 0.517 | 0.517 |

Append is 19.21/22.02/33.94 ns, versus Abseil's 4.42/4.84/6.68 ns. Exact-key
Abseil still leaves append ratios 4.016/4.317/5.025. The initial logical
inventory is 139,568 versus 270,184 bytes, with no key-overflow rows. These
results support investigating the complete mutation path independently of bag
allocation. Unchanged inventory by itself would not prove absence of transient
allocation; that qualification also relies on the executed inline source path.

At fanout 4, arm 0 spills on the fifth value and append costs
20.73/20.02/20.70 times ordinary Abseil. The larger inline budget avoids that
spill, but its 128-byte units reduce key capacity: 1,261 of 6,144 keys enter
key overflow, and append is still 7.04/6.44/6.13 times Abseil. The 192-byte arm
reduces key-overflow rows to five; append remains 5.13/4.55/5.15 times Abseil.
Initial memory ratios are 0.517, 0.680 and 0.776 for these three arms. Bag
spilling and key overflow are different costs; neither should be hidden by an
aggregate best-arm score.

Granite W2/fanout4/arm0 hit is 2.527 times Abseil, versus 1.316 on Zen5 and
1.168 on V2. Other Granite fanouts also have uneven independent-hit behavior.
Fanout is a runtime input to the same binary. This panel establishes the
timings and read-call fusion, not the cause of that anomaly; no instruction
count or comparison across fanouts is treated as a causal profile.

## Wider values and remaining losses

The same capacity trade changes with width. At W8, A10, initial fanout1,
8-byte-budget append spills and costs 22.57/21.44/20.33 times Abseil. The
16-byte-budget, same-128-byte arm stays inline and lowers that to
6.47/5.94/5.88, while its hit costs 2.29/2.84/1.80 times Abseil. On the third
value that arm spills instead. These are threshold shifts, not a width-agnostic
inline policy.

W16 cannot hold a value in arm 0's 8-byte budget, so its singleton already has
external bag storage: 591,024 logical bytes, 2.187 times Abseil. Arm 1 holds
one value inline and drops initial logical memory to 183,650 bytes (0.680),
but spilling on value two costs 6.52/6.96/5.29 times Abseil. W16 arm 0 wins
some append cells because its linked storage has room when the contiguous
baseline grows. At A10/fanout32 its append ratios are 0.351/0.431/0.443, while
hit is 2.449/1.775/2.423, serial 3.054/1.960/2.267 and lifecycle
4.047/4.150/4.676. Those wins do not establish broad bag-operation parity.

The map controls retain material losses as well. Arm 0 loses hit, serial and
update in all six W-by-A cases on every host. W16's two-geometry median hit
ratio is 2.91/3.11/2.77, and update is 3.66/4.05/3.82. Larger unit size does
not consistently fix lookup even when it removes key overflow. Full per-case
ordinary/exact-baseline comparisons, all arms, min/median/max and populations
are retained in each host's `comparison.csv`. No preset is selected here.

## Codegen and correctness qualification

All three hosts pass standalone checks for value widths
1/2/3/4/7/8/9/16/17/32/64 bytes. Only W2/W8/W16 are timed in this panel; these
checks are not performance coverage of the entire supported range.

The [DWARF audit](../value-widths-codegen/) resolves all 540 native read
regions against the captured driver. It follows outlined timing wrappers and
whole-loop helpers at the actual source callsite; phase assignment never uses
clock ordinal or lambda number. Every candidate and typed-Abseil read consumer
is fused at the per-key/per-value boundary. Whole-loop helper calls are outside
the query cycle, with no unresolved or indirect hot branches. The archived
timed ELF hashes exactly match those audited. This is static call-boundary
evidence, not dynamic frequency or an attribution of the mutation deficit.

Selected full-value witnesses beside each host's receipt separately show
two-byte loads and both eight-byte halves of W16 reaching the reduction and
serial dependency. The source digest's XOR cancellation does not allow the
compiled consumers to narrow the values to a low word. Full assembly, ELF and
compile flags are recoverable, including explicit target ISA/tuning and disabled
automatic loop/SLP vectorization.

## Limits and recovery

Each case/arm runs in a fresh process in seed-912 shuffled order, with nine
fresh owners/Pools per process. Repetitions within one process are not nine
independent machine trials. Abseil seed and process placement variation remain.
The byte inventory is requested storage plus metadata; owned backing is separate
and baseline backing zero means unknown. Lifecycle is the sum of separately
timed construct/build/update/erase/release phases. Oracles, inventory collection,
reads and Pool setup/drain are excluded. Inventory snapshots are after build,
update and erase; population fields always describe build. There is no transient
peak, RSS, measured residence, DRAM, prefetch-ring, MVCC or multiwriter claim.
Equal cardinality and unit bytes do not give each format equal occupancy.

The hosts differ in both ISA and microarchitecture; this is not an ISA-only
comparison. All workers completed and terminated. Source manifest:
`8962f570759f348c24a89c60a1483c01ef469f9f73763c592c84a4ac50ee513d`;
archive SHA-256:
`e15b4e078091389eaf315d246421cf97b848537fe2054e4235e0a7160beacf9a`;
driver SHA-256:
`310cbb41d43f13fd5d4f862804779666e92a130d12a9bd5327ca13ed538087ad`.
Group: `20261013T211118Z-4a441080`.

[context.json](context.json) records the exact worker archives and binary
identities. `report.py` is an explicitly retained **post-run analysis** copy of
the root report; it was not in the native source capture. `qualify.py` reproduces
its result, verifies every schedule/return code/raw chunk and ELF identity, checks
duplicate map inventories, and adds exact-baseline ratios. For example:

```sh
python3 workbench/spikes/hashtable-design/evidence/value-widths-native/qualify.py \
  workbench/spikes/hashtable-design/evidence/value-widths-zen5 /tmp/width-report
```

Each host retains byte-identical raw files and its artifact recovery reference,
plus clearly separate authored analysis and audit summaries. Full regeneration
commands are in its `context.json`.
