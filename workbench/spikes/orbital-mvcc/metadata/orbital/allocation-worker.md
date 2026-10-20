# Allocation and Worker metadata census

Measured from **7f73c818548c9c52b53818f2eb5af3c1d2451124**, not the moving
Stage2 source. The probe is [allocation_worker.cpp](https://github.com/ashtonsix/SixDB/blob/c8f9081104ef61ee897fbbaf954223ae0a3d7656/workbench/spikes/orbital-mvcc/metadata/orbital/allocation_worker.cpp).
Linux AArch64 under OrbStack, Clang **21.1.8**, C++23, `-O3`, no LTO,
libstdc++ **15**, glibc **2.42**, 4 KiB native pages. These are layouts and call
boundaries, not timings or a claim about Zen5. The standard library is not pinned.

The one-byte binding plane does **not** mean one byte of allocation metadata.
A 256-identity Group reserves **30,616 / 34,392 B** of charged metadata for
palettes A / B. Half its 16,768 B C++ record is Storage; the other half is mostly
two hint arrays. This deserves consideration beside per-Version metadata.
There is no additional physical-use record for each Hold: Holds share Storage.

## Public records and actual returns

`T` below means trivially copyable and trivially destructible; `D` means
nontrivial destruction; `S` means a stable non-copyable record. These traits
describe C++ mechanics, not permission to copy linked Signals or ownership.
`Result<T>` is the actual `std::expected<T, Error>` specialization. Expected's
overall trivial-copy trait is false here even for pointer values; its trivial
move and destructor still permit register returns.

| Type | Bytes / alignment | Fields and retained state | Use / lifetime | Return from forced ordinary boundary |
| --- | ---: | --- | --- | --- |
| `AllocationRequest` | 40 / 8, T | Four size words: body, reserved extent, alignment, padding; initialization enum | Creation/growth/preparation input, passed by reference | Indirect when returned; real API takes const reference |
| `Allocation` | 32 / 8, D | Storage pointer, address, body size, padding | Move-only physical use until release; hot allocation churn, no per-read requirement | `sret`; `Result<Allocation>` 40 B also `sret` |
| `Hold` | 32 / 8, D | Storage pointer, address, relative footprint offset/size | Explicit retained physical alias; final release can post owner completion | `sret`; `Result<Hold>` 40 B also `sret` |
| `Ref<Arena>` | 8 / 8, D | Intrusive Arena pointer | Pins the entire Arena lifetime; copy increments Arena references | **`sret` despite 8 B**; `Result<Ref<Arena>>` 16 B also `sret` |
| `Result<Pool*>` | 16 / 8 | Pointer or Error, discriminator | Setup result; no ownership itself | Two 64-bit registers |
| `Result<void>` | 8 / 4 | Error/discriminator | Status at setup, mutation, construction boundaries | One 64-bit register |
| `ReleaseHint` | 16 / 8, T | Optional temperature (8 B), last-access time (8 B) | Optional release advice; source age rejects stale hints | Two 64-bit registers; actual release argument also register-shaped |
| `Statistics` | 104 / 8, T | Thirteen 64-bit counters | Cold inspection; not a native access packet | `sret` |
| `PreparedBytes` | 24 / 8, T | Mutable span (16 B), readable-padding size | Helper's exclusive construction interval | Indirect return; actual constructor callback takes an indirect aggregate argument |
| `Preparation` | 192 / 8, S+D | Owner/backend/preparer pointers; Signal; Allocation; state/error/flags; request; Outcome; constructor/context | Caller-owned cold record stable through physical settlement and owner delivery | No value-return API |
| `Preparation::Outcome` | 32 / 8, T | Mapping/address pointers, prepared extent, success/error | Helper result inside Preparation, transferred by queued completion | No public return API |
| `Arena` | 32 / 8, S+D | Refcount, byte limit, charged bytes, ArenaState owner | Shared setup, outlives active physical uses and queued work | Ref returned; not the Arena value |
| `Pool` | 64 / 8, S+D | Arena/Lane pointers, state, 32 B Signal, PoolState owner | One stable owner Lane; retain Lane through close | Pointer returned |
| `Progress` | 16 / 8, T | Signal pointer and full generation, or terminal status in same word | Transient close/trim/retry observation; owns nothing | Two 64-bit registers |
| `Bytes` / `MutableBytes` | 16 / 8, T | Address and length | Borrowed native access interval | `bytes(Allocation)` returns two 64-bit registers |
| `ByteRange` | 16 / 8, T | Offset and size | Footprint input; owns nothing | Two-word argument in actual Hold wrapper |
| `Scope` | 32 / 8, T | ProgramId (16 B), borrowed encoded span (16 B) | Owner-defined scheme, not allocator state | Not used on this allocation path |
| `ObjectId` / `PartId` | 16 / 8, T | Two 64-bit identity words | Logical identity, independent of finite identity below | Not returned by allocation |
| `Epoch` / `SerialPosition` | 8 / 8, T | One coordinate word | Logical coordinate, not a physical lifetime | Not returned by allocation |

The generated LLVM signatures classify ordinary return boundaries explicitly:
`aw_return_ref` has `sret`, `aw_return_pool` returns `[2 x i64]`,
`aw_return_progress` returns `[2 x i64]`, `aw_return_status` returns `i64`.
The native assembly uses the corresponding AArch64 indirect-result address
for `sret`. A smaller RAII handle alone does not obtain an ordinary register
return. A borrowed packet and an owned packet are different lifetime choices.
Inlining can remove a particular boundary; this probe deliberately retains it.
No recommendation to add output parameters or change every owner ABI follows
from the classification alone.

## Persistent backing and cold records

| Type | Bytes / alignment | Fields / ownership role | Frequency and lifetime |
| --- | ---: | --- | --- |
| `Storage` | 32 / 8, D | Domain* (8), Arena Ref (8), maximum held extent (8), atomic users (4), identity (2), foreign-hint flag (1), padding (1) | One per live/reusable finite identity, or embedded in Window. Preallocated, not one heap/refcount header per Hold. Arena pin begins with first use and ends after final owner recycle |
| `Domain` | 112 / 8, S+D | Pool*, context*, finite flag; 24 B return Message; **48 B mutex**; queued flag; Arena Ref | One per Group/Window. Local ordinary use bypasses mutex; foreign return and hint publication share it. Queued pin survives physical return delivery |
| `Group` | 16,768 / 8, S+D | Domain; owner/list links; FiniteGroup owner; **256 Storage = 8,192 B**; **two 256 ReleaseHint arrays = 8,192 B**; live/return/trim state; Job/Message; retirement pins/counts | Long-lived reuse inventory. All fixed identity/hint slots exist even with one allocation. `uses` starts at offset 144; owner hints at 8,336; foreign hints at 12,432 |
| `Window` | 328 / 8, S+D | Domain; Storage; inventory/list links; mapping/address/reserved/prepared/charged extents; two hints; Job/Message; retirement pin/flags/charge; optional Preparation* | One reserved larger/aligned region, retained idle until trim. Used above finite cutoff or for incompatible alignment/reservation; no separate Window-preparation heap record |
| `ReadyCandidate` | 16 / 8, T | Group*, min/max/alignment (three 16-bit values), grid byte | Four positive selection hints in PoolState; never authority |
| `PoolState` | 368 / 8, S+D | Four ReadyCandidates (64 B), inventory vectors/list heads, interested links, notification Message/pin/job, counters and preparation debt | One per Pool; owner mutation, rare shared-budget interest publication |
| `ArenaState` | 208 / 8, S+D | Registry mutex; Pool-owner vector; interest head; job mutex/CV; job queue heads; stop flag; Thread | One per Arena; one OS reclamation helper. Thread stack/TLS/kernel state is additional |
| `Job` | 24 / 8, T | Intrusive next, context, callback | Embedded and reused; background reclamation/refund work, not native read |
| `FiniteGroup` | 448 / **64**, S+D | VM/control pointers and extents; immutable palette capacities/offsets (240 B); owner identity-grid availability; page counts | Separately allocated geometry owner. Reservation, page metadata and trim scratch are additional. 64 B alignment adds padding; do not equate record size with all geometry |
| `FiniteGroup::Grid` | 62 / 2, T | Identity bitmap (2), fifteen occupied masks (30), fifteen pending masks (30) | Sixteen grids for 256 identities; owner-side slot/preparation state |
| `FiniteGroup::Page` | 1 / 1, T | Absent/mapped/trimming/preparing state | One per payload page in page-rounded metadata mapping |
| `FiniteConfig` | 4 / 2, T | Palette byte, identity count | Cold geometry creation; runtime uses 256, isolated geometry supports 64/256/1024 |
| `Identity` | 2 / 2, T | Finite array index | Physical reservation identity, not ObjectId; reused after all physical uses end |
| `PageSpan` | 16 / 8, T | First page and page count | Trim/preparation ranges, owner/helper handoff |
| `Home` | 40 / 8, T | Grid/class/slot, capacity, address, PageSpan | Transient claimed physical slot; bound state lives in one-byte plane/geometry |
| `FinitePreparation` | 176 / 8, D | Request/Pool/Group/preparer pointers; Message; Arena Ref; Identity/Home; two spans/discard flags; phase/results | One charged heap record per accepted finite preparation until actual settlement; caller Preparation is another 192 B |
| `FiniteCandidate` | 56 / 8, T | Group*, Identity, Home | Transient ready selection, not retained per allocation |
| `Result<Identity>` | 8 / 4 | Identity or Error | Ordinary wrapper returns one register |
| `Result<Home>` | 48 / 8 | Home or Error | Ordinary wrapper uses `sret` |
| `Result<pair<Identity, Home>>` | 56 / 8 | Claim result | Ordinary wrapper uses `sret`; same-TU inlining may remove it |
| `Result<optional<FiniteCandidate>>` | 72 / 8 | Error / no hint / candidate | Ordinary wrapper uses `sret`; not a 72 B persistent record |
| `Result<unique_ptr<FiniteGroup>>` | 16 / 8, D | Geometry owner or Error | Cold create result uses `sret` despite 16 B |

## Worker state

| Type | Bytes / alignment | Fields / ownership | Lifetime and use |
| --- | ---: | --- | --- |
| `worker::Message` | 24 / 8 | Atomic next, context, ops pointer | Stable intrusive physical-completion record; queue owns a retained use via callbacks, not a universal new control block |
| `worker::MessageOps` | 24 / 8, T | Retain/handle/release function pointers | Static per operation kind, shared by many Messages |
| `worker::Lane` | 256 / **64**, S+D | CPU/notification setup; sentinel at offset 64; producer at 128; consumer at 192; owner/owner-list link at 200/208 | One physical owner/completion queue, not one per Object. Producer/consumer cache-line separation is deliberate; header's useful data ends at 216, rounded to 256 |
| `worker::Thread` | 8 / 8, S+D | std::thread handle | OS worker lifetime; handle size excludes stack/TLS/kernel state |
| `Signal` | 32 / 8, T | Lane*, readiness/error, generation, WaitLink* | Owner-Lane readiness source; embedded in Pool and Preparation. No allocation for wait list head; copy would not transfer enrollment correctly |
| `WaitLink` | 32 / 8, T | Signal*, next, waiter, callback | Caller-owned explicit enrollment interval; stable until detach/notification |

Message is `is_trivially_copyable == true` but **not copy- or move-constructible**
because of its atomic link. The same trait trap applies to `std::mutex` (48 B
on this platform). None of these linked or synchronized records should be
turned into packets merely because the byte-copy trait says true.

## Total allocation state and budget coverage

The geometry probe computes the same vector capacities charged by `make_group`:

| 256-identity Group | Palette A | Palette B |
| --- | ---: | ---: |
| Group C++ record | 16,768 B | 16,768 B |
| FiniteGroup C++ record | 448 B | 448 B |
| Binding mapping (256 useful bytes) | 4,096 B | 4,096 B |
| Page-rounded grid/page/trim-scratch mapping | 4,096 B | 4,096 B |
| Live-vector capacity | 80 B | 144 B |
| Trim-vector capacity | 5,088 B | 8,768 B |
| Discard-vector capacity | 40 B | 72 B |
| **Charged metadata** | **30,616 B** | **34,392 B** |
| Payload VA reservation | 2,605,056 B | 4,489,216 B |
| Payload pages | 636 | 1,096 |

Metadata amortizes to 119.6 / 134.3 B per identity only when all 256 identities
are used. Payload VA reservation is sparse address space, **not committed RSS**.
The 4 KiB control/page mappings are separate from payload `mapped_bytes`.
Reservation accounting also includes the binding mapping, so adding all
columns above would double-count it.

An actual one-Arena/one-Pool run produced:

| Inventory | Charged total | Payload mapped | Charged metadata | Extra setup/inventory requested heap bytes |
| --- | ---: | ---: | ---: | ---: |
| Empty | 0 | 0 | 0 | 680 |
| One 64 B Allocation | 34,712 | 4,096 | 30,616 | 688 |
| Add a 512 B Allocation | 38,808 | 8,192 | 30,616 | 688 |
| Add a 64 KiB Window | 104,672 | 73,728 | 30,944 | 752 |
| Closed after retirement | 0 | 0 | 0 | 688 |

The extra column is Arena (32) + ArenaState (208) + Pool (64) + PoolState (368)
plus actual owner-vector capacities. It is **outside** `charged_bytes` and
`Statistics::metadata_bytes`. The window vector reserves eight pointers on
first insertion (64 B); the Group vector retains its pointer capacity after
close. Pool removal then destroys those containers. Caller Lane (256 B),
Preparation/WaitLink records and OS thread state are also outside this budget.

The metadata charge counts requested C++ object sizes and vector capacities,
not general-purpose allocator chunk rounding/headers or fragmentation. It is
not process RSS or a complete process memory ceiling. A finite accepted
preparation adds 176 charged bytes for its backend record. A Window preparation
reuses its 328 B Window and embedded Message plus the caller's 192 B Preparation.
No bodies or retention wrappers should be counted twice when Stage2 owns one
Allocation and exposes multiple borrowed Views.

## Concrete leads for the joint re-attempt

The probe provides ordinary return boundaries, exact field positions and real
Group geometry/inventory cases for comparison, rather than a list of size
targets. The following changes remain **candidates**, not measured wins:

- Keep native borrowed bytes in their existing 16 B span shape; don't attach
  Allocation, Hold, Signal or Preparation bookkeeping to every point access.
- Compare shared Group active-use Arena pinning with the current per-Storage
  8 B pin. It could remove 2,048 B from each 256-identity Group and reference
  traffic, but the final foreign-return/retirement lifetime must remain real.
- Compare a compact or optional hint plane with two always-present 4 KiB arrays.
  Preserve independent owner/foreign advice and stale-timestamp rejection;
  a smaller temperature enum alone does not remove time-point alignment.
- Consider the eagerly reserved maximum trim-run vectors separately from the
  ready allocation path. They cost 5.1 / 8.8 KiB before any trim. Any lazy
  alternative must make fallible retirement bookkeeping explicit.
- Evaluate lifetime-bearing ABI changes against real composed callers. Making
  only a handle 8 B does not remove backing state or nontrivial-return machinery.

## Reproduce

Root has extracted `orbital/` and `loom/` from the frozen commit to ignored
`build/orbital-mvcc-metadata/source/`. The probe includes frozen `group.cpp`
once to measure its TU-local types, and exposes private geometry only in this
measurement TU. It does not edit runtime headers or supply copied type mirrors.

From the repository, run on Linux (use `orb -m ubuntu` in this workspace):

```sh
orb -m ubuntu bash -c '
  aw_frozen=build/orbital-mvcc-metadata/source/orbital
  aw_output=build/orbital-mvcc-metadata/allocation-worker
  aw_probe=workbench/spikes/orbital-mvcc/metadata/orbital/allocation_worker.cpp
  mkdir -p "$aw_output"
  aw_flags=(-std=c++23 -O3 -Wno-invalid-offsetof -fno-fast-math -ffp-contract=off -pthread
    -I "$aw_frozen/include" -I "$aw_frozen/src/allocation")
  clang++-21 "${aw_flags[@]}" -g "$aw_probe" "$aw_frozen/src/types.cpp" \
    "$aw_frozen/src/worker.cpp" "$aw_frozen/src/allocation/allocation.cpp" \
    "$aw_frozen/src/allocation/arena.cpp" "$aw_frozen/src/allocation/window.cpp" \
    "$aw_frozen/src/allocation/finite.cpp" -o "$aw_output/census"
  "$aw_output/census" > "$aw_output/census.jsonl"
  clang++-21 "${aw_flags[@]}" -S -emit-llvm "$aw_probe" -o "$aw_output/census.ll"
  clang++-21 "${aw_flags[@]}" -S "$aw_probe" -o "$aw_output/census.s"
'
```

Raw JSONL, LLVM IR, assembly and executable remain in that ignored output
directory. This note retains the useful results; none is a performance promise.
The final probe SHA-256 is
`d5f1e03535d16450a1aa31e084a2bd72d29cfd156b1293972c855a8028bc9569`;
the retained table matches JSONL
`c781918b5340cf58ca58550ab7f2c0fdf91826688af2f6d4f8acb86caeaca412`.
The complete local file receipt is `allocation-worker/receipt.json` under the
ignored metadata build directory.
