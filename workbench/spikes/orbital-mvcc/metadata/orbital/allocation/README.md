# Allocation metadata controls

These are isolated comparison controls, not a promoted allocator design.
[The frozen census](../allocation-worker.md) counts a 256-identity Group at
30,616 / 34,392 requested metadata bytes for palettes A / B. The
[prototype patch](compact-controls.patch) changes three independent costs:

| Change | A saving | B saving | Consequence |
| --- | ---: | ---: | --- |
| Bindings and owner metadata share one rounded mapping | 4,096 B | 4,096 B | Payload reservation excludes the control mapping; failed retirement remains retryable. |
| Cold trim inventory uses 16-bit first/count | 3,816 B | 6,576 B | Wide preparation spans remain; geometry and complete trim lists are checked before mutation. |
| Hint timestamps and optional temperature have separate planes | 3,584 B | 3,584 B | Full timestamps and owner/foreign handoff semantics remain unchanged. |
| **Resulting Group metadata** | **19,120 B** | **20,136 B** | Storage and two full timestamp planes remain substantial. |

Actual 64-Object Tuple1 / Series4 collections have identical logical bytes,
physical-use counts and retained payload capacity in both controls. The
independent-column eight-publication case grows three Groups: their metadata
falls from 103,176 to 60,408 B. These are requested capacities, not RSS or a
timing claim. Arena/Pool setup, caller Lane, OS thread state and physical
Representation metadata are additional. This does not meet the whole-collection
4–8 KiB target.

The [qualification receipt](qualification.json) identifies the nine changed
files and two captured runs. Both native and fully instrumented
ASan/UBSan/LSan builds pass 11 Orbital and 12 Loom runtime checks plus the actual
encoded collection matrix: late discovery, both writer Scope lifetimes,
old/current data through eight publications, Store teardown and narrow escape.
Compact spans also exercise all six geometries, malformed middle ranges,
boundary endpoints and mixed discard outcomes. Hint tests cover partial advice,
full-width timestamps and out-of-order foreign publishers.

An earlier sanitizer run exposed a test setup call inside `assert` under an
explicit `-DNDEBUG` build. Test targets now force assertions on; the qualified
runs include that correction. No implementation failure was suppressed.

## Repeat or compose

Apply the patch in an isolated checkout of `9f9e1c09`, or first check that its
context still matches the intended Allocation control:

```sh
git apply --check workbench/spikes/orbital-mvcc/metadata/orbital/allocation/compact-controls.patch
git apply workbench/spikes/orbital-mvcc/metadata/orbital/allocation/compact-controls.patch
cmake --preset release
cmake --build --preset release --target orbital_validate loom_validate
```

Use `orb -m ubuntu` from macOS. The captured `runner.py` repeats the full
collection accounting with the source snapshot, pinned compiler and explicit
sanitizer settings. Sources, full JSONL and logs are recoverable from the verified
[native bundle](native-artifact.json) and
[sanitizer bundle](sanitizer-artifact.json); the patch and compact receipt belong
here. Fetch either with `workbench/tools/artifacts.py fetch` into ignored output.

The next question is whether trim needs identity-level heat at all. Group advice
is the first comparison; spatial rolling epochs are a fallback for selective
reclamation. Heat is advisory and cannot replace physical lifetime authority.
Storage packing/shared routing is a separate structural comparison, with actual
range limits and overflow behavior, rather than assumed tiny counters.
