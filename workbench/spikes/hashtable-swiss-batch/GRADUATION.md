# ISA-selected ordinary overflow reads

The maintained reader compares **64 control bytes with AVX-512BW, 32 with AVX2,
and 16 otherwise**, retaining 16-byte logical groups and the 87.5% ceiling.
Small stores with fewer groups than a batch use the original scalar reader.
Selection follows the compiled ISA; there is no new caller option, runtime
strategy branch on the primary path, descriptor field or representation change.

This supersedes the initial screen's recommendation to retain scalar probing.
The default prioritises misses after routing through a primary miss and a
nonzero overflow indicator. It accepts the measured hit/mixed-read tradeoffs;
it does not introduce a set of workload-specific reader choices.


## Native whole-table results

Independent ns/lookup, lower is better. Each cell is **hit / 50% miss / all miss**.
Hot uses direct lookup; cold uses software prefetch distance 32 for Ikea/Abseil.
Boost uses its native lookup. Literal is the earlier read-only packet, 64 bytes
on AVX-512 and 32 bytes on AVX2. All Ikea arms retain identical physical state.
“Maintained” is the promoted production Ikea implementation; Scalar16 and
Literal are earlier Ikea reader controls, not external containers.
“85%” is primary-capacity occupancy (about 60–62% Swiss); “dense” fixes Swiss at
86.9141%, below the unchanged 87.5% ceiling.

| ISA | Working set | Occupancy | Scalar16 | Literal batch | Maintained | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AVX-512 | hot | 85% | 2.46/10.26/4.69 | 2.57/10.35/4.76 | 2.56/10.32/4.66 | 1.78/6.47/1.17 | 2.49/11.32/9.77 |
| AVX-512 | hot | dense | 2.79/12.26/8.07 | 2.88/11.46/6.24 | 2.84/11.40/6.11 | 1.79/6.50/1.19 | 1.79/6.85/1.87 |
| AVX-512 | cold | 85% | 26.74/23.47/8.32 | 26.75/24.79/8.15 | 26.68/24.60/8.08 | 28.57/26.46/15.00 | 23.84/26.97/21.56 |
| AVX-512 | cold | dense | 27.45/26.16/13.43 | 27.45/27.32/11.60 | 27.29/27.27/11.51 | 28.75/26.48/15.06 | 22.01/20.47/12.56 |
| AVX2 | hot | 85% | 3.91/10.09/5.14 | 3.99/10.21/5.27 | 3.99/10.17/5.20 | 1.74/6.25/1.15 | 2.43/11.01/9.66 |
| AVX2 | hot | dense | 4.20/11.82/8.10 | 4.26/11.37/7.02 | 4.25/11.33/6.95 | 1.76/6.28/1.17 | 1.75/6.63/1.83 |
| AVX2 | cold | 85% | 40.14/33.04/9.23 | 40.17/33.18/9.60 | 40.45/33.37/9.34 | 27.31/25.34/13.87 | 22.50/25.72/20.22 |
| AVX2 | cold | dense | 40.91/35.36/14.63 | 40.95/35.58/13.77 | 41.07/35.50/13.59 | 27.55/25.57/14.20 | 21.15/19.69/12.14 |

Sets and larger maps, dense all-miss ns/lookup:

| ISA | Working set | Value bytes | Scalar16 | Literal batch | Maintained | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AVX-512 | hot | 0 | 7.63 | 5.67 | 5.60 | 1.14 | 1.64 |
| AVX-512 | hot | 64 | 8.09 | 6.31 | 6.23 | 1.27 | 1.75 |
| AVX-512 | cold | 0 | 12.65 | 10.97 | 10.76 | 14.42 | 11.47 |
| AVX2 | hot | 0 | 7.87 | 6.77 | 6.74 | 1.13 | 1.60 |
| AVX2 | hot | 64 | 8.71 | 7.68 | 7.57 | 1.24 | 1.80 |
| AVX2 | cold | 0 | 13.53 | 12.67 | 12.54 | 13.28 | 10.95 |

### Whole-table writes

Ns/operation; cells are **build / existing replacement / 50%-existing upsert /
erase+refill pair**. Churn includes all nine pure refill rounds, with maintenance
pulses. Writes have one timed batch per seed; tiny hot construction batches are
noisy. The complete grids retain the repetitions, MADs and physical accounting.

