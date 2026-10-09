# Cut1 consumer code-generation control

This is a source/IR/assembly audit of the immutable `qualified-cut-1`, against
the frozen `7f73c81` baseline. It is not a timing result or an audit of current
working source, borrowed-parent admission, or the later compact-law caller.
The [receipt](../../../../../build/orbital-mvcc/metadata-consumer/cut-1-codegen/receipt.json)
and [commands](../../../../../build/orbital-mvcc/metadata-consumer/cut-1-codegen/commands.txt)
attribute the exact input cut and both outputs. All 23 candidate-source hashes
and all five output hashes match that receipt on this read-only inspection.

Compiler: Ubuntu Clang 21.1.8
`(++20251221033104+2078da43e25a-1~exp1~20251221153121.57)`, libstdc++15.
Recorded common flags are `-std=c++23 -O3 -g0 -Wall -Wextra -Wpedantic
-Wno-return-type-c-linkage -fno-fast-math -ffp-contract=off
-fdenormal-fp-math=ieee -DCONSUMER_METADATA_FROZEN=1
-DSIXDB_TUNE_NEOVERSE_V3=0`; include roots are the recorded frozen root,
`ikea/include`, and `orbital/include`. Commands use `taskset -c 2`.

| Target IR triple | Recorded target flags |
| --- | --- |
| `aarch64-unknown-linux-gnu` | `-march=armv8-a+simd -mtune=neoverse-v2` (native compiler target) |
| `x86_64-unknown-linux-gnu` | `--target=x86_64-linux-gnu --gcc-install-dir=/usr/lib/gcc-cross/x86_64-linux-gnu/15 --sysroot=/ -isystem /usr/x86_64-linux-gnu/include -march=x86-64-v3 -mtune=znver5` |

These are ordinary calling conventions: the source introduces no special ABI
attributes. Exact return signatures are in the receipt and
[AArch64 IR](../../../../../build/orbital-mvcc/metadata-consumer/cut-1-codegen/aarch64/candidate_abi.ll)
and [x86-64 IR](../../../../../build/orbital-mvcc/metadata-consumer/cut-1-codegen/x86_64/candidate_abi.ll).

| Exported operation | AArch64 ordinary return | x86-64 ordinary return |
| --- | --- | --- |
| `part8`, `descriptor_view8`, `descriptor_selected8` | Two register words, `[2 x i64]`; payload/error in `x0`, discriminator in low `x1` | `{ptr,i8}` in `rax`/`dl` |
| `part16` | Indirect `sret`, hidden result pointer in `x8` | Indirect `sret`, hidden result pointer in `rdi` |
| `part16_into` | Status in `x0`; successful state written to explicit output | Status in `rax`; successful state written to explicit output |
| `point8`, `chain8`, `chain16` | `void`, explicit caller-owned Outcome output | Same |

Cut1 exports no separate `point16`; point8 selects one step of the same helper
used by chain16. The
x86-64 full-selected signature passes the requested PartId on the incoming
stack (`byval`) after its other arguments fill the available registers.

## Actual emitted frames

The following numbers come from instructions in
[AArch64 assembly](../../../../../build/orbital-mvcc/metadata-consumer/cut-1-codegen/aarch64/candidate_abi.s)
and [x86-64 assembly](../../../../../build/orbital-mvcc/metadata-consumer/cut-1-codegen/x86_64/candidate_abi.s),
not C++ type sizes. AArch64 lists the maximum function-owned SP decrement.
x86-64 lists pushes plus RSP subtraction, excluding its incoming return address;
red-zone reach below that adjusted RSP is separate. Early refusal paths may
avoid these frames. No sequential callee frames are added together.

| Emitted function | AArch64 SP decrement | x86-64 owned adjustment | x86-64 red-zone reach |
| --- | ---: | ---: | ---: |
| `consumer_candidate_part8` | 32 | 8 | 0 |
| `consumer_candidate_part16` | 48 | 24 | 0 |
| `consumer_candidate_part16_into` | 48 | 24 | 0 |
| `consumer_candidate_descriptor_view8` | 32 | 8 | 0 |
| `consumer_candidate_descriptor_selected8` | 16 | 0, tail jump | 0 |
| `qualify_descriptor_tuple8` | 176 | 152 | 0 |
| `qualify_tuple_parent` | 32 | 0 | 24 |
| `qualify_selected_part8` | 48 | 40 | 0 |
| `detail::admit_tuple_part` | 144 | 120 | 0 |
| `consumer_candidate_point8`, `chain8`, `chain16` | 0, tail branch | 0, tail jump | 0 |
| `read_chain_into` | 48 | 48 | 60 |

