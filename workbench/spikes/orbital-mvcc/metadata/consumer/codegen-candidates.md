# Cut 3 candidate code generation

The canonical Tuple callers remove retained format-plan loads and reduce hot
stack use. The compact caller also reduces emitted stack use while retaining a
64-byte shared `ReadLaw`. Canonical point and chain bodies are independently
inlined, so their combined code is larger. These are compiler observations;
they establish no latency or throughput ranking.

The evidence is the immutable `qualified-cut-3` capture against baseline
`7f73c81`, identified by the [receipt](../../../../../build/orbital-mvcc/metadata-consumer/cut-3-codegen/receipt.json).
All 28 candidate, 197 baseline and 40 output hashes matched when reviewed.
The copied `native_law_abi.cpp` probe hash is
`1983f448a9fab2fd7836f9bed17ee841fccac7bb24f531f6e5bcd1d9a15f9ce6`;
the separately identified live `candidate_probe.py` hash is
`3b1927c7c0d338a8b4292746af0936c29b1e58531d4a5aa4efb83b8898f9dcd7`.
Both matched. The probe includes the captured candidate headers. This review
read existing artifacts; it ran no builds, tests or timings.

The [recorded commands](../../../../../build/orbital-mvcc/metadata-consumer/cut-3-codegen/commands.txt)
use Ubuntu Clang 21.1.8 (`2078da43e25a`), libstdc++ 15, C++23, `-O3 -g0
-UNDEBUG`, `-fno-fast-math -ffp-contract=off -fdenormal-fp-math=ieee`,
`CONSUMER_METADATA_FROZEN=1` and `SIXDB_TUNE_NEOVERSE_V3=0`. AArch64 uses
`-march=armv8-a+simd -mtune=neoverse-v2`. x86 uses
`--target=x86_64-linux-gnu --gcc-install-dir=/usr/lib/gcc-cross/x86_64-linux-gnu/15
--sysroot=/ -isystem /usr/x86_64-linux-gnu/include -march=x86-64-v3
-mtune=znver5`. The commands also record warning flags and the frozen include
roots. Hot-only probes select `CONSUMER_LAW_PROBE=1`, `2` or `3`.

Ordinary result ABI remains consequential:

| Exported value | AArch64 | x86 SysV |
| --- | --- | --- |
| Compact/canonical cursor qualification, `Result<Cursor>` 16 bytes | Payload/error in `x0`, discriminator in low `x1` | Payload/error in `rax`, discriminator in `dl` |
| Compact/canonical Part qualification, `Result<Part>` 24 bytes | Indirect return through `x8` | Indirect return through incoming `rdi` |
| Compact `ReadLaw` 64-byte copy | Indirect return | Indirect return; input copy is also passed in memory |
| Plain pointer cursor or canonical count control | One register | One register |
| Plain pointer/count Part or compact reference/count control | Two registers | Two registers |
| Fused point/chain calls | `void`, caller-owned `Outcome` 24-byte output | Same |

The existing full-Schema Part8 and status/out forms still return in registers;
Part16 still returns indirectly. No special calling convention is introduced.
On x86, selected admission's requested `PartId` is passed in incoming stack
argument storage after register exhaustion; that is separate from local frames.

Hot-only `point8` and 16-step `chain8` use these emitted frames. AArch64 values
are maximum function-owned SP decrements. x86 values separate saved-register
pushes/RSP subtraction from the deepest red-zone use below the adjusted RSP;
they exclude the incoming return address.

| Law | AArch64 wrapper / deeper helper | x86 wrapper / deeper helper |
| --- | --- | --- |
| Full Schema | Both wrappers 0, tail branch; shared helper 48 | Both wrappers 0, tail jump; helper 48 + 60 red-zone = 108 |
| Compact `ReadLaw` | Both wrappers 0, tail branch; shared helper 0 | Both wrappers 0, tail jump; helper 48 + 4 red-zone = 52 |
| Canonical | Inlined point 0, chain 0; no helper | Inlined point 8, chain 24; no red-zone or helper |

There are no external calls on these hot paths. The mixed `compact_abi` TU's
`chain16` uses the same compact helper, with the same extents; canonical
`chain16` is inlined with 0 AArch64 and 24 x86 bytes. The hot-only size comparison
below includes point8/chain8, rather than this additional chain16 export.

