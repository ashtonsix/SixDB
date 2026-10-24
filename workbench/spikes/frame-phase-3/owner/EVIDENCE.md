# Owner-side qualification

2026-10-24. These are functional checks, not a native performance score.

| Check | Completed evidence |
| --- | --- |
| Canonical descriptions | 235 checks, optimized and ASan/UBSan; independent little-endian bytes, guarded strict prefixes, malformed topology/coordinates, sharing, unchanged output on refusal. |
| Public Orbital material | Two-TU optimized and fully instrumented Orbital/material ASan/UBSan checks; retained interpretation after builder/Scope/Store destruction, selected backing only, exact identity/resource checks, append and same-version adoption. |
| Group image admission | 1,953 valid images, 991 guarded truncated prefixes and 26 malformed cases; all widths0–64 and all presets. Optimized and ASan/UBSan validator runs pass; the matching Ikea archive is uninstrumented, with exact hardware guards checking native accesses. |
| Complete owner history | 804 checks on a frozen bound→pack candidate: ordered/repeated/restored writes, slack exhaustion, exact summaries/contribution, refusal rollback, sparse group1 attachment, stale basis and explicit retry, untouched Part reuse, append, pack-only conversion and old readers. |

The complete history uses Clang21.1.8, Linux AArch64 NEON, compact presets,
`-O1`, and matching current SeriesPack/core/Orbital archives. It is a
correctness build. The shared stage source includes `a7aa52e8`'s group-entry
prior/empty-area fixes and `3c4e42c0`'s checked preset capability. Source and
command receipts are in `build/frame-owner-core-v1/{source.json,compile.json}`.
The nine owner runtime source files were compared byte-for-byte with that
successful capture before this account was committed.

One successful seven-command batch copies3,680 source bytes and emits7,602
bytes. The history publishes12,638 fresh image bytes and reuses4,073 image
bytes; after conversion the first old reader alone retains an extra2,134
payload-capacity bytes relative to the selected alternative. These are this
fixture's accounting observations, not general compression or traffic claims.
Append and conversion deliberately use full rebuilds. Failed-attempt work,
private preparation allocations, material metadata and allocator/control-block
overhead are excluded from those payload counters.

`build/frame-owner-material/group-review/` retains validator/source/library
hashes and binaries; `build/frame-description-review/` and
`build/frame-owner-material/` retain the separate sanitizer checks. PMR live
bytes count only resource allocations; interpretation vector capacities must
be added once per unique `&control.meaning()` for a retained metadata account.

The original directory-wide `88cb8ee1` accidentally included unfinished owner
files. Its seam measurements do not qualify those files. This account records
the subsequent checks and fixes. The new multi-stream/intervals binding and
native timing of the owner path remain separate work; no bound→pack result
is presented as evidence for those extensions.
