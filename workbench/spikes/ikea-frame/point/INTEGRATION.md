# Point gains survive the Intervals integration

The native residual-prefix program still roughly halves predicted-point cost
on the two Jena columns after removing the retired compact-run control and
adding dictionary composition. It remains far behind direct native-column
points. This closes the integration check; it does not select one encoding
or checkpoint policy for Frame.

This cohort uses one immutable source, maintained Intervals over the original
u8 end-minus-one run projection, four per-image checkpoint options, and the same
emitted shared getter for materialised/native prefix arms. The original
[native-prefix findings](NATIVE-PREFIX.md) explain the arithmetic and read grant;
their earlier samples are kept separately.

Matched delta bodies, all coarse blocks selected, median ns/request:

| Column | Checkpoints | Materialised prefix | Native prefix |
| --- | --- | ---: | ---: |
| Jena pressure | Native every16 | 22.97 | 10.95 |
| Jena pressure | Packed every16 | 24.61 | 11.95 |
| Jena pressure | Native every64 | 33.47 | 16.78 |
| Jena pressure | None | 58.11 | 26.87 |
| Jena temperature | Native every16 | 22.77 | 10.58 |
| Jena temperature | Packed every16 | 24.25 | 11.64 |
| Jena temperature | Native every64 | 32.07 | 16.64 |
| Jena temperature | None | 54.32 | 24.27 |

The corresponding native global FOR controls cost .46/.43 ns for pressure and
temperature. Across their complete images, native-prefix/native16 costs
11.05/11.67 ns versus 22.09/18.68 for the materialised leaf. Both original
common-body queries and whole-image choices remain in the
[complete samples](../evidence/interval-consumers-local/point/summary.json).
No reduction trick removes descriptor/width/kind dispatch, prediction
dependencies or run search. Plain/native forms and larger execution grain
remain essential alternatives.

All twelve inputs, native controls, both coarse masks and all fourteen arms
are retained, including the large whole-image gaps: native-prefix/native16
OS 21.45, ResolutionWidth 24.55, RegionID 28.69 and UserID 30.03 ns/request,
versus native FOR .84/.87/.92/1.43. These run-heavy cases are a real remaining
cost, not hidden by averaging them into the predicted-column improvement.
Ikea HELPER owns the separate native audit of the actual run projection;
these samples do not time a changed Intervals search policy.

The reader's default admission accepts logical values. Dictionary use is
explicit: a stored-code reader composes with `dictionary_points`, which looks
up the block's own edition after reading its code. Plain getters keep their
existing hot branch structure. Mixed raw/coded parts and sorted/frequency
editions are checked against originals; this cohort's timed images use the
default dictionary-none option. Column bindings, shared preparation and
logical/resident image bytes are reported separately for every arm.

## Qualification and limits

Pinned Clang 21.1.8, O3/native ISA/generic tuning, assertions, automatic
vectorisation, Apple/OrbStack AArch64 VM, vCPU7. Four batches of 4,096 independent
points per sample, fourteen rotated trials in each of two processes, reversed
input/arm order: 28 samples per cell. Each arm warms its actual request batch;
this bounds the warm request set, not whole-image cache residence. Every
complete u128 result is consumed and checked. Same-body residual/base bytes
match across checkpoint choices. No serial, cold/Loom or native-fleet claim.

Every original point on 59 columns passes for default, delta-exception,
sorted-dictionary and frequency-dictionary modes: respectively 142,828,096,
142,846,528, 142,864,960 and 142,864,960 comparisons. The literal exception suite
adds 303,104 checks across widths 1..64, all checkpoint options, first/last and
255 exceptions, wrapping checkpoint sums and original coarse coordinates.
The retired duplicate geometry is no longer counted twice; previous counts
and its measurements remain historical. Protected checks retain the none-mask
and exact-tail obligations.

Source capture
`ed97e4bd447bc82e4f4df380002ef2ce85b6d9385cc233f57a3b6a8b641d2df0`
uses encoding core f760ca39. Later borrowed-block/catalogue changes are not
implicitly measured. [Recovery and exact identities](../evidence/interval-consumers-local/REPRODUCE.md)
include binaries, raw results, compiler inputs and linked getter/prefix assembly.
