# Spatial consumer comparison

Reported line size: **64 B**; architecture: Zen 5. All layouts contain the same 96B logical record. Timings are complete consumer-loop ns/operation.

Reported cache capacity is context, not measured residency. Smoke sizes establish no DRAM claim.

Page/NUMA observations before and after each placement are in `placement.txt`; mapping requests alone do not establish residency or NUMA placement.

| Footprint | Core bytes / reported LLC | Logical bytes |
| --- | ---: | ---: |
| hot-control | 0.001 | 12288 |
| larger | 85.333 | 1073740800 |

Extension denominator: 0 = core only, 8 = one eighth, 1 = all. Phases remain separate; seeds/repetitions are summarized within each cell. Useful computation/prefetch/pattern are fixed by this run and retained in `analysis.json`.

| Footprint | Phase | Extension | K | Layout | ns/op median [min, max] | Modeled lines/op | Allocated bytes |
| --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| hot-control | 0 | 0 | 1 | dense96 | 1.854 [1.851, 1.856] | 1.500 | 12288 |
| hot-control | 0 | 0 | 1 | padded128 | 1.722 [1.720, 1.723] | 1.000 | 16384 |
| hot-control | 0 | 0 | 1 | split64+32 | 1.726 [1.725, 1.728] | 1.000 | 12288 |
| hot-control | 0 | 1 | 1 | dense96 | 2.087 [2.072, 2.093] | 2.000 | 12288 |
| hot-control | 0 | 1 | 1 | padded128 | 1.953 [1.952, 1.955] | 2.000 | 16384 |
| hot-control | 0 | 1 | 1 | split64+32 | 2.133 [2.128, 2.135] | 2.000 | 12288 |
| hot-control | 0 | 8 | 1 | dense96 | 2.080 [2.074, 2.087] | 1.555 | 12288 |
| hot-control | 0 | 8 | 1 | padded128 | 1.601 [1.596, 1.603] | 1.125 | 16384 |
| hot-control | 0 | 8 | 1 | split64+32 | 1.874 [1.872, 1.876] | 1.125 | 12288 |
| hot-control | 0 | 0 | 32 | dense96 | 1.329 [1.327, 1.330] | 1.500 | 12288 |
| hot-control | 0 | 0 | 32 | padded128 | 1.312 [1.311, 1.313] | 1.000 | 16384 |
| hot-control | 0 | 0 | 32 | split64+32 | 1.311 [1.310, 1.312] | 1.000 | 12288 |
| hot-control | 0 | 1 | 32 | dense96 | 2.060 [2.060, 2.064] | 2.000 | 12288 |
| hot-control | 0 | 1 | 32 | padded128 | 1.929 [1.926, 1.931] | 2.000 | 16384 |
| hot-control | 0 | 1 | 32 | split64+32 | 2.124 [2.124, 2.129] | 2.000 | 12288 |
| hot-control | 0 | 8 | 32 | dense96 | 1.496 [1.494, 1.496] | 1.555 | 12288 |
| hot-control | 0 | 8 | 32 | padded128 | 1.442 [1.440, 1.445] | 1.125 | 16384 |
| hot-control | 0 | 8 | 32 | split64+32 | 1.526 [1.521, 1.535] | 1.125 | 12288 |
| hot-control | 0 | 0 | 8 | dense96 | 1.162 [1.161, 1.163] | 1.500 | 12288 |
| hot-control | 0 | 0 | 8 | padded128 | 1.133 [1.131, 1.135] | 1.000 | 16384 |
| hot-control | 0 | 0 | 8 | split64+32 | 1.135 [1.133, 1.137] | 1.000 | 12288 |
| hot-control | 0 | 1 | 8 | dense96 | 1.942 [1.941, 1.948] | 2.000 | 12288 |
| hot-control | 0 | 1 | 8 | padded128 | 1.797 [1.795, 1.807] | 2.000 | 16384 |
| hot-control | 0 | 1 | 8 | split64+32 | 1.952 [1.949, 1.957] | 2.000 | 12288 |
| hot-control | 0 | 8 | 8 | dense96 | 1.379 [1.377, 1.380] | 1.555 | 12288 |
| hot-control | 0 | 8 | 8 | padded128 | 1.287 [1.286, 1.289] | 1.125 | 16384 |
| hot-control | 0 | 8 | 8 | split64+32 | 1.451 [1.449, 1.453] | 1.125 | 12288 |
| hot-control | 32 | 0 | 1 | dense96 | 1.854 [1.852, 1.856] | 1.500 | 16384 |
| hot-control | 32 | 0 | 1 | padded128 | 1.728 [1.728, 1.738] | 2.000 | 20480 |
| hot-control | 32 | 0 | 1 | split64+32 | 1.731 [1.727, 1.734] | 2.000 | 20480 |
| hot-control | 32 | 1 | 1 | dense96 | 2.079 [2.072, 2.087] | 2.000 | 16384 |
| hot-control | 32 | 1 | 1 | padded128 | 1.951 [1.949, 1.965] | 2.000 | 20480 |
| hot-control | 32 | 1 | 1 | split64+32 | 2.136 [2.131, 2.139] | 3.000 | 20480 |
| hot-control | 32 | 8 | 1 | dense96 | 2.075 [2.075, 2.077] | 1.570 | 16384 |
| hot-control | 32 | 8 | 1 | padded128 | 1.603 [1.599, 1.606] | 2.000 | 20480 |
| hot-control | 32 | 8 | 1 | split64+32 | 1.875 [1.874, 1.876] | 2.125 | 20480 |
| hot-control | 32 | 0 | 32 | dense96 | 1.328 [1.327, 1.330] | 1.500 | 16384 |
| hot-control | 32 | 0 | 32 | padded128 | 1.316 [1.313, 1.323] | 2.000 | 20480 |
| hot-control | 32 | 0 | 32 | split64+32 | 1.312 [1.310, 1.316] | 2.000 | 20480 |
| hot-control | 32 | 1 | 32 | dense96 | 2.064 [2.061, 2.065] | 2.000 | 16384 |
| hot-control | 32 | 1 | 32 | padded128 | 1.933 [1.928, 1.935] | 2.000 | 20480 |
| hot-control | 32 | 1 | 32 | split64+32 | 2.125 [2.121, 2.133] | 3.000 | 20480 |
| hot-control | 32 | 8 | 32 | dense96 | 1.498 [1.493, 1.499] | 1.570 | 16384 |
| hot-control | 32 | 8 | 32 | padded128 | 1.444 [1.441, 1.445] | 2.000 | 20480 |
| hot-control | 32 | 8 | 32 | split64+32 | 1.522 [1.522, 1.529] | 2.125 | 20480 |
| hot-control | 32 | 0 | 8 | dense96 | 1.164 [1.163, 1.165] | 1.500 | 16384 |
| hot-control | 32 | 0 | 8 | padded128 | 1.135 [1.133, 1.163] | 2.000 | 20480 |
| hot-control | 32 | 0 | 8 | split64+32 | 1.133 [1.133, 1.136] | 2.000 | 20480 |
| hot-control | 32 | 1 | 8 | dense96 | 1.952 [1.946, 1.956] | 2.000 | 16384 |
| hot-control | 32 | 1 | 8 | padded128 | 1.798 [1.797, 1.800] | 2.000 | 20480 |
| hot-control | 32 | 1 | 8 | split64+32 | 1.957 [1.956, 1.959] | 3.000 | 20480 |
| hot-control | 32 | 8 | 8 | dense96 | 1.380 [1.372, 1.387] | 1.570 | 16384 |
| hot-control | 32 | 8 | 8 | padded128 | 1.291 [1.288, 1.294] | 2.000 | 20480 |
| hot-control | 32 | 8 | 8 | split64+32 | 1.453 [1.450, 1.458] | 2.125 | 20480 |
| larger | 0 | 0 | 1 | dense96 | 150.290 [149.820, 150.711] | 1.500 | 1073741824 |
| larger | 0 | 0 | 1 | padded128 | 150.672 [150.419, 150.955] | 1.000 | 1431654400 |
| larger | 0 | 0 | 1 | split64+32 | 148.805 [148.518, 149.324] | 1.000 | 1073745920 |
| larger | 0 | 1 | 1 | dense96 | 150.730 [150.018, 150.976] | 2.000 | 1073741824 |
| larger | 0 | 1 | 1 | padded128 | 149.320 [149.150, 149.422] | 2.000 | 1431654400 |
| larger | 0 | 1 | 1 | split64+32 | 150.200 [149.992, 150.336] | 2.000 | 1073745920 |
| larger | 0 | 8 | 1 | dense96 | 150.306 [149.266, 151.211] | 1.563 | 1073741824 |
| larger | 0 | 8 | 1 | padded128 | 150.494 [150.328, 150.729] | 1.125 | 1431654400 |
| larger | 0 | 8 | 1 | split64+32 | 149.776 [149.549, 151.086] | 1.125 | 1073745920 |
| larger | 0 | 0 | 32 | dense96 | 29.717 [29.662, 29.740] | 1.500 | 1073741824 |
| larger | 0 | 0 | 32 | padded128 | 28.861 [28.854, 28.915] | 1.000 | 1431654400 |
| larger | 0 | 0 | 32 | split64+32 | 28.444 [28.335, 28.505] | 1.000 | 1073745920 |
| larger | 0 | 1 | 32 | dense96 | 42.446 [42.312, 42.561] | 2.000 | 1073741824 |
| larger | 0 | 1 | 32 | padded128 | 42.065 [42.028, 42.110] | 2.000 | 1431654400 |
| larger | 0 | 1 | 32 | split64+32 | 44.894 [44.886, 44.908] | 2.000 | 1073745920 |
| larger | 0 | 8 | 32 | dense96 | 32.631 [32.593, 32.676] | 1.563 | 1073741824 |
| larger | 0 | 8 | 32 | padded128 | 33.351 [33.329, 33.664] | 1.125 | 1431654400 |
| larger | 0 | 8 | 32 | split64+32 | 34.713 [34.572, 34.776] | 1.125 | 1073745920 |
| larger | 0 | 0 | 8 | dense96 | 29.031 [28.940, 29.105] | 1.500 | 1073741824 |
| larger | 0 | 0 | 8 | padded128 | 28.731 [28.698, 28.734] | 1.000 | 1431654400 |
| larger | 0 | 0 | 8 | split64+32 | 28.301 [28.204, 28.424] | 1.000 | 1073745920 |
| larger | 0 | 1 | 8 | dense96 | 40.409 [40.323, 40.581] | 2.000 | 1073741824 |
| larger | 0 | 1 | 8 | padded128 | 40.061 [40.018, 40.082] | 2.000 | 1431654400 |
| larger | 0 | 1 | 8 | split64+32 | 42.915 [42.851, 42.968] | 2.000 | 1073745920 |
| larger | 0 | 8 | 8 | dense96 | 32.475 [32.368, 32.683] | 1.563 | 1073741824 |
| larger | 0 | 8 | 8 | padded128 | 32.036 [32.022, 32.132] | 1.125 | 1431654400 |
| larger | 0 | 8 | 8 | split64+32 | 33.388 [33.311, 33.410] | 1.125 | 1073745920 |
| larger | 32 | 0 | 1 | dense96 | 149.845 [149.823, 149.997] | 1.500 | 1073741824 |
| larger | 32 | 0 | 1 | padded128 | 149.527 [149.189, 149.879] | 2.000 | 1431658496 |
| larger | 32 | 0 | 1 | split64+32 | 147.166 [147.027, 147.609] | 2.000 | 1073745920 |
| larger | 32 | 1 | 1 | dense96 | 150.911 [150.051, 151.591] | 2.000 | 1073741824 |
| larger | 32 | 1 | 1 | padded128 | 149.033 [148.716, 149.777] | 2.000 | 1431658496 |
| larger | 32 | 1 | 1 | split64+32 | 149.774 [149.630, 149.864] | 3.000 | 1073745920 |
| larger | 32 | 8 | 1 | dense96 | 149.514 [149.330, 149.655] | 1.562 | 1073741824 |
| larger | 32 | 8 | 1 | padded128 | 148.720 [148.503, 149.166] | 2.000 | 1431658496 |
| larger | 32 | 8 | 1 | split64+32 | 149.406 [149.386, 150.263] | 2.125 | 1073745920 |
| larger | 32 | 0 | 32 | dense96 | 29.753 [29.720, 29.889] | 1.500 | 1073741824 |
| larger | 32 | 0 | 32 | padded128 | 29.886 [29.780, 29.897] | 2.000 | 1431658496 |
| larger | 32 | 0 | 32 | split64+32 | 29.240 [29.223, 29.259] | 2.000 | 1073745920 |
| larger | 32 | 1 | 32 | dense96 | 42.496 [42.142, 42.559] | 2.000 | 1073741824 |
| larger | 32 | 1 | 32 | padded128 | 42.768 [42.646, 42.834] | 2.000 | 1431658496 |
| larger | 32 | 1 | 32 | split64+32 | 45.751 [45.641, 46.111] | 3.000 | 1073745920 |
| larger | 32 | 8 | 32 | dense96 | 32.685 [32.671, 32.736] | 1.562 | 1073741824 |
| larger | 32 | 8 | 32 | padded128 | 32.588 [32.533, 32.756] | 2.000 | 1431658496 |
| larger | 32 | 8 | 32 | split64+32 | 35.259 [35.221, 35.295] | 2.125 | 1073745920 |
| larger | 32 | 0 | 8 | dense96 | 28.980 [28.961, 29.042] | 1.500 | 1073741824 |
| larger | 32 | 0 | 8 | padded128 | 29.643 [29.545, 29.754] | 2.000 | 1431658496 |
| larger | 32 | 0 | 8 | split64+32 | 29.157 [29.091, 29.464] | 2.000 | 1073745920 |
| larger | 32 | 1 | 8 | dense96 | 40.368 [40.346, 40.567] | 2.000 | 1073741824 |
| larger | 32 | 1 | 8 | padded128 | 40.449 [40.429, 40.498] | 2.000 | 1431658496 |
| larger | 32 | 1 | 8 | split64+32 | 43.291 [43.176, 43.317] | 3.000 | 1073745920 |
| larger | 32 | 8 | 8 | dense96 | 32.488 [32.469, 32.525] | 1.562 | 1073741824 |
| larger | 32 | 8 | 8 | padded128 | 31.430 [31.375, 31.510] | 2.000 | 1431658496 |
| larger | 32 | 8 | 8 | split64+32 | 33.864 [33.825, 34.030] | 2.125 | 1073745920 |

## Limits

- Batch CLOCK_MONOTONIC_RAW wall timing; no per-load timers or PMU traffic.
- Line/page demand counts are per-operation address unions, not cache misses or transferred bytes.
- padded128 specifies a 128B stride; strict 128B row alignment requires a base phase divisible by 128.
- Compute rounds produce a consumed result, not a calibrated pre-load delay: extension addresses/conditions can be known earlier, so the compiler or CPU may issue their loads before the arithmetic completes.
- Footprints are labeled controls; capacity ratios alone do not establish residency.
- Idle sibling/background load is not enforced; placements are first-touch receipts.
- Only read consumers; no mutation, output materialization or migration costs.
- Sample min/max are observed ranges, not confidence intervals.
