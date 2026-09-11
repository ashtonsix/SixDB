# Packed integer kernel grains

Which physical grain and SIMD transformation suit each width and geometry?
This study owns the local codec question: lane work, coalescing, projection,
encoding and generated-code costs. Wider composition and traversal costs belong
to their own studies.

Focused investigations retain their implementation, independent check and
findings: [Local1](local1/README.md), [Local1 encoding](local1-encode/README.md),
[Local2](local2/v2-decision.md), [Local4](local4/README.md), [Local6](local6/README.md),
[NEON byte grain](neon-byte/README.md), [NEON local coalescing](neon-local/README.md),
and [NEON scan grain](neon-scan/README.md). The
[local region diagnostic](local-regions.cpp) is an additional mechanism probe.

Each result applies to its captured kernel and caller. The
[predecessor baseline](../ikea-composition/seriespack-predecessor/findings/baseline-20260911.md)
identifies retained corrections and remaining costs. Small replay specifications
in `replays/` use the [shared helper](../../tools/replay.py); selected evidence
keeps countercases and code costs alongside wins.
