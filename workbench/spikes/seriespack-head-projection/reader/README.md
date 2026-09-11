# Dense headed LocalPack regions and code placement

This isolated diagnostic compares eight-value physical fragments with 32- and
64-value dense regions for residual widths W=1..7 and separate H8/H16 heads.
It investigates a useful physical-grain change after an earlier Zen K10/H8/u16
endpoint showed a large placement effect with identical instructions. It does
not impose an alignment policy or modify production selection.

The subsequent [Zen findings](zen-findings.md) retain the first hardware
comparison and its conditional result. Granite Rapids remains to be measured.

`region.h` reads the existing exact residual region, expands into the smallest
sufficient working carrier (u16 for H8, u32 for H16), joins heads in registers,
and widens only at output stores. H16 uses the public representation: independent
byte planes, head0 highest byte and head1 immediately above the residual. Each
plane has its own pointer and per-eight-value stride. Coalescing requires every
used source to be dense; otherwise exact individual packets are used. Dense
remainder packets use the current fragment leaf.

The five full-feature arms are current tile8 AVX2, region32 AVX2, current tile8
AVX512, region32 AVX512, and region64 AVX512. AVX2-only builds expose the first
two. These names identify native leaf families; a full-feature compiler can
use GFNI in the AVX2 arms. The bound placement-view entry and effects machinery
are outside this experiment. In particular, full-feature K10/H8/u16 tile8 AVX2
and AVX512 optimize to identical 89-byte functions here. These are not exact
clones of the earlier bound endpoint whose placement sensitivity motivated the
probe; gains here cannot establish that endpoint was fixed.

## Checks and placement audit

`check.cpp` constructs public wire bytes independently, including zero and
maximum values. It checks all W=1..7 and sufficient u16/u32/u64 carriers, exact
input/output guard-page boundaries, zero count with inaccessible pointers,
modulo128 starts, 8-value remainders, dense planes, independently strided head0
and head1, and fully strided sources. H8 passes an inaccessible unused head1.
The AVX2 build passed 114,240 cases under QEMU. Full-feature execution remains
a hardware check; compilation passed. Each AVX2 linked benchmark variant also
passed its 66 fixture checks. These checks concern physical byte extents, not
cold cache-line access counts.

`build_variants.py` links one immutable timing object four times with section
padding 0/16/32/48. It checks function sizes, normalized instructions, exact
constant references and complete constant bytes across all four binaries.
Every timed function must shift by the requested amount modulo64 and contain
no helper calls. The current AVX2/full-feature builds contain 22/55 timed
functions with no stack references. `local-checks.json` retains hashes and
compact code-size evidence; binaries, full assembly and link maps remain in
ignored output. Function and backward-branch addresses are recorded separately.
A branch target is evidence about placement, not a diagnosed CPU mechanism.

## Build and run

From the Linux repository root, with the pinned compiler (prefix commands with
`orb -m ubuntu` when working through the macOS mount):

```sh
probe=workbench/spikes/seriespack-head-projection/reader
out=build/seriespack-headed-region
mkdir -p "$out"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -Iikea/include "$probe/check.cpp" -o "$out/check-avx2"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=x86-64-v3 -Iikea/include -c "$probe/bench.cpp" -o "$out/bench-avx2.o"
python3 "$probe/build_variants.py" "$out/bench-avx2.o" "$out/avx2"
```

Use the worker's full feature profile and target tune for the second build;
the local compile audit used `-march=znver5`. Cross compilation from the ARM
Linux host additionally uses `--target=x86_64-linux-gnu --gcc-toolchain=/usr`.
Pass those flags individually to the linker helper with
`--link-flag=--target=x86_64-linux-gnu --link-flag=--gcc-toolchain=/usr`.

Run the checker and `padN --check-only` on each eligible target before timing.
For the narrow placement question, run all four `padN --focus-k10` executables
sequentially on one pinned CPU. For the broader grain question, omit that flag.
`SIXDB_CPU` selects an allowed CPU; the binary sets and verifies affinity. The
same shape shares the exact payload/head0/head1/output addresses across all
arms, and their page offsets are included in every timing row. Different
executables must not be assumed to share virtual addresses.

Timing uses one opaque full-array call per pass. It calibrates a common pass
count per case to at least 20ms for tile8 AVX2, warms each arm, and records ten
balanced order rotations with serial case execution. This intentional rotating
order controls temporal drift in the focused pair; no cases execute concurrently.
Each row reports elapsed nanoseconds and nanoseconds per value. Footprints are
256, 8192 and 65536 values; cache residency is explicitly unestablished.

Full runs cover eleven width/head/carrier cases, producing 660 AVX2 or 1650
full-feature timing rows per placement. The K10 focus produces 60 or 150 rows.
Compare medians within matching host, feature ceiling, shape, carrier, count,
and placement. Report placement ranges as well as aggregate gains. A useful
candidate must survive both Zen5 and GNR; a single favorable placement or
wider-ISA arm does not justify adoption.

## Related evidence

The [Local1 diagnostic correction](../../packed-integer-kernels/local1/README.md) excludes its old
interleaved H16 fixtures from physical endpoint conclusions. This probe uses
independent public head planes from the outset of retained evidence. Headless
dense composition is investigated separately; arbitrary range reads and body-
containing LocalPack packets are outside the dense-tail coalescing comparison.