| ISA | Working set | Occupancy | Scalar16 | Literal batch | Maintained | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AVX-512 | hot | 85% | 9.89/3.39/9.09/17.83 | 10.53/3.43/9.04/17.94 | 11.27/3.43/9.07/17.58 | 7.10/2.38/2.75/7.06 | 6.80/2.94/22.71/14.38 |
| AVX-512 | hot | dense | 10.39/3.71/11.67/19.25 | 10.18/3.71/11.74/19.33 | 10.01/3.73/11.74/19.55 | 6.83/2.30/2.76/7.20 | 8.69/1.93/5.68/15.30 |
| AVX-512 | cold | 85% | 58.00/27.10/59.39/151.13 | 58.06/27.19/59.45/151.77 | 58.13/27.02/58.86/151.45 | 31.99/37.55/35.09/105.02 | 25.61/29.35/47.60/159.90 |
| AVX-512 | cold | dense | 58.00/28.07/72.68/155.02 | 58.03/27.93/72.85/155.95 | 58.45/27.94/72.64/155.69 | 31.59/38.11/35.61/105.95 | 39.38/26.72/39.92/146.55 |
| AVX2 | hot | 85% | 10.77/4.86/10.29/22.86 | 11.54/4.91/10.30/22.90 | 10.69/4.83/10.22/23.17 | 8.02/2.30/2.66/7.02 | 6.70/2.75/22.55/14.12 |
| AVX2 | hot | dense | 10.83/5.18/12.87/24.09 | 11.62/5.10/12.82/24.39 | 10.89/5.08/12.98/23.86 | 6.71/2.25/2.76/7.00 | 7.81/1.90/5.52/14.92 |
| AVX2 | cold | 85% | 70.69/40.80/74.05/213.49 | 71.49/40.40/72.78/213.53 | 70.54/39.81/73.73/213.27 | 30.82/36.25/33.61/101.62 | 24.29/27.53/45.16/153.93 |
| AVX2 | cold | dense | 71.08/41.33/88.29/217.95 | 71.57/41.36/88.74/218.00 | 70.90/41.66/89.38/217.37 | 30.54/36.47/33.91/102.20 | 38.14/25.42/38.11/141.29 |

For dense map8 misses, AVX-512 reduces hot/cold time by 24.3%/14.3%, and AVX2
by 14.2%/7.1%. The maintained implementation improves dense map8 all-miss
results beyond the original batch on both ISAs. Its metadata optimisation is a
small additional gain; most of the benefit comes from comparing logical groups
together without adding tuple candidates.

The accepted remaining tax is explicit. Across the selected whole-table
initial hot-direct/cold-prefetched grid, the largest AVX-512 regression is 4.8% for
ordinary map8 mixed cold reads (23.47 → 24.60 ns). The largest AVX2 regression
is 2.1% for ordinary hot set hits (3.43 → 3.50 ns). Dense hot misses gain most;
ordinary low-density probes have fewer continuation steps to eliminate.

After churn, Swiss growth can remove the initial dense-case advantage. In the
post-churn grid, the largest regressions are 6.9% for hot set hits and 6.4% for
mixed cold map8 reads on AVX-512; the largest AVX2 loss is 2.5% for hot set hits.
Ordinary cold map8 misses still improve 10.94 → 9.44 ns on AVX-512 and
12.13 → 11.02 ns on AVX2. These history-dependent costs remain part of the
chosen default; candidate masking removes extra tuple checks, not all extra
metadata traffic.

Real peer gaps remain visible: dense hot misses trail Boost and Abseil even
after this improvement, while dense cold AVX-512 misses beat both. Native
container capacity rounding is part of these whole-table results: Abseil doubles
its allocation between the ordinary/dense populations, and Boost uses a larger
allocation throughout. These are neither memory-matched fixtures nor a claim
that probe instructions alone explain the peer ranking.

Steady replacements, mixed upserts and erase/refill preserve the existing write
performance. The actual benchmark keeps erase inlined in all AVX-512 owners,
and outlined in all AVX2 owners. The initial small first-use construction rows
are checked by the separate frozen-binary replay described below.

## Costs addressed before promotion

The initial SIMD packet stored its group addresses in an array, then indexed
that array to locate a candidate tuple. Complete W40/b10/map8 assembly contains
five explicit stack-address references for that machinery and register pressure
on each ISA. The maintained reader has none. This is not a claim that its whole
function has fewer instructions: scalar fallback and candidate reconstruction
also contribute code.

The next groups are computed independently from the packet origin:
`group(q) = (first + q*step + q*(q+1)/2) & mask`. A surviving candidate's lane
reconstructs its physical slot; other probes do not retain an address array.
Two-group slot selection uses the first or second group directly. Four-group
selection uses immediate nibbles for triangular offsets 0/1/3/6, avoiding a
second multiply. Continuation is computed only after a packet cannot terminate.

Masking remains eager, before any tuple access. In particular, tag matches in
later groups must not create extra branches/full-key checks just because the
SIMD comparison saw them. Every matching lane of the first group containing an
empty remains eligible; tombstones do not terminate a probe.

An isolated literal AVX-512 metadata region has modeled backend reciprocal
throughput of 5.83 cycles in the initial packet and 5.17 in the maintained
reader. This region excludes tuple inspection, continuation when needed, the
caller/ABI and real caches. LLVM 21.1.8's `znver5` selector uses `Znver4Model`;
these numbers are a scheduling proxy, not measured Zen 5 lookup performance.

