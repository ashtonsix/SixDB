# Initial observations — 2026-09-11

The first controls support investigating this design further. They do not select
a layout policy, establish an arbitrary-map implementation, or measure server
transactional performance. See the [experiment boundary](README.md#first-experiment).

## Carrier feasibility

Pinned Linux Clang 21.1.8 was used to cross-compile [abi.cpp](abi.cpp). Producers
are noinline and consumers call through function pointers, so inlining cannot
hide the calling convention. [Assembly and source hashes](evidence/abi/receipt.json)
are retained alongside the probe.

| Proposed carrier / boundary | Observed result |
| --- | --- |
| Scalar 8 bytes | Return in `rax` / `x0` |
| One 64-byte vector, AVX-512 | Return in `zmm0` |
| Struct of two 32-byte vectors, default x86 System V | Hidden return buffer; producer stores 64 bytes and opaque consumer reloads them |
| Same AVX2 pair, Clang `regcall` | Return in `ymm0,ymm1`; inspected consumer forwards without payload stores/reloads |
| Two separate AVX2 vector arguments to a consumer | Register arguments and a tail jump in this example |
| Four 16-byte vectors, AArch64 | Homogeneous vector aggregate returns in `v0..v3`; opaque consumer forwards without payload stores/reloads |

This agrees with the [x86 psABI aggregate classification](https://gitlab.com/x86-psABIs/x86-64-ABI/-/raw/master/x86-64-ABI/low-level-sys-info.tex),
[AAPCS64 homogeneous-vector rules](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
and [Clang's `regcall` description](https://clang.llvm.org/docs/AttributeReference.html#regcall).
The probe uses byte-vector compiler types with the relevant size and alignment;
their ABI classification matches the proposed intrinsic vector carriers.

AVX2 aggregate return under ordinary System V is therefore an assumption to
revise. `regcall`, flattened continuation arguments, and smaller native transfers
remain candidates. This small demonstration does not price register preservation
under a substantial live caller state or select a CPS convention. Metadata should
also be considered when checking a complete signature: changing an aggregate's
members can change its ABI classification.

## Byte-edge screen

The [retained samples](evidence/neon-initial/samples.csv) contain 546 cases, each
with five sequential repetitions on CPU 2 of the local Apple-hosted AArch64 VM.
The [receipt](evidence/neon-initial/receipt.json) records compiler, build-input
hashes, binary/source hashes and validation. This is one screening process, with
warm repeating independent row-ID traces, not a confirmation campaign. Ratios
below are medians across the 42 layout/selection combinations within each row
count and operation. Times are nanoseconds per tuple, not per materialized byte.

| Rows | Operation | Direct packed ns | Bound packed ns | Median bound/direct ratio |
| ---: | --- | ---: | ---: | ---: |
| 1,024 | Read → native sum | 0.646 | 1.007 | 1.55 |
| 1,024 | Read → 64-byte materialization | 0.667 | 0.995 | 1.49 |
| 1,024 | Replace selected codes | 0.406 | 0.992 | 2.44 |
| 65,536 | Read → native sum | 0.785 | 1.053 | 1.36 |
| 65,536 | Read → 64-byte materialization | 0.960 | 1.035 | 1.10 |
| 65,536 | Replace selected codes | 0.938 | 1.338 | 1.43 |

The packed fixture uses 16 bytes for 48 codes, versus 48 bytes unpacked. For full
materialization at 65,536 rows, the six packed layouts take 1.045–1.056 ns direct
and 1.059–1.091 ns bound, versus 1.674 ns for the inline unpacked control. That
is evidence of potential in a favorable cooperating map with reduced footprint.
It is not an arbitrary-schema speedup claim. In particular, this test asks for
no byte permutations and exercises only one tuple per native transfer.

The hoped-for simple ranking of byte positions is not established. At 65,536
rows the single-kind direct read/sum cases span only 0.700–0.719 ns across all
three widths and six orders, while their replacements span 0.937–0.943 ns.
Closely grouped timings in one local process cannot select a byte order for a
different CPU or workload. Actual instruction shape also matters: the inspected
high-edge partial writer can lower preservation plus insertion to NEON `SLI`,
instead of a separate mask, shift and OR. A cost model of isolated code extraction
would miss that opportunity.

The bound-call cost is visible even with register returns and shared bodies.
For the tiniest warm replacements its extra cost is roughly 0.6 ns, large as a
ratio of a 0.4 ns inline operation. The current fixed four-vector return also
requires establishing empty regions; the opaque sum consumer reduces all three
code regions, whereas the inline consumer can eliminate known-zero regions.
These ratios therefore include optimization across the call boundary, not just
the branch instruction. This argues for measuring useful transfer shapes and
whole consumer regions, alongside one-tuple calls. It does not establish that
each caller should inline everything or require a private calling convention.

Validation passed 10,752 reads and 2,752,512 partial/full writes against a
bit-at-a-time oracle, including all old/new byte patterns, code positions and
unselected-bit/neighbor preservation. The same checks passed Linux ASan/UBSan.
Native bodies assume admitted value widths and valid 16-byte spans; ordinary
failure behavior and effect/publication integration are not implemented here.

## A measurement correction

An earlier screening harness applied a memory-capable `DoNotOptimize` barrier
to every row pointer. This spilled the pointer beside the checksum. In some
instantiations a paired reload read both slots after separate stores; those cases
showed a large timing cliff absent from equivalent separately loaded cases. Removing
that per-row barrier and keeping the checksum sink in a register removed the
cliff; inspected corrected loops have no such stack handoff. The table and
retained timing samples above use only the corrected harness. No byte-order
recommendation is drawn from the earlier run. A store-forwarding hazard is the
suspected mechanism; it was not separately confirmed with hardware counters.

## What to test next

The most informative next comparison is a runtime-bound mixed map against
compiled cooperating controls on the same packed bytes: lane-local and cross-lane
permutations, low/high/interior codes, sparse selections, and partial writers
which merge several inputs into one destination byte. Give each map a real
consumer and compare point access with multi-tuple transfer shapes. Include x86
runtime measurements before selecting strategies; the present x86 evidence is
ABI assembly only. The scalar 8-byte interface also needs an executable control.

This would connect the mechanism to a small analyser: enumerate a tractable set
of layouts for one mixed schema, prepare its operation maps, measure actual
read/write recipes, and see whether candidate costs predict the best choices
under different workload mixes. An arbitrary-map baseline and targeted
specializations should inform the size of that search space. We have not chosen
runtime masks versus code generation, a general search algorithm, sensing
cadence, layout refresh threshold, or migration policy.

Useful prior reading for that question:

- [HYRISE](https://www.vldb.org/pvldb/vol4/p105-grund.pdf) connects workload-driven
  vertical grouping to cache-cost estimation and candidate search. Its mechanisms
  need recalibration for byte packing, SIMD recipes and our mutation contract.
- [PAX](https://www.vldb.org/conf/2001/P169.pdf) distinguishes placement within
  a physical group from the outer storage unit; useful for the proposed planes.
- [H2O](https://stratos.seas.harvard.edu/publications/h2o-hands-free-adaptive-store)
  is relevant to later adaptation of different data portions. Its per-query
  adaptation policy is not an assumed cadence for SixDB.
- [ByteStore](https://arxiv.org/abs/2209.00220) describes an experiment-driven
  advisor over column layouts. It is a lead for model calibration rather than
  evidence about this transactional primitive.
