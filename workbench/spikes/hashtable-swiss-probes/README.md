# Swiss probe width

Completed Zen 5 comparison: **graduate eight-bit tags, retain 16-byte Swiss
groups and the 87.5% ceiling**. Ikea commit `50937e71` contains the promotion.
Neither 32 nor 64-byte groups earns adoption. See [results](RESULTS.md) for
whole-table reads, writes, actual Boost/Abseil controls and the losing cases.

The later [Swiss metadata-batch study](../hashtable-swiss-batch/README.md) widened
ISA reads to 32/64 metadata bytes while retaining **16-slot logical groups**
and their first-empty stopping rule. Those compatible decisions answer different
questions; wider loads did not promote wider logical groups.

The experiment used actual 16/32/64-slot logical probe groups, all with eight-bit
tags and tombstones. It held primary geometry, seed and encoding constant, and
matched total Swiss slots except for each width's minimum allocation. AVX2 used
one/two YMM comparisons for 32/64 bytes; AVX-512 used YMM/ZMM comparisons. A
reconstructed seven-bit owner checked the tag promotion separately. Filtered
continuation was excluded; its [earlier comparison](../hashtable-overflow/README.md)
also rejected increasing the Swiss ceiling.

The rejected width implementations and runners have been archived. There is no
production width option or maintained alternative owner. Only offline audit and
summary scripts remain here.

## Evidence and recovery

[Retained evidence](evidence/zen5/provenance.json) contains complete summary grids,
MADs, capacities, compiler/machine identity and primary-code comparisons for both
ISAs. Its [archive reference](evidence/zen5/artifact.json) recovers all 576 callers,
raw samples, binaries, disassembly, sources and logs. The offline report recipe
was executed successfully from the S3 archive.

Use Linux with the pinned toolchain; prefix these commands with `orb -m ubuntu`
from macOS:

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/hashtable-swiss-probes/evidence/zen5 build/swiss-width-report
```

The worker bundle's `source.tar.gz` captures the exact experiment source. A
second verified source archive is in Linux
`~/sixdb-archive/hashtable-swiss-probes-20261020/` ([receipt](archive.json)). Replay
uses captured source or binaries, rather than applying old experimental deltas
to a future production implementation.
