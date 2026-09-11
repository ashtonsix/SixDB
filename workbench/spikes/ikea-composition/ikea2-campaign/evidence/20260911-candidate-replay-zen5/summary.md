# Targeted retained-binary replay

Profile: zen5. Job: `20260911T140609Z-3526ecee`. Exact full-sweep binary; two process runs, seven pinned sequential 0.1-second repetitions per case. Cases were selected after the sweep for ratios above 1.4, excluding ordinary point loops and CPS. These are selected repeats, not independent coverage or new source.

| Case | Full sweep | Replay 1 | Replay 2 |
| --- | ---: | ---: | ---: |
| `overwrite/local/k7/all/ikea2-raw` | 1.403 | 1.323 | 1.295 |
| `overwrite/local/k7/all/ikea2-coverage` | 1.509 | 1.330 | 1.315 |
| `placed/k20h8/separated/partial/mutation-bound` | 1.498 | 1.416 | 1.482 |
| `placed/k20h8/interleaved/partial/mutation-bound` | 1.503 | 1.453 | 1.449 |

`comparisons.csv` names each control; `samples.csv` retains every repetition. `inputs.json` pins original source/binary/member hashes; `replay.json` verifies them and records execution. The artifact retains raw benchmark JSON and host details.
