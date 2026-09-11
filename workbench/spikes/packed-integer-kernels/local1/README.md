# AVX2 Local1 direct expansion diagnostic

This began as an isolated candidate; the bounded physical selection after paired
hardware measurements is recorded below. The initiating signal
is the AVX2-only Local1 decode gap against Calico on Zen 5 and Granite Rapids.
Calico expands packed bits directly; SeriesPack's existing AVX2 implementation
uses the general three-exchange transpose. The immediate LocalPack/ScanPack
predecessors remain the primary controls for the broader kernel surface.

The candidate in [the helper](pack.h) reads exactly four bytes
to produce 32 byte lanes. A byte shuffle replicates each packed byte over its
eight original indices; per-byte bit tests produce zero or one. Dense consumers
reuse the existing native unsigned widening. Smaller fragments read exactly one
byte and define all inactive lanes as zero. The 32- and 64-bit fragment variants
use a byte broadcast, per-lane shifts and a one-bit mask, avoiding the general
transpose followed by widening.

Only contiguous headless Local1 admits the four-byte dense read. In a wider
Local packet with a one-bit tail, body bytes separate the tail bytes of adjacent
packets. The separate fragment experiment leaves that layout intact and changes
only the tail reader, keeping native body/head reads, joins and stores shared.
No body coalescing or dense-tail permission is inferred from a one-bit residual.

## Checks and generated code

With Clang 21.1.8, `-O3 -march=x86-64-v3`, the independent
[oracle/extent check](check.cpp) passes 119,956 cases under
QEMU. It covers every packed-byte value across all supported fragment
`Begin`/`Count` combinations and unsigned lane widths, zero-count null input,
protected page edges, every input/output position modulo 128, dense leftovers
across 23 tile counts, and output redzones. The timing TU's independent wire
construction passes another 108 full-array checks, including headed packets.

[Standalone generated-code wrappers](audit.cpp) show these
counts, including return and constant setup, excluding alignment padding:

| Entry | Existing instructions / text bytes | Candidate instructions / text bytes |
| --- | ---: | ---: |
| Exact four-byte to 32 byte lanes | 17 / 84 | 8 / 45 |
| Eight values in byte lanes | 15 / 71 | 6 / 30 |
| Eight values in u16 lanes | 16 / 76 | 7 / 35 |
| Eight values in u32 lanes | 18 / 82 | 5 / 28 |
| Four values in u64 lanes, begin 0 | 18 / 82 | 5 / 28 |
| Four values in u64 lanes, begin 4 | 19 / 87 | 5 / 28 |

These are instruction counts, not timings. The inspected wrappers have no helper
calls or stack references. Dense widening increases the size of some whole-run
functions by introducing a 32-value loop and a remainder, so smaller fragment
code alone does not establish a universal text-size win. With GFNI enabled, the
existing dense region is already smaller (5 instructions / 30 bytes versus
7 / 42). This candidate does not justify replacing GFNI or AVX-512 kernels.

## Paired timing

[The standalone timing TU](bench.cpp) compares:

- Contiguous headless Local1: current native execution, transpose in 32-value
  regions plus widening, and direct expansion in the same regions plus widening.
- One-bit tails in Local packets: current and direct fragment readers with the
  same body reads, optional separate heads, native joins and exact stores.

It uses u8/u16/u32/u64 where the complete width fits, 256/8192/65536 values,
identical buffers and pass counts across each comparison, one opaque array call,
at least 20 ms per calibrated current-arm sample, and 12 rotating sequential
repetitions. The fragment cases cover payload widths 1, 9, 17 and 49, with
8- or 16-bit heads. The output records byte offsets within pages and reports
cache residency as unestablished. Allocation, input construction and validation
are outside timing. The current-native and current-fragment arms use the current
production headers. The transpose-region control explicitly retains the former
expansion, so its label remains meaningful after production adoption. Reproduce
the original paired comparison from the captured workspaces listed below.

From the Linux workspace, with the pinned compiler and the worker's selected ISA
flags (use `-march=x86-64-v3` for the AVX2-only profile):

