# Width-56 results

One captured run; medians and full repetition ranges. Capacity units are ns/request; bulk units are ns/256 values. Comparator/local ratios above one favour the local AoS7 implementation. All arrays have matched logical counts. Cache residence is unestablished; required payload line counts are trace accounting, not cache misses.

| Suite / operation | Values | Arm | Median ns | Range | Cycles | Comparator/local |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| bulk / decode | 32768 | fixed_planes | 66.792 | 66.696–66.902 | 256.257 | 1.584 |
| bulk / decode | 32768 | local_aos7 | 42.166 | 42.105–42.476 | 162.536 | 1.000 |
| bulk / decode | 32768 | plain_u64 | 42.669 | 42.508–42.767 | 163.518 | 1.012 |
| bulk / decode | 32768 | prior_shape | 103.008 | 102.215–103.496 | 379.219 | 2.443 |
| bulk / encode | 32768 | fixed_planes | 67.376 | 66.938–68.200 | 257.442 | 1.399 |
| bulk / encode | 32768 | local_aos7 | 48.149 | 48.006–48.210 | 184.707 | 1.000 |
| bulk / encode | 32768 | plain_u64 | 42.606 | 42.474–42.911 | 163.296 | 0.885 |
| bulk / encode | 32768 | prior_shape | 79.581 | 79.325–79.911 | 304.732 | 1.653 |
| bulk / sum | 32768 | fixed_planes | 67.391 | 67.110–67.590 | 258.481 | 3.820 |
| bulk / sum | 32768 | local_aos7 | 17.642 | 17.473–17.694 | 65.054 | 1.000 |
| bulk / sum | 32768 | plain_u64 | 12.862 | 12.848–12.912 | 49.473 | 0.729 |
| bulk / sum | 32768 | prior_shape | 108.613 | 108.196–113.210 | 402.365 | 6.157 |
| capacity / dependent | 1024 | fixed_planes | 7.499 | 7.442–7.510 | 28.757 | 0.964 |
| capacity / dependent | 1024 | local_aos7 | 7.779 | 7.720–7.811 | 29.859 | 1.000 |
| capacity / dependent | 1024 | plain_u64 | 6.391 | 6.334–6.419 | 24.444 | 0.821 |
| capacity / dependent | 1024 | prior_shape | 9.107 | 9.056–9.149 | 34.933 | 1.171 |
| capacity / dependent | 65536 | fixed_planes | 11.722 | 11.694–11.846 | 45.007 | 1.019 |
| capacity / dependent | 65536 | local_aos7 | 11.500 | 11.423–11.514 | 44.077 | 1.000 |
| capacity / dependent | 65536 | plain_u64 | 9.421 | 9.401–9.509 | 36.267 | 0.819 |
| capacity / dependent | 65536 | prior_shape | 12.137 | 12.025–12.367 | 46.476 | 1.055 |
| capacity / dependent | 67108864 | fixed_planes | 207.965 | 204.808–215.718 | 789.305 | 1.264 |
| capacity / dependent | 67108864 | local_aos7 | 164.502 | 163.980–165.388 | 629.826 | 1.000 |
| capacity / dependent | 67108864 | plain_u64 | 160.846 | 155.929–161.922 | 617.871 | 0.978 |
| capacity / dependent | 67108864 | prior_shape | 217.876 | 214.808–222.745 | 824.690 | 1.324 |
| capacity / get16 | 1024 | fixed_planes | 8.823 | 8.786–8.845 | 33.855 | 1.217 |
| capacity / get16 | 1024 | local_aos7 | 7.249 | 7.230–7.312 | 27.914 | 1.000 |
| capacity / get16 | 1024 | plain_u64 | 5.127 | 5.076–5.169 | 19.611 | 0.707 |
| capacity / get16 | 1024 | prior_shape | 26.091 | 25.922–26.147 | 99.964 | 3.599 |
| capacity / get16 | 65536 | fixed_planes | 9.631 | 9.587–9.648 | 37.074 | 1.229 |
| capacity / get16 | 65536 | local_aos7 | 7.835 | 7.798–7.855 | 30.040 | 1.000 |
| capacity / get16 | 65536 | plain_u64 | 5.819 | 5.810–5.871 | 22.458 | 0.743 |
| capacity / get16 | 65536 | prior_shape | 29.038 | 29.011–29.069 | 111.653 | 3.706 |
| capacity / get16 | 67108864 | fixed_planes | 66.264 | 62.591–66.745 | 250.682 | 1.421 |
| capacity / get16 | 67108864 | local_aos7 | 46.620 | 45.122–47.629 | 176.529 | 1.000 |
| capacity / get16 | 67108864 | plain_u64 | 38.317 | 38.107–38.900 | 144.894 | 0.822 |
| capacity / get16 | 67108864 | prior_shape | 211.197 | 200.971–212.949 | 787.095 | 4.530 |
| capacity / independent | 1024 | fixed_planes | 2.959 | 2.952–2.966 | 11.415 | 1.172 |
| capacity / independent | 1024 | local_aos7 | 2.525 | 2.512–2.552 | 9.790 | 1.000 |
| capacity / independent | 1024 | plain_u64 | 1.948 | 1.928–1.972 | 7.477 | 0.771 |
| capacity / independent | 1024 | prior_shape | 3.017 | 2.985–3.115 | 11.567 | 1.195 |
| capacity / independent | 65536 | fixed_planes | 3.118 | 3.112–3.159 | 12.048 | 1.156 |
| capacity / independent | 65536 | local_aos7 | 2.698 | 2.683–2.749 | 10.419 | 1.000 |
| capacity / independent | 65536 | plain_u64 | 2.026 | 2.021–2.082 | 7.813 | 0.751 |
| capacity / independent | 65536 | prior_shape | 3.142 | 3.127–3.175 | 12.078 | 1.165 |
| capacity / independent | 67108864 | fixed_planes | 23.992 | 23.470–25.954 | 90.288 | 1.520 |
| capacity / independent | 67108864 | local_aos7 | 15.785 | 15.665–16.291 | 59.345 | 1.000 |
| capacity / independent | 67108864 | plain_u64 | 13.635 | 13.239–14.117 | 51.353 | 0.864 |
| capacity / independent | 67108864 | prior_shape | 25.363 | 24.876–25.596 | 95.825 | 1.607 |

The generic cache-miss event reported zero throughout this run; treat it as uninformative.

The prior receives a compile-time width-56 Shape; its underlying API may still traverse that shape and materialize intermediate outputs. The fresh fixed_planes control uses the same bytes with direct width-specific operations. Direct sum consumes native AoS fragments; prior sum uses the prior reconstruction API. The API/region differences are intentional controls and are not a pure wire-only comparison.
