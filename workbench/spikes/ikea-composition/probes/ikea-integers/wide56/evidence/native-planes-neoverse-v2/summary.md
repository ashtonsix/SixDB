# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 218.254 | 218.205–218.873 | 608.141 | 3.656 |
| bulk / decode | 32768 | local_aos7 | 59.703 | 59.664–59.778 | 166.112 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 49.109 | 49.025–49.295 | 136.585 | 0.823 |
| bulk / decode | 32768 | prior_shape | 357.037 | 356.777–357.464 | 994.521 | 5.980 |
| bulk / encode | 32768 | fixed_planes | 115.812 | 115.665–115.886 | 322.485 | 1.619 |
| bulk / encode | 32768 | local_aos7 | 71.542 | 71.466–71.868 | 199.189 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 48.140 | 47.994–48.334 | 133.931 | 0.673 |
| bulk / encode | 32768 | prior_shape | 161.347 | 161.215–161.508 | 449.597 | 2.255 |
| bulk / sum | 32768 | fixed_planes | 276.717 | 276.549–277.160 | 771.142 | 5.918 |
| bulk / sum | 32768 | local_aos7 | 46.756 | 46.702–46.910 | 130.268 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 35.378 | 35.236–35.400 | 98.375 | 0.757 |
| bulk / sum | 32768 | prior_shape | 365.854 | 365.128–366.004 | 1018.713 | 7.825 |
| capacity / dependent | 1024 | fixed_planes | 8.359 | 8.289–8.370 | 23.240 | 1.077 |
| capacity / dependent | 1024 | local_aos7 | 7.759 | 7.743–7.805 | 21.635 | 1.000 |
| capacity / dependent | 1024 | plain_u64 | 5.873 | 5.854–5.940 | 16.396 | 0.757 |
| capacity / dependent | 1024 | prior_shape | 8.688 | 8.663–8.731 | 24.269 | 1.120 |
| capacity / dependent | 65536 | fixed_planes | 12.142 | 12.090–12.182 | 33.830 | 1.139 |
| capacity / dependent | 65536 | local_aos7 | 10.661 | 10.616–10.780 | 29.645 | 1.000 |
| capacity / dependent | 65536 | plain_u64 | 8.923 | 8.879–9.045 | 24.867 | 0.837 |
| capacity / dependent | 65536 | prior_shape | 12.127 | 12.122–12.287 | 33.843 | 1.137 |
| capacity / dependent | 67108864 | fixed_planes | 196.304 | 192.708–200.607 | 545.378 | 1.412 |
| capacity / dependent | 67108864 | local_aos7 | 139.016 | 137.203–142.437 | 386.335 | 1.000 |
| capacity / dependent | 67108864 | plain_u64 | 138.695 | 138.242–143.795 | 384.782 | 0.998 |
| capacity / dependent | 67108864 | prior_shape | 190.241 | 189.585–193.108 | 527.146 | 1.368 |
| capacity / get16 | 1024 | fixed_planes | 16.438 | 16.434–16.514 | 45.888 | 1.985 |
| capacity / get16 | 1024 | local_aos7 | 8.282 | 8.276–8.329 | 23.126 | 1.000 |
| capacity / get16 | 1024 | plain_u64 | 6.686 | 6.684–6.716 | 18.661 | 0.807 |
| capacity / get16 | 1024 | prior_shape | 41.978 | 41.939–42.129 | 117.012 | 5.068 |
| capacity / get16 | 65536 | fixed_planes | 19.147 | 19.089–19.176 | 53.294 | 1.906 |
| capacity / get16 | 65536 | local_aos7 | 10.044 | 10.035–10.127 | 28.026 | 1.000 |
| capacity / get16 | 65536 | plain_u64 | 8.601 | 8.531–8.665 | 23.828 | 0.856 |
| capacity / get16 | 65536 | prior_shape | 48.053 | 47.961–48.309 | 133.814 | 4.784 |
| capacity / get16 | 67108864 | fixed_planes | 136.736 | 136.416–140.959 | 379.160 | 2.627 |
| capacity / get16 | 67108864 | local_aos7 | 52.056 | 51.640–53.275 | 144.634 | 1.000 |
| capacity / get16 | 67108864 | plain_u64 | 50.126 | 49.090–50.455 | 138.389 | 0.963 |
| capacity / get16 | 67108864 | prior_shape | 316.889 | 309.654–317.464 | 880.751 | 6.087 |
| capacity / independent | 1024 | fixed_planes | 2.247 | 2.244–2.249 | 6.275 | 1.130 |
| capacity / independent | 1024 | local_aos7 | 1.988 | 1.985–2.104 | 5.547 | 1.000 |
| capacity / independent | 1024 | plain_u64 | 1.736 | 1.734–1.737 | 4.846 | 0.873 |
| capacity / independent | 1024 | prior_shape | 2.443 | 2.440–2.449 | 6.825 | 1.229 |
| capacity / independent | 65536 | fixed_planes | 3.170 | 3.164–4.331 | 8.853 | 1.374 |
| capacity / independent | 65536 | local_aos7 | 2.308 | 2.299–2.360 | 6.436 | 1.000 |
| capacity / independent | 65536 | plain_u64 | 1.862 | 1.858–1.868 | 5.201 | 0.807 |
| capacity / independent | 65536 | prior_shape | 3.307 | 3.300–3.366 | 9.229 | 1.433 |
| capacity / independent | 67108864 | fixed_planes | 32.551 | 31.496–34.796 | 90.151 | 1.628 |
| capacity / independent | 67108864 | local_aos7 | 19.990 | 19.708–20.458 | 55.258 | 1.000 |
| capacity / independent | 67108864 | plain_u64 | 15.961 | 15.648–16.655 | 44.183 | 0.798 |
| capacity / independent | 67108864 | prior_shape | 32.907 | 32.494–33.425 | 91.040 | 1.646 |

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
