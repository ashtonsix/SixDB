# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 66.841 | 66.628–66.918 | 256.283 | 1.589 |
| bulk / decode | 32768 | local_aos7 | 42.054 | 41.900–42.391 | 161.729 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 42.629 | 42.499–42.935 | 163.803 | 1.014 |
| bulk / decode | 32768 | prior_shape | 101.043 | 100.965–102.386 | 377.230 | 2.403 |
| bulk / encode | 32768 | fixed_planes | 66.902 | 66.734–67.025 | 256.754 | 1.388 |
| bulk / encode | 32768 | local_aos7 | 48.215 | 48.066–48.518 | 184.484 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 42.505 | 42.289–42.841 | 163.391 | 0.882 |
| bulk / encode | 32768 | prior_shape | 79.576 | 79.173–79.779 | 303.669 | 1.650 |
| bulk / sum | 32768 | fixed_planes | 67.206 | 67.104–67.604 | 258.478 | 3.835 |
| bulk / sum | 32768 | local_aos7 | 17.523 | 17.399–17.671 | 64.973 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 12.878 | 12.857–12.910 | 49.506 | 0.735 |
| bulk / sum | 32768 | prior_shape | 107.665 | 107.295–108.320 | 399.802 | 6.144 |
| capacity / dependent | 1024 | fixed_planes | 7.503 | 7.475–7.571 | 28.751 | 0.954 |
| capacity / dependent | 1024 | local_aos7 | 7.865 | 7.782–7.904 | 29.881 | 1.000 |
| capacity / dependent | 1024 | plain_u64 | 6.324 | 6.323–6.367 | 24.446 | 0.804 |
| capacity / dependent | 1024 | prior_shape | 9.086 | 9.043–9.151 | 34.948 | 1.155 |
| capacity / dependent | 65536 | fixed_planes | 11.805 | 11.660–11.914 | 45.053 | 1.027 |
| capacity / dependent | 65536 | local_aos7 | 11.497 | 11.439–11.558 | 44.029 | 1.000 |
| capacity / dependent | 65536 | plain_u64 | 9.438 | 9.392–9.473 | 36.218 | 0.821 |
| capacity / dependent | 65536 | prior_shape | 12.049 | 12.013–12.217 | 46.356 | 1.048 |
| capacity / dependent | 67108864 | fixed_planes | 210.970 | 205.592–215.525 | 799.544 | 1.307 |
| capacity / dependent | 67108864 | local_aos7 | 161.436 | 160.893–163.017 | 616.924 | 1.000 |
| capacity / dependent | 67108864 | plain_u64 | 160.214 | 157.748–160.662 | 611.258 | 0.992 |
| capacity / dependent | 67108864 | prior_shape | 214.690 | 209.844–216.547 | 813.883 | 1.330 |
| capacity / get16 | 1024 | fixed_planes | 15.055 | 15.016–15.089 | 57.959 | 2.062 |
| capacity / get16 | 1024 | local_aos7 | 7.302 | 7.249–7.358 | 27.923 | 1.000 |
| capacity / get16 | 1024 | plain_u64 | 5.111 | 5.066–5.126 | 19.618 | 0.700 |
| capacity / get16 | 1024 | prior_shape | 26.198 | 26.011–27.232 | 100.052 | 3.588 |
| capacity / get16 | 65536 | fixed_planes | 18.238 | 18.042–18.329 | 69.780 | 2.341 |
| capacity / get16 | 65536 | local_aos7 | 7.789 | 7.775–7.806 | 30.024 | 1.000 |
| capacity / get16 | 65536 | plain_u64 | 5.813 | 5.806–5.912 | 22.452 | 0.746 |
| capacity / get16 | 65536 | prior_shape | 29.286 | 28.999–29.561 | 111.776 | 3.760 |
| capacity / get16 | 67108864 | fixed_planes | 152.766 | 149.253–157.762 | 579.027 | 3.413 |
| capacity / get16 | 67108864 | local_aos7 | 44.759 | 44.482–45.899 | 170.379 | 1.000 |
| capacity / get16 | 67108864 | plain_u64 | 38.751 | 38.104–39.085 | 146.245 | 0.866 |
| capacity / get16 | 67108864 | prior_shape | 206.787 | 200.011–207.897 | 783.247 | 4.620 |
| capacity / independent | 1024 | fixed_planes | 2.975 | 2.967–3.016 | 11.433 | 1.167 |
| capacity / independent | 1024 | local_aos7 | 2.549 | 2.530–2.591 | 9.758 | 1.000 |
| capacity / independent | 1024 | plain_u64 | 1.942 | 1.928–1.950 | 7.476 | 0.762 |
| capacity / independent | 1024 | prior_shape | 2.994 | 2.988–3.005 | 11.543 | 1.175 |
| capacity / independent | 65536 | fixed_planes | 3.120 | 3.112–3.169 | 12.049 | 1.155 |
| capacity / independent | 65536 | local_aos7 | 2.700 | 2.690–2.742 | 10.435 | 1.000 |
| capacity / independent | 65536 | plain_u64 | 2.026 | 2.014–2.093 | 7.814 | 0.750 |
| capacity / independent | 65536 | prior_shape | 3.140 | 3.121–3.195 | 12.080 | 1.163 |
| capacity / independent | 67108864 | fixed_planes | 24.271 | 23.729–26.545 | 91.701 | 1.554 |
| capacity / independent | 67108864 | local_aos7 | 15.622 | 15.378–16.134 | 59.116 | 1.000 |
| capacity / independent | 67108864 | plain_u64 | 13.592 | 13.469–13.926 | 51.514 | 0.870 |
| capacity / independent | 67108864 | prior_shape | 25.251 | 24.359–26.620 | 94.973 | 1.616 |

The generic cache-miss event reported zero throughout this run; treat it as uninformative.

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
