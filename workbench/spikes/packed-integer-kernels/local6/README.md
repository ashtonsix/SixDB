# Local6 dense decode: enclosing loop grain and placement

This is an isolated investigation of the coherent Zen full-profile
Local6/u8 decode loss. It changes neither production nor the byte kernel.
The hypothesis is that enclosing compiler context changed loop grain and
placement enough to dominate a previously small gap. No cause or production
selection has yet been established by this experiment.

## What changed in the actual executables

[evidence.json](evidence.json) retains exact linked loops, source and executable
hashes, compile commands, median rows and artifact recovery references.
All rows below decode 8192 values into u8 using the AVX512 target family and
full feature profile. They are CPU-time medians of three repetitions, in ns.

| Capture | SeriesPack | Direct LocalPack | Native dense loop |
|---|---:|---:|---|
| Earlier Zen candidate | 48.76 | 43.49 | 64 values, 9 instructions |
| Coherent Zen | 95.06 | 38.99 | 512 values, 36 instructions |
| Coherent Granite Rapids | 80.25 | 88.60 | 256 values, 20 instructions |

The SeriesPack leaf remains an exact 48-byte masked load, byte permutation,
GFNI transform and 64-byte output store. The actual linked permutation and
GFNI constant bytes match between the earlier and coherent Zen executables.
The new coarse `decode_complete` context lets Clang unroll this leaf eight
times for Zen and four for GNR. The earlier Zen bound decoder emitted one
region per iteration. This is ordinary generated-code context sensitivity;
no new unroll directive was added to the Local6 byte kernel.

The direct LocalPack control still emits four regions per 256-value loop,
with 24 instructions. Its permute/zero-mask lowering retains an additional
`vpandq` per region; SeriesPack already avoids that operation by selecting a
zeroed input byte in the permutation. Thus there is no newly discovered
missing LocalPack arithmetic mechanism in this case. Loop grain, scheduling,
code placement and data placement require controlled comparison. The ratios
alone do not identify a microarchitectural cause.

## Controlled experiment

`regions.h` reuses the current exact byte leaf. Six arms in `bench.cpp` use
identical input/output addresses within every case:

- `public_captured`: the bound reader from the coherent captured library.
- `native_auto`: the current dense helper in this small wrapper context.
- `controlled64`, `controlled256`, `controlled512`: one, four or eight current
  regions per loop, with additional compiler unrolling disabled.
- `direct_predecessor`: the unchanged fixed-256-value LocalPack decoder,
  inlined into its ordinary runtime array loop.

Only Local6/u8 decode is timed. Counts are runtime 256/8192/65536; input starts
at page offset 0, and output starts at page offset 0/64/2048 in separately
allocated storage. The same independent bit oracle checks all arms. There are
18 balanced rotations, a common pass count calibrated to at least 20 ms for
`native_auto`, and warm-up before each sample. CPU pinning uses `SIXDB_CPU` or
the first allowed CPU. Expected output is 54 fixture checks and 972 timing
rows per placement; four placements produce 3888 timing rows per target.
Byte counts do not establish cache residency.

`build_variants.py` links the same immutable object and captured library at
four placements of the raw-operation section: 0/16/32/48 bytes modulo 64. It
checks identical normalized instructions and constants, and records every
backward-loop offset. The captured public decoder core stays fixed in `.text`
across those four variants; only its thin wrapper moves with the raw arms.
The public core is also audited for invariant code and address across variants.

Relinking the captured library changes its placement relative to the original
large benchmark: the Zen primary loop moves from mod64=32 to 0 in this local
build, and GNR from 16 to 48. It retains the same eight-/four-region object
code. `public_captured` is therefore a relinked public control, not a claim to
reproduce the original code address. A fast relinked public result would be a
useful placement/context signal, not proof that an alternative loop is needed.

## Checks and emitted controls

