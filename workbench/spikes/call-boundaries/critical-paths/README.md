# Single-operation critical paths

This Page-layout ABI control is retired with the disposable allocation driver.
Its original sources and commands are recoverable through the
[retirement guide](../../../benchmarks/retired-drivers.md); findings below remain historical.

October 9, 2026 bounded Stage 2 composition spike. Keeping ready read bodies
visible avoids introducing expensive class/TU seams. Specializing an admitted
fixed representation then removes dependent metadata loads and register
preservation. Integer decode remains the useful default in this case: moving
some work to SIMD or FP did not improve the modeled resource bound.

This is a complete point lookup over the current allocation-backed packed
consumer: directory search, ordered-to-physical row mapping, key search,
payload decode and the unchanged workload checksum. The unmodified
`lookup::Driver` with `ReadExecution::native_direct` is an executed correctness
control. It already inlines its useful read bodies; **ordinary → visible is
avoidance of hypothetical new seams, not a recovered Stage 1 speedup**.
There are no measured timings or claims about MVCC/Engine/Shore headroom.

## Equivalent arms and qualification

`ordinary` adds compiled Scope/Directory/View accessors around the actual
consumer. `visible` lets a friend query access those private fixture internals
and call the existing inline Ikea readers. `fused` specializes the actual
Local20 series and 20-byte tuple representation after exact admission;
multiply/SIMD/FP change only its Local20 decoder. Every query has the same
`Status(const Scope&, Key, Outcome&) noexcept` signature and output contract.
All six omit prefetch hints uniformly. The unchanged current Driver keeps its
existing hints and is not used as a matched performance comparison.

Shared cold admission checks backing footprint, exact plan transfers, stable
source objects, stream/tuple placement, unique physical slots and sorted keys.
It either borrows the uninterrupted owner lifetime or acquires real Orbital
Holds over the complete page footprint. These checks and acquisition costs are
outside **all** query arms. Revocation closes interest but retains backing;
closed and moved-from scopes leave output untouched. Page, Schema, Bindings
and directory metadata remain stable and fixture-owned. Physical retention
does not supply immutable contents or a versioned snapshot by itself.

The independent ordered-map oracle and actual native Driver passed 63,500
operation comparisons in each native and ASan/UBSan always-UAR run. Cases cover
empty and boundary populations, shuffled physical rows, hit/miss, key extrema,
wrong interpretation, invalid extents/duplicate slots, move/revoke, real
Allocation release while reading retained backing, and final arena charge
zero. The independent wire law checks every 20-bit value at all eight lanes:
8,388,608 cases per run in native, sanitized and x86 QEMU execution. The FP
boundary control checks four rounding modes and no raised exceptions.

## Static result: visibility first, specialization selectively

Clang/LLVM 21.1.8, C++23, `-O3`, assertions enabled, no LTO/fast-math/implicit
FP contraction. V2 and Zen5 use different native/cross standard-library
headers, recorded in the raw arguments; cross-ISA differences are not isolated.

| Complete query | Zen5 bytes | calls | saved GPRs | 64B code span | V2 bytes | calls / frame |
| --- | ---: | ---: | ---: | --- | ---: | --- |
| deliberately outlined ordinary | 443 | 7 | 6 + local seam state | 7–8 | 396 | 7 / 80B |
| visible bodies | 623 | 0 | 6 | 10–11 | 512 | 0 / none |
| fixed-format fused | 420 | 0 | 2 | 7–8 | 372 | 0 / none |
| fused multiply | 426 | 0 | 2 | 7–8 | 372 | 0 / none |
| fused SIMD | 416 | 0 | 2 | 7–8 | 380 | 0 / none |
| fused FP | 439 | 0 | 2 | 7–8 | 392 | 0 / none |

Code spans are alignment bounds, not observed cache misses. The outlined
query is smaller than visible because its callees live elsewhere; its seven
static call sites can execute repeatedly during search. Compare whole caller
and callees before judging code footprint. The existing Driver's much larger
symbol also contains route selection and setup, so it is not in this table.

The visible Zen5 query's twelve stack sites are six pushes and six pops:
**callee-save preservation, not twelve payload spills**. Fusion has two
push/pop pairs. Its found-row leaf has none; the SIMD leaf saves one GPR.
Neither visible nor fused leaf materializes the Record on the stack. This
example does not substantiate a broad store-to-load-forwarding problem.

The useful dependency change is in addressing. In the visible found-row leaf,
next/tag read-plan pointers feed source base/stride/offset and transfer
offset/shift/mask loads before the useful payload read. Series stride is also
loaded. Fusion uses one admitted base and fixed offsets; row arithmetic can
feed next, tag, and the two Local20 loads independently. The Zen5 leaf has
22 explicit memory-source reads before accounting for register restoration,
versus five in fused (one base plus four payload reads). These are manually
inspected static instructions, not physical memory transactions. The receipt's
automated `load_sites_lexical` is deliberately conservative and excludes some
memory-source arithmetic.

