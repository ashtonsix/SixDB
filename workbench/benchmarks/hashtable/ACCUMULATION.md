# Maintained set/map results and accumulation qualification

The maintained core preserves the compact representation and selected native
consumers while adding caller-sized mutable accumulation. Native x86 has strong
independent-read and accumulation results. Construction, bare AVX2, selected
mixed RMW cases, and ARM hit-heavy reads and accumulation retain losses.
The tables below separate throughput, memory and accumulation preparation;
dependent serial latency is outside this run's scope.

These are the broad maintenance measurements, with the corrected RMW supplement.
They predate the October 20 eight-bit overflow tag promotion. Its
[separate Zen 5 qualification](../../spikes/hashtable-swiss/logical-groups/RESULTS.md)
checks that change against the seven-bit owner and actual Boost/Abseil controls;
it does not replace this multi-CPU accumulation and growth panel. The full
scorecards contain every cell and MAD:
[990 core cells](evidence/accumulation-promotion-core.csv),
[306 accumulation cells](evidence/accumulation-promotion-accumulation.csv), and
[126 historical control cells](evidence/accumulation-promotion-before.csv).
The [receipt](evidence/accumulation-promotion.json) identifies captured sources,
jobs, recovery archives and code-preservation checks. Use the
[suite guide](README.md) for timer boundaries, memory definitions and reproduction.

## Core throughput: absolute examples

Each cell is **Ikea / Abseil ns per query, entry or update** as labeled. Lower
is better. Native means AVX-512 on Zen 5 and Granite Rapids, NEON on Neoverse V2.
These examples are selected by operation, not as each CPU's best cases.
All use 85% nominal occupancy. Warm rows have b10 and zero conditioning;
the conditioned row has b18 and a 512 MiB conditioning scan.

| Consumer | Zen 5 | Granite Rapids | Neoverse V2 |
| --- | ---: | ---: | ---: |
| Warm K14 set, independent all-hit lookup · ns/query | 1.481 / 2.069 | 2.192 / 2.475 | 4.459 / 3.806 |
| Warm K14 map8, independent all-hit lookup · ns/query | 1.934 / 2.499 | 3.179 / 3.109 | 5.088 / 4.315 |
| Conditioned K30 map8, prefetch32 all-hit lookup · ns/query | 18.495 / 21.557 | 12.809 / 15.910 | 19.646 / 19.413 |
| Warm K30 map8, construction · ns/entry | 9.637 / 6.840 | 14.101 / 9.768 | 17.464 / 10.993 |
| Warm K30 map8, existing increment · ns/update | 2.790 / 2.989 | 3.925 / 4.147 | 6.136 / 5.023 |
| Warm K30 map8, increment/upsert with 50% new keys · ns/update | 16.203 / 12.504 | 18.976 / 18.423 | 19.747 / 28.059 |

The lookup rows are independent throughput, **not dependent serial latency**.
The conditioned row uses ordinary software prefetching, not Loom, and does not
prove every request came from DRAM. Mutation rows use the ordinary private owner;
they do not include MVCC preservation or multi-writer coordination.

`K` is quotient width, not key width: K14/b10 means W24 and K30/b10 means W40.
The large K30/b18 case has W48. Eight-byte values are consumed completely.
The core CSV also retains map64, misses, new-key endpoints, both writers, erase,
growth, AVX2 and all measured losing cells. The historical
[RMW report](RMW.md) adds a separately measured native-batch comparison.

## Memory: distinct from timing

