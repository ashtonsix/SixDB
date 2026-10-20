# Assembly scheduling estimates

Use the pinned LLVM tools on a literal selected assembler source. `mca.py`
assembles first, retains encodings/relocations and then collects raw text/native
LLVM JSON reports, warnings, exact commands and versions into a new directory.
`scope.txt` puts each region's modeled metrics beside its recognized calls,
returns, branches and LLVM unmodeled-side-effect markers. It creates no job or
required experiment schema.

```sh
python3 workbench/tools/mca.py region.s \
  --triple=x86_64-linux-gnu --cpu=znver5 \
  --mattr=+avx2,-avx512f --timeline --output build/mca/candidate
```

Run from Linux; prefix commands with `orb -m ubuntu` from the Mac. The output
must be new. Unsupported instructions and ignored CPU/features remain failures;
there is no skip-unsupported mode. Tool versions must match
[the compiler pin](../../cmake/ClangVersion.cmake).

For a configured `.S` translation unit, select its real macros/includes/ISA flags
before modeling. The existing probe preserves MCA comments while preprocessing:

```sh
python3 workbench/tools/compile_probe.py BUILD --source PATH/algorithm.S \
  --assembly --output build/mca/preprocessed
```

The result is `00-algorithm/source.s`. For `.cpp`, the same command emits compiler
assembler. Original objects, dependencies and Ninja logs stay untouched. Put
`// LLVM-MCA-BEGIN name` / `// LLVM-MCA-END` around the selected path in `.S`;
`-E -P -CC` preserves those comments. A literal `.s` can use `#` markers too.
[MCA markers](https://llvm.org/docs/CommandGuide/llvm-mca.html#using-markers-to-analyze-specific-code-blocks)
select regions without deleting the rest of the assembly's constants/helpers.
MCA analyzes each region as a literal repeated sequence, not an executed CFG.
Several mutually exclusive paths in one region would all be counted.

## What the pinned selectors actually model

Checked with LLVM **21.1.8** on October 20, 2026. The source references below are
pinned to that exact release; a recognized CPU name does not imply its own model.

| Intended target | Selector | Scheduling model / observed resources | Interpretation |
| --- | --- | --- | --- |
| Zen 5, AVX2 or AVX-512 | `znver5` | **Znver4Model**, `Zn4*` | Zen4 scheduling proxy with Zen5 feature selection; cannot establish Zen5's relative 256/512-bit performance. |
| Neoverse V2 / Graviton4 | `neoverse-v2` | NeoverseV2Model, `V2Unit*` | Corresponding backend model; still not a memory-system or whole-machine estimate. |
| Neoverse V3 | `neoverse-v3` | **NeoverseV2Model** | V2 scheduling proxy, not measured V3 costs. |
| Local Apple M1 Pro / OrbStack | `apple-m1` | alias apple-a14, **CycloneModel**, `CyUnit*` | Older proxy, not a quantitative M1 performance model or a Neoverse result. |

Sources: pinned [X86 processors](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/llvm/lib/Target/X86/X86.td),
[AArch64 processors](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/llvm/lib/Target/AArch64/AArch64Processors.td).
Local macOS reports Apple M1 Pro; the Linux guest reports Apple implementer0x61,
part0 and MCA cannot identify its host CPU. VM scheduling need not expose a stable
mapping to physical performance/efficiency cores. Select an explicit model.

For NEON, use `--triple=aarch64-linux-gnu --cpu=neoverse-v2 --mattr=+neon`
(or the explicitly named proxy being studied). For AVX-512 F/BW/VL use
`--mattr=+avx512f,+avx512bw,+avx512vl`; additional extensions must match the actual
code. Keep build ISA and tuning choices explicit and separate.

**x86 assembly is not ISA-checked by these flags.** The negative capability
control assembled a ZMM compare successfully with AVX-512 F/BW/VL disabled.
An AVX2 label therefore needs source/encoding review, including EVEX-encoded
XMM/YMM forms and other extensions, not just absence of ZMM register text.
`disassembly.txt` retains raw encodings. This helper does not invent a complete
instruction-feature validator.

## Read the result with its missing costs

- Block reciprocal throughput is a backend resource estimate without
  loop-carried dependencies. Synthetic total cycles also reflect dependencies
  created by the particular repeated register sequence and pipeline fill/drain;
  they are not independent-call latency.
- Calls retain LLVM's placeholder behavior (currently a100-cycle assumption),
  not the callee's body. Returns do not transfer the simulated PC; conditional
  branches have no prediction/mispredict probabilities. Inspect `scope.txt`,
  the complete linked caller/callees and raw warnings. A branch may be a tail call.
  Do not subtract a call placeholder and add an unrelated callee timing as if
  overlapping scheduling and ABI costs were an additive complete result.
- Frontend fetch/decode, cache/TLB hierarchy, demand/prefetch traffic,
  store-to-load forwarding, real allocator/ownership/callback work and waiting
  are outside this analysis. Saved registers and result buffers are instructions,
  but their real memory service is not inferred from the model.
- The helper explicitly defaults `--noalias=false`. `--noalias` selects LLVM's
  optimistic load/store non-aliasing assumption; show the assumption being used.
  Neither choice is a store-forwarding or address-based alias simulation.

LLVM's [MCA guide](https://llvm.org/docs/CommandGuide/llvm-mca.html) explains those
limits. For a cache-latency sensitivity case, pinned21.1.8 accepts an explicit
`// LLVM-MCA-LATENCY 100` before a selected instruction and
`// LLVM-MCA-LATENCY` to reset. That is a supplied cycle scenario, not inferred
residency. Count dependent address waves separately, vary hit/miss and branch
assumptions explicitly, and avoid adding latency already present in the model.
Keep these cases separate from the default backend estimate and live results.

An actual operand-topology trap is Engine's NEON `digest64`: it loads through
`x0` and then returns the digest in `x0`. Repeating that region without the
caller's pointer setup creates a cross-iteration address dependency. Its x86
counterpart loads through `rdi` and returns in `rax`, so the isolated repetitions
have different dependencies. Total cycles divided by iterations therefore do
not compare independent-call latency or throughput. Include the actual caller's
input setup, or explicitly choose the same intended dependency topology.

Include a callee body in a region only if its real ABI/data dependencies and the
selected path are represented. The directory's full-operation decision still
needs actual Ikea/Loom/Orbital calls, useful outputs, publication and cleanup;
the [assembly-call guide](../spikes/assembly-calls/README.md) gives the boundary.
Use [workers](workers.md), [memory conditioning](residency.md) and
[PMU qualification](pmu/README.md) for live evidence. No model result establishes
a completed directory operation or authorizes C++ graduation by itself.
