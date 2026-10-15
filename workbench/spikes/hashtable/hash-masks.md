# Removing a hash-count dependency in source

The ordinary x86 lowering of a constant 40-bit mask can use a byte write to
the count register followed by BZHI. In some complete lookup loops, the rest of
that register still comes from the preceding result checksum. The hardware
dependency defeats the independent-query workload. A previous matched binary
intervention confirmed the effect in two loops; the geometry and selector
cohorts expose it again. A source remedy must charge any additional masks,
register pressure, spills and lost compiler simplification.

`hash_mask.h` supplies four experiment controls. Only the masking expression
inside `permutation::split` changes. Seeds, permutation, layout, fingerprint
execution, key generation and Abseil remain unchanged. The ordinary expression
is still the default; the source helpers are not selected policies.

| Control | Mask lowering | Consequential cost |
| --- | --- | --- |
| ordinary | Ordinary C++ expression | Compiler may leave a partial count definition |
| split | Separate pure MOVL and BZHI asm | Invariant count can hoist, with an additional live register |
| paired | MOVL immediately followed by BZHI | Count definition repeats, with a shorter live range |
| and | Full-register AND with the constant mask | Mask register and two-address operation replace the count dependency |

The explicit controls apply only to nonconstant inputs at widths 33–63 on BMI2
x86. Other widths, constant evaluation and NEON retain ordinary arithmetic.
Opaque operands guarantee full-register liveness; proven upper-zero result bits
remain visible to the caller. The helpers can still prevent merging the query
producer's mask with the permutation's mask. That cost is deliberately retained.
The [earlier standalone source probe](evidence/bzhi-control/source-probe/README.md)
documents this risk; those isolated instructions do not predict whole-table speed.

`hash_mask_check.cpp` compares actual parts and inverse reconstruction against
ordinary arithmetic for all eight key domains, several seeds and bucket widths,
zero/high-bit boundaries, exhaustively for narrow domains and randomized wider
inputs. All four controls pass under AVX2 emulation and in the native cohort;
these are correctness checks, not timings. The unchanged NEON branch passes its
checks and 36 complete-operation fallback histories. Native histories additionally
check independent contents, misses, replacement, deletion and memory inventories.

`mask-native.sh` uses K40 sets and two-byte maps across every fixed-cardinality
geometry. It interleaves all four separately compiled source controls and reverses
process order for a second pass. Selectors remain 16-byte searches, composed
field access remains fixed, and ordinary/exact-key Abseil stays in every binary.
The report requires identical physical geometry, tier populations and logical
memory across controls. The [native receipt](evidence/mask-native/README.md) now
qualifies all 51,840 rows, 1,152 fresh processes and 2,304 read/update regions on
Zen5 and Granite Rapids. Source and macro identities, timed binaries, archived
assembly and every report replay agree. All phase inventories match across
variants. Both workers have terminated. This call-boundary audit is distinct
from the targeted instruction/dependency analysis needed before adoption.

At K40/W2 A9/U128 and 6,144 keys, the independent-hit anomaly improves with every
explicit source control while physical storage and 16-byte selectors stay fixed:

| Host | Ordinary | Split MOVL/BZHI | Paired MOVL/BZHI | Full-register AND |
| --- | ---: | ---: | ---: | ---: |
| Zen5 | 8.219 ns | 2.959 ns | 2.969 ns | 2.905 ns |
| Granite Rapids | 11.985 ns | 3.860 ns | 3.931 ns | 3.840 ns |

This is a whole-source intervention, not the earlier address-preserving binary
patch. Already-fast A10/U128 map hits change from 2.897 ns to 3.012/2.934 ns on
Zen5 and 3.699 ns to 3.753/3.713 ns on Granite for split/paired. Their dependent
reads also become slightly slower. Across the finite candidate cells, median
split/paired hit and serial ratios are roughly 1–2% above ordinary, alongside
the large outlier gains and larger individual regressions. Such aggregate
descriptions do not select a control; the retained separate passes and all
operation results are the evidence for any particular case.

The AND control creates new severe set-hit regressions: at A11/U64, it costs
2.82 times ordinary on Zen5 and 3.05 times on Granite. It is unsuitable as a
blanket substitution. The [targeted instruction review](evidence/mask-diagnostic/review-index.json)
finds a remaining producer BZHI whose partial count definition retains the
previous query's selector mask. This is a different carried dependency from the
seeded-mask expression being changed. It does not establish an inherently
expensive AND operation or isolate how much of the regression this edge causes.

In the selected map paths, all three source controls remove the original
checksum-to-count edge. They retain an additional producer mask: split hoists
the full count but spills/reloads it each query; paired defines it immediately
before BZHI; AND keeps its mask in a register. The selected primary-success paths
nevertheless contain slightly fewer total instructions than ordinary. Neither
extra masking nor static instruction counts alone explain their small timing
differences. The review retains exact paths and recovery identities; these are
whole-source interventions with changed scheduling and register allocation.

The split/paired A9 map cases still cost roughly 1.37–1.51 times their own
ordinary Abseil controls. Removing the implementation anomaly therefore leaves
a substantive container read gap. No helper is adopted as the default.

This experiment addresses an implementation defect, not the wider design target.
It does not close the broad lookup gap, select a hash family, qualify set serial
latency, or replace the remaining multimap, worker, growth and residence work.
