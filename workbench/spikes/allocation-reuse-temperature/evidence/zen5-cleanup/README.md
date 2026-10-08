# Cleaned allocation API on the same Zen5 worker

Run `20261008T103211Z-1d8b16a4` reused Spot instance `i-0ba1519a0c997226b`.
The baseline is the exact archived **combined** binary from the
[selector campaign](../zen5-ablation/README.md), not its original ranked policy.
The cleaned cut also removes allocation preference arguments and release CPU
advice, changes the hint ABI/layout, and simplifies the corresponding callers.
[Findings](../../FINDINGS.md) distinguish the two comparisons.

Retained samples include every matching block: two policies, two widths, nine
cases, four alternating trials and eight blocks. `touch_bytes=0` means the whole
variable-sized payload. All traces checked checksums, live anchors, allocation
counts and zero charge after shutdown; all 23 native Orbital/Loom checks passed.
These are block mean allocation/touch/release costs, not database latency or
individual-operation percentiles. The cleanup's components are not separately
isolated by this comparison.

Context records exact commands, previous/current source and binary hashes, and
the recovery reference for the previous binary. The current worker archive
also contains the bounded replay recipe and its compiled sources. Compiler
output and full logs remain there. The worker was confirmed terminated.

Regenerate paired trial ratios and observed ranges from retained inputs:

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/allocation-reuse-temperature/evidence/zen5-cleanup \
  build/reports/reuse-cleanup
```
