# Allocation metadata controls

These are isolated comparison controls, not a promoted allocator design. The
performance study measures complete operations while these treatments reduce
the backing layer's costs; neither a smaller record nor a passing history alone
settles the choice.
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

## Further treatments

Apply these after the three-control patch, in order. Each treatment keeps its
own source cut; production Allocation is unchanged.

| Treatment | Populated A Pool metadata | What changes |
| --- | ---: | --- |
| [Storage16](storage16.patch), [qualification](storage16-qualification.json) | 15,040 B | One active Arena pin per Group; wide Window state stays with Window. |
| [Pure growth](growth.patch), [qualification](growth-qualification.json) | 15,040 B | Deletes protected-footprint tracking and implicit growth writes. Storage remains 16 B after alignment. |
| [Group advice](../heat/README.md) | 10,456 B | Advisory heat follows the Group, matching current reclamation granularity. |

Numbers are observed requested Pool metadata on the 4 KiB AArch64 guest, before
Arena/Pool setup, caller state and Object/Representation metadata. Even the
smallest Pool does not establish the complete 64-Object 4–8 KiB target.
Target-machine timing and complete collection costs remain separate evidence.

Growth retains physical bytes; it does not decide whether a reader or writer
may access their contents. The caller initializes new body/padding after
overlapping readers finish. The qualified native and sanitizer cuts pass all
23 runtime checks, 384 encoded collection rows and 16 migrated caller checks.
Their sources and logs are in the [growth archive](growth-artifact.json).

Group advice removes 4,584 B per Group without a demonstrated local point-operation
penalty. Spatial epochs add cold-page discrimination but have not earned adoption.
Neither policy can replace physical lifetime authority. Shared routing and
smaller physical-use records remain separate active comparisons.
