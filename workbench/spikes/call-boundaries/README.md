# Call boundaries: Loom, Orbital and Ikea

October 8, 2026 spike requested by Ashton: examine real caller/callee assembly,
large returns, register preservation and alternatives before MVCC, Engine and
Shore add more composition. No Loom/Orbital API or global build policy changed
in the initial spike. Orbital independently adopted the small Ref destructor
hint described below while closing its allocation TU refactor. Architecture
subsequently integrated a 16-byte Progress; the follow-up below qualifies its
actual caller assembly. The initial 32-byte evidence remains frozen.

The useful targets are **repeated Task results, live continuation state and
avoidable ownership wrappers**. `std::expected` can force materialization, but
replacing every result or calling convention is not supported. An ordinary
flattened callback can beat a custom convention's entry cost; an output
parameter can retain the same payload store/reload. Setup and allocation calls
have different frequency and semantic obligations from native visits.

## Evidence and limits

The initial matrix used the pre-integration 32-byte Progress.
[codegen.py](codegen.py) compiles real separate TUs with pinned Clang 21.1.8,
`-O3`, assertions enabled, no LTO, for Neoverse V2, Zen5 with AVX2, and full
Zen5. It records LLVM signatures **and final machine instructions**, with
relocations identifying object-file calls. Native uses libstdc++13.4; the x86
cross headers are libstdc++15.2. Do not attribute their differences solely to ISA.

[returns.cpp](returns.cpp) and [callers.cpp](callers.cpp) compare equivalent
refusal and arithmetic outcomes. [ring.cpp](ring.cpp) decodes a real 64-code
TuplePack row with `native_reader<64>`, accumulates native packets, and carries
scalar state through the actual Loom Ring. It uses held Orbital storage,
independent byte/scalar oracles, full drain, close and physical release.

Native and ASan/UBSan with always-UAR passed 4,096 return/status inputs,
preserved output on refusal, exactly-once synthetic ownership retirement, and
eight actual Ring cases. Ring cases include 64/128/192-byte vector state,
extra scalar metadata, active-only and overflow promotion, repeated spins,
completion and shutdown. These checks executed on the local AArch64 VM;
V2/Zen5 objects are static evidence. Compiler-capacity failures are retained.
Ring dependencies use `Hint::none`: promotion/lifecycle are exercised, but
issuing a non-none admitted hint during promotion is not covered by these cases.

The selected records and raw disassembly are in
[`build/call-boundaries-20261008`](../../../build/call-boundaries-20261008):
`selected.json`, `final-codegen/receipt.json`, `local-paired-control.jsonl`,
`sanitize-check.jsonl`, `thin-lto-kept/` and `ref-leaf-comparison.json`.
`stress-controls.json` indexes unique source-labelled failure logs under
`final-codegen/negative-controls/`. A historical recipe also compiled ordinary
`loom/src/ring.cpp` and overwrote shared `ring-metadata8` paths; those successful
objects are explicitly excluded from stress evidence. Its immutable receipt
retains the actual failure stderr; the curated report extracts it separately.
Native inputs are frozen in `build/captures/call-boundaries-20261008`
(digest `7ca7d3f3…`); the static matrix's macro-isolated Ring inputs are in
`build/captures/call-boundaries-20261008-v2` (`29cf30be…`). The latter only
replaces this spike. The report-correction and optional-inline-control revisions
left their normal producer behavior unchanged. The maintained probe now
has an explicit `HistoricalProgress32` control and a separate current-runtime
producer/consumer; it does not reinterpret new runtime results as old evidence.
Capture receipts identify all
included module headers, rather than just hashing the principal TU.

Stack instruction sites are not executed counts. In particular, RBP can be a
preserved scratch register, and `popcnt` is not a stack pop. Reports distinguish
actual SP accesses/frame setup from those cases. No hardware stall counters
were collected: stores followed by loads are a dependency to examine, not a
measurement of failed store forwarding or a cache miss.

