# Packet transfer and composition measurements

The final captures are [Zen 5 AVX2/AVX-512](evidence/final-zen/artifact.json)
and [Neoverse V2 NEON](evidence/final-v2/artifact.json), using Clang 21.1.8,
Release, without LTO. The benchmark CPU is pinned; cases and three repetitions
run sequentially. All values below are median CPU nanoseconds per original row,
including inactive rows. These are warm repeated scans of 1,024 rows.

The crossover has 3,220 cases per profile, including 1,366 matched
packet/repeated-point pairs. The maintained module benchmark has 261 cases per
profile. All completed without benchmark errors. The Ikea implementation matches
the measured source; later study changes retire the duplicate prototype and
isolate incremental build probes between profiles. The density experiment below
was rejected; its altered placement predicate is absent from Ikea.

## Small tuples and physical demand

The same four selected bytes, spread across a tuple, give different transfer
choices as extent and stride change. These are eight-row sum operations:

| Tuple extent / stride | Zen AVX2 packet / point | Zen AVX-512 packet / point | V2 packet / point |
| --- | ---: | ---: | ---: |
| 6 / 6 bytes | 0.473 / 2.526 | 0.302 / 2.880 | 0.858 / 4.313 |
| 8 / 8 bytes | 0.378 / 2.524 | 0.393 / 2.889 | 0.669 / 4.316 |
| 12 / 12 bytes | 1.415 / 2.524 | 1.112 / 2.890 | 1.810 / 4.315 |
| 12 / 64 bytes | 1.433 / 2.540 | 1.115 / 2.900 | 1.838 / 4.330 |

At six/eight bytes the complete tight units fit one physical packet; at twelve
bytes this eight-row operation gathers selected bytes. The fast six-byte case
needs the complete-unit route: rounding each row's short transfer up to an
eight-byte register slot alone does not produce the same result. For sixteen
three-byte tuples drawing two spread bytes, the final tight route takes
0.235 / 0.172 / 0.432 ns per row on AVX2 / AVX-512 / NEON. The explicit window
competitor takes 0.197 / 0.171 / 0.432 respectively.

Small demand also matters inside large tuples. A four-row read/update of two
spread bytes in a 24-byte tuple with stride 25 previously selected the wide
point lowering. Limiting that fallback to larger physical demand gives:

| Profile | Before packet | Final packet | Final repeated point |
| --- | ---: | ---: | ---: |
| Zen AVX2 | 6.972 | 2.594 | 4.412 |
| Zen AVX-512 | 7.979 | 2.009 | 4.522 |
| V2 NEON | 11.108 | 4.095 | 7.167 |

The [before Zen](evidence/small-demand-zen/cases.csv) and
[before V2](evidence/small-demand-v2/cases.csv) selections keep the surrounding
two/four/eight-byte demands and strides, including point controls. The final
[Zen](evidence/final-zen/cases.csv) and [V2](evidence/final-v2/cases.csv) selections
also retain the small-tuple and two-row families.

## Explicit residual primitive gaps

Each profile has 360 two-row pairs and 1,006 pairs across four/eight/sixteen/64
rows. A ratio above one means the packet is slower than repeated prepared point
operations. The point control uses the runtime scalar kernel for projections of
up to eight bytes and the native point kernel for larger projections.

| Profile | Two-row pairs above 1.4× | Worst two-row ratio | Four-or-more-row pairs above 1.4× |
| --- | ---: | ---: | ---: |
| Zen AVX2 | 12 / 360 | 1.50× | 0 / 1,006 |
| Zen AVX-512 | 0 / 360 | 0.90× | 0 / 1,006 |
| V2 NEON | 34 / 360 | 1.67× | 0 / 1,006 |

All exceptions are read/update operations in very lightly populated two-row
packets. AVX2 has six one-code and six two-code cases; V2 has 31 one-code and
three two-code cases. Complete case names, timings and counts by shape are in
[Zen coverage](evidence/final-zen/coverage.json) and
[V2 coverage](evidence/final-v2/coverage.json). The matrix has no application
frequency weights; these counts describe coverage, not production prevalence.

Two contributing costs remain: widening tiny projections into two 32-byte
decoded slots, and choosing a compact physical window that can include unused
bytes between two selected codes. The worst V2 case spends 5.562 ns versus
3.333 ns per row; the worst AVX2 case spends 3.509 versus 2.343. This is a
deliberate limit of the curated transfer choices. Tiny transactional accesses
can use the scalar point operation; scans of small projections can choose a
denser packet shape, independently of the physical tuple size. Further transfer
selection research should use representative consumers before adding policies.

## Rejected density threshold

A follow-up tested keeping a compact physical window only when its hull was at
most eight bytes or at least half its bytes were selected. Otherwise it assembled
only the selected bytes. This changed cold preparation, with no new kernel bodies.
The targeted sweep covers two/four-row packets, 12/16/24/32-byte tuples,
two/four/eight-byte demands, compact/spread maps, all three strides, and both
consumers: 708 cases and 288 packet/point pairs per profile.

| Profile | Packet cases >15% faster than retained | >15% slower | Worst slowdown |
| --- | ---: | ---: | ---: |
| Zen AVX2 | 15 / 288 | 38 / 288 | 2.45× |
| Zen AVX-512 | 22 / 288 | 49 / 288 | 2.27× |
| V2 NEON | 31 / 288 | 22 / 288 | 2.22× |

