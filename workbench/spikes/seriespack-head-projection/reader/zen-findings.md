# Headed local decode: Zen placement comparison

The 64-value AVX-512 candidate improves every measured comparison against its
eight-value control on this Zen 5 host. That supports a useful physical-grain
candidate; it does not yet select production behavior. Granite Rapids remains
unmeasured, and the smaller AVX2 candidate has a clear exception.

[The capture](zen-capture.json) identifies the source artifact, sample hashes and
checks. [All 924 medians](zen-results.csv) retain profile, placement, shape,
carrier, count, observed range and corresponding same-family control. Each
median comes from ten rotating sequential repetitions using shared buffers.
The three array lengths are 256, 8,192 and 65,536. Cache residency is unestablished.
The complete source/dependency and placement protocol is in the [README](README.md).

| Candidate and feature profile | Time / corresponding tile8 control, across all tested contexts | Interpretation |
| --- | ---: | --- |
| AVX2 region32, AVX2-only profile | 0.310–1.183× | K17/H16/u32 regresses; no blanket selection |
| AVX2 region32, full-feature profile | 0.253–0.910× | GFNI is available in this build; not an AVX2-only claim |
| AVX-512 region32, full-feature profile | 0.156–1.043× | Some K10/H8/u64 cases regress; several shapes are placement-sensitive |
| AVX-512 region64, full-feature profile | 0.120–0.768× | Consistent wins over the tested shapes, lengths and placements on this host |

At K10/H8/u16 and n=8,192, the tile8 AVX-512 control stays near 0.0603
ns/value. Region64 stays at 0.01355–0.01358 across the four placements. Region32
varies from 0.01781 to 0.02916. Its code and constants are identical across
those placements; a favorable location would overstate its robustness.

The AVX2-only exception is material: K17/H16/u32 at n=256 takes about 0.0709
ns/value in region32 versus 0.0600 in the tile8 control at three placements.
The larger counts also include regressions. The already selected direct one-bit
fragment has a different cost balance from a general transpose. The existence
of a larger admitted region does not establish that consuming it is preferable.

The hardware runs pass 114,240 AVX2 and 285,600 full-feature independent
wire/guard checks, plus 66/165 fixture checks for every linked placement. H16
uses independent high-byte-first head planes. The placement builder verifies
identical instructions/constants and no helper calls or stack references in
the timed functions.

The diagnostic's tile8 K10 function is an 89-byte isolated leaf/head loop,
not the earlier bound endpoint with a placement-sensitive loop. These results
therefore neither reproduce nor prove a fix for that endpoint. A selected
candidate still needs the other host's comparison, integration into the admitted
dense path, exact arbitrary-edge/stride checks and measurements of the actual
public endpoint. No new alignment policy follows from this experiment.