Source temporaries do not imply stack records here. Full-Schema endpoint/view
temporaries are scalarized; its AArch64 48-byte frame saves registers rather
than allocating each source endpoint separately. Compact reconstruction of a
`gpr_read` is scalarized too: AArch64 has no frame. Runtime transfer offset,
shift and mask loads remain in the compact helper, including within the chain.
Full-Schema code also reads transfer facts, with masks reloaded on search/hit
paths. Canonical code has no retained Schema/`ReadLaw` pointer or format-fact
loads: it uses stride 23, field addresses 0/8/16/19, and a fixed mask for the
value field's spare high nibble. Count and encoded payload loads remain. The
fixed transfer kernel is still the captured Ikea kernel.

| Hot-only object | AArch64 `.text` / `.rodata` / `.eh_frame` bytes | x86 `.text` / `.rodata` / `.eh_frame` bytes |
| --- | --- | --- |
| Full Schema | 480 / 0 / 104 | 665 / 0 / 136 |
| Compact | 440 / 0 / 80 | 563 / 0 / 136 |
| Canonical | 552 / 0 / 64 | 909 / 0 / 112 |

These sums include emitted shared helpers where present. They exclude cold
qualification, actual descriptor functions, construction, shared metadata,
payload/lifetime owners, linked-library code and linker deduplication. Object
bookkeeping is separate. Zero hot `.rodata` means constants reside in
instructions. The mixed compact/canonical admission-and-hot probe has 107
readonly bytes on each target: one 75-byte exact descriptor and 32 bytes of
parent comparison constants. Its total `.text` is 3,636 AArch64 / 5,287 x86
bytes; this combined object is not an incremental size charge for either law.
There is no extra global Schema constructor/default comparator in the emitted
probe TUs. Full-Schema control still borrows the externally charged Schema
(10,176 AArch64 / 15,296 x86 bytes in this capture); compact control borrows
the charged 64-byte `ReadLaw`; canonical needs no retained plan record. All
three still require external protection of the encoded body and count law.

Cold raw qualification still checks count multiplication/address bounds,
exact body extent, null rules, unit law and the exact 75-byte descriptor.
Compact also checks all four runtime transfer facts. Canonical's exact fixed
descriptor establishes those facts directly. AArch64 compact qualify8 has
a 48-byte wrapper and calls a frame-free qualify-Part helper: its temporary
24-byte Result packet survives here despite a register return. x86 qualify8
tail-jumps to a frame-free cursor helper. Compact qualify16 tail-branches on
AArch64; its x86 wrapper saves 8 bytes and calls a frame-free helper. Canonical
raw qualify8/16 are frame-free inlined leaves on both targets.

Actual selected admission retains Object/version/Program identity, canonical
24-byte parent metadata/count/reserved/width law, actual directory count one,
requested row `PartId`, and complete Part checks. Borrowing the charged
`FrozenDescription` header avoids the 88-byte describe result; it does not
cache admission or establish backing lifetime by itself.

| Full selected seam | AArch64 wrapper / admission helper / known concurrent path | x86 wrapper / admission helper / known concurrent path |
| --- | --- | --- |
| Full Schema, Describe88 | 16 / 176 / 384 | Tail 0 / 152 / 328 |
| Full Schema, borrowed `FrozenDescription` | 16 / 128 / 288 | Tail 0 / 104 / 232 |
| Compact or canonical, borrowed | 16 / 128 / 144 | Tail 0 / 104 / 112 during compact leaf call; canonical helper 104 |

Known paths count simultaneously active emitted consumer frames; x86 also
counts active internal call return addresses, excluding the initial return
address. The Describe88 path additionally enters the selected-Part helper
(48 AArch64 / 40 x86) and common admission helper (144 / 120). Borrowed
full-Schema admission calls the common helper directly. Compact's Part helper
is frame-free; canonical Part admission is inlined. External descriptor,
layout encoder and `bcmp` frames are absent from these artifacts, so these
numbers are not whole-process stack bounds. External calls can extend any
of the paths.

Describe88 calls `describe`, directory count, parent helper and selected-Part
helper. Borrowed full-Schema admission calls `description`, directory count,
actual Part lookup and the common Part helper; parent checks are inlined.
Both still call Tuple layout encoding and `bcmp`. Compact/canonical borrowed
admission retains the three actual descriptor calls but compares its exact
literal descriptor locally; no layout encoder or `bcmp` call remains there.
Selected admission requires a live Descriptor or catalog protection for that
Selection; Export instead protects its exported View and frozen metadata. The
small borrow itself acquires no owner. Four-column admission,
container policy and ownership release costs are outside this Tuple probe.
