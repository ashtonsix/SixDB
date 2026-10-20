# Dependent-chain caller diagnosis

The first Zen5 run's direct16 treatment increases batch64 dependent-16
reciprocal time by 7.00% for LMDB and 6.93% for Orbital. The exact measured
executable shows extra record materialization and an additional ordinary call
shared by both backends. These explain a plausible direction, not the measured
percentage or a specific hardware stall. The [selected timing](../../performance/evidence/zen5-direct-qualification-20261009/feedback/summary.csv)
belongs to the array-producing caller, not the following scratch-free treatment.

[Selected structural evidence](chain-evidence.json) identifies the recovered
whole executable, captured sources and independent whole-caller compiler replica.
It keeps static sites and explicit access counts separate from executed counters.

## Exact measured caller

The measured executable's `read_operation<FusedTuple16>` is 1,895 bytes;
`FusedTuple16::chain` is 375 bytes. Both Orbital and LMDB call that same operation.
The baseline `read_operation<TupleBody>` is 1,357 bytes and decodes its dependent
chain within the consuming loop. These sizes include other operations and cold
error paths; they are not executed instruction footprints.

| Chain work | Direct16 emitted behavior | Baseline emitted behavior |
|---|---|---|
| Initialization | Eight 32-byte zero stores at `0x190f0–0x19126` | No record array |
| Decode placement | Ordinary call at `0x19148` to `0x19a40` | Decode in consuming loop |
| Materialization | Two 8-byte stores per row at `0x19b94/0x19b98` | Fields consumed immediately |
| Consumption | Separate pass loads next/value/tag from the array | No array reread |
| Decoder preservation | Six pushes/pops, no payload-loop stack accesses or calls | Some caller-local spills |

For N completed rows, direct16 introduces **256 + 32N bytes of explicit scratch
accesses**: initialization, 16N bytes of record stores and 16N bytes of consumer
loads. That is 768 bytes for sixteen completed hops. This counts emitted accesses,
not cache-line transfers, DRAM traffic or store-forwarding stalls. There is no
second payload decode and no duplicate digest update.

The frame-size explanation by itself is wrong. Direct16 reserves 392 bytes plus
four preserved registers; baseline reserves 424 bytes plus six. The shared
operation frame also accommodates the larger `Row[16]` scan alternative. The
direct chain decoder's pushes/pops preserve ordinary ABI registers; they are
not sixteen hops of payload spills.

The surrounding backend callers also matter. Each constructs a 24-byte borrowed
body envelope on its stack, calls the shared operation and copies its 24-byte
Outcome to the offered output. These are argument/result objects, not additional
payload ownership. Selection/transaction acquisition and lazy qualification are
charged per batch/first object touch. Optional-cache engagement/bounds checks,
operation dispatch and the operation call remain per request. The latency and
throughput variants contain separate emitted loops; timing APIs in the latency
path are not evidence of timers inside the throughput operation.

## Exact scratch-free caller

Run `20261009T201950Z-eaae80c6` includes `ConsumedTuple16` and calls Ikea's
`metadata/consumer/operations.h` from the actual performance `run.cpp`. Its exact
measured disassembly was recovered and verified separately, SHA256 `66fc25ea…`.

The scratch-free non-scan chain is inlined into each backend's two trial loops.
The array zero/fill/reread path and its ordinary chain call disappear there;
scan still calls `read_operation<FusedTuple16>`. One chain implementation is now
copied into four backend/latency combinations. Exact executable observations are:

| Whole function | Direct16 bytes / local reserve | Consume16 bytes / local reserve |
|---|---:|---:|
| Orbital trial, both timing modes | 7,461 / 616 | 8,998 / 632 |
| LMDB trial, both timing modes | 4,553 / 472 | 5,971 / 552 |

These are whole unions of paths, excluding preserved-register pushes. More
emitted code and a larger outer frame do not establish a timing regression.

The exact scratch-free LMDB caller starts at `0x13320`. Its first chain loop
provides concrete remaining costs, given below as offsets from that address:

- Transfer offsets/shifts are loaded before the chain, then held in stack slots.
  At relative `0x433–0x488`, seven controls are stored at `rsp+0x80…0xb0`.
  The successful-hop range `0x4fc–0x5d4` reloads the source pointer and seven
  controls. These are scalar control spills, distinct from the removed records.
- The digest and row count remain in registers but are also written to Outcome
  at relative `0x60b/0x610` on each successful hop. The full record array is gone;
  sixteen bytes of intermediate output stores per hop remain.
