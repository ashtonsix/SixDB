# StreamPack caller performance controls

The original AVX-512 Direct ordinary projection loss is real in these warm
callers. A common function-alignment control removes the before/after gap by
**slowing the baseline**, not by improving the maintained binary. Keep the
default production flags. The evidence supports code-layout sensitivity; it
does not identify an instruction-cache, decoded-op-cache or branch-target cause.

[native.json](native.json) retains selected timings, all five paired ratios,
PMU availability, source and binary hashes, and recovery references.
[layout.json](layout.json) retains the complete ordinary-function comparison
and the default-versus-aligned text/padding inventory. Complete samples, linked
disassembly, ELFs, configured commands and captured source remain in the named
worker artifacts. No consumer or kernel was changed to produce the alignment
control.

## Three separate cohorts

| Cohort | Workload | Cases / measured PMU groups | Native workers |
| --- | --- | ---: | --- |
| Frozen R2 diagnostic | Original 108 records per invocation, including historical `shuffled` | 648 / 6,480 | `20261023T004329Z-92d309bc` NEON; `20261023T004329Z-decd9aa2` AVX2/AVX-512 |
| Corrected projection harness | Identical harness-only patch in both API arms; 132 records per invocation | 792 / 7,920 | `20261023T005357Z-3e4a6b58` NEON; `20261023T005357Z-144c5057` AVX2/AVX-512 |
| Frozen R2 alignment control | Original timed source, common additional `-falign-functions=64` | 216 / 2,160 | `20261023T010259Z-1bf3e14c` AVX-512 |

Each cohort uses the existing StreamPack benchmark, five alternating process
trials, both internal repeats, 256/65,536 rows, maximum 4,095 and one pinned
vCPU. A measurement performs 1,048,576 value visits. Release assertions remain
enabled; the configured StreamPack target disables both automatic vectorizers.
The runner builds only this benchmark and the existing PMU probe. Other Ikea
benchmark targets are not measured by this control.

The original R2 index `(i*65537u+17)&(rows-1)` is a rotation by 17 at both
retained population sizes. The maintained harness now names an explicit
`(i+17)&(rows-1)` traversal `rotated` and adds:

```cpp
(((i ^ (i >> 7)) * 0x9e3779b1u) + 17) & (rows - 1)
```

This is a deterministic permutation of power-of-two populations. Independent
full-population checks at both measured sizes prove every rank occurs once,
with no consecutive `+1` transitions. Cold checks in the maintained executable
also verify uniqueness and bounds. Index arithmetic stays inside the measured
loop; there is no extra permutation-array load. This is a warm permuted
traversal, not a cold-memory or stochastic random-access claim.

Independent checks sum the actual fixture's successor values. Separate full
simulations verify the dependent-chain terminal checksums, 0 for one chain and
476 for eight chains. Every collected value count/checksum matches. The source
dependency proof covers 44 literal project includes, including conditional
branches, and has no SeriesPack/TuplePack dependencies. The only projection
cohort change is its declared harness patch; current reader/kernel bytes match
the frozen after arm.

## Losses remain visible

The first diagnostic reproduces the original AVX-512 Direct ordinary rotation
loss: roughly 13–14% slower cycles/time at both populations and repeats. Retired
instructions remain equal, branch misses remain approximately equal, and the
65,536-row generic cache-miss count is lower after. This excludes an added
retired-work explanation for these callers without resolving their frontend
mechanism.

The separate projection cohort retains the same direction:

| AVX-512 Direct ordinary | Repeat 0 time change | Repeat 1 time change |
| --- | ---: | ---: |
| 256 rows, rotated | +11.86% | +11.89% |
| 256 rows, permuted | +7.82% | +13.35% |
| 65,536 rows, rotated | +11.09% | +11.19% |
| 65,536 rows, permuted | +0.43% | +0.25% |

Both small permuted cases lose more than 5% in every paired trial. Large
permuted ratios span about 0.94–1.05, so near-equal medians do not establish a
precise gain. Their generic misses are about 1.3 per visit, versus about 0.01
for rotated traversal; this unqualified event does not identify a cache level.
NEON's ordinary permuted medians are within 1.4%; AVX2's small case is nearly
equal and its large case improves about 2.4–3.4%.

