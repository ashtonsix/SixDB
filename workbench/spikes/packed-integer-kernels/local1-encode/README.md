# Local1 byte-input four-region encode pairing

This is an isolated candidate, not an adopted implementation. Hardware is
orchestrated separately by the SeriesPack task. `run.py` executes inside an
existing coherent worker workspace; it does not dispatch workers or modify
production files.

The coherent full-feature public Local1/u8 encode endpoint trails the immediate
LocalPack control on both targets (Zen 38.08 versus 24.55 ns per 8192 values;
GNR 77.43 versus 66.79). Their bit-extraction leaves already agree. The immediate
prior encloses four exact 64-value regions in one loop, while the public SeriesPack
loop encloses one. The earlier Zen shared-buffer diagnostic found that this
four-region loop recovers the gap at 8192 values, with a smaller gain at 65536.
Those measurements are a reason for this public paired test, not an acceptance
result. See `prior-signal.json` for exact context and source identifiers.

## Exact proposed change

`prepare_overlay.py` requires coherent `native.cpp` SHA `1abf641b…` and AVX512
header SHA `62562fc9…`. It writes a private include overlay and unified patch.
The only specialization added is LocalPack payload width 1 with byte-sized input
and GFNI plus AVX512VBMI2 enabled. Four invocations of the existing 64-value
movemask leaf consume exactly 256 bytes and write exactly 32 bytes. The existing
64-value and individual-tile remainder paths remain. There is no new bit map,
transpose, table, ISA choice, alignment promise, or unroll policy for another
width or input carrier.

This private low-field helper preserves projection from arbitrary input bits.
Public checked encode still validates actual logical width. In particular,
K9/H8 and K17/H16 with u8 input can use the new payload loop while the independent
head planes are populated by the existing head writers. Headed u16/u32/u64 inputs
and strided payloads remain controls, not candidates for this specialization.

Zen and GNR cross-compilation against the captured coherent archive changes only
one of 3896 `.text`/`.rodata` sections: the Local1/u8 typed encode function grows
by 112 bytes. Its hot loop changes from 7 instructions per 64 values to 16 per
256 values, retaining three instructions per leaf and one live vector/mask pair.
There are no calls or spills within that loop and no changed constant tables.
`generated-code.json` records exact object hashes, sizes and section comparison;
`loops.txt` retains compact disassembly. These are code-generation observations,
not hardware performance or exact-access acceptance.

## Checks and paired execution

Run from the exact coherent worker workspace:

```sh
python3 workbench/spikes/packed-integer-kernels/local1-encode/run.py zen5
# or granite-rapids, with SIXDB_RESULTS and SIXDB_CPU supplied by the worker
```

The driver verifies the complete local source closure and the exact full-profile
archive, then recompiles only `native.cpp` with the overlay first. The captured
compiler command, compile profile, benchmark objects, prior objects and original
archive remain unchanged. The new native object precedes the archive at link
time. Every command, resource record, link-input digest, binary digest, size and
linked disassembly is captured. No LTO or archive replacement is introduced.

Before timing, both normally optimized before/candidate guard binaries must pass
7633 cases. They use an independent wire oracle, single-bit bases over all 2048
input bits in a 256-value byte region, all four input carriers, arbitrary high-bit
projection, unaligned starts, counts from zero through 65536, partial packets,
sentinel gaps, front/end guard pages, and separately protected payload/head tiles.
Public bound and checked encode are exercised with H0/H8/H16 and independent head
strides. The private partial-packet driver stages its own zeroed boundary packet;
that scaffold is not a production change. Existing complete public-operation and
range guard binaries are also relinked against the candidate and run explicitly
with `--target avx512`. Full-feature hardware execution is required; no QEMU
workaround or disabled optimizer substitutes for these checks.

The captured Google Benchmark public fixture runs 16 encode/decode names at
256/8192/65536 values, five sequential repetitions, and before/candidate/candidate/
before process order. This includes the affected K1/H0/u8 encode endpoint plus
negative width, carrier, headed and decode controls. It produces 960 iterations.

`heads_bench.cpp` is a common public-only TU compiled once and relinked identically.
It supplies the narrower-than-logical-width input cases absent from the main
fixture: u8 at H0, H8 and H16, with dense or independently strided head planes.
Each invocation independently checks exact public bytes and stride sentinels.
It uses the same three lengths and process order, five sequential repetitions,
and 20 ms calibration, producing 300 timing rows. Input/output page offsets are
recorded. These separate executables share fixture code, not a virtual address
space; linked placement and cache placement remain possible influences. Inspect
the exact linked loops and negative controls before attributing small differences.

All four normal before/candidate guard executables and both tuned candidate native
objects compiled locally with pinned Clang 21.1.8. No AVX512 diagnostic was executed
on the ARM workstation. Selection awaits the retained public results on both
hardware targets. Scan3 remains a documented context/grain question; this experiment
does not expand into that width.

The adjacent replay driver expects the original captured checkout and its source
paths. Moving this study does not change those measured identities; use the
[archive recovery guide](../../ikea-composition/archive/validation-20260911.md) to restore them.
