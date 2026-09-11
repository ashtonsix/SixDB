# Targeted retained-binary replay

Profile: avx2. Job: `20260911T140609Z-6297e9ad`. Exact full-sweep binary; two process runs, seven pinned sequential 0.1-second repetitions per case. Cases were selected after the sweep for ratios above 1.4, excluding ordinary point loops and CPS. These are selected repeats, not independent coverage or new source.

| Case | Full sweep | Replay 1 | Replay 2 |
| --- | ---: | ---: | ---: |
| `overwrite/striped/k12/all/ikea2-coverage` | 1.472 | 1.449 | 1.463 |
| `decode/local/k28/ikea2` | 1.441 | 1.433 | 1.437 |
| `get16-ends/striped/k3/ikea2-region` | 1.523 | 1.558 | 1.566 |

`comparisons.csv` names each control; `samples.csv` retains every repetition. `inputs.json` pins original source/binary/member hashes; `replay.json` verifies them and records execution. The artifact retains raw benchmark JSON and host details.
