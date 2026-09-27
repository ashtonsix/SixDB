# Native reservation-policy evidence

This bundle retains the corrected 420-history comparison: 360 main cases,
24 longer sparse-WAN streams, and 36 actual held-broad controls. `FINAL.json`
contains all-offer accounting. There were zero errors, safety violations, event
budget stops or missing incidents. All 23,916 offers arrived; 21,141 completed
and 2,775 were unfinished only in authored 10 ms prefixes. Censoring is retained.

The findings remain directly beside this export in `../../NATIVE.md`,
`../../native-selected.json` and `../../native-provenance.json`; they are not
duplicated in the Git export. Their pre-retention snapshots and hashes are in
`finding-companions/` and `SOURCE.json` inside the archive. `REPORT.md` describes
the original isolated experiment; the maintained policy was promoted later.

## Contents and exact identities

* `source-v2/` and `source-v2.capture.json`: every captured source body/mode/hash,
  excluding worktree .git plumbing.
* `campaign-v2/` and `long-stream-v2/`: every case argument vector, scheduler
  choices, output, exit/status, complete transaction phases, aggregate result
  journals and source/binary provenance. There are 384 recorded choice streams.
* `held-broad-v2.jsonl`, `held_broad`, `held_broad.cpp` and associated provenance/
  build receipts: the remaining 36 cases. This wrapper did not record scheduler
  choices or full phase streams; it retained seed/config, result/telemetry,
  actual local-fix and first-overlap-grant times, and independently asserted
  holder exclusion and independent progress. Do not claim choice replay for it.
* `final-build-v2/`: exact executables/static libraries, build rules, compile
  commands and build log. Compiler object/cache directories are omitted.
* Original study/validation/analysis harnesses, final hashes and check logs.
* `campaign-v1/` and `superseded-v1/`: 110 partial superseded result rows,
  correction rationale and exact v1 source reconstruction metadata. The first
  younger-WAN shape lacked a B-only scope during the younger holder's drain.
  V1 per-case choice streams and old binaries were not selected for this bundle;
  the untouched original local run retains them. No v1 rows enter FINAL.json.

`SOURCE.json` provides the compact identity/accounting index. `MANIFEST.json`
hashes every other archived member. The artifact tool additionally hashes and
download-verifies the complete compressed S3 object. All source and executable
identities were checked before staging; stage members use copy-on-write clones.

## Recovery and reproduction

From the repository root on Linux (prefix `orb -m ubuntu` from macOS):

```sh
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/orbital-reservation-policy/evidence/native-policy \
  build/recovered/native-policy
python3 build/recovered/native-policy/verify.py
```

Archived case argv contains original absolute/relative build paths. To replay
a regional case, use the recovered exact binary and its recorded `--replay`
choice file, replacing the old output `--choices` path; preserve case options.
The source receipt records hashes independent of the former checkout location.
`verify.py` checks all member hashes, source identity/modes, exact binaries and
420-case accounting without Git; `capture.py verify` requires a live worktree.
Original harness paths also need adjustment after relocation; the maintained
`../../native.py` grid runner offers programmatic case generation for new runs.

Consumer CPU uses a fixed per-record model; policy probes/scans are separately
counted, not calibrated latency. These finite histories establish neither
sustainable throughput nor history reclamation. Admission is static full-body
leader-plus-follower; coordinator outcome persistence remains local. Native
pre-position cancellation is absent. Policies are fixed per run/lineage, with
no in-place replay-policy migration and no claim of identical cross-policy
wire sequences. The selected report includes regressions and direct-holder
delays as well as waiter-convoy improvements.
