# Ordinary short-region store probe

This bounded GNR pair asks whether the known split-store benefit has an
independent ordinary materialization seam. It replaces only a full 64-byte
AVX512 store in the existing private `finish_region` with two 32-byte stores,
retaining ZMM reconstruction and an experimental anti-remerging compiler
barrier. It adds no width/placement whitelist, public endpoint, traversal,
alignment permission or general expression driver.

The Local narrow pair shortcut uses this seam only for two or three remaining
contiguous payload tiles. Striped boundary traversal can repeat it, including
within a long request's boundary. Dense bulk leaves and all other native stores
keep their source. Source isolation does not guarantee identical surrounding
linked placement or timing; unchanged controls are retained.

`run.py` restores the original current GNR artifact at its original paths,
rehashes all 601 original sources and reconstructs common caller/check objects
against their recorded hashes. The original baseline must relink byte for byte.
Only native.cpp is recompiled for the candidate and only its archive member
changes. Both programs run operations, scalar/AVX2/AVX512 public wire and
protected-range checks. Existing fixture oracles verify the actual timed
outputs before timing.

The original linked benchmark supplies runtime and boundary cases, independent
head-placement encode controls, all u64 native bulk decodes and available u64 controls,
plus selected point/get16 readers including the unaffected dense Local56 path.
Carrier, whole-call output extent and consumer remain those of each existing
fixture; the controlled boundary output is 8 bytes modulo64, the runtime output
has natural unrecorded carrier alignment, and resident get16 is aligned64.
These are different caller contexts, not a complete alignment sweep. Long
headed bulk decode uses dense planes; gapped read timings are the existing
runtime/boundary cases, not an all-placement long-decode matrix.

Order is base/candidate/candidate/base, three sequential repetitions per case
with a 30 ms minimum, CPU0, 8192 bulk values and 4096 requested resident encoded
bytes. The worker uses default Spot capacity with periodic result uploads off;
the standard five-second Spot interruption observer remains part of its host
environment. Both variants share this environment. No production installation
is implied, especially given earlier Zen alignment-sensitive u64 split losses.

`analyze.py` audits the exact original case inventory, source archive, caller
objects, archive substitutions and baseline binary before retaining paired
samples, ordering edges, actual function bytes/relocations and separate
native/library/benchmark section sizes. It was written after the measured
source capture and is analysis code, not a workload change.
