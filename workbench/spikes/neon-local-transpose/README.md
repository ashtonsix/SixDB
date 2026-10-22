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
restores the previous source and [bit4.patch](bit4.patch) keeps only change 1.
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

## Open questions

- `bulk_arm` selects stripes for pure widths 1–7 because Local needed the
  transpose. Compare complete consumers again before keeping that preset.
- Mixed Local *reads* still transpose residuals in integer registers. The
  rejected vector rewrite used the six-operation form; the select form and a
  four-tile 32/64-row grouping may change that balance.
- An x86 AVX2 `transpose128` remains a separate body.
