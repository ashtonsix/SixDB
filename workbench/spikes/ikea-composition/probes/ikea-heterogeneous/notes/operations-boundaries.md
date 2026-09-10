# Selected algebra boundaries

This audit concerns the final benchmark executables with independent source
resolution choices and uninitialized cursor-frame storage. Text sizes and
static instructions are machine-code observations, not dynamic traffic or
cycle estimates. Timing interpretation belongs with the operation measurements.

## Captured executables

The inspected `ikea_heterogeneous_operations_bench` executables and matching
`assembly.txt` files are under `build/workers/WORKER/results/RUN/`:

| Target / output grain | Worker | Result run |
| --- | --- | --- |
| [Zen 5 / 2](../evidence/operations-zen5/artifact.json) | `20260910T014629Z-44f7ea8d` | `20260910T014653.848404Z-zen5` |
| [Granite Rapids / 2](../evidence/operations-granite-rapids/artifact.json) | `20260910T014630Z-d34173bb` | `20260910T014655.520320Z-granite-rapids` |
| [Neoverse V2 / 2](../evidence/operations-neoverse-v2/artifact.json) | `20260910T014631Z-68ff38f4` | `20260910T014658.578977Z-neoverse-v2` |
| [Neoverse V2 / 1](../evidence/operations-neoverse-v2-grain1/artifact.json) | `20260910T015131Z-90e88451` | `20260910T015156.275970Z-neoverse-v2-grain1` |

These are release captures using Clang 21.1.8, not the old local build directory
that inherited sanitizer flags. Their source archive contains
`EntryLanes16 entries_;`: cached frames are filled before first use, and point
paths do not read them. The old unconditional zero stores are absent.

## Shared boundaries and specialization

All three executables contain eight selected kernels: three physical operand
families (plain/plain, BEC/plain, BEC/BEC), each with union and intersection,
plus two Local/Local inline controls. Three metadata encodings and independent
point/cached choices do not multiply the factored BEC bodies into nine
pair-specific implementations.

| Actual text, excluding alignment and constant tables | Zen 5 | GNR | V2 |
| --- | ---: | ---: | ---: |
| Eight selected kernels combined | 73,868 B | 72,164 B | 91,092 B |
| Six point/frame resolver functions combined | 984 B | 984 B | 1,196 B |

The selected kernels contain no calls to BEC decoders. Decoder bodies, terminal
shortcuts, combination and stores remain inline inside each selected kernel.
Factored BEC/BEC has eight static indirect call sites; BEC/plain has four.
These are alternative point/frame paths for the first or adjacent ordinal,
not calls executed unconditionally per slice. Plain/plain and both Local/Local
inline controls are call-free. Call-free does not imply spill-free.

The resolver boundary uses ordinary scalar/pointer ABI:

- Point: metadata pointer and ordinal, returning one packed `uint32_t` in
  `EAX` or `W0`.
- Frame: metadata pointer, aligned group ordinal and output pointer, filling
  exactly 64 bytes owned by the cursor. V2 Local frame completion uses
  `stp q2,q3,[x2]` and `stp q2,q0,[x2,#0x20]`.

Packed metadata point mode calls once per selected source ordinal. Cached mode
fills a frame when the selected ordinal enters another 16-entry group. Direct
metadata is a curated scalar inline leaf in either mode; its bound frame
resolver is not used by this cursor. Each source independently selects its
resolver strategy, retained with that source when operands are canonicalized.

This boundary explicitly materializes metadata. It makes no in-register
aggregate-return claim. It also does not materialize decoded bitset arrays.

## Caller frames and decoder pressure

In actual V2 factored BEC/BEC union, the two cached frames occupy final
`SP+0x200..0x23f` and `SP+0x260..0x29f`. The first resolver is passed
`X2 = X29-0x90`; the second receives `X2 = SP+0x200`. Eight static `LDP Q`
sites reload these frames across the first/adjacent-ordinal paths. The
resolver's 64-byte writes belong to this metadata output contract; they are
not decoder spills.

The full V2 frame is 848 bytes. Its 160-byte save area contains 64 bytes for
`D8`–`D15` and 96 bytes for general registers including frame/link registers.
Besides the eight metadata reload sites, factored union has 61 vector stack
instructions accessing temporaries and constants. The adjacent four-stream
BEC region contributes many intermediate slots. These counts include distinct
conditional paths, and cannot be multiplied directly by selected cardinality.

