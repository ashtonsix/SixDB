# Ring dispatch and argument traffic

This comparison asks why already-interleaved regular Ikea work repeatedly ran
faster through a closed manual loop than through the shared Loom Ring. The
selected mechanisms live in [Loom](../../../../loom/docs/ring.md); this record
keeps the comparisons and their limits.

The recurring cost was at each dependency boundary: saving unchanged arguments,
updating memory-resident visit accounting, restoring the next frame, and losing
immutable-fact hoisting across callbacks. Cold error and synchronous fallback
calls also put stack frames onto ordinary callback paths. Prefetch selection
was already inline. An irregular Object lookup remains a useful counterexample:
its Ring could outperform the manual interpreter, so a closed regular loop is
not a model of every caller.

The selected Ring carries its countdown in a two-word native Cursor, inlines the
direct musttail jump, and provides unchanged and terminal handoffs. Its common
two-pointer Slot shrinks from 72 to 64 bytes. A bound common prefix carries
invariant scalar/pointer/vector values across jobs; only changing local state is
restored from Slots. A bounded Cohort adapter can amortize a visit across a few
independent item stages while retaining mixed microtask families and the same
hardware/pressure snapshot. Grain and common facts remain caller choices.

The first reusable Cohort passed its behavior checks but left
`std::__invoke_impl` calls around large kernels. V2 callback frames grew to
48 bytes; each active item paid an ordinary helper call. That adapter is a
measured negative cut. The selected implementation calls ordinary function
objects directly and snapshots its state base once per visit. A separate bounded
header-factor comparison keeps the old wrapper, same kernels/windows and the
same shared-context signature to qualify that change. Actual downstream fallback
calls remain part of complete lookup work.

The final header-factor campaign fixes the shared Context, cold-positive windows,
Ring core, driver and all eight execution arms. It compares the outlined wrapper
with direct invocation plus the state-base snapshot, at grains one/four, 128/4M
keys and one/32 chains, with seven repetitions. Its three source-identical jobs
are `20261013T040929Z-4ddeb41f` (V2), `20261013T040929Z-46a68be2`
(Granite AVX512) and `20261013T040929Z-5a6e2967` (Zen5 AVX512). Each passes
16 native checks and six O3 ASan/UBSan checks with always-on stack-use-after-return
instrumentation before measuring. The exact include traces establish which
Cohort header each executable compiled.

At 4M keys/32 chains with hints and grain four, these are reciprocal-throughput
medians in ns/lookup. Manual is the control in the direct executable, with the
same work, hints and logical chains.

| Host | Outlined Cohort | Direct Cohort | Matched manual | Remaining Ring overhead |
| --- | ---: | ---: | ---: | ---: |
| V2 | 19.58 | 17.93 | 16.32 | 9.9% |
| Granite AVX512 | 16.19 | 15.77 | 12.79 | 23.3% |
| Zen5 AVX512 | 14.62 | 13.96 | 12.07 | 15.6% |

Removing wrapper calls increases throughput here by 2.6–9.2%, without reaching
manual parity. Actual metadata callbacks contain no ordinary helper calls;
payload retains its synchronous downstream fallback. Inlining that payload
reduces V2 native frames from 48 to 16 bytes at grain one and keeps 48 bytes at
grain four. It increases x86 frames from 24 to 40 bytes at grain one and 40 to
72 bytes at grain four, spilling some Ring/Slot/countdown/Context values around the
fallback. Reduced slot traffic therefore does not guarantee reduced total
argument traffic. At hot 128 keys/one chain, Granite grain four regresses from
12.24 to 13.34 ns/lookup with hints, while grain one stays close to its matched
manual control. All singleton and no-hint controls remain in the evidence;
grain four is an optional caller composition, not a default scheduling policy.

These percentages concern the adapter correction after work is already
interleaved. In the final V2 control, the same large hinted Ring workload is
161.05 ns/lookup with one chain and 17.93 ns with 32 chains at configured grain
four (effective grain one/four respectively). The large overlap benefit is
present; it is separate from the remaining scheduler gap against an equally
interleaved manual implementation.

The preceding reusable-adapter campaign separately compares a local Context
argument with a shared prefix. On V2 the next-slot restore changes from a
16-byte pointer pair to one eight-byte frame pointer, but gains are modest:
4M/C32 with hints and grain one changes 24.34 to 23.57 ns/lookup; cold-positive
grain four is 19.03 local versus 19.06 shared. These results include the outlined
helpers and use a different source cut from the final factor. They establish
the saved bytes and the limits of carrying an opaque Context pointer; they do
not establish that every common prefix remains spill-free or that sharing a
pointer hoists all immutable kernel facts. This composition also changes the
Slot stride from 64 to 56 bytes; its 24-byte hot prefix straddles a cache line in
two of eight bank positions, so timings are not an isolated restore-cost test.

An independent two-stage packet control exposes a related caller pattern.
With eight mutable 16-byte vectors in native arguments, generic metadata
handoffs restore and re-save all 128 bytes even though only payload uses them.
Putting that state in a stable `Frame` and using the existing `Ring<Frame*>`
eliminates all metadata packet loads/stores and vector clears; payload alone
loads and updates the packet. Both retain one indirect tail jump and pass every
per-job lane, scalar result and visit-count oracle. Static restore wrappers
inline but do not eliminate unused incoming vectors when they forward those
values into an opaque dispatch. Zero/nondeterministic dummy arguments instead
emit eight vector clears; no alternative static dispatch ABI is selected.