## Returns: the actual boundary

| Result | Size here | Observed separate-TU placement |
| --- | ---: | --- |
| Two plain integers | 16 B | Two GPRs on both targets |
| `expected<uint64_t, Error>` | 16 B | Two GPRs |
| `expected<Pair, Error>` / `optional<Pair>` | 24 B | Hidden result pointer and payload stores |
| Three plain integers | 24 B | Hidden result pointer already, without a wrapper |
| `expected<Triple, Error>` | 32 B | Memory, including with x86 `regcall` |
| An owning one-word class | 8 B | Memory because of its nontrivial call ABI |
| Same one-word class with `trivial_abi` | 8 B | GPR |
| `expected` around that attributed owner | 16 B | Memory again |
| Orbital `Progress`, initial frozen cut | 32 B | Memory on every ordinary Task step |
| Orbital `Progress`, source-union integration cut | 16 B | Two GPRs; see actual poll comparison below |
| `Result<Allocation>` / `Result<Hold>` | 40 B | Memory with ownership cleanup |
| `Result<RingStorage>` | 64 B | Memory at configuration |

For `use_expected`, the V2 caller reserves 48 bytes, passes its result slot in
X8, tests the success byte, then reloads three payload words. Zen5 reserves
40 bytes and passes its result slot in RDI. The producer stores 24 payload
bytes and a success byte. Those payload stores/reloads are real; a second
full object copy is not implied by the source return statement.

Real `allocation_forward` forwards its final result address into Orbital,
tail-transferring on the normal allocated-worker path. The Loom wrapper's
frame is for `on_lane` checking and preserving arguments across that call;
it does not create a second 40-byte result. In the initial frozen matrix,
common Orbital allocation used a fresh 32-byte local output for its private
backend, then published ownership and moved the payload into the returned
expected. The follow-up now constructs that payload in the final expected.
Returning the same named result on backend success and refusal enables copy
elision; in-place construction alone did not remove the temporary.

The matching V2 callee shrinks from 592 to 512 bytes and its frame from 128 to
80 bytes; Zen5 AVX2/full shrinks from 591 to 393 bytes with entry stack use
136 to 56 bytes. Caller controls are unchanged. Backend refusal gains an
outlined assignment helper, including stack setup on Zen5. Including that
helper, selected text is 592 to 640 bytes on V2 and 591 to 509 bytes on Zen5.
This removes success-path materialization, with a refusal-side trade-off; it
does not establish faster allocation under pressure. Pin/user publication stays
after successful backend fill, and native plus sanitizer lifetime/refusal checks
pass. The [allocation assessment](../../../build/reviews/orbital-20261008-allocation-publication/assessment.md)
records the complete boundary review, rejected Hold/release alternatives and
[matching static evidence](../../../build/call-boundaries-20261008/allocation-publication/receipt.json).

