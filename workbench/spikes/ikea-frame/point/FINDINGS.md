# Point costs and prepared state

The current byte-minimising image is expensive for independent warm points.
Removing redundant per-block decoder bindings saves substantial resident capacity,
but does not remove prediction reconstruction or run search. Native global FOR
is a necessary representation control, alongside an uncompressed u64 baseline.
No checkpoint or Frame representation default follows from this experiment.

## Same bytes, different preparation

`compact_image` moves the existing encoded bytes without repacking and drops
redundant per-block erased decoders. A 65-entry, 1,040-byte point/range catalogue
is shared across images. Each entry invokes maintained typed Local SeriesPack
operations; no packed-bit/ISA algorithm is copied. Readers retain their image's
checkpoint choice and original coordinates.

This is a **point-consumer** preparation experiment. It does not preserve the
old `column`'s decoder-based whole-operation entry points and is not a drop-in
replacement for the shared encoding material. Sparse exception bindings remain:
the current private patched body retains exception width only in its decoder
binding. That interpretation must survive until explicit metadata replaces it.
Source placement and image lifetime remain owner obligations; the prepared owner
cannot be moved after borrowing.

Paired local ARM-VM medians, ns per warm independent point request. Each cell
contains 36 samples from two processes in reversed input/arm order, 18 rotations,
4 × 4,096 requests per sample, seed 713. Every batch consumes the exact u128 sum
of requested payloads, including its high half. Four independent arithmetic
accumulators avoid a single sum chain; these are complete request loops, not a
claim of four simultaneous software coroutines or DRAM throughput.

| Original column | Native u64 | Native global FOR | Eager native checkpoints | Shared native checkpoints |
| --- | ---: | ---: | ---: | ---: |
| Jena pressure, 65,536 rows | 0.62 | 0.48 | 21.64 | 21.64 |
| ClickBench OS, 2,999,808 rows | 1.37 | 0.80 | 20.29 | 18.61 |
| ClickBench URLHash, 2,999,808 rows | 1.36 | 1.38 | 13.17 | 10.60 |

Capacity plus owner/prepared bytes for those native-checkpoint point images:

| Column | Eager | Shared | Native global FOR |
| --- | ---: | ---: | ---: |
| Jena pressure | 187,014 | 126,702 | 131,176 |
| OS | 4,981,166 | 1,284,062 | 2,999,912 |
| URLHash | 29,821,006 | 26,075,510 | 23,998,568 |

The shared catalogue's 1,040 bytes are charged once per process separately.
These are vector allocation capacities and owner objects, not RSS, allocator
overhead or physical traffic. All competing images coexist during measurement;
each arm warms its actual request batch immediately before timing. This bounds
the request set but does not establish full-image cache residence.

The memory reduction is useful; it does not make these point kernels competitive
with a native column. OS keeps its compression advantage after prepared state is
accounted for. URLHash still has no useful point/memory case against native FOR
in this image. Representation choice must include useful operations, not only
encoded payload bytes. A native/plain option remains essential.

## Checkpoint cost on identical predictions

For Jena pressure, 235 blocks remain delta at the same residual width across all
eight images (four checkpoint choices × two run geometries). The driver verifies
the actual residual plane, rbase and anchor bytes before comparing their
common-point domain. Native every-16 / packed every-16 / native every-64 /
absent checkpoints cost 21.87 / 23.23 / 33.48 / 59.67 ns with eager preparation;
shared preparation costs 23.12 / 24.29 / 35.02 / 61.46 ns. Shared state saves
memory; it is not uniformly a speed improvement. The small packed/native gap is
not a fleet ranking on this VM. The large cost of reconstructing longer prefixes
warrants a different complete prefix consumer.

Source inspection identifies one candidate: the current point reader asks the
erased range decoder for fewer than sixteen values, then sums a u64 stack buffer.
That partial range takes scalar point decoding. A native masked prefix reduction
over the admitted block could remove the buffer and repeated scalar work. This
is an unmeasured proposal, not an established explanation of every point cost.
It belongs to Frame reconstruction, independently of Intervals spacing design.

## Correctness and evidence boundary

The extended check passes 49,227,392 exact reads for authored data, original Jena
pressure, its sentinel/fault block and OS. It covers eager/shared preparation,
all five kinds, all checkpoint modes, both run geometries and coarse masks.
Original integers also check point/range kernels at every width, unaligned
boundaries and bounded output writes. A deliberately nonminimal K63 predictor
has wrapped u64 cumulative residual checkpoints while logical values and signed
steps remain valid; both readers reconstruct it exactly. This is a modular
reconstruction law, not an ordered nonnegative interval-prefix law. Protected-none
and deliberately faulty descriptor reads remain checked.

The measurement identities retain ELF/source/input hashes, every arm and raw
repetition. The [selected comparisons](../evidence/heads-points-local/point/summary.json)
retain all repetitions for whole-image and matched-delta points; the
[verified recovery bundle](../evidence/heads-points-local/artifact.json) also
contains masked cases, sources and binaries. Shared-state timings use the source captured as
`678145797b2f4bc09efe690093f4c09745f58a65901c4ca414a4dfe2d244d6e6`.
The earlier eager-only exploration has a separate capture
`9ba834d04d70afee8f5562e814235cee8370d80e7b68c587ffe26038e340a559`;
do not pair its samples with the shared cohort. Pinned Clang 21.1.8, release
O3/native ISA/generic tuning, vCPU7 on the Apple/OrbStack AArch64 VM. Automatic
vectorisation is enabled. Local scheduling outliers and concurrent work on other
vCPUs limit close rankings. Serial latency, cold/Loom service and fleet policy
remain unmeasured here.
