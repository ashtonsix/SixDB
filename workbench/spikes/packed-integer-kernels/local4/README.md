# Local4 AVX2 projected-transpose diagnostic

This isolated experiment tests a concrete omitted LocalPack mechanism: when
encoding four bit planes, the final transpose need only produce the low 32 bits
of each 64-bit word. It has **not been selected for production**. Existing
production sources and earlier diagnostics remain unchanged.

The later [two-machine raw-array and public result](hardware.md) selects the
projected32 writer for integrated validation, with measured limits recorded.

## Prior signal and hypothesis

The earlier AVX2-only bulk capture measured SeriesPack Local4/u8 encode at
1.204× the direct LocalPack control on Zen 5 and 1.184× on Granite Rapids.
[old-loop-evidence.json](old-loop-evidence.json) preserves the exact source and
binary hashes, profile commands, median rows, linked loop instructions and
artifact recovery references. These are the old capture, not the newer header
checkpoint used by this experiment.

Both implementations perform the same first two transpose exchanges. The
prior's final exchange becomes four vector instructions; SeriesPack's full
transpose retains six before discarding each word's upper 32 bits. For the
last exchange, with `M = 0x00000000f0f0f0f0`:

```text
m = (x ^ (x >> 28)) & M
full = x ^ m ^ (m << 28)
low32(full) = low32((x & ~M) | ((x >> 28) & M))
```

The candidate computes this projection only inside the Local4 region writer;
it does not weaken the general transpose result. Existing exact packing and
source-carrier narrowing remain in use. Arbitrary input high bits are still
projected away by the trusted low-field operation.

The old Zen prior loop handles 64 values in 43 static instructions, while the
old SeriesPack loop handles 32 in 26. On Granite Rapids both handle 32, in 22
and 25 instructions respectively. Thus the discarded work is a shared
hypothesis; different loop grain alone does not explain both hosts. The old
8192-value SeriesPack path skips complete-tile and partial-value remainders;
there are no calls or spills in either hot loop. Its public dispatch/effect
boundary is still a difference from the raw predecessor control.

## Isolated arms

`bench.cpp` compares the same input and output addresses within each case:

- `current32`: current native dense helper.
- `projected32`: the same source loop and narrowing, with only the final writer
  replaced. This is the proposed change.
- `current64_grain_control` and `projected64_grain_control`: two 32-value regions
  per loop, retained only to distinguish grain from the projection change.
- `direct_predecessor`: the unchanged fixed-256-value LocalPack helper inlined
  into its original runtime array loop. Available for u8, its native interface;
  no fabricated widening/narrowing bridge is added for wider carriers.

Counts are runtime 256, 8192 and 65536; carriers are u8/u16/u32/u64. All timed
values fit width 4. Every arm is verified against an independent bit oracle
before timing and after each sample. The loop runs 20 balanced rotations,
calibrates one common pass count per case to at least 20 ms for `current32`,
and warms each arm immediately before its sample. Pinning uses `SIXDB_CPU`, or
the first allowed CPU. The output records entry and buffer page offsets.
Expect 51 initial fixture checks and 1020 timing rows. Residency is not
established merely by these byte counts. This is a raw array diagnostic,
without checked admission, view dispatch or effect accounting.

`check.cpp` separately covers zero length with inaccessible pointers, complete
and partial packets, independent unaligned starts modulo 128, independent
front/end page guards, strides 4/5/13/64, gap preservation and canonical final
packet slack. It exercises all four arms and source carriers, including full
carrier high bits. A 257-case basis oracle tests every input bit and zero.
The final partial packet is staged exactly into eight values; the unchanged
native single-tile helper encodes it. This boundary strategy is diagnostic
scaffolding and is not a new public endpoint.

## Generated code and local checks

Clang 21.1.8, `-O3 -march=x86-64-v3`, separately tuned for `znver5` and
`graniterapids`, was used. QEMU 10.1.0 provided supplemental AVX2 execution:

- 19,971 independent basis/oracle/guard/stride/partial cases passed, including
  130 exact-cell checks of the unchanged u8 predecessor.
- 51 benchmark fixture comparisons passed.
- The header overlay passed the existing production payload checker:
  308 configurations and 89,496 cases, including dense runs and exact guards.

