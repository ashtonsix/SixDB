# Targeted retained-binary replay

Profile: neon. Job: `20260911T140609Z-bbd192d9`. Exact full-sweep binary; two process runs, seven pinned sequential 0.1-second repetitions per case. Cases were selected after the sweep for ratios above 1.4, excluding ordinary point loops and CPS. These are selected repeats, not independent coverage or new source.

| Case | Full sweep | Replay 1 | Replay 2 |
| --- | ---: | ---: | ---: |
| `overwrite-all/striped/k4/ikea2` | 1.476 | 1.471 | 1.469 |
| `overwrite-all/striped/k10/ikea2` | 1.410 | 1.418 | 1.420 |
| `overwrite-all/local/k28/ikea2` | 1.509 | 1.507 | 1.507 |
| `overwrite-all/local/k36/ikea2` | 1.477 | 1.478 | 1.477 |
| `overwrite/striped/k12/all/ikea2-coverage` | 1.591 | 1.592 | 1.592 |
| `placed/k63h16/substituted/all/sum-native` | 1.443 | 1.442 | 1.447 |
| `decode/striped/k5/ikea2` | 1.430 | 1.318 | 1.328 |
| `decode/striped/k7/ikea2` | 1.608 | 1.439 | 1.460 |
| `get16-ends/striped/k7/ikea2-range` | 1.524 | 1.543 | 1.563 |

`comparisons.csv` names each control; `samples.csv` retains every repetition. `inputs.json` pins original source/binary/member hashes; `replay.json` verifies them and records execution. The artifact retains raw benchmark JSON and host details.
