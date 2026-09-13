# Spatial consumer comparison

Reported line size: **64 B**; architecture: Granite Rapids. All layouts contain the same 96B logical record. Timings are complete consumer-loop ns/operation.

Reported cache capacity is context, not measured residency. Smoke sizes establish no DRAM claim.

Page/NUMA observations before and after each placement are in `placement.txt`; mapping requests alone do not establish residency or NUMA placement.

| Footprint | Core bytes / reported LLC | Logical bytes |
| --- | ---: | ---: |
| hot-control | 0.000 | 12288 |
| larger | 1.422 | 1073740800 |

Extension denominator: 0 = core only, 8 = one eighth, 1 = all. Phases remain separate; seeds/repetitions are summarized within each cell. Useful computation/prefetch/pattern are fixed by this run and retained in `analysis.json`.

| Footprint | Phase | Extension | K | Layout | ns/op median [min, max] | Modeled lines/op | Allocated bytes |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| hot-control | 0 | 0 | 1 | dense96 | 2.750 [2.750, 2.761] | 1.500 | 12288 |
| hot-control | 0 | 0 | 1 | padded128 | 2.638 [2.621, 2.639] | 1.000 | 16384 |
| hot-control | 0 | 0 | 1 | split64+32 | 2.639 [2.567, 2.648] | 1.000 | 12288 |
| hot-control | 0 | 1 | 1 | dense96 | 3.539 [3.528, 3.559] | 2.000 | 12288 |
| hot-control | 0 | 1 | 1 | padded128 | 3.363 [3.346, 3.371] | 2.000 | 16384 |
| hot-control | 0 | 1 | 1 | split64+32 | 3.043 [3.029, 3.326] | 2.000 | 12288 |
| hot-control | 0 | 8 | 1 | dense96 | 3.312 [3.279, 3.340] | 1.555 | 12288 |
| hot-control | 0 | 8 | 1 | padded128 | 2.667 [2.622, 2.670] | 1.125 | 16384 |
| hot-control | 0 | 8 | 1 | split64+32 | 2.692 [2.661, 2.718] | 1.125 | 12288 |
| hot-control | 0 | 0 | 32 | dense96 | 1.668 [1.658, 1.702] | 1.500 | 12288 |
| hot-control | 0 | 0 | 32 | padded128 | 1.636 [1.632, 1.638] | 1.000 | 16384 |
| hot-control | 0 | 0 | 32 | split64+32 | 1.636 [1.630, 1.637] | 1.000 | 12288 |
| hot-control | 0 | 1 | 32 | dense96 | 2.459 [2.455, 2.464] | 2.000 | 12288 |
| hot-control | 0 | 1 | 32 | padded128 | 2.554 [2.542, 2.575] | 2.000 | 16384 |
| hot-control | 0 | 1 | 32 | split64+32 | 2.686 [2.684, 2.690] | 2.000 | 12288 |
| hot-control | 0 | 8 | 32 | dense96 | 1.935 [1.921, 1.939] | 1.555 | 12288 |
| hot-control | 0 | 8 | 32 | padded128 | 2.036 [2.004, 2.110] | 1.125 | 16384 |
| hot-control | 0 | 8 | 32 | split64+32 | 2.061 [2.033, 2.088] | 1.125 | 12288 |
| hot-control | 0 | 0 | 8 | dense96 | 1.477 [1.469, 1.481] | 1.500 | 12288 |
| hot-control | 0 | 0 | 8 | padded128 | 1.475 [1.472, 1.494] | 1.000 | 16384 |
| hot-control | 0 | 0 | 8 | split64+32 | 1.470 [1.468, 1.472] | 1.000 | 12288 |
| hot-control | 0 | 1 | 8 | dense96 | 2.304 [2.296, 2.312] | 2.000 | 12288 |
| hot-control | 0 | 1 | 8 | padded128 | 2.251 [2.244, 2.266] | 2.000 | 16384 |
| hot-control | 0 | 1 | 8 | split64+32 | 2.542 [2.534, 2.547] | 2.000 | 12288 |
| hot-control | 0 | 8 | 8 | dense96 | 1.754 [1.751, 1.801] | 1.555 | 12288 |
| hot-control | 0 | 8 | 8 | padded128 | 1.689 [1.685, 1.708] | 1.125 | 16384 |
| hot-control | 0 | 8 | 8 | split64+32 | 1.775 [1.772, 1.789] | 1.125 | 12288 |
| hot-control | 32 | 0 | 1 | dense96 | 2.753 [2.750, 2.758] | 1.500 | 16384 |
| hot-control | 32 | 0 | 1 | padded128 | 2.622 [2.574, 2.642] | 2.000 | 20480 |
| hot-control | 32 | 0 | 1 | split64+32 | 2.638 [2.594, 2.641] | 2.000 | 20480 |
| hot-control | 32 | 1 | 1 | dense96 | 3.532 [3.528, 3.557] | 2.000 | 16384 |
| hot-control | 32 | 1 | 1 | padded128 | 3.361 [3.345, 3.379] | 2.000 | 20480 |
| hot-control | 32 | 1 | 1 | split64+32 | 3.050 [3.027, 3.055] | 3.000 | 20480 |
| hot-control | 32 | 8 | 1 | dense96 | 3.318 [3.295, 3.341] | 1.570 | 16384 |
| hot-control | 32 | 8 | 1 | padded128 | 2.662 [2.635, 2.675] | 2.000 | 20480 |
| hot-control | 32 | 8 | 1 | split64+32 | 2.669 [2.638, 2.702] | 2.125 | 20480 |
| hot-control | 32 | 0 | 32 | dense96 | 1.669 [1.657, 1.682] | 1.500 | 16384 |
| hot-control | 32 | 0 | 32 | padded128 | 1.630 [1.629, 1.633] | 2.000 | 20480 |
| hot-control | 32 | 0 | 32 | split64+32 | 1.636 [1.633, 1.637] | 2.000 | 20480 |
| hot-control | 32 | 1 | 32 | dense96 | 2.474 [2.467, 2.483] | 2.000 | 16384 |
| hot-control | 32 | 1 | 32 | padded128 | 2.566 [2.534, 2.578] | 2.000 | 20480 |
| hot-control | 32 | 1 | 32 | split64+32 | 2.695 [2.684, 2.711] | 3.000 | 20480 |
| hot-control | 32 | 8 | 32 | dense96 | 1.935 [1.927, 1.966] | 1.570 | 16384 |
| hot-control | 32 | 8 | 32 | padded128 | 2.059 [2.017, 2.135] | 2.000 | 20480 |
| hot-control | 32 | 8 | 32 | split64+32 | 2.047 [2.030, 2.061] | 2.125 | 20480 |
| hot-control | 32 | 0 | 8 | dense96 | 1.475 [1.472, 1.478] | 1.500 | 16384 |
| hot-control | 32 | 0 | 8 | padded128 | 1.486 [1.474, 1.512] | 2.000 | 20480 |
| hot-control | 32 | 0 | 8 | split64+32 | 1.469 [1.466, 1.478] | 2.000 | 20480 |
| hot-control | 32 | 1 | 8 | dense96 | 2.309 [2.306, 2.313] | 2.000 | 16384 |
| hot-control | 32 | 1 | 8 | padded128 | 2.249 [2.243, 2.254] | 2.000 | 20480 |
| hot-control | 32 | 1 | 8 | split64+32 | 2.543 [2.539, 2.547] | 3.000 | 20480 |
| hot-control | 32 | 8 | 8 | dense96 | 1.755 [1.753, 1.798] | 1.570 | 16384 |
| hot-control | 32 | 8 | 8 | padded128 | 1.691 [1.683, 1.698] | 2.000 | 20480 |
| hot-control | 32 | 8 | 8 | split64+32 | 1.784 [1.775, 1.791] | 2.125 | 20480 |
| larger | 0 | 0 | 1 | dense96 | 185.788 [184.653, 186.296] | 1.500 | 1073741824 |
| larger | 0 | 0 | 1 | padded128 | 190.249 [189.594, 190.504] | 1.000 | 1431654400 |
| larger | 0 | 0 | 1 | split64+32 | 176.519 [176.098, 176.757] | 1.000 | 1073745920 |
| larger | 0 | 1 | 1 | dense96 | 186.859 [185.921, 188.013] | 2.000 | 1073741824 |
| larger | 0 | 1 | 1 | padded128 | 191.738 [191.303, 191.901] | 2.000 | 1431654400 |
| larger | 0 | 1 | 1 | split64+32 | 190.758 [190.610, 194.864] | 2.000 | 1073745920 |
| larger | 0 | 8 | 1 | dense96 | 186.631 [185.951, 187.681] | 1.563 | 1073741824 |
| larger | 0 | 8 | 1 | padded128 | 190.635 [190.014, 191.120] | 1.125 | 1431654400 |
| larger | 0 | 8 | 1 | split64+32 | 180.545 [178.620, 181.412] | 1.125 | 1073745920 |
| larger | 0 | 0 | 32 | dense96 | 29.622 [29.519, 29.832] | 1.500 | 1073741824 |
| larger | 0 | 0 | 32 | padded128 | 28.738 [28.721, 28.808] | 1.000 | 1431654400 |
| larger | 0 | 0 | 32 | split64+32 | 26.753 [26.716, 26.857] | 1.000 | 1073745920 |
| larger | 0 | 1 | 32 | dense96 | 41.948 [41.797, 42.069] | 2.000 | 1073741824 |
| larger | 0 | 1 | 32 | padded128 | 41.705 [41.620, 42.065] | 2.000 | 1431654400 |
| larger | 0 | 1 | 32 | split64+32 | 44.191 [44.142, 44.415] | 2.000 | 1073745920 |
| larger | 0 | 8 | 32 | dense96 | 32.510 [32.360, 32.728] | 1.563 | 1073741824 |
| larger | 0 | 8 | 32 | padded128 | 31.626 [31.523, 31.692] | 1.125 | 1431654400 |
| larger | 0 | 8 | 32 | split64+32 | 37.725 [37.677, 37.759] | 1.125 | 1073745920 |
| larger | 0 | 0 | 8 | dense96 | 29.562 [29.473, 29.653] | 1.500 | 1073741824 |
| larger | 0 | 0 | 8 | padded128 | 29.596 [29.405, 29.706] | 1.000 | 1431654400 |
| larger | 0 | 0 | 8 | split64+32 | 27.531 [27.482, 27.563] | 1.000 | 1073745920 |
| larger | 0 | 1 | 8 | dense96 | 40.387 [40.362, 40.514] | 2.000 | 1073741824 |
| larger | 0 | 1 | 8 | padded128 | 40.681 [40.421, 40.831] | 2.000 | 1431654400 |
| larger | 0 | 1 | 8 | split64+32 | 43.589 [43.314, 43.696] | 2.000 | 1073745920 |
| larger | 0 | 8 | 8 | dense96 | 32.455 [32.380, 32.556] | 1.563 | 1073741824 |
| larger | 0 | 8 | 8 | padded128 | 31.958 [31.737, 32.052] | 1.125 | 1431654400 |
| larger | 0 | 8 | 8 | split64+32 | 36.812 [36.761, 37.139] | 1.125 | 1073745920 |
| larger | 32 | 0 | 1 | dense96 | 185.738 [184.056, 186.573] | 1.500 | 1073741824 |
| larger | 32 | 0 | 1 | padded128 | 191.985 [190.891, 192.295] | 2.000 | 1431658496 |
| larger | 32 | 0 | 1 | split64+32 | 177.389 [176.705, 177.736] | 2.000 | 1073745920 |
| larger | 32 | 1 | 1 | dense96 | 186.926 [185.661, 187.547] | 2.000 | 1073741824 |
| larger | 32 | 1 | 1 | padded128 | 191.751 [190.994, 198.765] | 2.000 | 1431658496 |
| larger | 32 | 1 | 1 | split64+32 | 190.668 [190.135, 190.882] | 3.000 | 1073745920 |
| larger | 32 | 8 | 1 | dense96 | 186.046 [185.556, 187.455] | 1.562 | 1073741824 |
| larger | 32 | 8 | 1 | padded128 | 192.625 [192.443, 193.029] | 2.000 | 1431658496 |
| larger | 32 | 8 | 1 | split64+32 | 179.982 [179.682, 180.043] | 2.125 | 1073745920 |
| larger | 32 | 0 | 32 | dense96 | 29.577 [29.550, 29.807] | 1.500 | 1073741824 |
| larger | 32 | 0 | 32 | padded128 | 30.678 [30.595, 30.911] | 2.000 | 1431658496 |
| larger | 32 | 0 | 32 | split64+32 | 29.261 [29.160, 29.376] | 2.000 | 1073745920 |
| larger | 32 | 1 | 32 | dense96 | 41.974 [41.878, 42.065] | 2.000 | 1073741824 |
| larger | 32 | 1 | 32 | padded128 | 42.910 [42.801, 43.084] | 2.000 | 1431658496 |
| larger | 32 | 1 | 32 | split64+32 | 47.792 [47.761, 48.019] | 3.000 | 1073745920 |
| larger | 32 | 8 | 32 | dense96 | 32.512 [32.386, 32.673] | 1.562 | 1073741824 |
| larger | 32 | 8 | 32 | padded128 | 33.911 [33.791, 33.976] | 2.000 | 1431658496 |
| larger | 32 | 8 | 32 | split64+32 | 39.417 [39.285, 39.452] | 2.125 | 1073745920 |
| larger | 32 | 0 | 8 | dense96 | 29.645 [29.522, 29.798] | 1.500 | 1073741824 |
| larger | 32 | 0 | 8 | padded128 | 31.033 [31.010, 31.060] | 2.000 | 1431658496 |
| larger | 32 | 0 | 8 | split64+32 | 29.770 [29.625, 29.897] | 2.000 | 1073745920 |
| larger | 32 | 1 | 8 | dense96 | 40.537 [40.351, 40.636] | 2.000 | 1073741824 |
| larger | 32 | 1 | 8 | padded128 | 41.453 [41.263, 41.489] | 2.000 | 1431658496 |
| larger | 32 | 1 | 8 | split64+32 | 46.912 [46.791, 47.026] | 3.000 | 1073745920 |
| larger | 32 | 8 | 8 | dense96 | 32.455 [32.318, 32.775] | 1.562 | 1073741824 |
| larger | 32 | 8 | 8 | padded128 | 34.195 [33.999, 34.218] | 2.000 | 1431658496 |
| larger | 32 | 8 | 8 | split64+32 | 38.225 [38.160, 38.327] | 2.125 | 1073745920 |

## Limits

- Batch CLOCK_MONOTONIC_RAW wall timing; no per-load timers or PMU traffic.
- Line/page demand counts are per-operation address unions, not cache misses or transferred bytes.
- padded128 specifies a 128B stride; strict 128B row alignment requires a base phase divisible by 128.
- Compute rounds produce a consumed result, not a calibrated pre-load delay: extension addresses/conditions can be known earlier, so the compiler or CPU may issue their loads before the arithmetic completes.
- Footprints are labeled controls; capacity ratios alone do not establish residency.
- Idle sibling/background load is not enforced; placements are first-touch receipts.
- Only read consumers; no mutation, output materialization or migration costs.
- Sample min/max are observed ranges, not confidence intervals.