```sh
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -Iikea/include \
  workbench/spikes/packed-integer-kernels/local1/check.cpp \
  -o build/local1-check
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -Iikea/include \
  workbench/spikes/packed-integer-kernels/local1/bench.cpp \
  -o build/local1-bench
build/local1-check
build/local1-bench --check-only
SIXDB_CPU=2 build/local1-bench > build/local1-samples.jsonl
```

Use an allowed, otherwise idle CPU selected for the worker rather than assuming
CPU 2 is available. No production library or Calico include path is needed. The
existing frozen four-arm loop-grain diagnostic remains unchanged.

## Joint target result and physical selection

The paired samples were captured on 2026-09-10 with the same diagnostic source
on Zen 5 (`20260910T162104Z-6eddc6d9`) and Granite Rapids
(`20260910T162152Z-92cd62fc`) in the `seriespack-local1-diagnostic-20260910`
workspace. Each profile has 1,296 timing rows and passed the 119,956 helper plus
108 full-array checks. A later fixture audit found that 24 of those context
checks, and 288 of the timing rows per profile, use interleaved u16 heads for
H16. SeriesPack requires two independent byte planes. Those cases are preserved
as auxiliary tail/join measurements and explicitly excluded from endpoint
conclusions: each profile has 84 valid endpoint checks and 1,008 valid timing
rows. The compact data marks the head representation and selection eligibility
of every row and records the immutable raw sample hashes. The full-feature
profile has GFNI; the AVX2-only profile
does not. These runs exercise the AVX2 family under each profile's actual
compiler features, not a direct comparison with AVX-512 fragment kernels.

[Compact medians](results.json) retain every measured
case. Ratios below exclude the H16 auxiliary cases and compare candidate time
with the same-buffer native arm; ranges span the three array sizes and
applicable carriers/packet contexts. The remaining H0/H8 cases cover every
selected carrier and eight wider/headed packet contexts. The numeric ranges and
physical selection below do not change after the exclusion. The one-byte tail
reader itself has no head-layout dependency; production heads continue to use
the existing, correct independent byte planes.

| Choice | Zen 5 | Granite Rapids | Physical decision |
| --- | ---: | ---: | --- |
| No GFNI, dense Local1, all four carriers | 0.229–0.443 | 0.151–0.673 | Select direct 32-value region plus native widening |
| No GFNI, one-bit Local tails | 0.359–0.580 | 0.369–0.637 | Select direct one-byte fragments |
| GFNI, dense Local1 to u16 | 0.370–0.387 | 0.712–0.978 | Select direct 32-value materializer |
| GFNI, other carriers or tail fragments | Mixed across contexts and hosts | Includes regressions | Preserve existing lowering |

The grain control separates the contribution of widening after a 32-value
decode from the direct bit expansion itself. For example, on Granite Rapids at
the larger u64 counts, transpose-plus-widening and direct-plus-widening converge
near 0.67 of the former native endpoint. That supports the wider working region,
not a claim that direct bit expansion continues to improve that saturated case.

The AVX-512 Local1/u16 materializing endpoint already used the AVX2 implementation
for each tile. Its pre-change generated instructions and constants exactly
matched the AVX2 endpoint after normalizing labels, so it now preserves the same
measured dense region. AVX-512 fragment contracts and other materializing
carriers remain unchanged. Wider packets use only the one-byte fragment change;
their body bytes continue to prevent the dense tail-byte read.

The earlier Zen four-arm grain probe (`20260910T161756Z-8dbb8b31`) does not
support a general four-chunk unroll. Most large-array gains disappear, and the
loop has additional fixed work. Local1 encode at 8,192 values is a useful
remaining signal (0.736 of the native arm), but the paired cross-host encoder
evidence needed for a separate selection is not established here.

After physical integration, the AVX2 all-width payload check passes 308
configurations and 89,496 cases under QEMU, including new dense-decode source
and destination guards and remainder counts 2 and 5. The full-feature payload
check compiles, with execution delegated to hardware validation. Focused native
wrapper text changes from 1,262 to 1,219 bytes without GFNI, and from 2,086 to
1,933 bytes in the full-feature profile; these sums are not whole-library size.
The u16/u32/u64 dense wrappers without GFNI grow by 91/96/239 bytes for the
working-region loop and remainder, while fragment and u8 paths shrink. Inspected
wrappers have no helper calls or stack references. Unselected GFNI wrappers keep
their prior sizes.
