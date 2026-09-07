# Shared dirty-buffer measurements

Manual full-cycle ns/update: writes + batch-boundary queries + full drain/reset.
Independent phase medians need not sum to the cycle median. ARM-VM affinity is not proof of physical-core placement.

| Case | Writers | Method | Cycle ns/update | Write | Read | Replay | Reset | Spread |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| spread | 4 | eager_atomic | 153.53 | 152.96 | 0.57 | 0.00 | 0.00 | 11.8% |
| spread | 4 | eager_striped | 124.08 | 123.47 | 0.61 | 0.00 | 0.00 | 2.7% |
| spread | 4 | word_check_span | 233.76 | 170.71 | 5.29 | 57.06 | 0.58 | 5.5% |
| spread | 4 | read_flush_span | 235.97 | 170.38 | 0.57 | 64.54 | 0.47 | 5.1% |
| spread | 4 | append_scan | 129.71 | 61.08 | 10.37 | 57.71 | 0.22 | 2.6% |
| spread | 4 | append_flush | 148.58 | 60.36 | 0.58 | 86.27 | 0.37 | 2.5% |
| hot | 4 | eager_atomic | 147.78 | 146.78 | 1.00 | 0.00 | 0.00 | 1.9% |
| hot | 4 | eager_striped | 125.02 | 124.01 | 1.05 | 0.00 | 0.00 | 3.2% |
| hot | 4 | word_check_span | 85.29 | 71.13 | 2.68 | 6.39 | 5.28 | 2.9% |
| hot | 4 | read_flush_span | 85.32 | 72.45 | 1.03 | 6.82 | 4.75 | 3.1% |
| hot | 4 | append_scan | 75.50 | 63.10 | 5.70 | 6.64 | 0.10 | 2.0% |
| hot | 4 | append_flush | 74.00 | 65.78 | 1.05 | 7.05 | 0.14 | 3.4% |
| points | 4 | eager_atomic | 150.18 | 147.81 | 2.37 | 0.00 | 0.00 | 3.1% |
| points | 4 | eager_striped | 123.49 | 121.01 | 2.41 | 0.00 | 0.00 | 3.0% |
| points | 4 | word_check_span | 244.60 | 176.44 | 9.63 | 56.93 | 1.60 | 19.3% |
| points | 4 | read_flush_span | 239.97 | 171.28 | 2.92 | 66.31 | 0.98 | 4.3% |
| points | 4 | append_scan | 209.51 | 62.56 | 86.42 | 58.77 | 0.19 | 5.4% |
| points | 4 | append_flush | 151.03 | 61.56 | 2.32 | 86.89 | 0.30 | 5.5% |
| hot4096 | 4 | eager_atomic | 98.55 | 98.25 | 0.30 | 0.00 | 0.00 | 20.1% |
| hot4096 | 4 | eager_striped | 70.85 | 70.50 | 0.29 | 0.00 | 0.00 | 5.5% |
| hot4096 | 4 | word_check_span | 29.94 | 22.31 | 1.47 | 5.33 | 0.83 | 5.2% |
| hot4096 | 4 | read_flush_span | 29.29 | 22.74 | 0.29 | 5.53 | 0.79 | 6.0% |
| hot4096 | 4 | append_scan | 25.26 | 16.21 | 3.80 | 5.23 | 0.03 | 10.0% |
| hot4096 | 4 | append_flush | 21.95 | 16.32 | 0.26 | 5.36 | 0.05 | 5.3% |
| hot_roots | 4 | eager_atomic | 152.60 | 150.80 | 1.80 | 0.00 | 0.00 | 1.9% |
| hot_roots | 4 | eager_striped | 123.53 | 121.55 | 1.98 | 0.00 | 0.00 | 7.1% |
| hot_roots | 4 | word_check_span | 126.67 | 67.83 | 46.44 | 6.43 | 6.50 | 1.7% |
| hot_roots | 4 | read_flush_span | 83.50 | 69.98 | 1.75 | 6.79 | 4.95 | 15.9% |
| hot_roots | 4 | append_scan | 120.30 | 65.57 | 47.66 | 7.10 | 0.18 | 2.3% |
| hot_roots | 4 | append_flush | 72.43 | 63.67 | 1.71 | 7.05 | 0.14 | 4.9% |
