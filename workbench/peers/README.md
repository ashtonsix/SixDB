# Native peers

Use the real library directly. A peer dependency does not prescribe the
workload's records, encodings, transaction lifetime, flags or timed interval.

For installed LMDB (`liblmdb-dev` on Ubuntu):

```cmake
include("${PROJECT_SOURCE_DIR}/workbench/peers/lmdb.cmake")
target_link_libraries(my_comparison PRIVATE workbench::lmdb)
```

The imported target supplies only the actual native library and headers. It
does not build an old Orbital driver or impose a virtual Store/Record interface.
Record `mdb_version()` and the actual environment flags/map size with results;
the system package is discovered, not silently treated as a pinned peer version.
Keep begin/commit/abort, fresh versus retained snapshots, reserve/borrowed-byte
access and complete value consumption visible in the caller.

The [small native check](lmdb-check.cpp) demonstrates retained/fresh/reset-renew snapshot
visibility, abort during unwinding, cursor results and exact reserved bytes.
It is a correctness example, not a benchmark or a selected comparison policy.
Run `python3 workbench/tools/check.py lmdb` on Linux to configure, link and run
it with the pinned compiler in an isolated temporary directory. Prefix with
`orb -m ubuntu` from the Mac. No cloud worker or package installation occurs.

The maintained Hashtable comparison already shares a
[verified Abseil provider](../benchmarks/hashtable/abseil.cmake). Reuse it when
that exact peer fits; do not fork a second acquisition/build recipe. Dataset
preparation and identity stay with the [existing dataset tools](../datasets/README.md).
