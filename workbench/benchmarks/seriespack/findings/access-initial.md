# SeriesPack access findings

The [coherent full-width checkpoint](coherent-20260910.md) reports the later
three-machine correctness and performance results. This note owns the initial
measurements and the hypotheses they opened.

The [admitted-region follow-up](../../../spikes/seriespack-range-execution/admission.md) records the subsequent V2
and GNR caller-fact comparisons and repaired 1 GiB dependent-read pilot, including
remaining losses and the small/large-footprint endpoint reversal.
The [scalar-callback follow-up](../../../spikes/ikea-composition/call-boundaries/decoder.md) records the integrated
trusted-boundary change, three-machine pairs and remaining individual losses.

The first hardware access matrix exposed large losses against the immediate
LocalPack/ScanPack predecessors. Correct wire geometry and competitive bulk
decoding did not make the arbitrary-range endpoint competitive. These are
**original checkpoint measurements**, before the subsequent point and range
changes; they do not describe the current working tree's performance.

The [measurement contract](../measurements.md) explains the providers, typed output,
query generation and static-region alternative. [Compact evidence](../evidence/resident-initial/capture.json)
identifies six captured runs, source files, binaries, ISA flags, cache geometry
and recoverable artifacts. [All 840 case records](../../../spikes/ikea-composition/archive/validation-20260911.md)
retain the three repetitions in ns/query, actual logical/encoded/query extents,
and matched control names. This includes the controls and both x86 endpoint
families, not just the examples discussed here.

## Initial scope and result

Each machine ran widths 1, 5, 7, 12, 31, 56 and 64 where each representation
exists, at requested encoded sizes 4 KiB and 128 MiB. This is representative
timing coverage, not completion of the k=1..64 access matrix. Construction and
value verification are outside timing; checks passed on all six captures.
Google Benchmark ran three sequential repetitions of at least 30 ms on CPU 0.

The following ratios compare the bound SeriesPack range reader with the direct
same-wire predecessor. Each call materializes 16 u64 values at an original index
divisible by 16. Values above one mean SeriesPack took longer. The x86 rows use
the AVX-512 endpoint in the full-feature profile; V2 uses NEON without SVE2.

| Layout | Zen 5, 4 KiB | GNR, 4 KiB | V2, 4 KiB | Zen 5, 128 MiB | GNR, 128 MiB | V2, 128 MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Local1 | 7.34× | 2.11× | 1.91× | 2.37× | 2.48× | 1.75× |
| Local5 | 6.34× | 2.25× | 1.21× | 2.55× | 5.09× | 1.40× |
| Local7 | 7.16× | 2.07× | 1.23× | 2.58× | 2.43× | 1.42× |
| Local56 | 6.30× | 2.10× | 1.89× | 2.31× | 2.31× | 1.79× |
| Striped1 | 10.35× | 2.68× | 4.69× | 10.36× | 4.38× | 3.31× |
| Striped5 | 4.15× | 2.40× | 3.68× | 3.27× | 3.54× | 3.18× |
| Striped7 | 3.04× | 2.67× | 4.80× | 3.88× | 3.78× | 3.18× |

For scale, Zen's Striped1 result is 16.76 ns/query at 4 KiB and 158.16 ns/query
at 128 MiB, against 1.62 and 15.27 ns for ScanPack. GNR's Local5 result grows
from 14.52 to 86.63 ns/query, against 6.46 and 17.03 ns for LocalPack. These
losses warrant physical-path investigation; neither authoring equivalence nor a
favorable average across widths accounts for them.

Point and dependent reads tell a different story. At 128 MiB, Local56 bound
point is 1.01× its predecessor on Zen, 1.04× on GNR and 1.25× on V2; dependent
access is respectively 1.03×, 0.97× and 0.96×. The latter includes shared index
generation and must not be interpreted as isolated decode latency. Range losses
cannot be inferred from the point result or vice versa.