The full-selected AArch64 wrapper really calls and then normalizes the return
discriminator, despite the IR's `tail call` marker; its 16-byte frame remains
active. The x86-64 wrapper really tail-jumps. The fused wrappers tail-transfer
on both targets. In the x86-64 leaf body, six pushes account for 48 bytes and
the lowest spill is a four-byte store at `-60(%rsp)`: CFI's 56 bytes alone would
miss that red-zone storage.

For the visible concurrent paths, raw Part8/View8 admission uses 176 bytes on
AArch64 (`32+144`); Part16 and status-out use 192 (`48+144`). Their corresponding
x86-64 depths below wrapper-entry RSP are 136 (`8+8+120`) and 152
(`24+8+120`), including the nested call's eight-byte return address. Full
selected admission reaches 384 visible AArch64 bytes
(`16+176+48+144`) or 328 x86-64 bytes (`152+40+120+16`, two nested return
addresses). Its parent-only comparison branch instead reaches 224/184 bytes,
including x86-64's 24-byte red-zone use. Fused wrappers reach the leaf's 48
AArch64 bytes or 108 x86-64 bytes (`48+60`).

Those are partial paths in this emitted translation unit. `layout::encode`,
`bcmp`, `compact_descriptor::describe`, `Selection::part_count`, and
`Selection::part` remain external calls; their own frames and any subsequent
callees are absent from these files. Incoming caller frames, x86-64's initial
return address and caller argument storage are also excluded. These numbers
are neither full process-stack bounds nor general optimized frame claims.

## Residual work visible in cut1

All raw Part result/status wrappers call the same emitted admission helper.
It retains a 75-byte serialization scratch, calls `layout::encode` and `bcmp`,
then loads layout unit and all four prepared reader unit/transfer-width facts.
Part8 removes an indirect result packet; it does not remove this cold work.
Status-out retains the same wrapper adjustment as Part16 in this cut, but only
writes pointer/count after admission succeeds.

Full selected admission materializes the actual 88-byte rich Description,
checks its fields, calls actual Part count, qualifies the parent and discovers
the requested Part before the common Part helper. Parent comparisons lower to
inline scalar/vector comparisons in assembly; the IR `bcmp` at that seam is not
an actual emitted library call. Part-only View admission derives its metadata
address directly from the leaf offsets and does not perform full parent checks.

The complete point/chain body has no external calls. Named view and endpoint
records disappear as separate stack objects, but their dynamic transfer facts
remain: offsets/shifts load before the outer chain loop, while key/next/value/tag
masks load again within its search/hit paths. Exact eight/eight/three/four-byte
payload loads are visible, including the two-byte plus one-byte value load.
Point8 and chain8 still load Schema/count from their explicit control; chain16
receives those arguments directly. Canonical-law derivation, borrowing the
frozen parent and reusing immutable admission proof are concrete later
comparators for this work. These loads, calls and spills do not establish their
performance cost or the benefit of removing them.

Neither target IR/assembly contains `llvm.global_ctors`, `_GLOBAL__sub_I`,
`canonical_tuple_schema`, `DefaultTuple16`, or a Schema constructor reference.
The full Schema remains caller-owned through a pointer; IR describes that input
as 10,176 bytes on AArch64 and 15,296 bytes on x86-64. Neither value is a stack
allocation in this translation unit. The separate default-comparator TU is not
part of this code-generation control.

## Source attribution

All hashes below refer to `qualified-cut-1`, not today's files. The full
23-file manifest and generated-output hashes remain in the receipt.

| Frozen candidate file | SHA-256 |
| --- | --- |
| `candidate_abi.cpp` | `689f8be2b8200d3b8ea79c0bb93d7a371f9af515fe6a3686971d461197a3ee28` |
| `candidates.h` | `05e8fb750ea38cd1b04d26d06a1511ffc8359e635cc484109600cad57bb6cf67` |
| `adapters.h` | `cd13859a4b32d5e6b24621fece51a3448e1db943fed7a655bad63a3ca28af993` |
| `descriptor_adapter.h` | `da673faf7bc82cda3d0806f1f8e57494abbed01a69525ac54dde558bf090ea08` |
| `operations.h` | `0b0a500aa217ba9094a6938f5c0125fca2ead1205d10f0b3494a0d59369f8e16` |

Only this note was saved during the audit. No compilation, execution, timing or
Git operation was performed.
