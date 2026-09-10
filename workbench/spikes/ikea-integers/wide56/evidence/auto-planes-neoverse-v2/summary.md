# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 218.352 | 218.177–218.801 | 608.146 | 3.655 |
| bulk / decode | 32768 | local_aos7 | 59.748 | 59.524–59.835 | 166.120 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 49.330 | 49.210–49.484 | 136.670 | 0.826 |
| bulk / decode | 32768 | prior_shape | 356.620 | 356.448–357.448 | 993.385 | 5.969 |
| bulk / encode | 32768 | fixed_planes | 115.875 | 115.727–116.229 | 322.498 | 1.618 |
| bulk / encode | 32768 | local_aos7 | 71.595 | 71.464–71.705 | 199.243 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 48.069 | 47.959–48.212 | 133.818 | 0.671 |
| bulk / encode | 32768 | prior_shape | 161.235 | 161.120–161.556 | 448.910 | 2.252 |
| bulk / sum | 32768 | fixed_planes | 276.652 | 276.636–276.882 | 771.143 | 5.911 |
| bulk / sum | 32768 | local_aos7 | 46.804 | 46.726–46.912 | 130.197 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 35.414 | 35.249–35.482 | 98.259 | 0.757 |
| bulk / sum | 32768 | prior_shape | 365.076 | 364.661–365.865 | 1015.827 | 7.800 |
| capacity / dependent | 1024 | fixed_planes | 8.323 | 8.310–8.442 | 23.224 | 1.071 |
| capacity / dependent | 1024 | local_aos7 | 7.774 | 7.753–7.808 | 21.659 | 1.000 |
| capacity / dependent | 1024 | plain_u64 | 5.911 | 5.839–6.056 | 16.453 | 0.760 |
| capacity / dependent | 1024 | prior_shape | 8.748 | 8.682–8.888 | 24.244 | 1.125 |
| capacity / dependent | 65536 | fixed_planes | 12.144 | 12.124–12.215 | 33.896 | 1.142 |
| capacity / dependent | 65536 | local_aos7 | 10.637 | 10.631–10.641 | 29.689 | 1.000 |
| capacity / dependent | 65536 | plain_u64 | 8.934 | 8.903–9.130 | 24.917 | 0.840 |
| capacity / dependent | 65536 | prior_shape | 12.149 | 12.139–12.357 | 33.911 | 1.142 |
| capacity / dependent | 67108864 | fixed_planes | 180.541 | 178.688–197.103 | 501.688 | 1.308 |
| capacity / dependent | 67108864 | local_aos7 | 138.068 | 136.925–140.175 | 383.936 | 1.000 |
| capacity / dependent | 67108864 | plain_u64 | 135.005 | 133.342–140.550 | 375.600 | 0.978 |
| capacity / dependent | 67108864 | prior_shape | 189.678 | 178.303–195.372 | 526.654 | 1.374 |
| capacity / get16 | 1024 | fixed_planes | 22.885 | 22.839–22.913 | 63.734 | 2.777 |
| capacity / get16 | 1024 | local_aos7 | 8.240 | 8.239–8.325 | 22.996 | 1.000 |
| capacity / get16 | 1024 | plain_u64 | 6.724 | 6.719–6.771 | 18.767 | 0.816 |
| capacity / get16 | 1024 | prior_shape | 41.864 | 41.816–42.018 | 116.751 | 5.081 |
| capacity / get16 | 65536 | fixed_planes | 25.538 | 25.523–25.618 | 71.261 | 2.528 |
| capacity / get16 | 65536 | local_aos7 | 10.102 | 10.026–10.126 | 28.014 | 1.000 |
| capacity / get16 | 65536 | plain_u64 | 8.579 | 8.554–8.642 | 23.892 | 0.849 |
| capacity / get16 | 65536 | prior_shape | 47.818 | 47.777–47.830 | 133.336 | 4.734 |
| capacity / get16 | 67108864 | fixed_planes | 182.343 | 175.514–189.780 | 506.638 | 3.614 |
| capacity / get16 | 67108864 | local_aos7 | 50.448 | 50.073–52.434 | 139.905 | 1.000 |
| capacity / get16 | 67108864 | plain_u64 | 48.664 | 48.536–50.554 | 135.276 | 0.965 |
| capacity / get16 | 67108864 | prior_shape | 305.661 | 294.973–314.836 | 849.951 | 6.059 |
| capacity / independent | 1024 | fixed_planes | 2.247 | 2.245–2.254 | 6.276 | 1.130 |
| capacity / independent | 1024 | local_aos7 | 1.989 | 1.985–1.994 | 5.549 | 1.000 |
| capacity / independent | 1024 | plain_u64 | 1.738 | 1.738–1.742 | 4.854 | 0.874 |
| capacity / independent | 1024 | prior_shape | 2.444 | 2.438–2.484 | 6.823 | 1.229 |
| capacity / independent | 65536 | fixed_planes | 3.180 | 3.169–4.479 | 8.865 | 1.368 |
| capacity / independent | 65536 | local_aos7 | 2.326 | 2.313–2.341 | 6.484 | 1.000 |
| capacity / independent | 65536 | plain_u64 | 1.862 | 1.861–1.864 | 5.201 | 0.801 |
| capacity / independent | 65536 | prior_shape | 3.312 | 3.304–3.433 | 9.228 | 1.424 |
| capacity / independent | 67108864 | fixed_planes | 31.438 | 31.184–31.958 | 86.839 | 1.597 |
| capacity / independent | 67108864 | local_aos7 | 19.689 | 19.358–20.175 | 54.816 | 1.000 |
| capacity / independent | 67108864 | plain_u64 | 15.868 | 15.698–16.377 | 44.086 | 0.806 |
| capacity / independent | 67108864 | prior_shape | 33.083 | 32.391–34.540 | 91.678 | 1.680 |

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
