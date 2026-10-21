# Hot mixed-hit diagnosis

Zen 5 AVX-512, fixed memory-matched whole-table geometry, n=13,926. Medians
across three seeds, two fresh processes/seed, three one-million-request
intervals/process. Every PMU group has 100% running coverage and is unscaled.
All order controls retain the same query multiset and within-class order.

## Full consumer: time and branch misses

Each cell is **ns/lookup / retired branch misses per lookup**. Lower is better.
Full consumers count hits and hit keys; maps also consume the complete value.

| W / value | Table | Random 50% | Alternating 50% | Runs of 64 | Grouped 50% | All hit | All miss |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 32 / set | ikea | 6.933 / 0.5013 | 1.023 / 0.0003 | 1.033 / 0.0007 | 1.030 / 0.0003 | 0.969 / 0.0001 | 1.093 / 0.0004 |
| 32 / set | boost | 6.145 / 0.5293 | 1.313 / 0.0217 | 1.461 / 0.0393 | 1.286 / 0.0220 | 1.408 / 0.0157 | 1.166 / 0.0285 |
| 32 / set | abseil | 6.423 / 0.5286 | 1.390 / 0.0290 | 1.572 / 0.0501 | 1.423 / 0.0312 | 1.259 / 0.0032 | 1.556 / 0.0559 |
| 32 / map8 | ikea | 7.114 / 0.5012 | 1.288 / 0.0003 | 1.307 / 0.0007 | 1.276 / 0.0003 | 1.416 / 0.0001 | 1.134 / 0.0004 |
| 32 / map8 | boost | 6.347 / 0.5297 | 1.541 / 0.0219 | 1.675 / 0.0396 | 1.491 / 0.0220 | 1.805 / 0.0156 | 1.169 / 0.0285 |
| 32 / map8 | abseil | 7.124 / 0.5260 | 1.663 / 0.0283 | 1.925 / 0.0461 | 1.772 / 0.0287 | 1.737 / 0.0031 | 1.798 / 0.0539 |
| 40 / set | ikea | 8.797 / 0.5003 | 2.251 / 0.0000 | 2.209 / 0.0000 | 2.200 / 0.0000 | 2.135 / 0.0000 | 2.263 / 0.0000 |
| 40 / set | boost | 6.159 / 0.5289 | 1.325 / 0.0199 | 1.449 / 0.0369 | 1.276 / 0.0204 | 1.434 / 0.0144 | 1.108 / 0.0257 |
| 40 / set | abseil | 6.294 / 0.5295 | 1.399 / 0.0303 | 1.593 / 0.0521 | 1.432 / 0.0314 | 1.265 / 0.0039 | 1.581 / 0.0581 |
| 40 / map8 | ikea | 9.156 / 0.5013 | 2.996 / 0.0007 | 2.972 / 0.0012 | 2.905 / 0.0007 | 3.076 / 0.0002 | 2.732 / 0.0011 |
| 40 / map8 | boost | 6.346 / 0.5272 | 1.505 / 0.0204 | 1.639 / 0.0371 | 1.459 / 0.0210 | 1.751 / 0.0157 | 1.143 / 0.0258 |
| 40 / map8 | abseil | 6.636 / 0.5276 | 1.672 / 0.0297 | 1.919 / 0.0477 | 1.771 / 0.0305 | 1.754 / 0.0039 | 1.783 / 0.0564 |

## Membership-only consumer

Each cell is **ns/lookup / retired branch misses per lookup**. No hit-key
sum or mapped-value consumption occurs in this loop.

| W / value | Table | Random 50% | Alternating 50% | Grouped 50% |
| --- | --- | ---: | ---: | ---: |
| 32 / set | ikea | 6.926 / 0.5012 | 1.011 / 0.0003 | 1.008 / 0.0003 |
| 32 / set | boost | 6.282 / 0.5292 | 1.317 / 0.0224 | 1.297 / 0.0235 |
| 32 / set | abseil | 6.213 / 0.5277 | 1.313 / 0.0288 | 1.347 / 0.0292 |
| 32 / map8 | ikea | 7.033 / 0.5013 | 1.049 / 0.0003 | 1.045 / 0.0003 |
| 32 / map8 | boost | 6.450 / 0.5308 | 1.431 / 0.0223 | 1.376 / 0.0228 |
| 32 / map8 | abseil | 6.428 / 0.5275 | 1.469 / 0.0286 | 1.527 / 0.0293 |
| 40 / set | ikea | 8.668 / 0.5003 | 2.195 / 0.0000 | 2.162 / 0.0000 |
| 40 / set | boost | 6.232 / 0.5273 | 1.334 / 0.0211 | 1.289 / 0.0213 |
| 40 / set | abseil | 6.219 / 0.5285 | 1.317 / 0.0302 | 1.356 / 0.0309 |
| 40 / map8 | ikea | 8.991 / 0.5005 | 2.663 / 0.0006 | 2.561 / 0.0007 |
| 40 / map8 | boost | 6.371 / 0.5281 | 1.413 / 0.0209 | 1.385 / 0.0209 |
| 40 / map8 | abseil | 6.422 / 0.5287 | 1.411 / 0.0295 | 1.452 / 0.0309 |

