# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 25.120 | 24.713–25.681 | 111.609 | 1.028 |
| bulk / decode | 32768 | local_aos7 | 24.441 | 24.328–24.504 | 108.540 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 28.713 | 28.596–28.749 | 127.852 | 1.175 |
| bulk / decode | 32768 | prior_shape | 48.505 | 48.360–48.623 | 215.117 | 1.985 |
| bulk / encode | 32768 | fixed_planes | 18.765 | 18.481–19.700 | 82.663 | 0.769 |
| bulk / encode | 32768 | local_aos7 | 24.401 | 24.345–24.676 | 108.546 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 19.258 | 19.032–19.567 | 85.429 | 0.789 |
| bulk / encode | 32768 | prior_shape | 31.282 | 31.229–31.381 | 138.721 | 1.282 |
| bulk / sum | 32768 | fixed_planes | 21.600 | 21.508–21.794 | 95.860 | 2.029 |
| bulk / sum | 32768 | local_aos7 | 10.645 | 10.634–10.687 | 47.289 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 14.218 | 14.154–14.261 | 63.455 | 1.336 |
| bulk / sum | 32768 | prior_shape | 51.716 | 51.621–51.747 | 229.878 | 4.858 |
| capacity / dependent | 1024 | fixed_planes | 6.078 | 6.060–6.103 | 27.124 | 0.996 |
| capacity / dependent | 1024 | local_aos7 | 6.102 | 6.091–6.152 | 27.204 | 1.000 |
| capacity / dependent | 1024 | plain_u64 | 5.363 | 5.362–5.388 | 24.007 | 0.879 |
| capacity / dependent | 1024 | prior_shape | 6.399 | 6.293–6.442 | 28.378 | 1.049 |
| capacity / dependent | 65536 | fixed_planes | 8.706 | 8.686–8.739 | 38.875 | 1.031 |
| capacity / dependent | 65536 | local_aos7 | 8.448 | 8.441–8.734 | 37.726 | 1.000 |
| capacity / dependent | 65536 | plain_u64 | 7.888 | 7.838–7.905 | 35.112 | 0.934 |
| capacity / dependent | 65536 | prior_shape | 8.693 | 8.674–9.705 | 38.728 | 1.029 |
| capacity / dependent | 67108864 | fixed_planes | 180.710 | 178.680–181.364 | 804.873 | 1.139 |
| capacity / dependent | 67108864 | local_aos7 | 158.708 | 157.672–160.482 | 704.336 | 1.000 |
| capacity / dependent | 67108864 | plain_u64 | 155.877 | 155.564–156.856 | 694.611 | 0.982 |
| capacity / dependent | 67108864 | prior_shape | 182.398 | 181.341–189.634 | 810.845 | 1.149 |
| capacity / get16 | 1024 | fixed_planes | 10.616 | 10.588–10.746 | 47.366 | 2.396 |
| capacity / get16 | 1024 | local_aos7 | 4.430 | 4.417–4.519 | 19.750 | 1.000 |
| capacity / get16 | 1024 | plain_u64 | 3.180 | 3.177–3.190 | 14.206 | 0.718 |
| capacity / get16 | 1024 | prior_shape | 20.936 | 20.902–21.091 | 93.178 | 4.726 |
| capacity / get16 | 65536 | fixed_planes | 12.861 | 12.850–13.027 | 57.517 | 2.717 |
| capacity / get16 | 65536 | local_aos7 | 4.734 | 4.703–4.752 | 21.038 | 1.000 |
| capacity / get16 | 65536 | plain_u64 | 3.385 | 3.371–3.391 | 15.136 | 0.715 |
| capacity / get16 | 65536 | prior_shape | 23.053 | 23.009–23.114 | 102.467 | 4.869 |
| capacity / get16 | 67108864 | fixed_planes | 166.413 | 165.502–166.705 | 740.546 | 3.731 |
| capacity / get16 | 67108864 | local_aos7 | 44.602 | 44.380–44.866 | 198.502 | 1.000 |
| capacity / get16 | 67108864 | plain_u64 | 34.851 | 34.708–38.650 | 155.363 | 0.781 |
| capacity / get16 | 67108864 | prior_shape | 186.775 | 185.530–188.153 | 831.167 | 4.188 |
| capacity / independent | 1024 | fixed_planes | 1.803 | 1.794–1.806 | 8.044 | 1.204 |
| capacity / independent | 1024 | local_aos7 | 1.497 | 1.490–1.508 | 6.660 | 1.000 |
| capacity / independent | 1024 | plain_u64 | 1.336 | 1.330–1.475 | 5.951 | 0.892 |
| capacity / independent | 1024 | prior_shape | 1.800 | 1.793–2.036 | 7.980 | 1.202 |
| capacity / independent | 65536 | fixed_planes | 1.971 | 1.966–1.976 | 8.793 | 1.245 |
| capacity / independent | 65536 | local_aos7 | 1.583 | 1.576–1.592 | 7.060 | 1.000 |
| capacity / independent | 65536 | plain_u64 | 1.408 | 1.405–1.414 | 6.287 | 0.889 |
| capacity / independent | 65536 | prior_shape | 2.203 | 1.968–2.290 | 8.849 | 1.391 |
| capacity / independent | 67108864 | fixed_planes | 18.161 | 17.886–18.270 | 80.529 | 1.427 |
| capacity / independent | 67108864 | local_aos7 | 12.730 | 12.508–12.838 | 56.867 | 1.000 |
| capacity / independent | 67108864 | plain_u64 | 9.398 | 9.242–9.467 | 41.786 | 0.738 |
| capacity / independent | 67108864 | prior_shape | 18.091 | 18.034–18.695 | 80.790 | 1.421 |

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
