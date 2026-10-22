# Native predicted prefixes

Native masked reduction roughly halves the measured prediction point cost.
It does not make the encoded point path competitive with a native column.
The [preparation findings](FINDINGS.md) remain a separate, earlier cohort.

The new prefix reads complete sixteen-row SeriesPack packets and reduces them
with maintained native mask/sum operations. The final partial packet contributes
only requested lanes. The owner admits the entire 256-row delta plane: the
prefix starts at a native16 boundary and cannot cross that block. Up to fifteen
extra residuals can be read inside the grant. This is **not** an exact-tail
helper for arbitrary run/checkpoint arrays; their occupied final tile may contain
only eight readable rows. Empty requests read no source.

The materialised control and native program use the **same emitted getter**.
A prepared flag chooses only the delta-prefix leaf in the shared width catalogue.
Point/run/patch paths do not get separate getter bodies. The materialised leaf
retains a typed SeriesPack range decode and u64 scratch; the native leaf has
neither scratch nor scalar partial decoding. Both sum modulo u64, as prediction
reconstruction requires. An exact u128 aggregate must not silently adopt this law.

## Same-body result

Matched-delta points, ns/request, 52 samples per cell:

| Column | Checkpoints | Materialised prefix | Native prefix |
| --- | --- | ---: | ---: |
| Jena pressure | Native every16 | 24.86 | 12.33 |
| Jena pressure | Packed every16 | 26.43 | 13.20 |
| Jena pressure | Native every64 | 35.45 | 18.85 |
| Jena pressure | None | 61.72 | 29.11 |
| Jena temperature | Native every16 | 24.65 | 11.92 |
| Jena temperature | Packed every16 | 26.30 | 12.84 |
| Jena temperature | Native every64 | 34.97 | 18.48 |
| Jena temperature | None | 58.69 | 25.35 |

Two processes reverse input/arm order; 26 rotated trials cover all 26 arms, with
four batches of 4,096 independent points per sample. Images, actual bytes,
queries, consumption and arithmetic law match within this cohort. All nine
input cases, both run geometries, native controls and losing cells remain in the
[selected samples](evidence/native-prefix-local/accepted/summary.json).
Native checkpoints still cost least point work here; this does not establish
one policy for mixed workloads or another CPU.

For whole pressure/temperature images with native checkpoints, the common getter
falls from 24.16/20.15 to 11.81/12.55 ns. Native global FOR remains 0.52/0.51 ns.
Thus reduction roughly halves this reconstruction cost, but width/kind dispatch,
prediction dependencies and run search remain real work. Native/plain forms and
execution grain stay essential choices. No Frame default is selected.

An earlier template-getter cohort also improved predicted points, but changed
non-delta prologues and produced variation on unrelated kinds. It is archived
as a diagnostic. The accepted getter has one 112-byte frame; the 2 KB scratch
exists only inside the materialised delta leaf. Linked inspection checks that
both programs share that getter and the native K2 leaf contains the native
reduction without a decoded buffer. This is not a whole-machine cost model.
Other local timing movement does not establish an algorithmic change on the
shared non-delta paths.

The catalogue now has four pointers per width (point, range and two prefix
programs): 2,080 bytes once per process. Readers add one prepared flag. Do not
reuse the first cohort's exact capacity totals; each sample records its actual
accounting. Sparse exception bindings remain pending shared-owner integration.

## Checks and identity

The accepted experiment freezes material `58a344ae`, with explicit patched
widths and five kinds, under source digest
`d038a38480c746d904bd63632b4c99576034c3f0b0f362d8532a3f5ac42d671a`.
The new optional delta-with-exceptions kind has separate reader qualification
pending. 282,327,616 exact points pass across 59 original columns, every width,
all checkpoint modes, both prefix programs, coarse masks, K63 wrapping checkpoints
and protected tails/none sources.

The [recovery bundle](evidence/native-prefix-local/artifact.json) includes both
cohorts' binaries/raw samples, selected linked disassembly and exact derivative
source replacements against the already retained shared source. The
[recovery instructions](evidence/native-prefix-local/REPRODUCE.md) explain those
references; source trees are not duplicated in this archive.

Pinned Clang 21.1.8, release O3/native ISA/generic tuning, vCPU7 on the
Apple/OrbStack AArch64 VM; automatic vectorisation enabled. Each arm warms its
actual request batch. All images coexist. This bounds the warm request set,
not full-image cache residence. VM scheduling/background work limits close
rankings. No cold/Loom, serial-latency or fleet policy is established.