| V2 selected kernel | Text: grain 2 → 1 | Frame: grain 2 → 1 | Static vector stack instructions: grain 2 → 1 |
| --- | ---: | ---: | ---: |
| BEC/BEC factored union | 17,508 → 4,524 B | 848 → 400 B | 69 → 10 |
| BEC/BEC factored intersection | 17,512 → 4,552 B | 848 → 432 B | 65 → 10 |
| Local/Local inline union | 19,716 → 5,604 B | 848 → 224 B | 77 → 13 |
| Local/Local inline intersection | 20,908 → 5,628 B | 816 → 256 B | 61 → 14 |
| BEC/plain union | 7,208 → 1,788 B | 304 → 240 B | 14 → 6 |
| BEC/plain intersection | 7,240 → 1,792 B | 304 → 240 B | 14 → 6 |
| Plain/plain, each operation | 500 → 500 B | 0 → 0 B | 0 → 0 |

On both x86 targets the factored BEC/BEC prologue saves six general registers,
aligns RSP to 64 bytes, then reserves 384 bytes. BEC/plain reserves 256 bytes
with the same saves/alignment. Alignment adds a variable amount; these reserve
sizes are not total frames. Zen Local inline uses its saves and the red zone
without a separate subtraction. GNR Local inline additionally subtracts
104/152 bytes for union/intersection. Their stack usage illustrates why the
ABI, cached metadata output and compiler temporaries need separate accounting.

## Grain comparison: fewer spills did not win

The actual V2 grain-one executable has 24,888 bytes of selected-kernel text,
with the same 1,196 bytes of shared resolvers. BEC/BEC has four static resolver
call sites and BEC/plain two; the inline controls remain call-free.

In its factored BEC/BEC union, four `LDP Q` sites reload the two 64-byte cursor
frames at `SP+0x40` and `SP+0xa0`. The other six vector stack instructions are
two stores and four reload sites for hoisted constants at `SP` and `SP+0x10`.
There is no additional vector decoder-intermediate stack slot in this
particular function. Local/Local inline union still has one 16-byte decoder
intermediate spill/reload alongside constants. These classifications are from
the final executable; they exclude the removed frame initialization.

Despite the smaller code and fewer spills, grain one lost the dense and
clustered comparisons. For complete `local-frame` intersection with all slices
selected, median call time increased from 40,401.4 to 44,156.1 ns on
`random_dense` (+9.29%), from 10,017.1 to 10,590.9 ns on `census-income`
(+5.73%), and from 32,283.4 to 35,106.9 ns on `msmarco` (+8.75%). These are
five-repetition measurements from the same V2 worker; see the retained
[grain-two summary](../evidence/operations-neoverse-v2/algebra-summary.csv) and
[grain-one summary](../evidence/operations-neoverse-v2-grain1/algebra-summary.csv).
Dispersed selections were roughly equal; terminal-heavy inputs sometimes
favoured grain one. Grain two remains the default. A smaller frame alone does
not settle execution-grain choice, and this audit does not isolate the precise
instruction scheduling or loop-amortization cause of the measured difference.

## Reproduction

The linked artifact receipts recover the actual source, executable and assembly
for each table row. `llvm-nm-21 -S -C` gives the symbol sizes;
`llvm-objdump-21 -dr -C` gives the prologues and call/stack sites. Restrict the
inspection to `selected_kernel`, `resolve_point` and `resolve_frame` symbols.
Do not substitute a current-source rebuild for an archived benchmark ELF.

The initial compile-only V2 experiment disabled adjacent outputs, retaining
`decode_bec2(A_s,popA,B_s,popB)` plus `combine_halves` for two operands at one
coordinate. It sharply reduced code duplication and four-stream pressure.
That motivated the live output-grain control. The live control preserves
plain/plain pairing and all source identity and terminal shortcuts. The
initial scratch variant also disabled plain pairing and used the earlier
shared-mode API, so its numbers are not the final grain comparison.

Ignored audit material is in
`build/experiments/ikea-heterogeneous-algebra-native/`. `audit-final.py` reads
actual ELF sizes and hashes plus static call/vector-stack sites;
`final-grain{1,2}-audit.json` holds the inspected results. Compiler stack-usage
outputs and controlled objects are supporting checks, not substitutes for the
captured executables. The original scratch source/header and assembly remain
there to reproduce the initial hypothesis. No live kernel or metadata
implementation was changed by this audit.
