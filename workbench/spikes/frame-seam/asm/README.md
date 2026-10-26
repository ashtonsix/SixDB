# Masked reductions: assembly findings

Full disassembly and vectoriser remarks are in [the recovery artifact](artifact.json).
The initial neutral-mask diagnostic is d9a2ea72; the exact-halves leaf comparison
is 9018305b. Both retain their source IDs in the archive. Raw listings are kept
out of the maintained source tree. Recover with:

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/frame-seam/asm/artifact.json build/recovered/frame-mask-assembly
```

The rejected summary used guarded MIN/MAX updates that LLVM did not recognise
as reductions. Using zero for SUM/MAX and UINT64_MAX for MIN makes the updates
unconditional and vectorisable. The neutral SUM already vectorised; its early
loss on one-third masks was a density crossover, not missing vector code.
The [primitive source](../mask/kernels.cpp) retains both formulations and sparse
controls. The [native landing](../landing/README.md#density-policy) qualifies the
actual production defaults and architecture-specific choices.

For AVX2 exact halves, the old width-64 masked body tests each row bit and
conditionally performs scalar `add`/`adc` into a u128 accumulator. The repaired
body expands bits to lane masks with vector shifts/comparisons, masks values,
separates low/high halves and uses `vpaddq`; weighted paths use `vpmuludq`.
There is no per-row branch. The initial outlined callback assembly still spills
accumulators across the callback, so native timing, not instruction count alone,
decides the result. The retained native leaf controls measure 0.29–0.38× old
time for wide weighted sums and 0.26× for plain width-64 exact SUM on Zen 5
AVX2. The all-zero width-64 masked case remains 1.15× slower in isolation.

The [typed-reader driver](../leaf/exact_halves.cpp) builds against either the
frozen old expression header or the current header, without an erased read
library replacing either body. Its independent oracles cover unweighted,
weighted, masked and weighted+masked sums, absolute weight origins, partial
ranges and both u128 halves. Timed calls have compiler memory barriers. NEON and
AVX-512 leaf bodies are unchanged; their timing differences are controls, not
claimed gains from the AVX2 repair.
