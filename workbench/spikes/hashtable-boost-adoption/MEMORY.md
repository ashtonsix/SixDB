# Equal-budget whole-table comparison

Ashton's question is whether Boost's earlier throughput lead survives when
Ikea and Abseil can spend a comparable amount of useful table memory. This
compares maintained Ikea, Boost 1.92.0 and the pinned Abseil on native Zen 5
AVX-512. It does not change any production container or introduce MVCC overhead.

The [full scorecard](evidence/memory/scorecard.md) includes actual memory,
hot/cold hit/mixed/miss reads, writes, map replacement, post-churn reads and the
native/expanded/over-budget capacity frontier. Throughput is in million
operations/s; the CSVs retain ns/op and median absolute deviations.

## What the comparison establishes

**The memory difference was material.** Boost does not retain a general read
lead when the other tables can spend comparable useful memory. Ikea's hot W32
set/map8 all-hit reads reach 1,035/706 M/s, versus Boost's 711/552 and Abseil's
794/576. Cold W40 sets reach 190/123/188 M/s for hit/50%-miss/miss, versus
Boost's 45/44/71 and Abseil's 78/65/89, using the existing home-prefetch APIs.
That cold comparison spends 256/272/288 MiB respectively.

**It does not establish a general Ikea win either.** Every hot 50%-miss fixture
still favors both peers. Hot W40 sets/maps lose materially even with the larger
primary allocation. Cold W50 map8 hits and mixed reads also trail: 31/36 M/s
versus Boost's 35/38 and Abseil's 46/50. Cold misses and sets are much stronger.
Direct calls and caller-prefetched calls have different rankings; the complete
direct table is retained beside the scheduled results.

**Spending more on Ikea is not monotonically beneficial.** For example, cold
W40 sets go from 64.09 MiB and 212 M/s hit reads to 256 MiB and 190 M/s. Hot
W40 map8 hit reads fall from 400 to 325 M/s when increasing from 206 to 384 KiB.
The bucket exponent changes quotient width, tile/slot geometry and the chosen
encoding; allocating more primary buckets is not equivalent to lowering the
load of an otherwise identical flat array. Those are observations, not an
instruction to change the production geometry selector.

Plain writes retain substantial losses. The expanded W40 cold set/map builds
reach 12.2/11.3 M/s versus Boost's 38.8/31.5; erase/refill pairs reach 4.3/4.2 M/s
versus 10.9/9.5. Hot W32 churn is much closer, at 158/141 M pairs/s versus
Boost's 165/147, and ahead of Abseil's 69/70. These are ordinary operations,
with neither table performing MVCC, locks, preservation or damage publication.

**Existing home prefetch hides much of the cold-write deficit.** In the paired
follow-up, W40 map8 construction improves from 11.5 to 44.1 M/s; Boost reaches
28.9 and prefetched Abseil 37.6. Known-existing map replacement reaches 91.4
M/s versus 29.5/43.1. This uses a caller batch loop with 32 future keys visible,
not a new container algorithm or an automatic improvement to isolated point
calls. Peers receive the same opportunity through their available public API;
Boost has no public home-prefetch hook. Adding one to Boost remains unmeasured.

Complete maintenance does not disappear. W40 set erase/refill reaches 9.9 M
pairs/s versus Boost's 10.9 and prefetched Abseil's 10.5; W40 map8 is essentially
level at 9.5/9.5/10.1. W50 sets/maps reach 14.2/12.9 versus Boost's 10.9/9.4
and Abseil's 10.6/10.0. Including the half-erasure before 50%-new upserts leaves
Ikea level with Boost on W40 sets and ahead on the other three cold fixtures.
The raw non-prefetched controls stay in the scorecard. This is a scheduling
benefit supported by paired measurements, not evidence that the scalar gaps
are harmless in every consumer.

## Work held fixed

| Fixture | Key domain W | Distinct entries | Values |
| --- | ---: | ---: | --- |
| Hot W24 | 24 bits | 27,852 | Set / eight-byte map |
| Hot W32 | 32 bits | 13,926 | Set / eight-byte map |
| Hot W40 | 40 bits | 13,926 | Set / eight-byte map |
| Cold W40 | 40 bits | 14,260,633 | Set / eight-byte map |
| Cold W50 | 50 bits | 14,260,633 | Set / eight-byte map |

