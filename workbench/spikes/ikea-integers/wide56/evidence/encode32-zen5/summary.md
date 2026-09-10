# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 26.025 | 25.576–26.095 | 115.880 | 1.065 |
| bulk / decode | 32768 | local_aos7 | 24.435 | 24.344–24.624 | 108.853 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 28.604 | 28.443–28.671 | 127.707 | 1.171 |
| bulk / decode | 32768 | prior_shape | 47.729 | 47.719–47.877 | 212.880 | 1.953 |
| bulk / encode | 32768 | fixed_planes | 18.759 | 18.358–19.501 | 83.322 | 0.818 |
| bulk / encode | 32768 | local_aos7 | 22.919 | 22.757–23.103 | 102.164 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 19.910 | 19.804–20.183 | 88.623 | 0.869 |
| bulk / encode | 32768 | prior_shape | 30.804 | 30.729–30.971 | 137.136 | 1.344 |
| bulk / sum | 32768 | fixed_planes | 21.509 | 21.453–22.441 | 95.852 | 2.027 |
| bulk / sum | 32768 | local_aos7 | 10.610 | 10.545–10.935 | 47.156 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 14.078 | 13.981–14.267 | 62.503 | 1.327 |
| bulk / sum | 32768 | prior_shape | 31.800 | 31.769–31.877 | 141.823 | 2.997 |

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
