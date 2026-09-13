# Bec256 checks

Use the [pinned Linux build](../../../BUILDING.md). Build only the checks needed
for the change; `ikea_validate` includes these targets and the three examples.

| Target | Behavior |
| --- | --- |
| `ikea_bec256_check` | Independent wire comparison, every population, singleton/complement positions, exhaustive 16-bit patterns, checked replacement and rejection, overlap, neighbour preservation and exact page-boundary access |
| `ikea_bec256_composition_check` | Two independent bodies, native and ordinary pair reads, shared inline/CPS bodies, early completion, exact native pair writes and source/destination byte overlap |
| `ikea_example_bec256_{ordinary,native,integration}` | Executable caller, native composition and coordinated body/metadata examples |

`reference.h` is a bit-at-a-time transcription of the fixed wire law, carried
from the [Bec256 probe](../../../workbench/spikes/ikea-composition/probes/ikea-blocks/README.md).
It recounts every region and enumerates byte patterns independently of production
tables, SIMD instructions and packing helpers. Preserve that independence when
changing the codec. The [representation guide](../../docs/bec256/representation.md)
defines the format; agreement with a changed implementation alone is insufficient.

Run the wire check on a native profile and an ordinary scalar fallback build.
AVX2-only x86 selects the latter. Pair checks and the native example state when
their native section is unavailable; their success does not imply an AVX2 native
composition profile. Sanitizers check bounded stores, malformed inputs and the
native continuation bridge as well as round trips.

`python3 ikea/test/headers.py BUILD --module bec256` checks each supported header
independently. The [composition study](../../../workbench/spikes/bec256-composition/README.md)
owns performance controls, bitstream-assembly experiments and the larger-bitset
metadata comparisons. Timing those experiments is separate from correctness.

`analysis.cpp` checks the statistical estimate against an independently extracted
feature/model fixture, including all populations and full/empty-byte permutations.
`predictor_reference.h` retains the frozen quadrant coefficients and independent
bitwise feature definitions from the original predictor study. Threshold checks
cover equality, disabled/zero cutoffs, population-error precedence, exact accepted
writes, preserved effects on declines and native pairs. Model conformance does
not imply an error bound; statistical accuracy belongs with the retained corpus
and structural holdout evidence.