The write resolver and staged reader remain on one-group steps. The ordinary
reader retains the existing out-of-line `read_overflow` boundary, keeping the
new state outside the primary loop. Clone, growth and accumulation use the same
physical control/tuple format. No source order or mutation admission/damage
contract changes.

## Verification

The maintained component check covers two/four-group masks, arbitrary triangular
steps, wrap, unaligned backing, first-empty-group termination, high lanes,
tombstones and reconstructed slot addresses. Local ASan/UBSan passes all 24
maintained checks, including W1–64, sets/maps, guarded mutations, RMW, storage,
clone, growth and accumulation. Random histories also retain the exact 26,487
full-key checks and their order across 96,000 instrumented lookups.

Cross-compiled complete primary read, upsert, erase and RMW callers remain
identical: 28 functions for each candidate/control pair, 56 comparisons per ISA
including the initial packet control. The native comparison repeats this check
and runs the complete maintained owner suite before timing each ISA.

## Native context and recovery

Job `20261020T210821Z-7755d271` ran sequentially on a dedicated AMD EPYC 9R45
Zen 5 c8a.2xlarge host, CPU 0, no SMT/background observer. Clang 21.1.8 used
`-march=znver5` or `-march=x86-64-v3 -mtune=znver5`, with identical inlining
threshold/function alignment for all arms. Actual Boost 1.92.0 and Abseil
`2065f4ded0558c6f89fee67c8e5228feb4eb960e` retain their native APIs/default hashes
and allocation policies.

The 300 callers produce 23,100 raw rows: three seeds, three read repetitions,
one million independent queries per read batch and ten erase rounds. Hot is
K30/E30/b10/W40 with V0/8/64; cold is K30/E30/b20/W50 with V0/8. Cold reads
follow a 256 MiB conditioning pass. Hot does not promise L1 residency, and cold
does not establish every access reached DRAM. No new serial, Loom, ARM or Intel
performance claim is made.

[Retained native evidence](evidence/graduated/provenance.json) includes actual
peer controls, the full direct/prefetched and routed grids, all write phases,
MADs, allocations, machine/compiler identity, exact tuple traces and checks. Its
[archive](evidence/graduated/artifact.json) recovers raw samples, complete
assembly, binaries and captured sources. The offline audit/summary recipe was
regenerated from that verified S3 archive with identical retained outputs.

[Static evidence](evidence/graduation-static/provenance.json) retains sanitizer
results, complete caller comparisons, pre-promotion scalar-control comparisons
and the isolated metadata model. The MCA recipe also regenerates byte-identical
scope reports from archived literal assembly.

## Construction anomaly closed with frozen binaries

The main sweep timed each small construction once per seed. Its AVX-512 map8
row showed a 14% difference, with one native sample faster and two slower than
the scalar control. This did not reproduce in a bounded replay on the same
instance using the exact original binary hashes, without recompilation.

The replay runs construction in a fresh process, disables subsequent read/write
phases, and retains full internal validation. It uses three seeds and rotates/
reverses all five arms: 15 executions per hot arm/case and 9 per cold arm. Binary
hashing warms the executable file cache before exec; this measures construction
with a resident executable, not cold executable paging. The original sweep's
rows remain above and in the raw evidence.

Timings are ns/initial entry at 85% primary occupancy. The timer includes
peer `reserve(n)`, Ikea primary initialization and on-demand
Swiss growth, followed by ordinary insertion for every input. It does not use
a bulk builder or write-prefetch pipeline. Here hot/cold distinguishes table
size; the read-conditioning pass is not part of construction.

| ISA | Working set | Value bytes | Scalar16 | Initial batch | Maintained | Boost | Abseil |
| --- | --- | --- | --- | --- | --- | --- | --- |
| AVX-512 | hot | 0 | 6.99 | 7.03 | 7.00 | 4.55 | 5.56 |
| AVX-512 | hot | 8 | 9.71 | 9.67 | 9.68 | 6.88 | 6.65 |
| AVX2 | hot | 0 | 7.85 | 7.84 | 7.81 | 4.46 | 5.50 |
| AVX2 | hot | 8 | 10.86 | 10.63 | 10.66 | 6.89 | 6.62 |
| AVX-512 | cold | 0 | 46.96 | 46.78 | 46.94 | 25.09 | 22.81 |

The apparent small-table construction regression **between Ikea reader arms**
did not persist. The construction deficit against peers remains: AVX-512 map8
takes 9.68 ns/entry versus Boost's 6.88 and Abseil's 6.65, while the large set
takes 46.94 versus 25.09 and 22.81. This replay preserves existing construction
performance; it does not establish competitive construction or attribute the
gap to individual initialization, packed-write or overflow-growth costs.
[Retained replay evidence](evidence/construction/provenance.json) includes the
345-execution audit and medians/MADs. Its archive recovers every sample, command,
binary hash and source; the report recipe regenerates the summary/audit.
The earlier launcher attempt collected no timings because the source capture
omitted an evidence reference; the successful replay includes that reference
explicitly. No maintained source or binary changed between the two measurements.