Every arm of a fixture has identical key/value inputs, seed, query stream,
cardinality and mutation trace. W24/W32 peers use native 32-bit keys; W40/W50
use 64-bit keys. Eight-byte values are byte arrays on all three implementations,
as in the preceding controls, and are completely consumed. W24 hot and W40 hot
retain the earlier K14/K30 native geometry; cold W50 retains the earlier K30
large fixture. W32 hot and W40 cold add useful nearby capacity alignments.

Ikea changes its bucket exponent and therefore quotient width when spending
more memory. Its maintained encoding selector follows that geometry. Peers use
their native reserve/capacity policies. The **cardinality and key domain never
change between arms**. The original native-sized owners remain controls.

## Budget and accounting

The capacity census obtains actual allocations from the unchanged container
interfaces. Boost's minimum native allocation is the initial reference. Abseil
may reserve additional slots, giving a nearby peer allocation usually about 6%
larger. The common usable ceiling is the larger of those chosen peer allocations.
Ikea gets its largest tested useful primary geometry below that ceiling.

Native capacities are discrete. Where Ikea leaves substantial unused budget,
the next geometry above the Boost reference is retained too. The raw arm names
`budget_lower`/`budget_upper` refer to that reference, not the common ceiling:
owner-header bytes alone can put an otherwise matching allocation in the upper
arm. Actual bytes determine admission to the common ceiling. A geometry above
that ceiling is an **over-budget reference**, not included in a within-budget
win claim. No arm gets artificial padding,
different entry counts, changed keys, packed peer key types or modified growth
policies to manufacture equality. Retained-byte ratios remain visible alongside
throughput; this is useful-capacity matching rather than exact byte equality.
The remaining Ikea/Boost difference is as large as 25% on W40 map8 (384 versus
512 KiB hot, 384 versus 512 MiB cold W50). The next Ikea geometry is outside the
common ceiling and is shown separately. In contrast, cold W40 map8 has almost
exactly 512 MiB on both, and hot W32 map8 is 384/392 KiB. Claims should use the
individual rows, not call every fixture byte-identical.
The raw plan's `budget` field names the Boost reference; `memory.csv` separately
records the common `ceiling`. Native and expanded Ikea results are both retained:
spending more on primary geometry can hurt cold locality, so the study does not
assume the largest owner is fastest.

Reported table bytes include requested owned backing and the concrete owner
object. They exclude malloc bookkeeping, code/shared libraries, common input
arrays and verification storage; they are not RSS. All arms use allocation
counters. Ikea's benchmark storage derives its ordinary heap buffer without
adding fields, counts construction/destruction only, and checks its byte total
against `bytes_used()`. Point operations gain no counter or MVCC branch.

Every phase also records maximum simultaneous backing, including old and new
rehash allocations. A write that expands or temporarily exceeds the starting
budget is reported with that consequence. The summary amortizes all seven pure
erase/refill rounds instead of selecting the cheap rounds between maintenance.

## Measured operations

- Independent reads: all hits, 50% misses and all misses, before and after churn;
  all returned keys and value bytes contribute to checked results.
- Construction: allocation/reservation plus ordinary insertion of precomputed
  distinct inputs.
- Replacement through the ordinary mixed operation and through known-existing
  operations. Peers use `find` plus direct mapped replacement for the latter.
- 50%-new upserts at constant live population. The required half-erasure phase
  is measured separately; it is excluded from the labeled upsert time.
- Erase/refill pairs, including required maintenance, plus their separate phases.

Hot means no cache conditioning and repeated independent requests; it does not
promise L1 residence. Cold uses the large population and a 256 MiB conditioning
scan before each batch. Both direct and caller-prefetched reads are retained.
Ikea and Abseil expose a home prefetch and use distance 32; ordinary Boost has
only its internal candidate prefetch. That scheduling difference is explicit,
and direct results remain available for all three. No new Loom or dependent
serial-latency claim is made.

Three seeds run five fresh-process trials per hot arm and two per cold arm.
Each read mode gets three one-million-request repetitions per process. Arm
order rotates and reverses across trials. The executable file is hashed before
every execution, giving every arm a resident executable-file-cache policy.
Samples within a process are not independent process trials.

The driver verifies complete membership/payload state, insertion/erasure counts
and each timed result outside service timing. The offline audit additionally
requires identical read answers across all arms and trials of each seed, including
the final state after the common write trace. Native ASan/UBSan qualification
precedes release measurements. The source is adapted from the archived validated
Swiss whole-table driver; no research overflow implementation is substituted.