- Key search still computes midpoint, a 23-byte row address, a variable `shrx`,
  mask and comparison (`0x4b8–0x4db`). It reloads the selected key for final equality
  (`0x514–0x524`). The measured Schema has unshifted full-width key/next fields,
  but its runtime transfer controls prevent folding these operations away.
- `next` is decoded early (`0x54a`) and remains in a register, while four serial
  digest mixes and the output stores precede the loop backedge (`0x618`). The
  next search depends on `next`; its logical result does not depend on the digest.

The second timing path and Orbital have their own register allocation; this
list is not a dynamic counter for all paths.

## Private local-Outcome control

Both complete captured source manifests were also compiled locally with pinned
Clang21.1.8, `-march=x86-64-v3 -mtune=znver5`, independent TUs and no LTO. A third
private copy changes only output placement: the existing chain accumulates an
unescaped local Outcome and assigns the caller reference once. It changes no
search, payload decode, digest mix or calling convention. No live performance or
consumer file is edited. This is compiler inspection, not runtime qualification.

The control **does remove the per-hop Outcome stores and repeated Schema mask
loads**. All four masks are loaded before the chain. In LMDB the key mask stays
in a register; next/value/tag masks join seven offset/shift controls in stack
slots. Orbital keeps the value offset in a register, leaving six offset/shift
spills plus those three masks. Control spills therefore remain rather than
disappearing.
In the first LMDB loop, relative `0x418–0x4c9` loads/spills controls;
`0x4e0–0x652` is the hop loop with no Schema reloads or Outcome stores. It still
reloads spilled controls. The digest and row count stay scalar until exit.

| Paired local replica | Consume16 bytes / local reserve | Local Outcome bytes / local reserve |
|---|---:|---:|
| Orbital trial | 7,720 / 616 | 7,848 / 632 |
| LMDB trial | 5,955 / 552 | 6,129 / 568 |

These replica outer callers differ from the measured executable's lowering,
including installed cross headers. Do not substitute their sizes into the
timing receipt. The old decoder does match the exact measured decoder's
instruction offsets, mnemonics and non-control operands; operation functions
match offsets/mnemonics with linked address differences. The bounded result is
that eliminating the aliasing stores permits mask hoisting while increasing
live controls and register pressure in the actual batch composition. More
static code/stack sites do not establish worse executed performance.

## Feedback for the next iteration

The remaining question is whether the complete-operation body can keep controls
and digest state scalar without the surrounding batch loop forcing these spills.
Compare inlining with outlining one complete
operation or specializing its qualified transfer shape; keep the ordinary call,
24-byte result and all setup charged in the complete caller. Avoid treating an
isolated packet return as the result.

Two smaller instruction-chain candidates are supported by the emitted code:
an admitted unshifted full-width key/next procedure can remove runtime shift/mask
work from this particular Schema, and a seek result can retain a candidate
key/comparison when available instead of always loading it again. Preserve
generic transfer laws, misses, empty data and end-of-range behavior. Ikea's
compact explicit-law and stateless fixed-law variants attack the live controls
directly; this local-output control does not qualify those separate variants.

Batch64 currently executes complete requests serially. It amortizes selection
and qualification but introduces no AMAC/Loom Ring interleaving; the inspected
read loops emit no prefetch instructions. This comparison therefore diagnoses
the caller/codec composition, not Loom's expected memory-latency-hiding gain.

## Reproduce and limits

The ignored private captures and replica receipt live under
`build/orbital-mvcc-metadata/loom/chain-codegen/`. The exact first disassembly was
recovered through its owning artifact and matches SHA256 `32be28cd…` in the run.
Both jobs' artifacts own exact executable recovery and their immutable sources.
[chain_codegen.py](https://github.com/ashtonsix/SixDB/blob/c8f9081104ef61ee897fbbaf954223ae0a3d7656/workbench/spikes/orbital-mvcc/metadata/loom/chain_codegen.py) compiles the actual captured
`performance/run.cpp`, reusing the existing relocation-aware analyzer:

```sh
orb -m ubuntu python3 workbench/spikes/orbital-mvcc/metadata/loom/chain_codegen.py \
  --captures build/orbital-mvcc-metadata/loom/chain-codegen/captures \
  --output build/orbital-mvcc-metadata/loom/chain-codegen/replica-new --local-output
```

This diagnosis starts no timed worker, edits no performance source and reports
no executed PMU counts or modeled cycles. It does not change the separate
earlier frozen `7f73c81` metadata census. Tooling owns timing/PMU interpretation;
the private placement control has no measured performance claim.
