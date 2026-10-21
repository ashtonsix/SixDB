# The hot 50/50 hit/miss cliff

**Random membership is causing a branch-prediction throughput cliff inside
lookup.** Keeping the same query multiset and changing only hit/miss order
removes most of the loss. PMU counts change from roughly half a branch miss per
lookup to nearly zero, while retired instruction work stays almost unchanged.
Membership-only lookup retains the cliff, so full-value consumption is not its
main cause. This is an internal mixed-path limitation, not a requirement that
callers predict which keys will hit.

[Complete tables and counters](evidence/hit-mix/tables.md) retain all actual
Ikea/Boost/Abseil controls, orderings, sinks and endpoint baselines.

## Measured result

Full consumer, native Zen 5 AVX-512. Each row uses the same keys and the same
50/50 mix; only ordering changes. Times are ns/lookup, lower is better.

| Ikea surface | Random | Alternating | Random branch misses/op | Alternating branch misses/op |
| --- | ---: | ---: | ---: | ---: |
| W32 set | 6.933 | 1.023 | 0.5013 | 0.0003 |
| W32 map8 | 7.114 | 1.288 | 0.5012 | 0.0003 |
| W40 set | 8.797 | 2.251 | 0.5003 | 0.00002 |
| W40 map8 | 9.156 | 2.996 | 0.5013 | 0.0007 |

Grouped and 64-hit/64-miss runs recover similar throughput. The pattern itself
is therefore a material workload property; final selectivity alone does not
predict cost. These orderings are diagnostic controls, not a suggested query
rewrite based on unavailable membership information.

Boost and Abseil have the same broad pathology. For W32 sets, random ordering
costs 6.145/6.423 ns, versus 1.313/1.390 alternating; their random streams have
about 0.53 branch misses per operation. Ikea has **fewer**, not more, branch
misses in these cells, yet loses the random case. W40 also retains a predictable
base-cost gap. Removing value and hit-key summation does not fix Ikea: random
membership-only times are 6.926/7.033/8.668/8.991 ns in the four rows above.

The W32 set's random/alternating retired instruction counts are both 26.52/op;
observed cycles are 31.09 versus 4.58. W40 set counts are both 58.50 instructions,
but 39.47 versus 10.09 cycles. Do not call the divided cycle loss a raw CPU
branch-recovery latency: it includes the complete lookup's dependency chain,
speculation and available independent work.

## What the assembly says

The native W32/set full-consumer loop has one product, address extraction,
primary loads, packed-key shuffles/comparisons, then:

```asm
kortestb %k0, %k1
jne      primary_hit
cmpb     $0, overflow_byte
je       miss
call     read_overflow
```

The primary match branch also gates the hit count and hit-key reduction; there
is no separate unpredictable key-summing branch in this loop. Membership-only
assembly retains the primary-match branch. The W40 exact comparison needs more
work before that decision. A later resolving branch plausibly restricts useful
lookahead and increases the effective throughput loss, but aggregate PMU counts
do not isolate that contribution from all other speculation effects.

W40/set has zero real spills in every measured process. That excludes required
Swiss work as the explanation for its cliff. It **does not exclude wrong-path
Swiss probes** following a speculative overflow-byte decision. The separate
paired barrier experiment below tests that hypothesis instead of inferring
the answer from the zero-spill count.

## Are speculative Swiss probes particularly harmful here?

**Blocking speculative Swiss loads does not recover this cliff.** In a paired
cohort, a research header overlay adds LFENCE at `read_overflow` entry, before
the Swiss probe. Full-consumer random 50% times are:

| Surface | Baseline ns | LFENCE ns |
| --- | ---: | ---: |
| W32 set | 6.918 | 6.921 |
| W32 map8 | 7.115 | 7.114 |
| W40 set | 8.791 | 8.794 |
| W40 map8 | 9.147 | 9.144 |