Fusion removes metadata dependency height, not the serial checksum. Key →
next → value → tag mixing remains the same observable recurrence. Reordering
its terms or replacing it with a reduction would change the workload. The
current key binary search also remains dependent; this spike does not turn
one search into a collection of independent operations.

## Pipeline alternatives: useful negative result

Exact compiler-emitted successful found-row tails, same Page/row/key/output
signature, including saves/restores and checksum, excluding final return:

| Tail | Zen5 bytes | LLVM21 modeled block reciprocal throughput | V2 bytes | V2 modeled bound |
| --- | ---: | ---: | ---: | ---: |
| visible Ikea bodies | 288 | 13.3 | 248 | 11.3 |
| fused native integer | 184 | 8.0 | 168 | 7.3 |
| fused multiply gather | 193 | 8.2 | 168 | 7.3 |
| fused SIMD gather | 190 | 8.5 | 172 | 7.8 |
| fused exact FP rebuild | 207 | 9.0 | 188 | 8.5 |

The modeled values are cycles per block under LLVM's pinned resource model,
not whole-query latency or hardware throughput. LLVM's selected Zen5 model
prints `Zn4*` resource names; the complete reports preserve that detail.
Repeated regions reuse/clobber argument registers, so their total-cycle output
is not independent-call latency. Branch prediction, directory/key search,
cache residency, admission and ownership acquisition are excluded. Exact
region extraction refuses branches/calls/multiple returns, preserves ARM
immediates, accepts final x86 `retq`, restores literal-pool definitions and
assembles each region before modeling. Parser controls are retained.

Native x86 gathers four plane bits with BMI2 PEXT; ARM uses masked integer
multiplication. The alternative x86 multiply adds an instruction without an
advantage in modeled resource demand. SIMD uses byte-sign extraction on x86
or weighted lane reduction on ARM and then crosses back to scalar checksum
work. The FP arm reconstructs `float(high16) * 16 + float(tail4)` exactly:
all integers and intermediates are below 2^20, representable in binary32 in
every rounding mode. It adds integer↔FP conversions and a dependent FP
multiply/add chain; there is no modeled payoff. This exactness proof is
specific to Local20 and is not permission to approximate wider integers.

## What to carry forward

Keep cold validation, ownership and retirement in compiled TUs. Keep small
ready read bodies visible at hot cross-class boundaries through narrow friend
functions or an admitted access object. Friend access alone does not make an
out-of-line body visible, and an attribute cannot remove dependent metadata.
The current caller is already an example of visibility without friend access.

Fixed-schema fusion merits selective follow-up where its closure is actually
stable and frequently executed. It trades reusable read plans for stronger
admission and duplicated representation knowledge, so this wire decoder is a
private comparison, not a proposed production endpoint. Integrating the real
Stage 2 types will require their selected/versioned context and revocation
rules, with qualification retained across the full caller. A custom calling
convention, output-parameter change or blanket `always_inline` policy is not
supported by this result. No shared runtime or design API changed here.

## Reproduce and recover

From the repository root, prefix commands with `orb -m ubuntu` on the Mac:

```sh
cmake --preset release -DSIXDB_SPIKES=call-boundaries
cmake --build --preset release --target critical_paths_check critical_paths_wire_check
build/clang/release/workbench/spikes/call-boundaries/critical-paths/critical_paths_check
build/clang/release/workbench/spikes/call-boundaries/critical-paths/critical_paths_wire_check
```

For sanitizer qualification, use a separate root build with
`-fsanitize=address,undefined -fsanitize-address-use-after-return=always
-fno-omit-frame-pointer` in compile/link flags and halt on either error.
Capture immutable sources with the existing capture helper, then:

```sh
python3 workbench/spikes/call-boundaries/critical-paths/codegen.py \
  --source build/captures/NAME --output build/critical-paths/NAME
```

`--reuse PREVIOUS` verifies the original captured files, objects and compiler
assembly before reanalysis. It requires a receipt with compiler-assembly
hashes. Cross headers/sysroot are installed inputs, not embedded toolchains.

[Selected evidence](evidence/20261009) keeps complete-query static summaries,
all five modeled alternatives and qualification provenance. Full compiler
assembly/objects, exact regions/reports, checked binaries, source capture and
the two-file v3→final source delta have a verified S3 recovery reference there.
Superseded compilation cuts and the fixture's earlier missing teardown poll
are omitted: they do not answer the research question. `summarize.py BUNDLE`
regenerates the two compact JSON reports from recovered raw receipts. No
source, evidence or shared changes from this spike were staged or committed.
