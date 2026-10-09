# Allocation heat without per-identity timestamps

Try Group-wide advice first. Current `trim_cold` consumes `Group::last_use`, so
per-identity timestamps have no demonstrated reclamation benefit in that policy.
The small runtime treatment saves **4,584 B per Group** and shows no clear local
point-operation penalty. Spatial recency can distinguish hot and cold pages
within a Group, but has not earned production adoption through these traces.
None of this advice grants access, retains storage, or makes a page reclaimable.
Allocation users, occupied Homes and trim state remain authoritative.

## The usable treatment

[`group-advice.patch`](group-advice.patch) changes two runtime files in the
captured GrowthRequest + Storage16 candidate. [`base.json`](base.json) identifies
its exact headers and seven runtime TUs. Main production and live worktrees are
untouched. Root/Tooling own target-machine integration and the composed screen.

The owner merges supplied advice directly into existing `last_use`. Foreign
releases merge into one bounded record under the existing Domain mutex and post
the existing return Message, including non-final releases. Its existing Arena
pin protects the route. The callback merges that record independently of
returned identity bits. Advice arriving between handle and release reposts;
a stalled publisher makes both owner callback stages try-lock and defer.
Owner-local advice takes no mutex, posts no Message and reads no clock.

This is Group-wide advice, not exact per-identity semantics. Supplied timestamps
reject Group-stale updates; omitted fields preserve prior fields. Unstamped
advice follows arrival order, and local advice is applied before final retirement.
A newer timestamp on one identity can make another identity's older temperature
update stale. A delayed unstamped cold hint can overwrite a recent hot hint.
Those effects can change reuse/reclamation preference, never lifetime safety.

Actual measurements against the matched captured runtime:

| Requested bytes | Per-identity control | Group advice | Change |
|---|---:|---:|---:|
| `sizeof(Group)` | 9,104 | 4,520 | −4,584 |
| One populated A Pool, public metadata | 15,040 | 10,456 | −4,584 |
| One populated B Pool, public metadata | 16,056 | 11,472 | −4,584 |

These public Pool counters do not include all Arena/thread/libc setup. The two
hint planes (4,608 B in total) become a foreign `ReleaseHint` and pending flag; padding
makes the replacement 24 B. Existing `last_use` and queue records remain charged.

Two pinned local AArch64 guest sweeps, 2M warm allocate/release cycles per case,
gave control 47.7–56.1 ns and treatment 46.3–49.6 ns across 64/1,536 B bodies and
empty/supplied hints. This is a weak directional wall-time screen, with visible
noise. It excludes data access, concurrency and latency distributions; it is
not a composed database or a target-machine performance claim.

## Spatial fallback

[`heat.h`](heat.h) / [`heat.cpp`](heat.cpp) compare the bounded Group aggregate,
a whole-Group epoch bit, and physical recency. Two banks hold one presence bit
for the current epoch and one for the previous epoch. They do not encode exact
HOT/WARM/COLD plus timestamp order. Small Homes mark every crossed 128 B cell;
larger Homes have one bit per physical Home. Cold release never clears a bit.
Heat stays with physical bytes when an identity is reused elsewhere.

Owner updates use atomic OR because distinct identities can share a word.
Foreign bank selection and OR hold the existing Domain mutex together. Owner
rollover try-locks that mutex and can defer; neither update reads a clock or
allocates. Supplied access times map against an externally advanced global epoch
boundary. Missing times mean arrival recency, so late unstamped advice can look
recent. HOT and WARM both mark; COLD declines to mark. Marks survive one rollover
and expire on the second. These are advisory trade-offs, not a proposed contract.

At 256 identities / 4 KiB native pages:

| Prototype storage | A | B |
|---|---:|---:|
| Spatial banks, total request | 768 B | 960 B |
| glibc usable bank allocation | 776 B | 968 B |
| Whole-aperture two-bank alternative | 5,088 B | 8,768 B |
| Existing combined-control occupied bytes | 1,968 B | 2,488 B |
| Occupied bytes with spatial banks appended | 2,736 B | 3,448 B |

Both proposed in-map placements still round to the existing 4 KiB control map.
That is layout arithmetic from the frozen Grid62/Page1 geometry, not an
implemented saving: the prototype actually allocates separate banks and has a
72 B header, including 16 B observer counters. Whole-Group epochs allocate 16 B
banks (24 B glibc usable). The standalone Group aggregate header 104 B includes a separate Message,
routing pointers and 24 B counters; the runtime patch reuses those facilities.
Reserved body VA (A 2,605,056 / B 4,489,216 B) is reported separately from mapped
body pages. Sparse traces still pay the fixed bank capacity.

[`trace.cpp`](trace.cpp) uses real allocations at 48/64/1,536 B, 8 and 256 users.
Single-page sparse small bodies offer no page choice. With 3/4/96 mapped pages,
or sparse 1,536 B bodies over 3 pages, spatial marks select a cold page while coarse
recency ties all pages. Current production trim also uses coarse Group advice;
this is an additional capability, not proof of a regression from simplifying it.
Page ranking uses captured Home spans as an observation oracle. Its fixture
vector is explicitly reported as fixture request/usable bytes, not runtime
metadata. Its traversal is not a production sweep-cost
measurement. A real page-ranking path and benefit under pressure remain untested.

Local prototype updates usually cost about 1.6 ns for Group merge versus 3.5–3.9 ns
for spatial OR. Uncontended foreign updates were about 12 ns for the coalesced
Group prototype and 11–12 ns for spatial banks; no owner callback ran inside
those timing loops. A/B rollover clears 384/480 B per epoch in roughly 24–31 ns. These
are isolated warm operations on this guest with an already supplied Home;
lookup of the current physical range is additional work. Small-bank geometry is not enough to
establish that fine advice wins overall.

## Checks and reproduction

[`runtime.cpp`](runtime.cpp) checks actual partial/stale advice, non-final foreign
publication, owner-final release before drain, a stalled publisher, advice between
handle/release, and zero observed C++ allocations inside releases.
`trace.cpp` also checks a 48 B Home crossing two cells, different identities
sharing a word, real identity reuse, initial/late timestamped advice, delayed
unstamped advice, held bytes and failed trim. All checks execute under `NDEBUG`.
Native `-O3` and fully instrumented ASan/UBSan/LSan `-O1` runs passed.

The [qualification receipt](qualification.json) names the exact candidate source
and separates the child's recorded histories from command-by-command capture.
Sources, spatial outputs and local timing sweeps are recoverable from the
[verified archive](artifact.json); binaries remain disposable local output.
To build against the patched capture, set
`src=build/orbital-mvcc-metadata/heat/growth-group-advice/orbital` and compile
`runtime.cpp`, `$src/src/{types,worker}.cpp` and `$src/src/allocation/*.cpp` with
C++23, its include/source-allocation directories, `-pthread` and
`-Wl,--wrap=pthread_mutex_lock,--wrap=pthread_mutex_trylock`. The spatial trace
also compiles `heat.cpp`; [`timing.cpp`](timing.cpp) gives the small local screen.
Use one matched source/header tree; older library and current private-record
headers are not interchangeable. No production heat change is promoted here.