The W40/set overlay also uses fully serializing CPUID before probing: 8.793 ns,
against the same 8.791 baseline. Every such process has zero real spills and
therefore no required helper call, avoiding CPUID's large retired execution
cost. Membership-only and predictable-order controls retain the same result.
Branch-miss counts and retired instruction work are likewise essentially
unchanged. No fence belongs in the maintained implementation on this evidence.

This is 324 fresh processes and 972 complete, unscaled PMU intervals with 100%
coverage, three seeds and two trials. Nine executable hashes and 24 cross-variant
answer identities pass. Assembly verifies both barriers are present and all ten
baseline/variant direct-loop pairs retain the same instruction sequence and
register allocation after normalizing relocation addresses and padding. Only
the helper body changes. [Paired tables](evidence/overflow-speculation/tables.md)
and [raw recovery](evidence/overflow-speculation/artifact.json) retain this cohort
independently of the earlier order experiment. Its accepted worker is
`20261021T000018Z-d2e3872c`; the first attempt stopped before timing on a
diagnostic local-variable/template-parameter naming conflict and is excluded.

The helper-entry barrier still permits speculative call/prologue/front-end work
before entry; it blocks the subsequent Swiss probe, not every wrong-path effect.
The result rules out speculative Swiss memory accesses as the dominant cause
of this measured cliff. It does not price overflow-heavy or cold/skewed tables.
The primary-match decision remains the useful optimization target.

## Scope and verification

There are 864 fresh processes: four surfaces, three actual libraries, three
seeds, two trials, full and membership-only consumers, and six order/mix controls.
Each process measures three one-million-request intervals. All 2,592 per-loop
PMU groups completed at 100% running coverage, without scaling or multiplexing.
They count only the calling thread's user-space loop, excluding key/query
generation, table construction, validation and output. The required cycle and
instruction events share their group with retired branches and branch misses.

The table population is 13,926 throughout. Ikea uses the maintained measured
profile: W32 b11/E24 for sets/maps, W40 set b12/E29 and map8 b11/E29. These are
the equal-budget hot geometries from [the memory comparison](MEMORY.md).
Queries are generated once per process; reordering partitions them into known
hit/miss classes and retains every key and within-class order. All three
libraries receive identical logical inputs. All-hit/all-miss controls stay in
the retained tables. No prefetch, Loom scheduling, MVCC or write work is timed.

Every process verifies membership and complete payload answers outside timing.
The offline audit verifies eight executable hashes and 72 exact answer
identities across all orderings and implementations. The caller diagnostics
also passed 216 local ASan/UBSan processes over three seeds, all orders/mixes and
both sinks. Six additional local sanitizer processes exercise PMU JSON output;
local counter unavailability remains explicit rather than becoming zero counts.

The accepted worker is `20261020T234718Z-8430ef56`, AMD EPYC 9R45,
`c8a.2xlarge`, Clang 21.1.8, `-O3 -g0 -DNDEBUG -march=znver5 -mtune=znver5`,
inline threshold 1,000, function alignment 64, without LTO. Its
[recovery reference](evidence/hit-mix/artifact.json) contains raw timings,
counters, binaries, source capture, compiler commands and assembly. An earlier
attempt (`20261020T234322Z-20443732`) stopped on the first process because its
counter-output wrapper mishandled the PMU helper's newline. It is excluded.

Rebuild the retained tables without new timing:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-boost-adoption/evidence/hit-mix \
  build/reports/hashtable-hit-mix
```

For new measurements, `native-hit-mix.sh` uses the shared Linux PMU helper.
The driver options are read-only diagnostics and require `--writes 0`.
The extra sink/order instantiations can change generated code and layout, so
these paired results do not silently replace the earlier cohort's exact timings.

## Consequence for consumers

Population-aware sizing, a compatible measured encoding, native batch binding
and future-home prefetch make the intended choices easier to use. They do not
remove this warm mixed-hit branch issue. Do not document random membership as
caller misuse or recommend sorting by unknown hit status. Any proposed kernel
change must retain predictable-path throughput and actual overflow correctness.
No lookup kernel or default is changed by this diagnostic.