The first sweep contains 636 fresh processes and 32,928 rows, with 180 exact
cross-arm read-result identities. Its 24 native sanitizer qualification processes
cover three seeds, sets/map8, two Ikea geometries and actual peers. Median
absolute deviations across the retained result families are generally below
1.1% of their medians; the maximum is 4.8%. These are repeated measurements on
one native machine, not estimates of cross-machine variation. A representative
W40/b10/map8 assembly check has identical 129-instruction complete read loops
with ordinary versus counted heap storage; ordinary point insertion also matches
apart from symbol/address spelling, including the same unwind relocation.

The bounded write follow-up runs 120 fresh processes on the same worker: three
seeds, two trials, four cold fixtures and five arms (Ikea/Abseil direct and PF32,
plus native Boost). It changes only the benchmark caller loop, not container
code, and verifies 36 post-churn answer identities and eight binary hashes.
Disabling its initial read sweep also changes the common RNG position used to
shuffle write ranks, so compare its distance 0/32 pair within that cohort; do
not attribute all cross-cohort absolute differences to prefetch. Eighteen local
ASan/UBSan processes qualify the new loop with both distances and actual peers,
including all seven refill rounds; stderr is empty. The first sweep verifies
33 native executable identities. Production Ikea headers/sources are identical
across both captures, and the retained/peak memory consequences stay comparable.

## Reproduction and recovery

The runs use AMD EPYC 9R45 (Zen 5), a `c8a.2xlarge` with eight physical CPUs,
no SMT and 16 GiB, pinned to one CPU. Clang 21.1.8 builds with `-O3 -g0 -DNDEBUG`,
`-march=znver5 -mtune=znver5`, the existing inline threshold 1,000 and function
alignment 64, without LTO. Boost and Abseil retain their actual default hashers.
Boost's exact release/archive pin is in `CMakeLists.txt`; Abseil uses the shared
[pinned provider](../../benchmarks/hashtable/abseil.cmake). Ikea's maintained
architecture default, primary path, encoding selector and 87.5% Swiss ceiling
are unchanged. The benchmark's legacy `SWISS_GROUP_BYTES=16` macro only reports
logical Swiss capacity; it does not override the maintained probe width.

The [evidence](evidence/memory/provenance.json) selects summaries, all capacity
arms, audits and execution identities. Its recovery bundle contains both full
raw cohorts, native binaries, compile commands, exact source snapshots, sanitizer
receipts and the report scripts. Main sweep worker:
`20261020T224905Z-52f4e5b7`. The current driver adds the separately paired write
prefetch option. Follow-up worker: `20261020T231027Z-c24a999b`. Recover the main
sweep's captured source for exact binary reproduction of that earlier cohort.

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-boost-adoption/evidence/memory \
  build/reports/hashtable-memory
```

For new native measurements, run `native-memory.sh` with the normal worker
environment, then the bounded `native-write-prefetch.sh`. Summarization and
report generation are offline; they do not run new measurements. The original
native allocations remain useful choices, not failed candidates to discard.

## Strategic review

Workbench and Architecture both recommend making pinned upstream libraries
ordinary foundations while retaining custom components where representation,
execution or a necessary contract provides a concrete benefit. Both support
doing this comparison before selecting a container. Keep upstream utilities and
tests initially, centralize the dependency pin, and keep any patch set small and
visible. Ikea should own integrated container adapters; logical identity,
isolation, retained versions and publication remain with the enclosing caller.

They agree that allocator hooks alone cannot provide complete structural byte
preservation, and lifetime retention alone does not freeze contents. Ordinary
private/exclusive access, guarded edits and staged execution can stay separate,
so support for the stronger contracts need not tax every ordinary lookup. These
are strategic judgments; the present comparison measures plain operations and
does not qualify a Boost integration fork or choose a consumer representation.

My recommendation from the combined evidence is to keep Ikea where its compact
representation, scheduled cold execution or same-seed bulk operations pay, and
to treat a vendored upstream table as a credible ordinary mutable alternative.
The memory-matched evidence does not support wholesale replacement of Ikea by
Boost. It also does not justify requiring Ikea for a hot general-purpose table
that performs materially better with a peer. Keep the caller's semantic contract
separate from the selected physical container. A Boost home/staged hook or
guarded mutation patch would need its own integration measurement; today's
native Boost numbers are not that result. Growth/accumulation are existing
separate evidence, not newly measured by this study.
