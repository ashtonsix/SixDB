# Domain-sized native cross-column controls

2026-10-23. Narrow native X removes the earlier isolated OS SUM advantage as
robust headroom. The OS grouped-query win survives the measured native controls;
Region and IMDb retain substantial native advantages. This is a separate source
and timing cohort from [the original u32 comparison](CROSS-COLUMNS.md), with no
pooled samples or changes to its retained results. No Frame interface or format
policy is selected.

## The comparison that was missing

The original native controls admitted X as u32. Actual values need fewer bits.
The earlier [point study](../point/FINDINGS.md) already demonstrated the importance
of a native global-FOR representation control. Here, preserve all nine original
programs and add native logical-X lanes and native global FOR, each with flat
and four-row coalescing grouped loops.

Global FOR means a per-part minimum plus the narrowest u8/u16/u32 residual lane,
matching the point study. It is **byte-aligned, not exact-bit packed**. Fit the
domain over live non-NULL X only. Missing/NULL fillers cannot widen it. C indices
remain part-local u8/u16. The actual fitted domains are in
[the domain inventory](evidence/native-domains-local/domains.csv).

| Input | Logical X lane | Global-FOR residual lane | FOR base |
| --- | ---: | ---: | ---: |
| OS / width | 16 bits | 16 bits | 0 |
| Region / timing | 16 bits | 16 bits | 1 |
| IMDb / year | 16 bits | 8 bits | 1948 or 1949 |
| Functional / decorrelated | 32 bits | 16 bits | 20000 |

The operations and logical transports are unchanged: selected nullable C/X,
`X >= threshold`, grouped COUNT/SUM into the maintained Ikea hash table, plus
X-only predicated SUM with unsigned zero identity. X-only SUM has no C predicate;
it is not full materialized projection or point access. Original coordinates,
coarse/fine selection, presence, independent validity, per-part dictionaries,
logical category regrouping and the separate NULL group all remain intact.

Explicit NEON compares 16 u8, eight u16 or four u32 values per vector. SUM widens
before accumulation and adds `selected_count * base`. When the predicate accepts
the whole residual domain, count the selected mask directly once per block.
Grouping retains scalar indexed category accumulation, four-row coalescing where
selected, and late logical-key hash insertion. Linked native entry points contain
no calls and thus no Frame decoder calls. These loops differ in shape/base
arithmetic from the earlier u32 loop; this is a complete-program comparison,
not an isolated causal measurement of lane width.

## Complete grouped queries

All following query numbers are median **ns per warmed complete query** on local
AArch64 OrbStack vCPU6, all-row selection and threshold zero. Each query creates,
consumes and retires its actual hash output. Preparation is outside these clocks.
These are neither dependent point latency nor interleaved reciprocal throughput,
and no cache-level, DRAM or Loom qualification follows. Read the calibration
limits below before using close rankings.

Native columns show the faster measured flat/coalesced alternative for that cell;
the domain column also takes the faster logical-lane/FOR alternative. This is
an observed comparison floor, not an implemented automatic selector. All programs
and selectivities remain in [the full query table](evidence/native-domains-local/queries.csv).

| Input | Independent encoded | Shared encoded | Native C-index/u32-X | Native domain X / FOR | Native shared codes |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS / width | 205,057 | 73,943 | 239,895 | 236,458 | 148,183 |
| Region / timing | 345,536 | 180,609 | 162,036 | 117,472 | 151,987 |
| IMDb / year | 19,166 | 9,020 | 12,424 | 15,662 | 4,147 |
| Functional | 379,368 | 225,669 | 298,454 | 308,284 | 77,948 |
| Decorrelated | 423,648 | 528,218 | 314,120 | 317,224 | 278,612 |

The new controls barely change OS's measured grouped floor: the best two-column
native control is about 236 us, while the existing shared-code native control is 148 us.
The encoded operation's run-aware work remains lower at 74 us. This supports
local grouped headroom against these controls, not a claim against every possible
native algorithm or another ISA. It does not establish that narrow grouped
kernels are exhausted.

Region benefits materially from native u16 consumption and still loses with the
shared encoded program. The functional case preserves its large gap to the native
shared-code histogram. Decorrelation still loses both runtime and memory. IMDb's
grouped timings have particularly poor duplicate calibration; their close ordering
cannot qualify a format or kernel choice.

## X-only predicated SUM

| Input | Independent encoded | Shared owner SUM | Native u32 X | Native logical lane | Native global FOR | Native shared map |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| OS / width | 32,603 | 27,451 | 52,634 | 25,552 | 25,878 | 186,116 |
| Region / timing | 82,500 | 105,925 | 101,360 | 61,628 | 59,125 | 17,318 |
| IMDb / year | 2,568 | 6,703 | 1,105 | 529 | 345 | 4,333 |
| Functional | 112,730 | 132,634 | 51,391 | 52,147 | 25,391 | 195,622 |
| Decorrelated | 123,425 | 198,146 | 53,186 | 53,187 | 27,282 | 209,021 |

OS's old isolated-SUM story changes: native u16 takes roughly 25.6–26.3 us versus
27.5 us for the shared owner SUM. Treat this as near parity, rather than retaining
the earlier native-u32 advantage as robust headroom. IMDb's native FOR SUM takes
about 345 ns, compared with 1105 ns for the earlier native-u32 path and 6703 ns for
the shared owner. The functional and decorrelated native-FOR scans roughly halve
the measured u32 cost. Strong native shared-map controls remain visible; Region's
sparse X demand particularly favours that path.

Narrower storage does not automatically improve grouped execution. Indexed
accumulation, masks, base reconstruction and loop shape remain consequential;
several new grouped paths lose to an existing native program. Retaining those
controls prevents a narrower but slower implementation from defining the baseline.

