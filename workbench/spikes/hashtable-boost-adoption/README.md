# Taking Boost's flat table into SixDB

Source/footprint assessment, October 20, 2026; memory and mixed-hit studies,
October 21. Current direction is to retain Ikea and improve its usability.
This inspects the same **Boost 1.92.0** used by the actual peer controls. These
studies change no maintained lookup kernel or representation. The source probe below
measures dependency and binary footprint. The subsequent
[memory-matched comparison](MEMORY.md) is the current throughput evidence:
Boost's native-memory lead does not imply a general read lead at comparable
budgets; Ikea retains significant ordinary-write and some read gaps.
The [mixed-hit diagnosis](HIT-MIX.md) isolates the warm branch-prediction cliff
and tests speculative Swiss traffic separately. This research does not propose
a container migration.

**Boost is a credible ordinary set/map foundation.** Using it unchanged for
private or exclusively owned tables is inexpensive. A pinned fork with storage,
mutation-admission and staged-read hooks is also credible; that work is distinct
from implementing a new probing algorithm. Fine-grained byte preservation is
the main integration cost. The public allocator API does not supply it.

The evidence does not justify requiring one custom table to win every workload.
Ikea's compact sets, selected independent reads and same-seed growth/accumulation
remain useful specializations. Boost trades some of those properties for a
general element layout and a mature ordinary mutable-table implementation.

## Source and dependency size

Counts are **physical source lines including comments and blank lines**, not
an estimate of code we would have to author. Counted from the verified release:

| Source scope | Lines |
| --- | ---: |
| Shared nonconcurrent engine: `detail/foa/core.hpp` + `table.hpp` | 3,075 |
| Public `unordered_flat_map.hpp` + `unordered_flat_set.hpp` | 1,695 |
| Flat map/set element policies | 144 |
| All reached `boost/unordered/` headers in the probe | 6,916 |
| Entire reached Boost header closure | 30,439 |

The compiler reaches **243 Boost headers, 907,658 bytes (0.87 MiB)** on the
tested Linux NEON, AVX2 and AVX-512 builds. This excludes standard-library and
compiler headers. It is a platform/compiler include closure, not a complete
portable vendoring manifest. The whole `boost/unordered/` implementation is
27,014 lines, including closed-addressing, node and concurrent containers that
this adoption does not require. Our current `ikea/hashtable/` headers total
4,497 lines, excluding their SeriesPack/TuplePack implementation dependencies.

Boost.Unordered is header-only. Its published direct dependencies are Assert,
Config, Container Hash, Core, Mp11, Predef and ThrowException; this compilation
also reaches Describe and Exception transitively. In the measured closure,
Unordered accounts for 6,916 lines, Predef for 8,563, Mp11 for 4,717 and
Config/its root headers for 3,976. Much of the apparent source bulk is reusable
portability and traits machinery, not additional table algorithms.

Use a pinned provider and a thin SixDB wrapper first. It needs no built Boost
library and does not require configuring or compiling the entire distribution.
The existing controls already fetched this release with an exact archive hash.
A production provider could use that source directly or a maintained dependency
subset; copying just the 243 observed headers would lose untested platform paths.

Separating the engine from Boost is technically feasible under C++23:
allocator/pointer traits, empty-base storage, bit utilities, assertions,
configuration attributes, integer typedefs and exception wrappers have standard
or project equivalents. Hash mixing must retain its measured behavior. Public
defaults, serialization glue, type checks and debugger helpers also reference
Boost facilities. A standalone port would therefore own several thousand lines
plus its replacements and tests. It would be a fork, not a 200-line extraction.
Keeping utility dependencies initially gives a much smaller upgrade diff.

The existing engine already separates element policy, hash/equality, allocator,
group/probing logic and backing arrays. Its public front ends can stay or be
replaced by a smaller SixDB-facing API. Direct use of `detail::foa::table` is
possible at this pin, but deliberately takes ownership of an internal interface.

