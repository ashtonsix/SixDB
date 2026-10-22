# NEON Local residual transpose

Can SeriesPack's NEON Local residual transpose do less vector work? Local tiles
store residual bits as bit planes, so every NEON Local read and write of a
residual performs an 8×8 bit transpose per tile. Promoted to Ikea on
2026-10-22.

## Starting point

- The [six-CPU snapshot](../ikea-cpu-comparison/README.md) had Zen 5 at 21.9×
  Neoverse V3 for `ordinary/k7/local/read/concrete`: x86 uses a GFNI affine
  instruction where NEON used three six-operation XOR delta swaps.
- The [Neoverse V3 study](../ikea-neoverse-v3/README.md#seriespack-mixed-local-residual-transposes)
  rejected moving *mixed* Local read residuals from integer registers to the
  vector `transpose128` (all 60 cases lost). That integer path is unchanged
  here; this study reduces the vector transpose itself.
- The [rebuild assessment](../ikea-composition/ikea2-campaign/assessment.md)
  established the dense 64-row pure-tail read/write groups and the one-bit
  weighted-sum writer. Both remain; R ≤ 2 reads keep their `vtst` path.
- Disassembly of the maintained body was throughput-bound: 4 TBL plus 72
  logic/shift operations per 64 rows (about 0.34 cycles/value on M1).

## Changes

1. **Select form.** Each stage exchanges bit p of mask M with bit p+S as
   `BIT(BIT(x << S, x >> S, M), x, keep)`: four operations instead of six. Clang
   21 lowers `vbslq` with a constant *or* loop-invariant mask (even one hidden
   behind an empty `asm`) to AND/AND/ORR, seven operations and slower than the
   original. `insert_bits` emits `BIT` directly; it overwrites only its fresh
   first operand, so no mask copies appear. The masks for S=7 and S=14 never
   cross a 32-bit lane.
2. **Quad layout for dense 64-row pure tails.** Gather planes 0–3 of four tiles
   into 32-bit lanes (and planes 4–7 when R > 4). The two low stages run on
   those lanes; the high stage (S=28 swaps nibbles between bytes b and b+4)
   becomes `SLI`/`SRI` by 4 between the two registers; `ZIP1/ZIP2` (read) or
   `UZP1/UZP2` (write) restore order. For R ≤ 4 planes 4–7 are absent, so one
   register carries four tiles. Per 32 rows: read R ≤ 4 is 1 TBL + 12 other
   operations (was 2 TBL + 36); R = 5–7 is 2 TBL + 20.

Short packets, mixed-width writes and placed paths keep `transpose128`, which
now uses the select form.

## Results

Spike binaries differ only in the three patched NEON headers and compile their
own SeriesPack objects. `current` is the promoted implementation; [original.patch](original.patch)
restores the previous transpose source and [bit4.patch](bit4.patch) keeps only change 1.
Both evidence directories predate promotion and label these `baseline`
(original) and `quad` (current). Cases: 8,192 logical rows, `read` = concrete
`dense_range`, `read16` = 8,192 random aligned 16-row ranges, `write` =
prepared maintained replacement of all rows with sum and coverage.
Six alternating rounds × three repetitions, pinned, Clang 21.1.8,
`armv8-a+simd`, `neoverse-v2` tune.

Speedup over original, median of round medians:

| Case | V2 / c8g select | V2 quad | V3 / c9g select | V3 quad |
| --- | ---: | ---: | ---: | ---: |
| k3 read | 1.58 | **2.76** | 1.69 | **2.85** |
| k4 read | 1.58 | **2.92** | 1.66 | **2.92** |
| k5–k7 read | 1.55–1.57 | 1.66–1.73 | 1.64–1.65 | 1.79–1.83 |
| k3–k7 read16 | 1.23–1.33 | 1.29–1.51 | 1.34–1.37 | 1.36–1.63 |
| k2 write | 1.25 | 1.58 | 1.30 | 1.71 |
| k3–k7 write | 1.42–1.50 | 1.50–1.74 | 1.49–1.52 | 1.58–1.84 |
| k11/12/20/28/31 write | 1.13–1.30 | 1.13–1.30 | 1.14–1.24 | 1.14–1.24 |
| Controls: k1, k2 read, k8, k64, mixed reads | 0.998–1.002 | 0.999–1.003 | 0.98–1.00 | 0.98–1.01 |

The V3 k64 read control moved 2% in both candidates although its code is
unchanged; treat ±2% as noise there. Absolute V2 k4 read: 1400 → 480 ns per
8,192 values. Text bytes of the measured functions shrink 3–10% at k2–k7
(for example k3 4892 → 4380) and grow about 1% (+56 bytes) at mixed widths,
which now load three more mask constants.

[V2 evidence](evidence/spike-c8g-neoverse-v2/) · [V3 evidence](evidence/spike-c9g-neoverse-v3/).
Each holds the summary, all round samples, sizes, checks and host receipt.

## Checks

- `neon_local_transpose_check_VARIANT`: one million random and 64 single-bit
  inputs per variant against a bitwise reference transpose.
- The maintained `ikea_seriespack_check` built against each overlay. It reads
  K = 1–7 dense Local through the 64-row path with unaligned starts and covers
  dense mutation for widths 1–7, both against an independent wire encoder.
- Every timed case validates its decoded values, sums and point reads first.

## Promotion

The [promotion run](evidence/promotion-c8g-neoverse-v2/) on c8g.4xlarge
(Neoverse V2) checked the edited Ikea headers:

- `ikea_validate` passes all 70 tests under both `neoverse-v2` and `neoverse-v3`
  tunes. ASan/UBSan (`-O1`) SeriesPack and descriptor checks pass.
- The spike comparison repeats against the promoted source to three decimals.
- The **whole maintained SeriesPack suite** (150 cases, `--suite=all`, five
  repetitions, two order-reversed trials) built from current and original
  sources: geometric-mean speedup 1.033, median 1.000. 29 cases are more than
  3% faster: k7 Local read 1.66×, k7 maintained write 1.56×, k28 maintained
  write 1.14×, depth-3/7 k7/k12 pipeline mutations 1.08–1.38× in every
  composition style (fused, inline, CPS, scratch calls), and headed k31h8 and
  k63h16 placed mutations 1.03–1.13×.
- Two cases read more than 3% slower, and neither is a reproducible change.
  `ordinary/k64/local/point/bound` (0.929×) has no residual. Its current trial 1
  matches original (6,316 ns); trial 2 drifted from 7,411 to 6,831 ns within
  its own repetitions. `placed/k31h8/substituted/all/sum-materialized`
  (0.964×) alternates between about 2,440 and 2,670 ns in *both* variants.

The full suite JSON stays in the worker archive named by `artifact.json`.

## Reproduce

```sh
python3 workbench/tools/worker.py run workbench/spikes/neon-local-transpose/cloud.sh \
  --machine neoverse-v2 --instance-type c8g.2xlarge --env JOBS=8 --arg=--rounds --arg=6
```

[graduation.sh](graduation.sh) adds Ikea validation under both ARM tunes,
ASan/UBSan SeriesPack checks and the maintained suite built from current and
original sources. Locally, configure with `-DSIXDB_SPIKES=neon-local-transpose`
and run [run.py](run.py) on the built directory. OrbStack on Apple silicon
pins a virtual CPU that can move between performance and efficiency cores;
use its timings only for direction.

## Mixed Local reads

Mixed Local residuals (body bytes plus R residual bits) were transposed in
integer registers per 16-row packet; the [Neoverse V3
study](../ikea-neoverse-v3/README.md#seriespack-mixed-local-residual-transposes)
lost 0.69–0.73× moving that packet to the six-operation vector transpose. The
promoted change instead gathers **four tiles** for R ≥ 3 in 32-row groups
(`neon_group::tail`): it loads the 32-bit word that *ends* at each tile's
residual (or the 64-bit word when R > 4), which never leaves the tile, shifts
the planes down and shares the quad transpose with dense pure tails. Dense
reads of widths 9–16 already run in 32-row groups; wider widths use 16-row
groups and keep the integer path. [intreg.patch](intreg.patch) restores it.

Six alternating rounds, `read` = 8,192-row `dense_range`; speedup of the
promoted gather over the integer path (median of round medians):

| Case | V2 / c8g | V3 / c9g |
| --- | ---: | ---: |
| k11 read | 1.72 | 1.69 |
| k12 read | 1.26 | 1.33 |
| k13 read | 1.24 | 1.27 |
| k15 read | 1.13 | 1.20 |

Every other case (k1–k8, k20–k64 reads, all `read16` and writes) is within
±1.6% on both hosts ([V2](evidence/mixed-c8g-neoverse-v2/summary.txt),
[V3](evidence/mixed-c9g-neoverse-v3/summary.txt); label `mixed4`). Text bytes of
the measured functions shrink 2–4% at k11–k15. The maintained 150-case suite
([summary](evidence/suite-mixed4-c8g-neoverse-v2/suite-summary.txt)) has
geomean 1.000 and contains no k9–16 Local bulk read; its one case beyond −3% is
`ordinary/k64/local/point/bound`, which has no residual and alternates the same
way in the earlier promotion run. Checks: transpose and maintained SeriesPack
checks per variant, `ikea_validate` under both ARM tunes and ASan/UBSan
SeriesPack checks on the patched source ([validation](evidence/validate-mixed4-c8g-neoverse-v2/)).

**Rejected: 32-row groups above 16 bits** ([mixed4w.patch](mixed4w.patch),
against the pre-promotion source). Moving dense, placed and selected-sum reads
of widths above 16 to 32-row groups so they reach the gather looked faster on
OrbStack (k20 1.6×) but lost on Graviton: k20–k63 reads 0.59–0.95×, and the
suite's headed placed sums k20h8/k31h8/k63h16 0.66–0.90×
([suite](evidence/suite-mixed4w-c8g-neoverse-v2/suite-summary.txt)).

## Striped constant-mask selects

Clang 21 lowers a constant-mask `vbslq` to AND/AND/ORR. An audit of the
`bulk_arm` preset benchmark's disassembly (any ORR of two vector ANDs/BICs
defined shortly before it) found this pattern in the striped tail readers and
writers: the R = 5/7 read select, the R = 3/6 read joins, the partial striped
writer's select and the complete-tile masked ORs. The Local `vtst` paths' BIC
is a comparison inversion, not a select. [bitsel.patch](bitsel.patch) emits
`BIT` at all six sites; [bitread.patch](bitread.patch) and
[bitwrite.patch](bitwrite.patch) split readers from writers. All pass the
maintained SeriesPack check. Striped preset cases, six alternating rounds
([V2](evidence/bitsplit-c8g-neoverse-v2/summary.txt),
[V3](evidence/bitsplit-c9g-neoverse-v3/summary.txt); full `bitsel` in
[V2](evidence/bitsel-c8g-neoverse-v2/summary.txt) and
[V3](evidence/bitsel-c9g-neoverse-v3/summary.txt)):

- **Readers:** bulk reads k5 1.35–1.40×, k7 1.30–1.40×, k15 1.24× (V3) and
  k14 1.05–1.14×, but every k5/k7/k14/k15 filter is 2–5% slower on both hosts,
  and 16-row reads are mixed (k7 0.86× on V2, k14 0.92× on V3). The filter
  loops themselves barely change; the asm boundary changes their register
  allocation and repeats loads the compiler previously hoisted.
- **Writers:** k7 maintained writes 0.90–0.91× on both hosts.

**Not promoted:** neither split is a win on every metric. A select form the
compiler can still schedule around (or a filter that does not share the
reader) would be needed to keep the read gains.

## Headed placed sums

`placed/k20h8`, `k31h8` and `k63h16` sums trail Zen 5 by 4–5×. A probe of
k20h8 `sum_regions` (16-row grain) runs about 85 instructions per 16 rows:
two integer-register residual transposes, four u32 widenings and joins, four
compares and the selection masks. It is near issue throughput (about one
cycle per row on V2); 128-bit lanes hold four u32 values where AVX-512 holds
sixteen. Two candidates, maintained 150-case suite on c8g:

- [sumpair.patch](sumpair.patch) pairs u32 values before widening in grouped
  sums over two accumulators: geomean 0.998, neutral (the suite's headed sums
  run 16-row groups) ([suite](evidence/suite-sumpair-c8g-neoverse-v2/suite-summary.txt)).
- [sum32.patch](sum32.patch) adds 32-row selected-sum groups above 16 bits so
  headed residuals reach the four-tile gather: k20h8 0.87–0.88×, k31h8 0.67–0.79×,
  k63h16 0.82–0.90× ([suite](evidence/suite-sum32-c8g-neoverse-v2/suite-summary.txt)).
  The loss matches the rejected `mixed4w` dense reads, so the per-16-row
  integer path is not this gap's bottleneck.

## Preset follow-up

Does the faster Local transpose change `bulk_arm`? [preset.cpp](preset.cpp)
times Local against striped, both maintained, for every width `bulk_arm`
stripes (1–7, 10, 12, 14, 15, 20) on 8,192 dense rows: `read`, `read16` and
`write` as above, `point` = 8,192 random `get_unchecked`, and `filter` = 128
complete 64-row less-than windows. Seven randomly interleaved repetitions,
pinned, on c8g (V2 tune) and c9g (V3 tune); see the
[summary](evidence/preset-summary.txt) (striped/Local time) and the
[V2](evidence/preset-c8g-neoverse-v2/) and [V3](evidence/preset-c9g-neoverse-v3/) runs.

**Keep `bulk_arm`.** Stripes still win every bulk read on both hosts
(0.21–0.57× Local time) and every maintained write (0.35–0.78×). Local keeps
its advantage for points (striped 1.03–1.93× slower, except k2/k4 on V3) and for
filters at most widths (1.07–2.25×; k6/k7 favor stripes). Short random 16-row
reads are mixed by width and host. `compact` already covers point-oriented use.

## Open questions

- Mixed Local writes still transpose each 16-row packet with `transpose128`;
  a four-tile write would need 32-row grouping in the packet writer.
- An x86 AVX2 `transpose128` remains a separate body.
