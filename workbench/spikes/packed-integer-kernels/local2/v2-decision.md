# Local2/u8: V2 selection of the 64-value encoder region

The exact-binary ABBA replay supports extending the existing Local3..7/u8
encoder coalescing mechanism to Local2/u8. The selected change is two encoder
conditions: admit W2 in the helper and its dense u8 selection. Decode, wider
carriers, individual/strided tiles and exact pair/single remainders stay unchanged.
The root task selected this mechanism after the replay; live application is held
for a coherent integration with the independently assessed changes.

All 21 cases passed in each process, five sequential 50 ms repetitions on pinned
Neoverse V2 CPU0. Before/candidate columns below pool ten CPU ns/value samples
per variant from the two process orders. The
[compact table](evidence/v2-20260910/cases.csv) retains all 420 repetitions as
84 process/case rows, including every unchanged physical and public control.
[Provenance](evidence/v2-20260910/provenance.json) and the
[replay artifact](evidence/v2-20260910/artifact.json) preserve exact binary and
source recovery references.

| Case | Before | Candidate | Candidate / before |
|---|---:|---:|---:|
| `local2_grain/bound/256` | 0.203910 | 0.167136 | 0.8197 |
| `local2_grain/bound/8192` | 0.200155 | 0.161107 | 0.8049 |
| `local2_grain/bound/65536` | 0.199871 | 0.160536 | 0.8032 |
| `local2_grain/native/256` | 0.200279 | 0.165198 | 0.8248 |
| `local2_grain/native/8192` | 0.199783 | 0.164117 | 0.8215 |
| `local2_grain/native/65536` | 0.199829 | 0.166362 | 0.8325 |
| `bulk/series/neon/local/k2/h0/u8/encode` | 0.200222 | 0.161115 | 0.8047 |

The public bound encoder improves by about 18–20% across 256, 8,192 and 65,536
values. The ordinary 8,192-value K2 encode case improves about 19.6%. K1/K3
encode and K1/K2/K3 decode controls remain close, as do the unchanged explicit
coalesced64 and predecessor arms. At 8,192 values the candidate bound result
beats the immediate predecessor256 by about 3%; at 65,536 it is also close to
or slightly ahead of that control. This meets the omitted physical mechanism
identified in the retained-source review, without demanding a larger 256-value
local block or changing the primitive's wire contract.

The candidate transposes four existing 16-value carriers and issues one TBL4
and one 16-byte store for 64 values; the old path issued four TBL1 operations
and four 4-byte stores. The actual candidate native loop has 80 instructions
per 64 values, with no hot calls or stack accesses. Only the Local2/u8 typed
encoder differs among 2,105 native functions; all 2,104 other instruction bodies
are identical. Text grows 380 bytes and read-only data 16 bytes, without new
dispatch tables or unwind growth.

Both versions passed 15,504 independent ASan/UBSan dense encoder cases, 780
public placements with 14,040 reads and 7,692 mutations, and 6,192 exact checked/
bound encode guard cases. Tests include arbitrary high-bit projection, all four
carriers, tile counts 0..33, every u8 source bit across 64/72 values, and headed
K10/H8 and K18/H16 cases. This is targeted validation supported by the preceding
coherent aggregate; it is not a newly rebuilt aggregate result.

Source closure: coherent native.cpp `1abf641b` and baseline header `24ca6129`;
candidate header `2f1e2f69`. The exact binary pair excludes both the all-head and
Scan4 candidates. Before SHA is `f3cdffaa4ad5e2164fcde97c9105b02d202d40ca846d2c0ee8770625f7591d5e`;
candidate SHA is `24f65cfb20af4efb5b0a0cb587b8bcb54cb42f9d6a942003c4f78349d197e4e0`.
Their original archive `eabcd356` includes the patch, complete source closure,
compile/link receipts, independent checks and generated-code comparison.

Each process shares the actual allocations among its physical/bound control
arms. Identical addresses across different processes are not promised. The
three tested extents establish extent-spanning behavior, not a claim of cache
residency, and the observed result is for dense u8 input rather than all carriers.
