# Phase-only profile of the unchanged prepared Zen5 executable

The profiling replay reproduces both large independent-hit gaps. It narrows the
investigation toward the next-key hash and primary-bucket path, but does not
prove the partial-register dependency hypothesis or improve graduation readiness.
No hashtable source or executable bytes changed in this job.

| K40/A10 hit case | Original median ns | Profiled median ns | Profiled p10–p90 | Hit samples |
| --- | ---: | ---: | ---: | ---: |
| Set exact composed | 2.895 | 3.337 | 3.254–3.406 | 738 |
| Set exact Direct | 6.998 | 8.010 | 7.859–8.155 | 1,836 |
| Multimap exact composed | 12.737 | 14.677 | 14.375–14.994 | 1,706 |
| Multimap wider-both composed | 5.039 | 5.925 | 5.745–6.090 | 705 |

The replay slow/fast ratios are 2.40× for sets and 2.48× for multimaps. Absolute
profiled times are roughly 15–18% above the original panel; these timings check
reproduction and do not replace unprofiled benchmark results. Each sequential
process executes 1,000 full original-driver repetitions, retaining all nine
phases and all contents checks. Logical bytes, populations and operation counts
match the original cases. There is no randomized profiling order or independent
machine repetition.

`cycles:u` was sampled in frequency mode at 997 Hz. The exact retained ELF SHA
is `9907b313adb693150c7717f758150bc2b981b1a566a672eeb588d59743f0a6ac`, build ID
`1d99b3271bbae9ab6ab16b4104062cdac4a512d2`. The
[parser](analyze.py) reads binary perf records, validates samples against the
native perf-script output and captured totals, and relocates each process using
its executable mmap plus the ELF executable LOAD segment. It retains only
addresses inside the [previously audited timed-hit loops](../prepared-native/codegen-notes.md).
Every retained sample resolves to an audited instruction on CPU 0 in user mode.
Warmup, other operations, and unrelated code are excluded from these distributions.

| Sample location category | Set composed | Set Direct | MM exact | MM wider-both |
| --- | ---: | ---: | ---: | ---: |
| Next-key hash and initial bucket location/count | 167 | 688 | 625 | 88 |
| Primary fingerprint path | 198 | 444 | 291 | 114 |
| Primary suffix path | 306 | 594 | 271 | 102 |
| Bag resolution and complete value consumption | — | — | 273 | 229 |

These are **sample counts at instruction locations, not causal cycle costs**.
The slow loops have more samples in their hash/primary path, while multimap bag
samples differ much less. Slow-set peaks include the bucket address calculation
at `0x813652` and suffix byte load at `0x8136af`; slow-multimap peaks include the
bucket-count test at `0x95750a` and suffix byte load at `0x95757f`. The sampler does
not uniquely identify BZHI as the cause. Other codegen/branch/load differences
remain, and there are no branch-miss or front-end/backend stall samples here.

The event has `precise_ip=0`, which permits arbitrary skid. Adaptive periods
vary: retained-hit minima/maxima range from about 3.1 M to 4.3 M cycles across
runs. Both unweighted counts and period-weighted distributions are retained;
neither is an instruction-level causal decomposition. Recorded hit-sample
density is 965–1,038 samples per summed timed-hit second, consistent with the
requested frequency but not proof of complete coverage. No LOST, LOST_SAMPLES,
THROTTLE or UNTHROTTLE records occur in any data section. Per-phase enabled/running
times are unavailable. Successful availability probes had full coverage for their
own short workloads; that does not establish this profile's coverage or IPC.

[analysis.json](analysis.json) retains mmap/relocation, record counts, sample
periods, input hashes and scope. [hit-samples.csv](hit-samples.csv) contains the
4,985 selected samples; [hit-addresses.csv](hit-addresses.csv) and
[hit-regions.csv](hit-regions.csv) provide auditable aggregations. [replay.csv](replay.csv)
retains every phase's median, range, p10/p90 and relation to the original timing.
Original 36,000 CSV rows, PMU availability receipts, job and host metadata remain
beside them. Binary perf data, native text dumps, recording logs and profile
runner source are recoverable through [artifact.json](artifact.json), with exact
commands in [context.json](context.json). The original ELF comes from its separate
native artifact; profile-runner and original-source digests are distinguished.

Job `20261013T201821Z-34e68e97` completed and its worker terminated. A full-width count-definition control remains a separate parent-owned
experiment; this receipt contains no modified-kernel measurement.