[local-checks.json](local-checks.json) retains exact compiler/run commands and
executable hashes. Hardware must rerun the normally optimized checker and
fixtures before timing.
There are no emulation performance claims.

[generated-code.json](generated-code.json) records exact binary/object hashes,
function sizes, first-loop inspection and constant sections. Full disassembly
and audits remain in ignored `build/seriespack-local4-projection/` and can be
regenerated using `audit.py`.

| u8 primary loop | Current | Projected | Values per iteration |
|---|---:|---:|---:|
| Zen tune | 26 instructions | 24 instructions | 32 |
| Granite Rapids tune | 25 instructions | 23 instructions | 32 |

Every source carrier's primary loop drops two instructions, with no loop calls
or stack accesses. The compiler keeps an additional complement-mask constant
live in the projected primary loop. The separate four-carrier production
witness object changes `.text` from 2906 to 2922 bytes on Zen tuning, and from
2421 to 2405 on Granite Rapids tuning; its constant sections grow by 24 bytes
in both. These object totals include unchanged remainder code and per-function
constants before linker merging. Static instruction counts are not cycle or
micro-op measurements, and smaller hot loops need not reduce total text.

## Build and measure

From the repository root on Linux, using the pinned compiler (prefix commands
with `orb -m ubuntu` from macOS):

```sh
trial=workbench/spikes/ikea-composition/validation/diagnostics/local4_projection
out=build/seriespack-local4-projection
mkdir -p "$out"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -mtune=znver5 -Iikea/include -Iworkbench/spikes/ikea-composition/probes/ikea-integers "$trial/check.cpp" -o "$out/check"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -mtune=znver5 -Iikea/include -Iworkbench/spikes/ikea-composition/probes/ikea-integers "$trial/bench.cpp" -o "$out/bench"
"$out/check"
SIXDB_CPU=0 "$out/bench" --check-only
SIXDB_CPU=0 "$out/bench" > "$out/samples.jsonl"
python3 "$trial/audit.py" "$out/bench" "$out/bench-audit.json"
```

Use `-mtune=graniterapids` for the GNR worker. For ARM-host cross compilation,
add `--target=x86_64-linux-gnu --gcc-toolchain=/usr`; local supplemental
execution uses `qemu-x86_64 -cpu max -L /usr/x86_64-linux-gnu`. Do not enable
GFNI or AVX-512 for this diagnostic. Hardware execution is orchestrated separately by the SeriesPack task (root);
this directory does not launch or schedule workers.

## Public-endpoint overlay

If shared-buffer hardware results justify proceeding, `prepare_overlay.py`
creates a standalone header overlay and a small patch. It verifies both the
production baseline header and the exact projected helper hash, refuses
ambiguous substitutions, and never writes to production. Existing outputs
must be identical. No loop-grain policy, other width, GFNI path or AVX-512
path changes.

```sh
python3 "$trial/prepare_overlay.py" --root . --output "$out/public-overlay"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -mtune=znver5 -I"$out/public-overlay" -Iikea/include ikea/test/seriespack/payload_x86.cpp -o "$out/overlay-check"
"$out/overlay-check"
```

The baseline `native_avx2.h` SHA-256 is
`3e78b77bcea0a0229f18b8dd2fbf27a01696b00931703db1610750445501f9f9`;
the candidate is
`17c6665559b319460d28c83f41731cfc83c7f6d117a3bd32fa84308b42fe80de`.
The generated patch SHA-256 is
`b23d250cb2aa58be341cd554bacf36f19921ee708d22ab5127237e12e056df94`.

For a paired public build, reuse the coherent capture's native.cpp and all
other captured dependencies, add the overlay include directory **before** its
production include directory, rebuild that native.cpp object and relink the
same benchmark objects. Preserve profile flags and fixture. Do not apply the
patch to the live checkout or pair different native.cpp checkpoints. Review
Local4/H0 and the headed Local payload-width-4 cases (K12/H8 and K20/H16), across
input carriers. Retain normal correctness results, exact commands, source and
binary hashes, generated code and paired whole-operation samples. Adoption
requires that evidence; a raw-array gain alone is insufficient.
