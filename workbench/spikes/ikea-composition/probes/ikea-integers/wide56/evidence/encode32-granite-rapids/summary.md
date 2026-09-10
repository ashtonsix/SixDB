# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 66.892 | 66.677–67.306 | 256.313 | 1.578 |
| bulk / decode | 32768 | local_aos7 | 42.385 | 42.093–42.407 | 161.708 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 42.678 | 42.525–42.796 | 163.518 | 1.007 |
| bulk / decode | 32768 | prior_shape | 103.132 | 101.615–103.441 | 379.056 | 2.433 |
| bulk / encode | 32768 | fixed_planes | 66.810 | 66.704–67.155 | 256.412 | 1.531 |
| bulk / encode | 32768 | local_aos7 | 43.624 | 43.504–43.773 | 167.069 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 42.638 | 42.579–42.884 | 163.515 | 0.977 |
| bulk / encode | 32768 | prior_shape | 79.808 | 79.176–81.887 | 304.912 | 1.829 |
| bulk / sum | 32768 | fixed_planes | 67.380 | 67.171–67.458 | 258.450 | 3.795 |
| bulk / sum | 32768 | local_aos7 | 17.757 | 17.641–18.121 | 65.113 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 12.897 | 12.798–13.005 | 49.371 | 0.726 |
| bulk / sum | 32768 | prior_shape | 109.266 | 108.796–109.738 | 405.547 | 6.154 |

The generic cache-miss event reported zero throughout this run; treat it as uninformative.

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
