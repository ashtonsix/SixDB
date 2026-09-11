# Packing and projecting separate head planes

When does combining head and payload work help, and when do independent placement,
gaps and small extents erase that gain? Start with the [H16 findings](findings.md):
dense wins coexist with small/gapped losses and substantial text growth.

The [encoder](encoder/README.md), [compact H16 candidate](compact/README.md),
and [headed reader](reader/README.md) keep focused alternatives and checks.
[replay-all-head.json](replay-all-head.json) selects retained exact binaries for
[the shared replay helper](../../tools/replay.py). New public-API placement
measurements belong in the [recurring suite](../../benchmarks/seriespack/README.md);
this study retains candidate explanations and evidence.