The compact selection keeps **all ten original R2 case medians at least 5%
slower**, with their new-cohort correspondence, and **all fourteen** new-cohort
negative medians at that threshold. Seven of the new negatives lose at least
5% in all five pairs. The AVX2 Width/location8 large ordered negatives have
paired ratios spanning gains and losses; their median losses are retained
without calling them repeatable structural regressions. The new harness does
not silently replace the original R2 comparison.

## What the alignment control establishes

At 256 rows, ordinary Direct rotation changes from default R2
`2.780 → 3.165 ns/value` to aligned `3.167 → 3.164`. At 65,536 rows it changes
from `2.789 → 3.149` to `3.154 → 3.153`. Cycle counts show the same baseline
slowdown. The maintained after cost is effectively unchanged. No aligned case
median among the 216 controls loses 5%, but that is not a production speedup.
Default and aligned absolute values come from separate workers; each
before/after comparison uses matched frozen inputs and flags.

All 493 hot projection/chase functions in each aligned binary start at
address modulo 64 equal to zero. The ordinary projection remains 400 bytes;
its default start moves from modulo 64 of 32 to 48 across APIs, while both
aligned starts have modulo 64 of zero. Global alignment also moves callees,
so this does not isolate the projection's entry address alone.

The common flag adds 20,848 bytes before and 20,912 bytes after to `.text`,
entirely accounted for by additional NOP/inter-function gap bytes in this
inventory, about 1.6% text growth. Gaps use sized function symbols and can
include unsized code; NOP totals also include interior alignment.

All fourteen complete ordinary functions retain instruction counts under both
default and aligned builds. Twelve normalized operation sequences match;
the remaining two functions each have four different RIP-relative literal displacements.
Normalization discards linked branch/call addresses and RIP comments, while
retaining registers, memory operands and immediates. Exact linked disassembly
and all remaining differences are recoverable. Fetched default ELFs match
their original native receipt SHA256 values; local analysis uses pinned
LLVM 21.1.8 on CPU 8, with commands/output hashes in `layout.json`.

## Practical boundary and reproduction

The supported typed-checked route remains useful when a shared layout can be
bound once: AVX-512 permuted 256-row calls cost about 1.75 ns/value versus
3.43–3.61 for ordinary calls; at 65,536 rows they cost about 4.30 versus 6.26.
These are complete measured consumers, including typed rebinding and checks.
They do not measure one-shot tiny mixed-layout commands or their preparation
break-even. The next handle comparison must charge those costs and retain the
original command order, content admission, masks, tails and actual effects.

Use [cohesion-v2-stream-pmu.py](../../../cohesion-v2-stream-pmu.py) and its
shell entry point to rebuild captured before/after sources. Default execution
keeps configured flags; `--profile avx512 --function-alignment 64` selects the
separate control. `prepare --permuted` applies the same projection-only patch
to both frozen API arms; `prepare --frozen-r2` preserves the original harness.
Preparation requires the original paired source capture, rather than a
substitute from the latest checkout.

For exact recovery, extract a worker's `jobs[].artifact` object from
`native.json` to an ignored reference file, then use
`workbench/tools/artifacts.py fetch REF.json build/recovered/NAME`. The archive
contains `source.tar.gz`, source manifest, `receipt.json`, `paired-pmu.json`,
raw CSV/PMU trials and binaries/disassembly. Its `paired-pmu.json` hash is
recorded here. To repeat the compact selection, retain every case with
after/before median time at least 1.05, all Direct ordinary projection cases
and the Direct typed-checked permuted cases; keep all five paired ratios.

Required cycles/instructions have full coverage without multiplexing throughout.
NEON generic cache misses are unavailable (`EINVAL`), not zero. Counters cover
calling-thread user-space work including clock reads and are unscaled. The
unchanged workload requests the basic group; an available frontend-stall event
in the separate probe is not a per-consumer observation. ASLR is recorded as
enabled and unchanged. These limits are retained alongside the favorable and
unfavorable results.
