# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 25.428 | 24.380–25.602 | 112.968 | 1.041 |
| bulk / decode | 32768 | local_aos7 | 24.433 | 24.382–24.562 | 108.969 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 28.512 | 28.271–28.761 | 126.852 | 1.167 |
| bulk / decode | 32768 | prior_shape | 47.920 | 47.883–48.174 | 213.599 | 1.961 |
| bulk / encode | 32768 | fixed_planes | 19.108 | 18.561–19.345 | 85.261 | 0.783 |
| bulk / encode | 32768 | local_aos7 | 24.399 | 24.296–24.851 | 108.953 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 19.329 | 18.971–19.759 | 86.186 | 0.792 |
| bulk / encode | 32768 | prior_shape | 30.415 | 30.354–30.504 | 135.529 | 1.247 |
| bulk / sum | 32768 | fixed_planes | 21.482 | 21.426–21.605 | 95.834 | 2.029 |
| bulk / sum | 32768 | local_aos7 | 10.587 | 10.580–10.739 | 47.155 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 14.263 | 14.037–14.554 | 63.102 | 1.347 |
| bulk / sum | 32768 | prior_shape | 31.730 | 31.706–31.768 | 141.706 | 2.997 |
| capacity / dependent | 1024 | fixed_planes | 6.071 | 6.062–6.108 | 27.193 | 0.999 |
| capacity / dependent | 1024 | local_aos7 | 6.075 | 6.058–6.092 | 27.187 | 1.000 |
| capacity / dependent | 1024 | plain_u64 | 5.357 | 5.353–5.403 | 24.006 | 0.882 |
| capacity / dependent | 1024 | prior_shape | 6.341 | 6.327–6.502 | 28.423 | 1.044 |
| capacity / dependent | 65536 | fixed_planes | 8.725 | 8.717–8.824 | 39.086 | 1.031 |
| capacity / dependent | 65536 | local_aos7 | 8.459 | 8.446–8.509 | 37.891 | 1.000 |
| capacity / dependent | 65536 | plain_u64 | 7.921 | 7.859–8.107 | 35.278 | 0.936 |
| capacity / dependent | 65536 | prior_shape | 8.683 | 8.667–8.739 | 38.915 | 1.026 |
| capacity / dependent | 67108864 | fixed_planes | 181.145 | 180.151–182.080 | 807.288 | 1.154 |
| capacity / dependent | 67108864 | local_aos7 | 156.940 | 156.666–158.784 | 699.327 | 1.000 |
| capacity / dependent | 67108864 | plain_u64 | 156.121 | 155.638–158.600 | 695.997 | 0.995 |
| capacity / dependent | 67108864 | prior_shape | 181.497 | 180.691–183.026 | 808.085 | 1.156 |
| capacity / get16 | 1024 | fixed_planes | 4.884 | 4.881–4.977 | 21.874 | 1.344 |
| capacity / get16 | 1024 | local_aos7 | 3.633 | 3.625–3.699 | 16.263 | 1.000 |
| capacity / get16 | 1024 | plain_u64 | 3.073 | 3.061–3.077 | 13.744 | 0.846 |
| capacity / get16 | 1024 | prior_shape | 21.127 | 21.076–21.221 | 94.354 | 5.816 |
| capacity / get16 | 65536 | fixed_planes | 5.471 | 5.462–5.480 | 24.482 | 1.309 |
| capacity / get16 | 65536 | local_aos7 | 4.179 | 4.159–4.209 | 18.687 | 1.000 |
| capacity / get16 | 65536 | plain_u64 | 3.295 | 3.288–3.301 | 14.740 | 0.788 |
| capacity / get16 | 65536 | prior_shape | 22.959 | 22.872–23.093 | 102.371 | 5.493 |
| capacity / get16 | 67108864 | fixed_planes | 63.549 | 63.505–63.638 | 283.437 | 1.433 |
| capacity / get16 | 67108864 | local_aos7 | 44.338 | 44.251–44.512 | 197.397 | 1.000 |
| capacity / get16 | 67108864 | plain_u64 | 35.039 | 34.967–36.851 | 155.939 | 0.790 |
| capacity / get16 | 67108864 | prior_shape | 187.689 | 186.685–188.550 | 835.834 | 4.233 |
| capacity / independent | 1024 | fixed_planes | 1.811 | 1.810–1.823 | 8.108 | 1.209 |
| capacity / independent | 1024 | local_aos7 | 1.498 | 1.494–1.535 | 6.683 | 1.000 |
| capacity / independent | 1024 | plain_u64 | 1.324 | 1.322–1.331 | 5.934 | 0.883 |
| capacity / independent | 1024 | prior_shape | 1.800 | 1.792–1.803 | 8.045 | 1.201 |
| capacity / independent | 65536 | fixed_planes | 1.963 | 1.950–1.967 | 8.775 | 1.243 |
| capacity / independent | 65536 | local_aos7 | 1.580 | 1.572–1.619 | 7.065 | 1.000 |
| capacity / independent | 65536 | plain_u64 | 1.394 | 1.390–1.397 | 6.239 | 0.882 |
| capacity / independent | 65536 | prior_shape | 1.953 | 1.942–1.957 | 8.716 | 1.236 |
| capacity / independent | 67108864 | fixed_planes | 18.094 | 18.036–18.300 | 80.826 | 1.433 |
| capacity / independent | 67108864 | local_aos7 | 12.627 | 12.528–12.725 | 56.435 | 1.000 |
| capacity / independent | 67108864 | plain_u64 | 9.406 | 9.294–9.579 | 41.995 | 0.745 |
| capacity / independent | 67108864 | prior_shape | 18.216 | 18.038–18.550 | 81.374 | 1.443 |

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