## Instruction work versus predictor loss

Full consumer; **cycles / retired instructions / branch misses**, per lookup.
These are observed loop counts, not a raw architectural misprediction penalty.

| W / value | Table | Random 50% | Alternating 50% |
| --- | --- | ---: | ---: |
| 32 / set | ikea | 31.09 / 26.52 / 0.5013 | 4.58 / 26.52 / 0.0003 |
| 32 / set | boost | 27.57 / 27.93 / 0.5293 | 5.89 / 27.67 / 0.0217 |
| 32 / set | abseil | 28.80 / 29.25 / 0.5286 | 6.22 / 29.25 / 0.0290 |
| 32 / map8 | ikea | 31.92 / 32.02 / 0.5012 | 5.77 / 32.02 / 0.0003 |
| 32 / map8 | boost | 28.49 / 29.45 / 0.5297 | 6.91 / 29.20 / 0.0219 |
| 32 / map8 | abseil | 31.97 / 32.75 / 0.5260 | 7.44 / 32.75 / 0.0283 |
| 40 / set | ikea | 39.47 / 58.50 / 0.5003 | 10.09 / 58.50 / 0.0000 |
| 40 / set | boost | 27.64 / 26.91 / 0.5289 | 5.93 / 26.66 / 0.0199 |
| 40 / set | abseil | 28.23 / 30.26 / 0.5295 | 6.27 / 30.26 / 0.0303 |
| 40 / map8 | ikea | 41.09 / 69.54 / 0.5013 | 13.43 / 69.54 / 0.0007 |
| 40 / map8 | boost | 28.47 / 28.44 / 0.5272 | 6.75 / 28.19 / 0.0204 |
| 40 / map8 | abseil | 29.78 / 34.76 / 0.5276 | 7.51 / 34.76 / 0.0297 |

## Paired barriers at overflow-helper entry

Each cell is **ns/lookup / branch misses per lookup**. CPUID is included
only for W40/set, where every process has zero real spills and therefore
no required overflow-helper call. This avoids paying its large retired cost.
The helper barrier does not prevent all speculative call/stack/front-end
work before entry; it blocks the subsequent Swiss probe execution.

| W / value | Sink | Order | Baseline | LFENCE | CPUID |
| --- | --- | --- | ---: | ---: | ---: |
| 32 / set | full | random | 6.918 / 0.5016 | 6.921 / 0.5010 | — |
| 32 / set | full | alternating | 1.025 / 0.0004 | 1.009 / 0.0003 | — |
| 32 / set | full | grouped | 1.030 / 0.0003 | 1.030 / 0.0003 | — |
| 32 / set | hits | random | 6.927 / 0.5011 | 6.929 / 0.5014 | — |
| 32 / set | hits | alternating | 1.012 / 0.0003 | 1.014 / 0.0003 | — |
| 32 / set | hits | grouped | 1.009 / 0.0003 | 1.008 / 0.0003 | — |
| 32 / map8 | full | random | 7.115 / 0.5009 | 7.114 / 0.5013 | — |
| 32 / map8 | full | alternating | 1.287 / 0.0004 | 1.289 / 0.0004 | — |
| 32 / map8 | full | grouped | 1.273 / 0.0003 | 1.277 / 0.0003 | — |
| 32 / map8 | hits | random | 7.033 / 0.5012 | 7.038 / 0.5013 | — |
| 32 / map8 | hits | alternating | 1.049 / 0.0003 | 1.050 / 0.0003 | — |
| 32 / map8 | hits | grouped | 1.044 / 0.0003 | 1.044 / 0.0003 | — |
| 40 / set | full | random | 8.791 / 0.4998 | 8.794 / 0.5004 | 8.793 / 0.5004 |
| 40 / set | full | alternating | 2.251 / 0.0000 | 2.250 / 0.0000 | 2.251 / 0.0000 |
| 40 / set | full | grouped | 2.199 / 0.0000 | 2.200 / 0.0000 | 2.198 / 0.0000 |
| 40 / set | hits | random | 8.662 / 0.5006 | 8.658 / 0.5001 | 8.655 / 0.5004 |
| 40 / set | hits | alternating | 2.195 / 0.0000 | 2.194 / 0.0000 | 2.194 / 0.0000 |
| 40 / set | hits | grouped | 2.161 / 0.0000 | 2.162 / 0.0000 | 2.161 / 0.0000 |
| 40 / map8 | full | random | 9.147 / 0.5011 | 9.144 / 0.5010 | — |
| 40 / map8 | full | alternating | 2.996 / 0.0007 | 2.998 / 0.0007 | — |
| 40 / map8 | full | grouped | 2.904 / 0.0007 | 2.907 / 0.0007 | — |
| 40 / map8 | hits | random | 9.022 / 0.5010 | 8.974 / 0.5003 | — |
| 40 / map8 | hits | alternating | 2.669 / 0.0007 | 2.669 / 0.0007 | — |
| 40 / map8 | hits | grouped | 2.558 / 0.0007 | 2.562 / 0.0007 | — |
