# Measured compilation boundaries

This is an assembly audit of the initial and corrected hardware captures,
not a performance model. The native 16-entry metadata carrier is an inline
implementation choice. The compiled BEC region is a fixed decode/intersection/
count operation; these results do not establish a universal fragment interface.

The initial September 10 captures were Zen `003352Z-a1cf00b2`, GNR
`003352Z-9d2cee6f`, and V2 `003352Z-e308f9a8`. Every nominal inline reader
actually called compiler-outlined `CountOperations<inlined>::count_pair` and
`count_one`. Their implicit, unused `this` occupied a sixth scalar argument
position for pairs. Their BEC bodies had the same symbol sizes as the explicit
five-argument wrappers. Local's range loop was additionally outlined on V2;
Scan's was outlined on all three targets. This campaign is a compiler-boundary
control, not the intended inline versus split comparison.

Forcing the selected operation methods, authored range function, cursor access,
and execution wrapper to inline produced the intended distinction. In corrected
captures, every inline reader has zero calls, and each split reader has one
static pair call site and one single-body call site for the odd tail. No outlined
`CountOperations` or `count_range` survives. The call sites execute once per
requested pair and once for an odd final block, respectively.

| Corrected capture under `build/workers/` | Result directory |
| --- | --- |
| `20260910T003840Z-66c72cb9` | `20260910T003904.440425Z-zen5` |
| `20260910T003840Z-53340712` | `20260910T003905.398749Z-granite-rapids` |
| `20260910T003840Z-c957501c` | `20260910T003904.788896Z-neoverse-v2` |

Run [boundary-audit.py](../boundary-audit.py) on each `results/RUN` directory
to reproduce exact ELF symbol sizes, call sites, input hashes, and totals. It
also identifies the original outlining. It does not infer dynamic stack traffic
from a static instruction count. The frame classifications below come from
following register definitions and stack-derived addresses in those captures.