The two-row, 24-byte tuple selecting two spread bytes at stride 25 improves on
all profiles: update costs fall from 6.651 / 4.942 / 11.472 to
4.334 / 3.340 / 6.280 ns per row on AVX2 / AVX-512 / NEON. However, four rows
selecting four spread bytes from a 16-byte tuple lose cheap vector transfers.
At stride 17, reads rise from 0.745 / 0.666 / 1.287 to
1.822 / 1.306 / 2.611 ns. The rule also abandons useful complete-unit routes for
some twelve-byte tuples. Fewer loaded bytes are not a sufficient cost model.

The rule is rejected rather than supplemented with per-case exceptions.
[Zen](evidence/density-zen/cases.csv) and [V2](evidence/density-v2/cases.csv)
retain both implementations and every competitor/repetition across this targeted
matrix. Their `artifact.json` recovers the density experiment;
`baseline-artifact.json` recovers the retained implementation. These captures
also rerun the 261-case maintained suite and checks. They do not replace the
full-matrix coverage above.

## Ordinary operations and CPS

The maintained update benchmark uses the same decode/update functions for inline
and CPS execution. Checked modes validate widths/ranges, reserve journal capacity
and report byte effects. The body column omits those obligations.

| Operation / profile | Body | Checked inline | Checked CPS |
| --- | ---: | ---: | ---: |
| Four spread rows, AVX2 | 12.268 | 18.444 | 19.262 |
| Four spread rows, AVX-512 | 4.178 | 9.877 | 11.067 |
| Four spread rows, V2 | 19.755 | 30.734 | 35.687 |
| 64 one-byte rows, AVX2 | 0.073 | 0.112 | 0.198 |
| 64 one-byte rows, AVX-512 | 0.078 | 0.107 | 0.195 |
| 64 one-byte rows, V2 | 0.168 | 0.230 | 0.374 |
| Alternating 64 one-byte rows, AVX2 | 0.600 | 1.477 | 1.765 |
| Alternating 64 one-byte rows, AVX-512 | 0.082 | 0.946 | 1.208 |
| Alternating 64 one-byte rows, V2 | 1.084 | 2.424 | 3.210 |

The alternating one-byte case demonstrates why payload throughput alone is
insufficient: the AVX-512 body uses masked transfers, while the operation must
describe 32 disjoint written spans. Its checked/body ratio is large even though
the checked operation remains below one nanosecond per original row. The active
row mask and required effects are explicit in both execution modes.

CPS still has a meaningful fixed call cost. For the tight 64-row packet,
checked CPS adds about 5.5 ns per packet on Zen and 9.2 ns on V2. The separately
retained eight-row one-byte case exposes a less amortized operating point.
These measurements justify useful packet-sized stages; they do not imply that
splitting every tiny expression into a continuation is inexpensive. The shared
stage bodies preserve the choice of inline or CPS execution.

## Compilation and validation

The first monolithic benchmark TU exceeded the 2 GiB V2 worker's capacity:
2m09.5s elapsed and 1,522,500 KiB peak compiler RSS before failure. Its
[failed capture](evidence/compile-before-v2/artifact.json) is retained. Grouping
the seven shape instantiations into three benchmark TUs and removing impossible
small-grain tail alternatives keeps the final build within that worker:

| Profile | Selected target build | Peak compiler RSS | Packet TU text | Full benchmark text |
| --- | ---: | ---: | ---: | ---: |
| Zen AVX2 | 83.52 s | 1,115,816 KiB | 184,286 B | 1,246,882 B |
| Zen AVX-512 | 79.99 s | 1,057,240 KiB | 181,667 B | 1,217,254 B |
| V2 NEON | 129.16 s | 1,225,588 KiB | 151,576 B | 1,065,410 B |

These are the runner's selected module/test/example/benchmark builds, excluding
toolchain/bootstrap work. The broad benchmark includes both execution modes and
Google Benchmark; its text size is not an inline-versus-CPS size comparison.
Selected raw resource and size witnesses are retained for
[Zen](evidence/final-zen/compilation.json) and [V2](evidence/final-v2/compilation.json).
Before the tail simplification the V2 packet TU alone occupied 263,216 bytes.
The final small-demand paths add useful specialization while remaining below
that earlier footprint.

Preparation-only/library-kernel-header/caller-example edits take 0.29 / 6.98 /
2.22 seconds on Zen AVX2 and 0.45 / 11.63 / 3.89 seconds on V2. Their resource
files and Ninja logs are in the final bundles. The captured AVX-512
preparation-only result included pending changes from the preceding profile and
is excluded. The runner now brings each profile current before its edit probes.
The density experiment verifies that correction: preparation/header/caller edits
take 0.28 / 6.71 / 2.06 seconds on AVX2, 0.29 / 6.45 / 2.28 on AVX-512 and
0.45 / 11.47 / 3.84 on NEON. These measurements use the altered cold predicate;
kernel bodies and dependency structure are unchanged. The selected resource
files and exact rebuild target lists are retained for
[Zen](evidence/density-zen/compilation.json) and
[V2](evidence/density-v2/compilation.json).

The final snapshot passes independent wire checks (5,301,385), operation and
ownership checks (43,137 ownership cases), all seven packet shapes, native nested
maintenance and the packet example on all three profiles. Baseline x86-64 packet
and operation checks also pass. Local `ikea_validate`, ASan/UBSan packet checks
and the packet example pass. Standalone public/author-header compilation passed
during the same implementation pass. The maintained tests cover sparse/tail
access, byte effects, late value/capacity rejection, substitution and original-row
coordinates independently of the benchmark's transfer fixtures.
