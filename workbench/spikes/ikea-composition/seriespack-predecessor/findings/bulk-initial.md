# SeriesPack bulk findings

The [coherent full-width checkpoint](coherent-20260910.md) reports the later
three-machine results and remaining gaps. The selected follow-ups below now
reach the module; the historical checkpoints retain their original measurements.

## Selected follow-ups, 10 September

Local4's AVX2-only encoder now projects the final transpose directly onto its
four surviving output planes. The 32-value grain and exact stores are unchanged;
GFNI and AVX-512 builds keep their existing paths. The
[public paired comparison](../../../packed-integer-kernels/local4/hardware.md) supports
this narrow change: the 8,192-value u8 medians improve about 13% on Zen and 12%
on GNR. All affected public medians improve across the tested extents, while the
retained unchanged controls limit interpretations of the smaller headed gains.

NEON Local2/u8 encoding now uses the existing 64-value coalescing helper that
already serves Local3..7. The [V2 comparison](../../../packed-integer-kernels/local2/v2-decision.md)
shows about 18–20% lower bound-encode time at 256, 8,192 and 65,536 values.
Other carriers, strided tiles, decode and exact remainders retain their paths.
The integrated headers exactly match the measured candidates. Fresh NEON public
and range checks pass all 206 descriptions, and the AVX2 payload checker passes
under QEMU; [integration evidence](../../../packed-integer-kernels/evidence/selected-kernels-20260910/integration.json)
records the scope and source identities. These checks add no new timing claim.

The current Scan4 candidate remains deferred. The
[callsite-inline follow-up](../../../packed-integer-kernels/neon-scan/inline-decision.md)
repeats its 256-value public regression at 3.66%; the inline arm worsens it to
15.98%. Its retained V2 public pairing
improves larger encodes but slows a 256-value encode by 3.5%, even though its raw
kernel improves. A newly outlined typed-encoder call is a concrete hypothesis
for a separate public-call comparison, not an established explanation or a
reason to accept that loss. Scan6's earlier evidence still does not support
spreading the same grouping policy.

## Initial bulk checkpoint

The full-width implementation has native correctness coverage, but its first
bulk measurements exposed physical losses worth fixing. This note keeps that
checkpoint separate from later, narrowly measured changes. The
[workload contract](../workloads.md) defines what is timed; the
[capture index](../evidence/bulk-initial/capture.json) identifies five original runs
and recoverable source/binary artifacts. [All 4,410 native case records](../../archive/validation-20260911.md)
retain per-case medians/ranges and available controls. Raw repetitions, scalar
endpoints and missing-carrier controls remain as captured, not inferred.

The runs cover all 206 legal descriptions across k=1..64, encoding and decoding
8,192 values with u64 and the smallest sufficient unsigned carrier. Zen 5 and
Granite Rapids have separate AVX2-only and full-feature captures; Neoverse V2
uses NEON without SVE2. Full-feature x86 binaries also register AVX2 endpoints.
Their instruction ceiling is the full profile, and comparisons with the
AVX-512 predecessor are not ISA-matched. The table leaves those ratios empty.

## Primary predecessor gaps

LocalPack/ScanPack are the immediate controls for narrow u8 codecs; the earlier
width-56 body supplies the wide local control. The initial implementation had
failed to preserve some of their useful physical choices. Their direct inclusion
in the benchmark exposed this clearly.

| Initial case | Machine/profile | SeriesPack ns/value | Time / immediate predecessor |
| --- | --- | ---: | ---: |
| Local7 u8 encode | V2 NEON | 0.2477 | 1.430× |
| Local7 u8 decode | V2 NEON | 0.2222 | 1.322× |
| Local3 u8 encode | V2 NEON | 0.2200 | 1.273× |
| Striped4 u8 decode | V2 NEON | 0.0204 | 1.403× |
| Striped6 u8 encode | V2 NEON | 0.0212 | 1.248× |
| Local4 u8 encode | Zen AVX2-only | 0.0534 | 1.204× |
| Local4 u8 encode | GNR AVX2-only | 0.0612 | 1.184× |