These are **reported bytes** for the Zen 5 native all-hit read fixtures at the
same occupancy. The table states counters rather than RSS: Ikea includes buffer
capacity and its owner object; Abseil uses its allocated-table byte estimate.
See [memory accounting](README.md#memory-accounting) for exclusions.

| Fixture | Ikea bytes | Abseil bytes |
| --- | ---: | ---: |
| K14/b10 set | 69,752 | 163,840 |
| K14/b10 map8 | 340,088 | 425,936 |
| K30/b10 set | 71,800 | 147,432 |
| K30/b10 map8 | 211,064 | 278,456 |
| K30/b10 map64 | 1,185,912 | 1,195,624 |
| K30/b18 map8 | 54,263,928 | 71,303,096 |

The compact key representation matters much more for sets and narrow maps
than for map64. These counts do not include the lifetime of retained older
owners or the peak memory of an operation that keeps source and destination.

## Accumulation: preparation and fold are separate

`accumulate(destination, source, reducer)` operates over exclusive prepared
backing and preserves coherent sources. The caller chooses geometry, explicitly
copies or transfers an initial input, reserves Swiss capacity and publishes the
result. Initial destination and subsequent calls preserve per-key source order;
the reducer need not be associative. Failure/exception requires discarding the
destination. [Usage](../../../ikea/docs/hashtable/usage.md#mutable-accumulation)
and the [example](../../../ikea/examples/hashtable/accumulation.cpp) own the API.

The following fixed case uses K30/b10 map8 sources, eight inputs, 50% shared keys
and a b13 destination. Values use the ordered byte-fold reducer. Each control
prewalks its own incoming storage outside timing. Both have exact union sizing,
and Ikea's final spill bound is known before preparation. All values are median
**ns per incoming entry**, excluding source zero from the denominator.

| CPU, native ISA | Ikea prepare | Abseil prepare | Ikea fold | Abseil fold | Ikea total | Abseil total |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Zen 5 AVX-512 | 0.716 | 2.279 | 9.727 | 9.984 | 10.498 | 12.257 |
| Granite Rapids AVX-512 | 1.332 | 3.284 | 11.977 | 13.674 | 13.344 | 16.980 |
| Neoverse V2 NEON | 1.788 | 2.973 | 17.732 | 13.810 | 19.511 | 16.756 |

The total is the median of measured trial totals, not the sum of two medians.
The broad panel below compares **fold time only**. It is an unweighted summary
of cells, not a workload average or a preparation-inclusive speedup.

| CPU, native ISA | Basic faster cells | Median Ikea/Abseil fold time | Wide faster cells | Median Ikea/Abseil fold time |
| --- | ---: | ---: | ---: | ---: |
| Zen 5 AVX-512 | 53/54 | 0.679 | 40/48 | 0.734 |
| Granite Rapids AVX-512 | 54/54 | 0.661 | 41/48 | 0.789 |
| Neoverse V2 NEON | 25/54 | 1.042 | 21/48 | 1.092 |

Pinned Clang 21.1.8 and pinned Abseil 20260817.0; three seeds and four balanced
AB/BA repetitions. Basic covers K14/K30 sets/map8/map64 and two/eight sources
with 0/50/100% overlap. Wide covers K31/K47/K55 sets/map8. Small wide-key
destinations and ARM retain real losses. These results do not qualify copy/add
reducers, partial values, owning multimaps, K-way execution or unknown-size
online accumulation.

## Preservation against the earlier implementation

Before/after map8 accumulation uses an external SHA-verified source capture on
the same hosts. That old harness has four controls; the maintained one has two.
These are separate runs, not paired same-binary measurements.

| CPU | Basic median after/before, 18 cells | Wide median after/before, 24 cells |
| --- | ---: | ---: |
| Zen 5 | 0.997 | 0.986 |
| Granite Rapids | 1.002 | 0.995 |
| Neoverse V2 | 0.994 | 0.994 |

The largest slower cell is large K47 map8, two sources, 100% overlap: 11.8%
on ARM, 5.1% Zen and 5.6% Granite. Compared leaf instruction streams match;
differing harnesses/runs do not prove the cause of the timing difference.
The complete control scorecard retains that cell.

All 1,365 K-to-encoding/kernel bindings match the original compiled selector.
Assembly comparisons cover complete read, mixed/existing write, growth,
accumulation and focused RMW consumers on native x86/NEON and x86 AVX2.
Normalization removes addresses, RIP-relative constant addresses and NOPs;
it is not a semantic proof of constants or external callees. See the receipt
for source hashes and individual comparison artifacts.

## Harness correction and validation scope

The full campaign checks all maintained read/write/growth/RMW panels, 22 module
checks, 30 standalone headers and five ASan/UBSan check targets per CPU/ISA as
applicable. A benchmark-only repair restores the input mixer's inline body:
RMW creates initial payloads inside timing, so outlining it added calls per
value byte. The RMW supplement replaces those rows in the current scorecard.
The original raw rows remain recoverable, and the table implementation is
unchanged. For example, Zen K30/map8 existing RMW is 2.790 ns/update here,
versus the earlier 2.812; the mixed loss remains 16.203 versus Abseil's 12.504.

The final audit covers 97,992 raw rows including superseded RMW rows. Extended
assembly comparisons match all 21 selected consumers and compared outlined
bodies in each of five CPU/ISA configurations. This maintenance growth control
is Abseil copy/reserve; the [original qualification](QUALIFICATION.md) also
retains reserved insertion. Historical and corrected scores must be read with
their respective harness/source receipts.

## What was retained in production

The module has one maintained accumulation engine with shared physical codecs,
primary/Swiss resolution and geometry. It retains measured first-hit/full-mask
control flow and expression lifetimes where they affect code generation. The
construction profiles keep selected data separate from provenance. Maintained
benchmarks use actual pinned Abseil and compiled shared orchestration; old frozen
owners and launchers are [archived](archive.json).

The same [receipt](evidence/accumulation-promotion.json) records source footprint:
520,236 to 449,653 bytes across production, merge research, benchmark code and
tests/examples, excluding evidence/generated profiles. Whitespace-stripped bytes
fall from 418,825 to 316,755 while lines rise from 9,260 to 9,856. This is a source
maintenance comparison, not a runtime memory or complexity metric. Owning
multimap and K-way work remain outside the graduated container.