The [Boost Software License](https://www.boost.org/LICENSE_1_0.txt) permits use
and modification; preserve its notices/license when vendoring source.
[Release and dependencies](https://www.boost.org/library/1.92.0/unordered/).

## Compiled footprint

Matched probes use C++23, Clang 21.1.8, `-O3 -g0`, assertions enabled, ordinary
exceptions/RTTI, hidden visibility, section garbage collection and no LTO.
AVX-512 targets `znver5`; AVX2 uses `x86-64-v3 -mtune=znver5`. These x86 binaries
are linked with LLD on the Linux ARM host and **were not executed**.

| Probe, Zen 5 AVX-512 target | Boost `.text`, bytes | Ikea `.text`, bytes | Boost stripped ELF, bytes | Ikea stripped ELF, bytes |
| --- | ---: | ---: | ---: | ---: |
| One W40/map8 instantiation | 6,238 | 15,120 | 17,512 | 24,384 |
| Five set/map instantiations | 29,004 | 43,248 | 54,648 | 57,528 |

The five-type palette is W24/set, W24/map8, W40/set, W40/map8 and W40/map64.
Both implementations perform construction, complete-value lookup, existing RMW,
erase, copy and iteration. Ikea uses its measured encoding/kernel selection,
and its used compiled SeriesPack/TuplePack dependencies are linked. The probe
does not instantiate descriptor dispatch, guarded mutation, growth or accumulation
on either side. The driver is included: empty baselines have 390/534 bytes of
`.text` and 5,168/5,440-byte stripped ELFs. These are example linked footprints,
not a prediction of SixDB's final marginal binary size. Other users may already
link the shared pack helpers; assertions and template signatures also add strings.

AVX2 `.text` is 6,409/15,952 bytes for Boost/Ikea map8 and 29,996/48,464 for the
palette. NEON `.text` is 5,004/13,480 and 23,676/41,728. Native ARM ELF padding
makes total file sizes poor indicators of these code differences. No Boost
shared library appears in the x86 probe's dynamic dependencies.

Single local consumer compile/link runs take roughly 1.7–2.3 seconds for Boost,
versus 1.1–2.2 for Ikea after its compiled support objects exist. These are
uncontrolled build observations, not a compile-throughput benchmark. Boost adds
no compiled support library. Its runtime geometry also avoids instantiating a
different table type for every bucket exponent, packed width and kernel.

## Integration seams

| Concern | Unchanged Boost | Work if retaining Ikea's stronger seam |
| --- | --- | --- |
| Unsigned W-bit domain | Use a 32/64-bit key and caller validation | Exact quotient compression/bitmaps would be a substantial representation fork |
| Ordinary sets/maps | `find`, `insert`, `insert_or_assign`, `erase`, iteration | Thin naming/result adapter; values may remain fixed byte arrays |
| Inline RMW | `find` then modify `it->second`; `try_emplace` returns the insertion/update position | Guarded callbacks can use a temporary value before publishing its stores |
| Allocation | Standard allocator parameter; one combined element/control allocation, including alignment padding | Track the allocation's physical resource, extent and retention; expose those extents to the owner |
| Capacity | `reserve`, `rehash`, `max_load`; normal insert may grow | Preflight growth outside bounded edits and report `needs_capacity` before entering them |
| Logical key locks | Caller-supplied exclusion around operations | Physical group/header protection still required; key grants alone are insufficient |
| Byte effects | Existing mapped-value addresses are public through iterators; metadata addresses/stores are private | Add a bounded structural-mutation plan and before-store hooks for metadata, entries and owner state |
| Coherent retained reads | Retain an immutable owner or a protected exclusive owner | Owner/allocator integration retains backing and interpretation across views/waits |
| Loom/AMAC | Synchronous lookup with internal candidate prefetch | Expose prepared home/group/tuple stages; ordinary flat-map API has no home-prefetch or resumable probe operation |
| Ordered accumulation | Loop over sources in caller order; `try_emplace`, then reduce an existing value in place | A thin operation can retain source order and caller-owned sizing/copying |
| Same-seed growth | Enumerates, hashes and relocates entries | Ikea's reversible quotient split and unchanged-encoding packing shortcuts do not carry over |
| Same-seed merge | Ordinary per-key insertion/update; no same-seed bulk shortcut | New algorithm work if preserving Ikea's specialized bucket-wise savings |
| Multimap | No flat multimap with the same engine | Caller-owned collection/handle updates fit RMW; owning packed bags remain separate work |
| Persistence | Native container owns pointers/allocator state; optional logical serialization is separate | It is not a stable on-disk or relocatable format; capture/recovery needs the owning representation's contract |

The [flat-map API](https://www.boost.org/doc/libs/latest/libs/unordered/doc/html/unordered/reference/unordered_flat_map.html)
exposes allocators but not backing geometry, node extraction or stable addresses
across rehash. Maximum load is internally managed. Ordinary capacity is
`15 * 2^g - 1`, with an initial maximum load of about 87.5% except tiny tables.
Erasure can lower `max_load()` to control probe drift, so a once-reserved
population bound alone does not prove indefinite churn will remain allocation-free.

`merge` has transfer semantics: it moves nonconflicting entries out of its source
and leaves conflicting entries there. It is not ordered reducer accumulation.
An external source-ordered `try_emplace`/reduce loop supplies our accumulation
semantics without changing the probing engine. Source order matters between
tables; iteration order within a unique-key source does not reorder two values
for the same key. Destination sizing/copying stay with the caller.

## MVCC: the consequential choice

Neither ordinary Boost nor Ikea's table implements MVCC by itself. Ikea provides
explicit `enter`/`prepare`/`before` hooks and allocation-free guarded structural
edits; the enclosing object supplies isolation and retained versions. Boost's
public API does not provide corresponding structural hooks.

**The reported Ikea performance comparisons do not invoke those hooks.** They
use the plain owner, with no key-lock context, preservation, damage journal,
snapshot resolution or per-lookup shared-ownership acquisition. The ordinary
`insert`/`find` paths do not branch on an MVCC flag. An owner allocation policy
can be supplied independently, but that does not enable guarded mutation.
Boost's current advantages therefore cannot be explained by charging MVCC only
to Ikea. Existing-value RMW and ordinary iteration also do not require MVCC.

**Unmodified Boost can be a versioned material** when the enclosing owner uses
private drafts/whole-owner copying or an independently valid page-preservation
scheme. Publish a coherent immutable/protected owner and retain it while readers
use it. Copies are O(capacity); Boost has a same-layout fast copy path for
eligible trivially copyable entries. That can be useful, but it does not make
whole-table copying a cheap point-write policy. A retained `shared_ptr` or custom
allocator alone neither freezes bytes nor versions index membership.

**Fine-grained byte-preserving edits need explicit integration.** Existing-value
RMW is relatively easy: after logical/physical admission, use the mapped address
for preservation and damage. For fixed byte values, run a possibly throwing
updater on a temporary and publish/store after it succeeds.

Structural edits touch more than the pair: insertion may mark overflow bits in
several groups, set the destination tag and change size; erase clears a tag and
can change both size and the maximum-load bound. The ordinary engine updates
those fields directly. An allocator's construct/destroy callbacks cannot observe
all these stores or replacements through `it->second`.

A narrow integration fork could retain the existing engine and add:

- backing/header access with live allocation identities and extents;
- a read-only insertion/erasure plan under exclusive/private physical admission;
- one complete fallible preservation/journal preflight before any live stores;
- infallible before-store notifications and a non-growing execution path.

The important ordering issue is that insertion currently sets overflow bits
while scanning for an available slot. Adding a callback immediately before each
store is insufficient for Ikea's **refusal leaves no effects** contract: all
fallible preparation must finish first. Pre-reserving capacity, collecting the
affected groups, then executing the plan would address that. Clear, reserve,
rehash, swap and raw mutable references must also have explicit boundaries, not
silently bypass the protocol. Initial integration can stay restricted to the
POD keys/byte values/handles already served by Ikea; arbitrary C++ value lifetimes
make byte-preservation semantics more demanding.

`concurrent_flat_map` is a different candidate, not an MVCC adapter. Its group
synchronization, visitation API and bulk reads do not supply snapshots, version
selection or byte preservation. Its performance must be measured separately;
the ordinary Boost results do not qualify it. Existing SixDB logical grants
would also have to compose with its locks. See the
[concurrent API](https://www.boost.org/doc/libs/master/libs/unordered/doc/html/unordered/reference/concurrent_flat_map.html).
The owning contracts remain [Ikea integration](../../../ikea/docs/integration.md),
[guarded table operations](../../../ikea/docs/hashtable/usage.md#integrate-retained-versions-and-byte-effects)
and [Orbital material](../../../orbital/docs/material.md).

### Expected performance after integration

These are implementation judgments, not measurements of a completed Boost fork:

| Path | Expected impact if the integration preserves the existing layout/engine |
| --- | --- |
| Plain reads/writes | Near-zero necessary extra work: retain ordinary entry points, put guarded planning on separate opt-in paths, and acquire owner lifetime once per useful batch |
| Reads of a retained version | Existing inner lookup can remain unchanged after the owner selects and retains coherent backing; version selection/lifetime has its own enclosing cost |
| Guarded existing-value writes | Admission, value preservation and damage publication add work to either table; Boost's mapped address is directly available |
| Guarded insert/erase | Complete preflight and structural metadata effects add real work; the peer write advantage needs a matched integrated measurement |
| Resumable/prefetched reads | A separate prepared/staged interface need not change ordinary `find`; scheduling and prefetch policy can alter the cold ranking |

Runtime callback dispatch, repeated shared ownership acquisition or forcing all
ordinary writes through a generic guarded plan would be avoidable costs, not
requirements of MVCC support. Ordinary-path assembly and native preservation
checks would test this. Copying Ikea's packed-primary representation into Boost,
by contrast, would change the algorithmic tradeoff that adoption is meant to buy.
Our existing plain-path work includes exact packed quotient search/write and
two-level fallback; it saves key storage and dependent reads in favorable cases,
but is not uniformly cheaper when the table is hot.

## What the existing performance evidence buys

Our [latest whole-table comparison](../hashtable-swiss-batch/GRADUATION.md)
establishes stronger Boost hot reads and ordinary construction/churn in many
K30 native-capacity cases. The subsequent [equal-budget study](MEMORY.md)
changes the read ranking materially and retains those native controls. Ikea
retains strong cold-set and miss-heavy advantages and specialized same-seed
growth/accumulation. The source/footprint probe itself adds no timing evidence.
Patching storage/hooks or changing hash/layout still needs a preservation check;
plain Boost measurements do not predict the complete MVCC or Loom composition.

Capacity rounding is material. For the ordinary hot K30 population of 13,926,
Boost reserves 30,719 slots (about 45% occupied), versus Abseil's 16,383. Boost
cannot fit it below the preceding capacity's initial maximum load. Reported
set/map8 allocations are **278,536/524,288 bytes** for Boost,
**147,432/278,456** for Abseil and **71,800/211,064** for Ikea. At the ordinary
large population, those become 272/512 MiB, 144/272 MiB and 72/208 MiB.
These ratios belong to those fixtures; they are not universal bytes/key claims.
Boost's hot lead is real for its native allocation, but is not a measured
memory-matched engine lead. A smaller population may stay below its preceding
allocation boundary. A lower-capacity policy fork would change probing and
growth assumptions and need its own evidence.

My recommended starting point, if adopting Boost, is **the pinned upstream
ordinary engine plus a thin owner adapter**, with a narrowly scoped hook patch
only where the actual preservation/staging contract requires it. Keep the
utility dependency and upstream tests. Do not simultaneously replace utilities,
change probing and promise the existing timing numbers. This makes the work
owning integration seams rather than maintaining another general hash algorithm.
It also makes the forfeited compression and same-seed shortcuts explicit.

## Evidence and reproduction

[Footprints](evidence/footprint.json), [reached header hashes](evidence/boost-headers.json)
and [key source identities](evidence/source.json) retain the numerical inputs.
The release archive was locally SHA-256 verified as
`5c1d40cb8e19adbf740a4ec2da35b3e58f3f5804b1dce44deb53df72193cbc6c`.
Both native palettes match an independent scalar checksum oracle at populations
1/1,024/8,192 and seeds 713/991/1729. This exercises the probe, not the complete
upstream or Ikea API. No throughput, MVCC or concurrency qualification is claimed.

```sh
orb -m ubuntu python3 workbench/spikes/hashtable-boost-adoption/audit.py \
  --boost build/clang/dev/_deps/swiss_boost-src \
  --output build/hashtable-boost-adoption
```

`--boost` can name another extraction of the same pinned archive. The script
writes raw `results.json` and `boost-headers.json`; retained footprint evidence
adds source/archive and binary fingerprints. x86 linking requires the existing
cross GCC sysroot/toolchain and LLD. Local build observations vary with the host.