Follow-up shared-buffer comparisons separate the physical work from differences
between independent fixtures. For example, the first Striped4 decode ratio
does not establish a 40% loss in the leaf algorithm: the paired V2 raw-native
versus predecessor-region result is about 8%. The region size and public call
path need to be tested separately.

The supported NEON Local3..7/u8 change processes 64 values across adjacent
eight-value wire tiles. It recovers the predecessor's useful grouping while
retaining exact pair/single remainders. Paired V2 encodes improve 9–27% and
decodes 6–24% across those widths. A 256-value region adds little and is not
selected. The [coalescing decision](../../../packed-integer-kernels/neon-local/v2-decision.md)
retains every affected case, size/access checks and the measured limits. These
paired savings must not be multiplied into the table's separately captured
baseline to invent a final endpoint result.

## Broader controls and conditional choices

Calico supplies additional full-width comparisons, including cases for which
there is no immediate predecessor. Its constant-shape adapter and differing
layout are explicit in the measurement contract. It remains useful for finding
weak physical choices, without making its representation a SeriesPack contract.

- AVX2-only Local1/u64 decode initially takes 1.60× Calico's local control on
  Zen and 1.33× on GNR. The paired direct-bit-expansion experiment supports a
  32-value dense region and direct one-bit fragments without GFNI. With GFNI,
  only dense u16 materialization wins consistently enough to select across both
  hosts. [The Local1 decision](../../../packed-integer-kernels/local1/README.md) records the
  narrower feature conditions. Its old H16 auxiliary fixture used a different
  head organization and is excluded from SeriesPack endpoint conclusions.
- V2 Local8/u64 encode initially takes 1.43× the byte-plane control. A measured
  64-value loop around the existing exact eight-value gathers saves 23–27%
  across three extents. [The byte-body decision](../../../packed-integer-kernels/neon-byte/v2-decision.md)
  retains the remaining gap and distinguishes payload encoding from the separate
  all-head traversal. It introduces no new gather algorithm.
- Zen's full-feature headed encodes have larger whole-layout gaps: Striped
  K17/H16/u64 takes 0.1828 ns/value versus 0.0692 for Calico, about 2.64×.
  The captured Calico `Shape` also stores two leading byte planes for K17/K23;
  the number of head planes does not explain the loss. Calico places these in
  each 256-value cell, while the SeriesPack fixture places whole-array heads
  separately. Exact fixed-shape lowering, joint head extraction and the cost of
  narrow stores remain under investigation. The whole-operation ratio alone
  identifies neither the cause nor the benefit of any particular change.

General loop unrolling did not produce consistent wins across extents and
hosts. A promising single count is insufficient grounds for spreading an
unroll policy. The separate Zen K10/H8/u16 decode anomaly is also not evidence
for replacing an identical algorithm with an AVX2-labelled copy: a linker-only
placement experiment reproduces the slowdown with unchanged instructions and
constants. [The seam findings](../../native-regions/findings.md) retain that causal experiment
and its limited microarchitectural interpretation.

The later [headed dense-region comparison](../../../seriespack-head-projection/reader/zen-findings.md)
finds consistent Zen wins for a 64-value AVX-512 region, while some smaller
AVX2 cases regress. It remains an isolated candidate pending GNR and actual
bound-endpoint measurements; it does not close the earlier anomaly by itself.

## Completion evidence

These are diagnostic checkpoints, not final acceptance of every width/target.
The next coherent implementation capture must repeat affected endpoints and
ordinary controls, retain exact ISA/source identities, and expose remaining
material losses individually. Local fixes do not establish results for headed
layouts, other carriers or array sizes. Correctness checks and byte-identical
authoring wrappers do not supply the missing performance evidence.

Bulk results also say little about arbitrary small ranges: the initial
[access matrix](access-initial.md) finds much larger losses against the same
predecessors. Native consumer grain and horizontal reduction frequency have
their own [composition findings](../../native-regions/findings.md). These are distinct costs
whose boundaries matter to a composition-ready implementation.