`check.cpp` independently constructs and decodes public bit-plane bytes. It
checks all four raw implementations plus the captured public reader for
complete/partial counts, strided placement, unused output preservation,
front/end guard pages, unaligned starts modulo 128, and zero-length operations
with inaccessible pointers. The unchanged predecessor is checked where its
complete dense-cell interface applies. A 385-case basis oracle covers every
wire bit of the 64-value leaf and zero. The expected total is **6471 checks**.

Normally optimized checkers compile cleanly for Zen and GNR. They have **not
been executed locally**: this ARM host does not provide the required AVX512
execution. Hardware must pass the checker and each fixture before timing.
There are no emulation timings or claimed hardware passes for this diagnostic.

[generated-code.json](generated-code.json) retains executable/library/object
hashes, placement audits, sizes and first-loop instructions. The raw auto
wrapper emits one region in both targets here. Raw controlled64/256/512 loops
emit 9/21/37 instructions, respectively, with no loop calls or stack accesses.
Their counter form is not byte-identical to the original public loop; this is
a controlled grain comparison. Full-function boundary paths can contain
stack references or calls, which are not in the measured complete-array loop.

## Build and run

Use the pinned Linux Clang 21.1.8; prefix Linux commands with
`orb -m ubuntu` from macOS. Select the matching coherent captured
`libikea_seriespack.a`, whose hash is recorded in `generated-code.json`.
The corresponding source must have native.cpp SHA-256
`1abf641b246af9c075e8234a9e1b3390c412a8410af9353d84d5c52cd57350da`.
Do not silently relink a different production checkpoint.

```sh
trial=workbench/spikes/packed-integer-kernels/local6
out=build/seriespack-local6-grain
library=/path/to/coherent/avx512/libikea_seriespack.a
mkdir -p "$out"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=znver5 -Iikea/include -Iworkbench/spikes/ikea-composition/probes/ikea-integers -c "$trial/bench.cpp" -o "$out/bench.o"
clang++-21 -std=c++23 -O3 -Wall -Wextra -Werror -march=znver5 -Iikea/include -Iworkbench/spikes/ikea-composition/probes/ikea-integers "$trial/check.cpp" "$library" -o "$out/check"
"$out/check"
python3 "$trial/build_variants.py" "$out/bench.o" "$out/variants" --library "$library"
for pad in 0 16 32 48; do
  SIXDB_CPU=0 "$out/variants/pad$pad" --check-only
  SIXDB_CPU=0 "$out/variants/pad$pad" > "$out/pad$pad.jsonl"
done
```

Use `-march=graniterapids` with the GNR captured library. ARM-host cross builds
add `--target=x86_64-linux-gnu --gcc-toolchain=/usr`; pass both also as repeated
`--link-flag=...` arguments to `build_variants.py`. Hardware is orchestrated
separately by the SeriesPack task (root). These sources do not launch workers.

## Actual public-endpoint pairing

If controlled hardware evidence favors a grain, `prepare_overlay.py` creates
an exact hash-checked header overlay for 64, 256 or 512 values. It changes only
the loop directive for GFNI AVX512 Local6/u8 dense decode; the leaf, bounds,
other widths, carriers and non-GFNI paths remain untouched. It requires both
the coherent native.cpp and native_avx512.h hashes, and does not edit either.

```sh
python3 "$trial/prepare_overlay.py" --root . --output "$out/overlay256" --grain 256
```

For the 256-value overlay, the candidate header SHA-256 is
`13e8d134ef902f18fd39da0c19bae0710aa7acdeff2e7716db717e5976548fbe`;
the patch is
`1ccea5b8b459b207b3a05fe36f132e76138140e7690430dda6a52393d409c4fd`.
A small `witness.cpp` compile confirms four leaf instances in its main loop;
this is not a public performance result.

Rebuild the same captured native.cpp with the overlay include directory before
its production include directory, then relink every other captured object
unchanged. Run normal public correctness and paired public Local6/u8 decode
measurements before selecting anything. Keep code-placement information and
both host results; raw grain wins alone do not justify a blanket unroll or
alignment policy.