The [retained evidence index](../measurements.md#validation-retained-evidence-and-recovery)
links both generations' exact source and binary bundles. For example, from the
Linux repository root (prefix with `orb -m ubuntu` on the Mac):

```sh
python3 workbench/tools/artifacts.py fetch workbench/spikes/ikea-heterogeneous/evidence/zen5 build/recovered/heterogeneous-zen5
python3 workbench/spikes/ikea-heterogeneous/boundary-audit.py build/recovered/heterogeneous-zen5
```

## Code sharing

Function text bytes exclude alignment padding, literal pools, unwind tables,
checks, and other provider functions. Shared BEC text is counted once for the
split total. All three metadata layouts use those same two compiled bodies.

| Function or group | Zen | GNR | V2 |
| --- | ---: | ---: | ---: |
| Direct inline | 2,965 | 2,929 | 3,952 |
| Local inline | 3,606 | 3,594 | 4,804 |
| Scan inline | 3,995 | 3,995 | 5,048 |
| Direct split caller | 246 | 245 | 216 |
| Local split caller | 891 | 885 | 1,168 |
| Scan split caller | 1,219 | 1,219 | 1,436 |
| Shared BEC one-body region | 1,204 | 1,198 | 1,248 |
| Shared BEC two-body region | 1,557 | 1,539 | 2,420 |
| Three inline readers | **10,566** | **10,518** | **13,804** |
| Three split callers plus shared regions | **5,117** | **5,086** | **6,488** |

## The caller's live metadata

Direct reads scalar metadata for the requested bodies and retains no native
metadata frame. Corrected Local and Scan split callers preserve their 64-byte
frame around every pair call. On x86 this is one ZMM store/load; on V2 it is
one `ST1`/`LD1` of four Q registers through stack-derived pointers. These are
real live-state spills after the cursor's address escape has been eliminated.

For example, corrected V2 Local stores the frame at `0xecac`, calls BEC, and
reloads it at `0xed00`. Scan stores at `0x10458` and reloads at `0x104c4`.
Their stack frames occupy 192 and 224 bytes, including saved scalar registers.
The BEC callee's frame is additional. Full vector lanes are caller-clobbered
under these ordinary ABIs; V2's preservation of D8–D15 covers only their low
64 bits and does not preserve the four-Q metadata carrier.

This differs from the initial outlined-cursor case: the metadata frame then
had a permanent home in the outer wrapper's stack. Refills wrote it, and group
hits reloaded it after calls; it was not stored anew before every pair call.

Corrected x86 inline Local keeps metadata vectors off stack. Inline Scan also
keeps them off stack but spills three scalar values. V2 full inline still
spills its metadata frame around the inlined pair computation: Local uses
`sp+0x1a0..0x1df`, Scan `sp+0x150..0x18f`.

## V2 pressure inside the selected region

The explicit BEC pair region allocates 80 bytes: 64 for ABI saves of D8–D15,
and 16 for a byte-population intermediate. It neither materializes decoded
bitsets nor stores the unused decoded bit lengths. Its one-body counterpart
only saves/restores D8/D9. The caller's metadata spill is separate from both.

Full inlining lets the compiler hoist the four 64-byte code tables and two
16-byte constants, but it puts them in stack storage. The following byte
counts describe executed stack accesses within one pair iteration, excluding
metadata refill, prologue/epilogue, the odd tail, payload, and query accesses.
They are architectural accesses, not cache misses or measured bandwidth.

| V2 inline pair loop | Metadata store/load | BEC intermediate store/load | Constant reloads |
| --- | ---: | ---: | ---: |
| Direct | 0 / 0 | 96 / 96 | 288 |
| Local | 64 / 64 | 128 / 128 | 288 |
| Scan | 64 / 64 | 112 / 112 | 352 |

Local's eight temporary Q slots occupy `sp+0x120..0x19f`; Scan's seven occupy
`sp+0xe0..0x14f`. Local reloads its four table groups at `0xe064`, `0xe0f4`,
`0xe100`, and `0xe10c`. Scan reloads the group at `sp+0x190` twice, at
`0xf8fc` and `0xfac4`, in addition to the other three groups. Each inline
reader initially stores 288 bytes of lookup constants once for its pair-loop
path. Total inline frame allocations are Direct 464, Local 576, Scan 592 bytes.

By comparison, split Local/Scan's per-pair metadata preservation is 64 bytes
each way, and the BEC pair callee saves/restores a further 80 bytes each way.
This excludes the split caller's range-level prologue and metadata-refill work.
Removing calls therefore does not imply eliminating stack traffic on V2.

## Boundary and access contract

The explicit two-body region uses five scalar/pointer arguments and a scalar
return: SysV RDI/ESI/RDX/ECX/R8 → RAX, or AAPCS64 X0/W1/X2/W3/X4 → X0. The
one-body region uses three arguments. The outer prepared range kernel has six
scalar/pointer arguments; it is a separate boundary. There is no hidden result
buffer, native-vector parameter, error result, or hot validation at the BEC call.

Each BEC body retains its provider's 64-readable-byte contract. Query accesses
are exact: x86 folds a 32- or 64-byte load into the final AND; V2 loads one or
two pairs of Q registers. The range accesses query bytes
`[32*first, 32*(first+count))`. It does not query predecessor blocks while
reconstructing metadata, or read a second query block for an odd tail.
Admission currently requires a query allocation covering the entire source,
which is stronger than this executed range footprint.

The immediate low-risk correction was making the intended compilation boundary
explicit and inspecting it. Further choices should follow the paired timings:
retaining a compiled BEC region on V2 is a credible preset, while x86 can keep
the metadata carrier live through its inline recipe. A future bounded screen
could shorten the lifetime of only the final pair's metadata frame; currently
the split loop reloads it before testing whether another iteration needs it.
That is a hypothesis, not a measured gain or a reason to broaden the ABI.