Three paired warm local VM batches favor Frame pointers by 1.33–1.44× with reset,
enqueue, drain and result consumption charged. This is a caller-composition
comparison: the native-packet arm uses full `yield`, while the Frame arm uses
`yield_unchanged`. It establishes neither a restore-only speedup nor cold-memory
AMAC throughput. The useful finding is the avoided per-stage packet traffic,
achieved through the same production scheduler.

Controls use a frozen N16 two-stage Hashtable driver, Map and Ikea source from
worker `20261013T010243Z-8c261d06`. Old, Cursor and explicit-handoff variants
share qualified windows, logical requests, checksums and selected hints. Cohort
controls keep the number of independent chains and maximum useful stage work
between service boundaries fixed. Setup, enqueue and drain are charged; bank
configuration is amortized outside timed operations. Timings are reciprocal
throughput, including at one chain; the C32 direct arm is not serial latency.

An earlier budget-marker implementation was rejected after singleton/ragged
StreamPack regressions. It remains a negative control in the selected evidence,
with its actual captured source hash and stale copied provenance note recorded
separately. A failed Granite cohort qualification contributes no timing cells.
The x86 O3 sanitizer compilation failure survives moving test-observation
temporaries into ordinary helpers. Its optimized native callbacks have no
allocas: the failure is in realigned ordinary callers that invoke the native
convention and need a preserved stack base. The fixed ordinary entry bridge now
isolates that call on both architectures; actual native state and tail transfers
remain instrumented. The full native qualification records the repaired cut.
LLVM 21 [marks registers clobbered by native invokes](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/llvm/lib/Target/X86/X86ISelLoweringCall.cpp#L2493)
and [rejects a required clobbered frame base](https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/llvm/lib/Target/X86/X86RegisterInfo.cpp#L575);
the retained IR distinguishes this caller failure from a native alloca.

Native PMU collection is a separate instrumented campaign using the existing
Orbital benchmark Session. V2 provides real cycles, instructions and generic
frontend stalls with full PMU group coverage and no scaling or multiplexing.
Its generic backend-stall event rejects `EINVAL`. The counted region charges
the timed operation and two clock reads; it does not establish a cache-miss or
memory-stall breakdown. IPC must be read alongside instructions and cycles:
bookkeeping can raise IPC while making the operation slower.

In that separate V2 PMU campaign, 4M keys/C32 with hints gives these basic-event
medians. Each cut has its own matched manual control, 16.86–16.97 ns/lookup.

| Cut | ns/lookup | Instructions/lookup | Cycles/lookup | IPC |
| --- | ---: | ---: | ---: | ---: |
| Old Ring | 21.23 | 170.40 | 58.62 | 2.91 |
| Cursor, explicit handoff | 20.10 | 150.40 | 55.58 | 2.71 |
| Bounded grain four | 18.41 | 143.56 | 50.91 | 2.82 |
| Matched manual, old cut | 16.86 | 95.40 | 46.64 | 2.05 |

Cursor removes about 20 instructions per lookup; cycles fall less, so IPC also
falls. The generic frontend stalled-cycle fraction is 4.66% old, 5.22% Cursor,
5.85% grain four, and 5.79–5.84% manual in the separate stalls-event runs. This
does not establish a frontend-stall reduction or locate the remaining backend
cost. PMU running equals enabled time, but differs slightly from wall time;
those observations do not establish uninterrupted execution or CPU frequency.

The [selected evidence](evidence/20261013/recovery.json) keeps endpoint controls,
all their repetitions and source/host qualification together. `native.csv`
contains the original Cursor and handwritten-cohort comparisons; `shared.csv`
contains the measured negative reusable adapter; `cohort-factor.csv` contains
the complete final callable factor, including singleton and no-hint cases.
`pmu.csv` and its raw receipts keep the separately instrumented counters.
The packet-state selection, exact assembly excerpts and failed qualification
diagnosis retain their stated limits. Full compiler output and captured source
trees are recovered through the indexed worker archives instead of duplicated
here. Different source cuts and PMU/timing regions are not pooled.

From the Linux repository root (prefix with `orb -m ubuntu` from macOS), rebuild
the selected report or recover its archived bytes with the shared tools:

```sh
python3 workbench/tools/artifacts.py report \
  workbench/spikes/call-boundaries/ring-dispatch/evidence/20261013 \
  build/reports/loom-ring-dispatch
python3 workbench/tools/artifacts.py fetch \
  workbench/spikes/call-boundaries/ring-dispatch/evidence/20261013 \
  build/recovered/loom-ring-dispatch
```

To rerun a native comparison, recover the corresponding job's `source.tar.gz`
with `worker.py fetch JOB --file source.tar.gz`, verify the source archive hash
in `recovery.json`, and extract it into a fresh ignored checkout. The frozen
runner, controls and compile settings are in that source archive. The final
factor uses `run-cohort-direct.sh`, seven repetitions, V2 `neon` or x86
`DISPATCH_ISAS=avx512`, with the recorded host tune. Captured sources retain the
original experimental driver; rebuilding current Ikea would answer a different
question. The [local packet control](evidence/20261013/static-restore-scratch/reproduction.txt)
has its own small source capture and reproduction script.