## Image capacity and preparation

Bytes include each image's C indices and X representation, vector/control capacity
and the native domain/base holders. They exclude common logical C keys and the
three common presence/validity planes, query state, stack, executable code,
allocator rounding and test oracles. This is **not RSS**. The common three-plane
harness remains a control, not a required Frame format. Existing shared forward
maps remain native u32; they are one tested layout, not a lower bound.

| Input | Independent encoded | Shared encoded | Native u32 | Native logical lanes | Native global FOR |
| --- | ---: | ---: | ---: | ---: | ---: |
| OS / width | 81,532 | 68,222 | 983,256 | 590,280 | 590,280 |
| Region / timing | 146,512 | 201,148 | 1,179,864 | 786,888 | 786,888 |
| IMDb / year | 8,420 | 7,280 | 20,624 | 12,592 | 8,496 |
| Functional | 616,056 | 215,976 | 983,256 | 983,496 | 590,280 |
| Decorrelated | 616,056 | 2,118,532 | 983,256 | 983,496 | 590,280 |

The native category array is physically reused in the harness but charged to each
standalone native image. Native X fitting/copying has a real construction cost.
The following are **single diagnostic component observations** for X alone;
category construction occurred separately. They are not full from-logical costs,
stable construction benchmarks or values to sum into a one-shot query claim.

| Input | Logical-X lane fitting/copy, ns | Global-FOR X fitting/copy, ns |
| --- | ---: | ---: |
| OS / width | 518,699 | 448,576 |
| Region / timing | 775,486 | 867,942 |
| IMDb / year | 24,957 | 11,750 |
| Functional | 1,022,940 | 398,993 |
| Decorrelated | 1,035,050 | 682,212 |

Common row-mask/category-flag preparation, existing dictionary preparation, owner
predicate-weight construction, first-call cache cost and extra capacity remain
in [the query table](evidence/native-domains-local/queries.csv). For OS, the shared
owner's X weights add 9468 B, with 20208 ns preparation and a 48332 ns first call
in these single observations. The native X paths need no query-specific weight
map. The common preparer also creates C flags for grouping; it is not a minimal
standalone X-only binder.

The [material inventory](evidence/native-domains-local/material.csv) and query
workspace fields keep each category separate. Their different component scopes
preclude inferring a full encoding or one-shot query winner. The 28 counterbalanced
[private C-edit repair records](evidence/native-domains-local/repairs.csv) remain:
all live C/X positions are checked, X is preserved, and old const images remain.
These are whole-part repair/retirement controls, not actual Orbital MVCC, mandatory
repair grains or measurements of X-domain growth/widening.

## Calibration, correctness and recovery

All 13 authored documents pass 3432 reported full-output comparisons, including
lane cutovers, high/nonzero bases, threshold boundaries, UINT32_MAX, NULLs, holes,
tails and empty X domains. Corpus and coupling cases pass another 624 comparisons
plus all live repair-position checks. Authored coupling partners preserve the
exact C sequence and each part's X multiset, independently asserted; input hashes
also match the prior retained originals.

Thirteen programs use 26 Williams rows, balancing all 156 ordered within-row
pairs twice, with equal batching per query/operation. The two-sample pilot and
first contended 26-row attempt remain separate. The latter had 3.86% median and
32.61% worst exact-duplicate discrepancies; its timings support no conclusions.
Root, Claude and HELPER then parked active local compilation/qualification jobs
for one complete quiet run and resumed after its completion.

The quiet run still has **2.32% median and 22.68% maximum** absolute median
difference for the exact same-function/input/workspace duplicate. The worst
case is IMDb all-row grouping. No further run was selected to remove that limit.
All-row per-input controls follow; all other controls and raw repetitions are
retained, including the extra native X same-path comparisons.

| Input, all rows | Exact duplicate GROUP delta | Exact duplicate X SUM delta |
| --- | ---: | ---: |
| OS / width | 1.57% | 2.52% |
| Region / timing | 4.71% | 0.47% |
| IMDb / year | 22.68% | 4.90% |
| Functional | 2.65% | 2.55% |
| Decorrelated | 9.67% | 5.23% |

Close rankings remain unqualified. Large gaps and exact memory inventories answer
the bounded comparison; the table does not establish general statistical precision
or fleet policy. Wider/native and FOR loops have different dispatch/loop shapes;
exact-bit FOR, vector grouped accumulation and additional native algorithms remain
outside this case. x86 fallback is scalar: there is no AVX2/AVX-512 result here.

The [qualification](evidence/native-domains-local/qualification.json) records the
actual binary, flags and source closure. All 1239 C++/header/CMake source files in
the actual build tree match the frozen capture. Ikea is byte-verified at
`2ce62539`, encoding at `3c3b5824`; live interface changes are excluded. Clang21.1.8,
Release/O3/native ISA/generic tuning and disabled consumer auto-vectorization match
the recorded commands. Reusing the earlier frozen-library incremental build
avoided repeating its unchanged width matrix; three new consumer TUs were compiled
and linked. No object from the abandoned cold build was used.

The [recovery instructions](evidence/native-domains-local/REPRODUCE.md) and
[artifact](evidence/native-domains-local/artifact.json) retain full frozen source,
ELF, raw quiet/rejected cohorts and linked disassembly. Existing logical inputs
are recovered by verified prior artifact/member references. The new target is
`frame_execution_columns_domains`; its frozen runner and offline report are
`columns_domains_run.py` and `columns_domains_report.py`.

For Frame's schema-fidelity question, the additional lesson is concrete: native
lane/domain choices belong among the useful representation alternatives, and an
isolated field can prefer a different program from grouped consumption of shared
codes. These results do not settle a universal schema or execution boundary.
