# Native codec controls

The shared [provider](../../../../peers/frame-codecs.cmake) pins official source
archives by commit and SHA-256. Targets expose TurboPFor's upstream library,
FastLanes FFOR, and ALP/RD primitives. The wrappers call actual upstream APIs;
no codec implementation is copied into this spike. This is the primitive layer
of front A, not a complete peer performance comparison.

## Native boot observations

The subsequent [IEEE audit](../alp-audit/README.md) qualifies the complete RD
composition on its tested corpus and exposes arithmetic failures in automatic
decimal ALP at this pin, including ordinary inputs. Use named ALP_RD for the
audited general-IEEE control. The optimized ALP boot results below remain
reproducible observations; they do not establish defined arithmetic or a safe
general-IEEE decoder.

Pinned Clang 21.1.8 builds execute on Zen 5 (AVX2 and AVX-512 callers) and
Graviton 4 (NEON). Each profile passes:

| Control | Independent checks |
| --- | --- |
| TurboPFor | 329 round-trip cases; 12,672 actual P4 direct point accesses, including outliers and patch boundaries. |
| FastLanes FFOR | 248 cases across every width for u8/u16/u32/u64, zero/nonzero bases, output extent and canaries. |
| ALP primitives | 18 float/double cases, bit-exact unfused/fused/RD reconstruction, including signed zero, infinities, quiet NaNs and denormals. |

The source audit compares all 3,655 captured SixDB inputs and all 4,164 extracted
upstream files against the pinned archives. Before/after executable hashes
match the recovered worker archive identities. These are optimized functional
checks; they neither time operations nor establish a fully sanitized peer
build. The separate IEEE audit instruments the actual executed RD composition
and decimal paths and retains their failing cases and floating-environment limits.

Run the [worker script](worker.sh) through the existing worker tool, or configure
the root CMake project on the appropriate native ISA with
`-DSIXDB_SPIKES='frame-phase-1;frame-phase-2' -DSIXDB_FRAME_CODEC_CONTROLS=ON`
and build `frame_turbopfor_check`, `frame_fastlanes_check`, `frame_alp_check`.
Use the pinned Linux toolchain and explicit march/tune as in the worker script.

## Comparison boundaries

- TurboPFor's own make recipe builds baseline and AVX2 translation units
  separately. The AVX-512 caller does not turn its upstream library into an
  AVX-512 codec. Keep actual codec dispatch and caller ISA separate.
- FastLanes here means its 1,024-value FFOR primitives. It does not mean the
  complete FastLanes encoder, file format, expression engine or random accessor.
  Its named random-access benchmark is not a qualified point baseline.
- ALP and FastLanes both export FFOR symbols. Each control links one peer into
  a separate executable so link order cannot select another implementation.
- TurboPFor supports real FOR/PFor direct access. Price fresh versus retained
  preparation, preparation bytes and consumed answers when timing it; decoding
  an entire block is not the only valid point control.
- The peer's sampling-selected ALP/RD route is honored. A value set containing
  floats does not by itself justify forcing one decoder family.

The first artifacts are retained in [Zen 5](../evidence/codecs-zen5) and
[Graviton 4](../evidence/codecs-graviton4). They contain exact test outputs and
executable hashes; recovery references retain the full sources, peer source
manifests, compile commands, binaries and logs. No performance ranking follows
from these boot checks. Encode/decode plus useful consumption, preparation,
capacity and complete Frame workloads remain front A work.
