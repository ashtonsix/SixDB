# Short stores inside the expression materializer

Explicit 32-byte stores remove the large aligned AVX512/u32 gap in the measured
Zen materializer. They do not fix scalar boundary work, and this result does
not justify applying the policy to every output or promoting the traversal.
It extends the earlier [GNR admitted-region result](short-stores.md) to
actual expression callbacks with runtime begin/end and a scalar consumer.

## Comparison and scope

Job `20260910T214554Z-8115dc1e`, capture
`af289242bd37f457f8befe66f7813c1f611ca0b88d723510eb2381d2246b7c20`,
keeps the [forced scalar-run traversal](expressions.md#scalar-runs-and-remaining-gaps)
and changes only its native materializing sink. Working widths and native read
regions stay independent of output width. The output remains exact u32/u64
storage, with no padding or additional source reads.

The selected sink emits `vmovdqu64` from a YMM plus a memory-destination
`vextracti64x4` for each 64-byte output portion. A 32-byte portion emits just
the low store. Nonvolatile inline assembly declares the exact two 32-byte
output spans and vector inputs; it has no memory read, global memory clobber,
register/flags modification or CPU fence. This is a candidate ordinary storage
policy, not atomic publication. Earlier local witnesses include registers31
and both assembler dialects. The policy containing an empty compiler memory
barrier is not this timed candidate.

The frozen scalar-run benchmark relinks byte-identically. Only its two candidate
objects are replaced; the same runtime caller and selector objects, libraries,
source cache and other benchmark objects are retained. Binary order is
before/after/after/before, with ordinary/authored provider order reversed in the
last two blocks. Each case has three sequential 30 ms repetitions per process,
pinned to Zen CPU0.

The [audit and retained observations](evidence/materialize-region-20260910/20260910T214554Z-8115dc1e/provenance.json)
cover 516 source records, native AVX2/AVX512 sink and expression guards,
baseline identity, unchanged cached sources and all 2,700 samples with independent
runtime counter/checksum reconstruction. The actual worker AVX2 instruction and
relocation assembly is identical to the frozen callback. All four AVX512
callbacks have only 32-byte native output stores, no inner calls and no vector
stack staging. The scalar timed caller remains the same compiled object.

## Result and limits

| Aligned origin16/count16 case | Frozen native sink | Explicit 32-byte sink | Ordinary in new program |
| --- | ---: | ---: | ---: |
| AVX512 Striped12/u32 | 5.173 | 2.574 | 2.820 |
| AVX512 Striped28/u32 | 5.650 | 3.271 | 3.425 |
| AVX512 Striped12/u64 | 2.659 | 2.684 | 3.124 |
| AVX512 Striped28/u64 | 3.430 | 3.436 | 3.349 |

Numbers are median CPU ns/query. Both u32 cases improve with separated samples
against both the frozen sink and ordinary decoder. The roughly 50.3%/42.1%
recovery is useful evidence for a selected short-output implementation. It is
not a direct measurement of store-forwarding latency, nor a rejection of wider
working values. The u64 cases do not share that recovery: Striped12 has a small
separated loss against its frozen sink; Striped28 overlaps.

Across the 34 changed AVX512 comparisons, six medians improve: five separated
wins, 12 separated losses and 17 overlaps. The five wins include mixed
Striped12/u32, Striped12/u64 and Striped28/u32, but those mixed cases remain
slower than ordinary decoding. Across all 68 comparisons, the unchanged AVX2
callbacks add 19 separated losses and no median improvements. Those movements
are retained, not assigned to execution of the AVX512 store policy. The
[complete paired table](evidence/materialize-region-20260910/20260910T214554Z-8115dc1e/frozen-recovery.csv)
keeps every case and both ordering edges.

Against ordinary in the new program, only 15 of 68 medians improve, with
12 separated wins, 45 separated losses and 11 overlaps. Shifted Striped28/u64
still takes 15.383 versus 9.167 ns/query at origin17/count16. The adjacent
context table also retains 36 separated losses and 17 separated wins among
157 ordinary/control cases across programs. No aggregate regression allowance
is inferred from the two strong aligned wins.

The next native-edge comparison keeps the original full native sink, so its
boundary result remains separable from this store change. A later combination
must be measured as an actual whole callback; composing useful source snippets
does not guarantee the same emitted instructions or timings.
