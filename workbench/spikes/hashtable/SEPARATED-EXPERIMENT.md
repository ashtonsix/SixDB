# Separated controls: closed experiment

On 15 October 2026 Ashton chose to return to the co-located bucket hierarchy.
The quick experiment separated fingerprint controls from transposed key/value
groups, comparing a rectangle with a rectangular base plus bitmap-packed tails.
It did not earn replacement of the hierarchy. Its implementation is archived;
this page retains the decision, useful lessons and access to the full evidence.

## Findings carried forward

- A successful lookup depends on the selector result before locating its payload.
  Group pointers, payload stride and the residence of metadata matter as well.
  Abseil also separates controls: separation alone is not an explanation of loss.
- M=4 reduced payload slack and stride, but increased group metadata and allocation
  frequency. Removing packed tails did not remove the control-to-payload dependency.
  Reservation and geometric growth materially changed allocation histories.
- Existing-key mutation entered a large insertion-capable body, adding costs
  independent of layout. The hierarchy work carries forward the lesson about fused
  ready operations, not a universal requirement to split controls.
- Keep serial latency, reciprocal throughput and memory separate. Preserve cold
  Loom wins even when a candidate loses to the preceding hierarchy. Scratch/fixed
  geometry results do not qualify growth, retained-reader mutations or every kind.

The [current reflection](RECALIBRATION.md) and [graduation assessment](GRADUATION.md)
own the consequences. M, rectangles and packed tails are not production options.
Unfinished domains, container kinds, lifetime and composition requirements return
to the hierarchy; they were not rejected by this experiment.

## Representative cold comparison

Granite Rapids, K40 map with eight-byte values, 57.6 million keys, base-plane
candidate. These are one-repetition observations, not estimates of repeat
variability. C1 is an answer-dependent hit chain; C32 is reciprocal throughput of
32 dependent streams. Hierarchy/candidate use Loom hints; Abseil C32 is the better
of its matched direct and public-prefetch arms. This working set exceeds the
480 MiB LLC, without claiming that every request is served by DRAM.

| Metric | Preceding hierarchy | Abseil | Separated candidate |
| --- | ---: | ---: | ---: |
| Serial hit, ns/request | 233.33 | 205.49 | 344.57 |
| C32 hit, ns/request | 20.77 | 35.75 | 26.47 |
| Requested storage, B | 1,140,858,128 | 1,140,850,616 | 1,186,620,894 |
| Additional owner metadata, B | 576 | 16 | 16,778,760 |

The archived `COMPLETED-COLD.md` contains all matched hit/mixed variants on Zen 5,
Neoverse V2 and Granite, including C1, C32 and manual controls and memory accounting.
`RESULTS.md` owns the hot sets/maps/multimaps comparison, `WEAKNESSES.md` the
mechanisms and counterexamples, and `STATUS.md` the final coverage limitations.
These are historical cohorts; the [current scorecard](RESULTS.md) uses its own
identified configurations. Do not splice their timings or footprints together.

## Recovery

The [complete experiment archive](archive/separated-experiment.json) includes all
43 final files: sources, runners, documents, selected summaries and original native
archive references. Upload and download verification checked its SHA-256 and file
count before the top-level directory was removed. From the repository root on
Linux (prefix with `orb -m ubuntu` from macOS):

```sh
python3 workbench/tools/artifacts.py fetch workbench/spikes/hashtable/archive/separated-experiment.json build/recovered/separated-experiment
```

For exact measured binaries and their sibling modules, use the native job archive
references in the recovered `evidence/*/references.json`; those bundles contain
captured repository sources and original outputs. Historical manifests and runners
refer to their original directory names.

The [hierarchy pre-move archive](archive/hierarchy-before-move.json) and
[reference pre-move archive](archive/reference-before-move.json) also preserve
every file of those trees before path updates. They recover into fresh ignored
directories with the same `fetch` command. They are recovery snapshots, not new
benchmark runs or separate current specifications. Current live report recipes
and build paths use the consolidated root; retained measurements are unchanged.