These allocation/release codegen figures predate the
[reuse-selection promotion](../allocation-reuse-temperature/FINDINGS.md#removing-hint-ranking),
which removed allocation preference arguments and release CPU advice. They
describe their captured API/layout cuts, not a remeasurement of the current signatures.

`hold_consumer` reserves 80 bytes on V2, calls Hold acquisition, reads the
success flag and useful fields, then invokes the owning destructor. The
acquisition also checks the owner lane and adjusts physical use. A scalar
address return is not equivalent to that retained use. In this spike the Hold
is acquired once around all native Ring work, not once per decoded value.

Statistics returns 104 bytes and Ring storage returns 64 bytes, but both
are setup/observation examples. Their size alone is a weak optimization reason.

## Repeated Task status is a better candidate

In the initial captured sources, the real `task_step` returning
`Progress::done()` wrote four fields: kind, null Signal, default error and zero
generation. `loom::poll` set up a memory result on every indirect step and
reloaded the fields needed by each case. Its V2 frame was 128 bytes; only
32 bytes were the result slot. The rest supported the polling loop and nested
calls and cannot all be charged to Progress.

Two equivalent private alternatives are in the probes:

```cpp
Kind step(Task& task, WaitDetail& detail);
// done/yield: scalar status only
// waiting: write exact Signal* and full uint64_t generation
// failed: write the exact Error

struct CompactProgress {
    uint64_t reference_and_kind;
    uint64_t detail;
};
```

The compact control uses the Signal's proven alignment for two kind bits.
It preserves the full pointer, generation and error. It is an experiment,
not a new public representation; decoding, diagnostics and future status
extensions must remain clear. The output-detail alternative is easier to
inspect, but retains a memory slot for waiting. Stable Task-owned wait detail
would permit a scalar callback return without a separate temporary; that adds
Task state and must preserve observation-before-check and refusal behavior.

| Equivalent consumer | V2 text | Zen5 text | V2 frame |
| --- | ---: | ---: | ---: |
| Initial 32-byte `Progress` shape | 96 B | 61 B | 48 B |
| Scalar kind plus output detail | 88 B | 53 B | 48 B |
| Compact 16-byte result | 48 B | 45 B | 16 B |

The compact control returns in X0/X1 or RAX/RDX and removes the result-slot
stores/reloads. This merits a complete scheduler comparison if Task step
frequency is high. Its checks establish result encoding and consumption,
not integration through actual wait enrollment, wakeup, detach or cancellation.
Those semantics and scheduler speed remain unqualified by this alternative.

### Source-selected union: actual integration follow-up

Architecture selected a simpler representation using the existing waiting
source to select an explicit union, with private construction and the same
done/yield/fail/wait factories:

```cpp
Signal* source_; // nonnull selects waiting
union Detail {
    uint64_t generation; // waiting: all 64 bits
    struct { Kind kind; Error failure; } status; // other states
} detail_;
```

This is 16 bytes here and uses neither pointer tagging nor extra Task state.
The current probe calls its accessors; `progress_result` explicitly returns
`HistoricalProgress32`, while `progress_current_result` returns the runtime type.
The private factory/accessor comparison passed 576 cases spanning three Signal
addresses, six generation boundaries through `UINT64_MAX`, all eight Errors,
and all four kinds. Its equivalent dispatch consumer is eight bytes smaller
than the private tagged control on each target. These checks qualify encoding
and consumption. The integrated [Task checks](../../../loom/test/worker.cpp)
also pass natively and under ASan/UBSan with always-UAR: full-width generation
parking/retry, exact errors, terminal-source retry, detach and late physical
returns retain their existing scheduling and ownership behavior.

The captured **actual** `loom::poll` and its inlined `park` now consume X0/X1
or RAX/RDX directly after `task.step`. The old hidden result pointer and four
result-field reload sites disappear. Waiting compares the full
returned generation directly with the Signal's generation. `kind()` adds a
source-pointer test/select; the failed path can still select the default Error
in registers. This selection does not add a frame or a helper call.

| Actual function | V2 before → after | Zen5 AVX2/full before → after |
| --- | ---: | ---: |
| `poll` text | 752 → 732 B | 803 → 796 B |
| `poll` explicit stack reservation | 128 → 96 B | 40 → 8 B, plus six unchanged register saves |
| Step returning `done()` text | 24 → 12 B | 33 → 13 B |

The 32-byte frame reduction matches the removed result slot; loop state and
ordinary call preservation remain. On x86 the total entry stack use excluding
the return address is 88 → 56 bytes. The `done()` producer now writes registers
instead of four result fields. Both arms use identical captured Worker/hint
headers, the pinned compiler, separate TUs, `-O3`, and no LTO. The full worker
source diff also contains a configure-time hardware-discovery change outside
these compared poll/park paths; no whole-TU improvement is attributed to Progress.

Raw commands, header hashes, objects, LLVM signatures and source diffs are in
[`source-union-progress/`](../../../build/call-boundaries-20261008/source-union-progress),
particularly `actual-poll/receipt.json`. The compact retained follow-up is
[progress16-summary.json](evidence/20261008-progress16-summary.json).
The updated probe passed its 4,096 input checks and eight actual Ring cases in
both native and ASan/UBSan with always-UAR builds, using the separate frozen
`call-boundaries-progress16-20261008` capture (`72103e45…`). It reports the
historical 32-byte and current 16-byte layouts separately. This is compatibility
evidence for the spike; Architecture's scheduler lifecycle tests remain separate.
No scheduler timing or store-forwarding stall claim follows from these sizes.
The original matrix, timing and compiler-failure receipts remain unchanged.

## Output parameters, callbacks and visibility

All these arms preserve a refusal on every eighth input and the same successful
three-word value:

| Consumer boundary | V2 text | V2 frame | What remains |
| --- | ---: | ---: | --- |
| `expected<Triple>` | 76 B | 48 B | Payload store/reload and status |
| Status plus `Triple&` | 68 B | 48 B | Payload store/reload; scalar status |
| Ordinary callback with three word arguments | 16 B | 0 | Tail transfer to producer, then completion |
| `preserve_none` callback | 108 B | 160 B | Native entry preservation |
| Visible inline callback | 44 B | 0 | Useful arithmetic and refusal branch |

The ordinary callback transfers separately passed values in ordinary ABI
registers. It removes the fat return without requiring a custom convention:

```cpp
Word produce(Word n, void* state, Finish finish) {
    if (refused(n)) return finish(state, 0, 0, 0, refused_code);
    const auto value = make(n);
    return finish(state, value.a, value.b, value.c, 0);
}
```

A callback is useful when the consumer belongs directly after production. An
erased indirect target still costs dispatch and can inhibit specialization.
Synchronous completion is not asynchronous ownership transfer. Owning results
must keep exactly-once retirement, failure preservation and cancellation;
stage-local RAII cannot survive a terminating tail hop.

A local ThinLTO contrast removes `noinline` in **both** ordinary and LTO arms,
keeps five consumer symbols against dead stripping, and uses the same sources.
The separate-TU expected consumer goes from 76 bytes with a result slot to
44 bytes with no calls or stack use. Output and callback consumers converge
to that same 44-byte computation; refusal remains. These diagnostic links
were not executed. This demonstrates that visibility can erase the wrapper,
not that LTO should be enabled globally. An always-inline declaration cannot
recover a body unavailable in a normal TU. Existing target properties explicitly
disable IPO; setting CMake's global initializer alone does not override them.

The rotated-order local VM control, nine repetitions of a million calls per arm,
has medians of 2.06 ns expected, 3.31 ns output, 1.74 ns ordinary callback,
3.61 ns native callback and 0.97 ns visible inline callback. This is useful
as a counterexample to “output/native must be faster”; it is not a Zen5/V2
ranking or an isolated store-forwarding penalty. Checks and construction
are outside the timing; the caller loop and wrapper dispatch are included.

## Native Ring: register capacity and unavoidable storage

The actual Ring keeps different requests alive between visits. Enqueue writes
their arguments into Slot storage; yield saves the current values; resume loads
the next request. Those accesses implement interleaving. A calling convention
cannot eliminate them while preserving distinct request state.

The repeated fast handoff can nevertheless avoid a call frame. The real
TuplePack callback has no ordinary helper call on any successful compiled
target. V2's one- and two-packet families use no SP at all. Generic decode
controls still make the bodies roughly 1.7 KiB, so “frame free” is not “tiny”.

| Family | V2 callback | Zen5 AVX2 callback | Full Zen5 callback |
| --- | --- | --- | --- |
| 64 B vector state + four scalar/pointer values | No SP | RBP push/pop, no payload spill | RBP push/pop, no payload spill |
| 128 B vector state + four values | No SP | RBP push/pop | RBP push/pop |
| 192 B vector state + four values | 192 B frame; stack-passed vectors | Six YMM arguments; RBP push/pop | Three ZMM arguments; RBP push/pop |
| 64 B state + extra metadata | Eight extras: no SP | Four extras: scalar spills | Four extras: scalar spills |
| Eight scalar extras on x86 | — | **Compiler register-allocation failure** | **Compiler register-allocation failure** |

The x86 failure names both `MemoryRing::yield` and `retire_yield`. It is an
observed pinned-compiler limit for this concrete family, not a universal
numeric ABI capacity. On ARM, twelve 128-bit vector arguments exceed ordinary
vector argument registers even under `preserve_none`; vectors arrive on the
stack and are written back for the next tail hop. Full-width payloads can be
native on one target and stack-shaped on another.

The ordinary-to-native bridge is paid once per spin. The inspected V2 entry
saves 160 bytes: X19–X30 and the lower halves of D8–D15 in this pinned compiler.
It restores them when the native region returns. The separate `spin_live`
control keeps vector values live across the call and accounts for caller
preservation as well. Current `retire_yield` frames/calls occur on completion,
including overflow promotion and admitted hints; they are not the ordinary
continuation frame. Keep those costs separate when deciding visit grain.

Retain the fixed-frame AArch64 shim. Ikea's
[execution evidence](../tuple-layout/module-evidence/README.md#validation-abi-and-compilation)
records a realigned caller keeping its frame base in X19 across a native call
that clobbers it. The shim is a correctness accommodation for Clang21; only
it excludes ASan/UBSan, while authored callbacks stay instrumented. The new
wide-payload checks passed with that existing accommodation.

## Attributes, macros and practical choices

`preserve_none` belongs to a bounded native tail family with amortized entry.
`preserve_most` is a different experiment for a small returning helper with
significant state live in its caller. Our five-live-word leaf control's V2
caller uses 64-byte/default, 208-byte/preserve-none and 16-byte/preserve-most
frames. The tiny most-preserving callee uses only volatile argument registers;
a real helper calling other code could move the save burden into its callee.
Qualify caller **and** callee before adopting it.

`regcall` remains justified for Ikea's specific AVX2 two-vector return;
it leaves the tested wide expected in memory. `trivial_abi` changes small
nontrivial class passing, but the wrapper counterexample survives and Allocation
still exceeds the ordinary integer-return limit. Do not apply it wholesale to
owners or address-dependent classes.

`always_inline` is useful for small body-visible leaves and authored packet
work. Orbital's final refactor added it only to `Ref::~Ref`'s nullable check,
leaving actual release compiled. Its temporary success-path destructor call
disappeared; the linked consumer text fell 80 bytes, allocate 580→576 bytes,
and no other emitted Orbital function grew. Orbital separately passed its
20 native/20 sanitizer tests and 16 consumer groups in both modes. `flatten`
and callsite inline hints do not fix unavailable bodies or erase required
cleanup. Blanket forced inlining risks increasing live ranges and code size.

Macros should encode a tested private family, not imply portable fallbacks:
the existing `LOOM_NATIVE_CC` and `IKEA_CHAIN_CC` mark matching function-pointer
types and entries. This spike's `CB_*` names are experimental controls. If a
shared helper eventually earns its place, it should check attribute support,
pin its compiler/target assumptions, and reject an unsupported musttail family
rather than silently switching convention. One nullable destructor needs no
attribute framework. Keep diagnostic/sanitizer variants exercising real entry,
return, overflow and cancellation behavior.

Clang's [attribute reference](https://clang.llvm.org/docs/AttributeReference.html)
describes these contracts, including unstable `preserve_none` and changed
destruction/relocation rules for `trivial_abi`. The
[Arm procedure-call standard](https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst)
explains indirect results and homogeneous vector aggregates. Above recommendations
use the pinned compiler's emitted code, rather than assuming current docs
describe its precise register set.

## Composition and advice carried to Loom

Keep MVCC authority, generation observations, physical protection and cancellation
in stable operation-owned state across waits. The current composed lookup already
passes `Frame*`; Orbital's design View registers a caller-owned borrow and uses
separate retained records for escape. That is a useful shape to preserve as
Engine/Shore add context. Carry only values whose repeated use earns registers;
adding transaction/lock/error state to every native signature can cross the
observed spill and compiler-capacity boundaries.

Fuse small work inside an admitted native region and rotate after meaningful
dependent work. Bind/retain/admit once where semantics permit, requalifying after
relocation or authority change. Let external/user-facing boundaries keep explicit
results and lifetimes. No Engine/Shore implementation exists to measure yet.

The recommended follow-on to Loom's accepted hardware-policy spike was a consumer
composition test: mixed demand codes and useful scalar/vector work, including
ordinary entry, repeated switches, retirement/promotion, code footprint and
caller live state. Its uniform four-scalar policy family remains valid evidence
for that family. Its simple selector does not need another dispatch campaign.
Compare the complete operation against ordinary inline/batched work and retain
negative cases. Ikea already measured tiny CPS stages losing to inline, and
[smaller grain losing 5–9%](../ikea-composition/probes/ikea-heterogeneous/operations/measurements.md#native-grain-the-smaller-kernel-loses-throughput-here)
despite fewer spills. These examples and the new capacity failure were sent
directly to Loom; policy/runtime files were left with their owner.

Loom subsequently implemented that composition check with an actual mixed
64-byte TuplePack caller, keeping demand and policy in stable owner state.
Its owner-qualified V2, Zen5 AVX2 and full Zen5 bodies have no ordinary hint
calls or vector stack spill sites; entry and retirement remain separately
reported. The owner's native, always-UAR ASan/UBSan and TSan checks passed.
The current [Loom guide](../../../loom/README.md) owns the implementation and
lifecycle explanation; its source/check/ABI receipt is
[`build/loom-hints-implementation-20261008/receipt.json`](../../../build/loom-hints-implementation-20261008/receipt.json).
That concrete family does not establish capacity for wider live signatures or
primary-hardware policy speed. The initial spike's `Hint::none` limits above
remain limits of those frozen cases.

## Reproduce

The [verified ABI archive](evidence/20261008-artifact.json) preserves the original
codegen receipts, objects/IR/disassembly, negative controls, captured code and
native/sanitizer check binaries. Historical paths and hashes in the two selected
summaries remain unchanged. Archive members retain their repository-relative
`build/...` paths; `selection.json` records the retained files. Recover with:

```sh
orb -m ubuntu python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/call-boundaries/evidence/20261008-artifact.json \
  build/recovered/call-boundaries-20261008
```

Run on Linux; prefix commands with `orb -m ubuntu` from the Mac:

```sh
cmake -S . -B build/call-boundaries-local -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DSIXDB_SPIKES=call-boundaries
cmake --build build/call-boundaries-local --target call_boundary_probe -j2
build/call-boundaries-local/workbench/spikes/call-boundaries/call_boundary_probe
python3 workbench/spikes/call-boundaries/codegen.py \
  --source . --output build/call-boundaries-local/codegen
```

The cross matrix expects the installed x86 sysroot and C++15 headers recorded
in its commands. `--reuse EXISTING_CODEGEN --output NEW` verifies object hashes
and reinspects without rebuilding. Ring's `CB_METADATA` control separates a
compilable four-extra-word x86 family from the retained eight-word failure.
`CB_INLINE_CONTROL` permits a visibility/LTO contrast without changing the main
separate-TU cases. Local timing is opt-in with a call count argument; use the
quiet machine after compilation and checks finish.