A later 1 GiB pilot exposed a further limit of those dependent cases: their
deterministic next-index function can enter short cycles. For width 56, an
independent trace finds cycles of 99 or 559 positions despite the large encoded
allocation. These historical dependent timings therefore do **not** establish
cold access. The independent point/get16 query stream is separate. The repaired
dependent workload passes all-width coverage/replay checks and is integrated
into the recurring benchmark. The [later pilot](../../../spikes/seriespack-range-execution/admission.md) preserves
the data dependency and advances a Weyl nonce across batches; it still does not
establish per-query cache misses.

## Physical causes and response

Inspection found that short striped ranges rebuilt their residual bytes for
each output-sized group: two u64 values for NEON, four for AVX2 and eight for
AVX-512. The generic boundary machinery also retained metadata copies/reloads,
scalar-edge state and checks for later groups after a short range was complete.
Local narrow ranges missed the predecessor's exact adjacent-tile read. The
replacement under validation borrows admitted placement, assembles a useful
residual region once, joins body/head bytes in registers and widens at the final
store. It keeps arbitrary original ranges and exact boundaries; the caller does
not acquire a new get16-only API.

Regular striped point reads had another avoidable cost. For residual widths
1, 2 and 4, every group reads the same stripe and only its bit shift changes.
The old constant-group dispatch produced an indirect jump table for random
indices. Striped1 bound point at 4 KiB took 9.69 ns on Zen and 8.17 ns on GNR,
against about 1.38 and 1.37 ns for ScanPack. The physical reader now uses one
byte load and an index-derived shift; bodies and separately stored heads compose
as before. This recovers the predecessor's elementary access pattern.

The point change passes the independent wire and static-point checks. Focused
x86-64-v3 generated-code witnesses for all six regular striped payload widths
use scalar registers with no helper calls, branches or stack accesses; this is
an instruction inspection, not a speed measurement. An
OrbStack before/after screen supports the Striped1/2 hypothesis, but its regular
Striped4 bound case slowed about 15% while untouched controls also varied. That
screen is not a hardware acceptance result. The complete candidate needs a
matched run, including unchanged widths and both point strategies.

The irregular arithmetic/constant-offset choice remains separate. For example,
at 4 KiB on Zen, Striped7 static constant-offset access takes 6.86 ns versus
1.17 ns for the arithmetic variant; at 128 MiB the corresponding times are
15.73 and 19.37 ns. A single strategy does not win both contexts. Static regions
also change the call boundary and query-loop organization. Their recorded final
incomplete query-batch omission belongs in comparisons with bound point calls.

Reviewing the complete ScanPack reader also found that the arithmetic selector
had omitted its R3/R6 fragment classes. These now use the verified one-/two-byte
plans, with W14 body addresses supplied by SeriesPack's own stripe placement.
The independent raw-wire and protected-boundary checks now exercise both point
strategies. A local screen moves bound R3/R6/W14 from 6.56/4.82/7.48 to
2.51/3.38/3.84 ns/query. Unchanged constant-offset instruction bodies also move
16–31% in that screen, so those are diagnostic observations awaiting a coherent
hardware comparison. They do not establish the final gain or its cause across
all targets and resident sizes.

## What remains to establish

The 4 KiB runs carry 64 KiB of query indices and therefore do not establish that
all timed work resides in L1. The 128 MiB runs carry approximately 64 MiB of
query indices. GNR reports a 480 MiB LLC, so that allocation fits below the
reported LLC capacity. Calling this result DRAM-cold would be unsupported.
V2 reports 36 MiB LLC and this Zen instance reports 8 MiB; a working set larger
than those caches still does not prove that each measured read misses them.

The next coherent capture must measure the corrected public endpoints, retain
the direct predecessors, complete all supported unheaded k=1..64 point/get16
and dependent cases, and establish the intended cold condition from actual
encoded/query footprints and the access method. Headed and strided correctness
coverage is broader than this first timing workload. Separate
[composition and casing findings](../../../spikes/ikea-composition/native-regions/findings.md) address native consumer
grain, reduction frequency, effects and publication-facing wrappers.
