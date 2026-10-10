# Private fixed-reservation Tuple prefix control

Comparison against `composed::Inline::append`, first qualified privately on
`df29d705831ec9aec226ea68487a7f2d68a71a74` and measured in capture `f4491ddd`. This is one append-only Object and an
explicit reservation input, not a container or a proposed module API. Fresh-copy
append remains the selected baseline. There is no update, conversion, rollover,
reserve prediction, split, navigation, or multithreaded writer protocol here.

`PrefixGrowth` reuses Material's Schema, initial constructor and Shape proof,
count-specific target/tail shaping, native-read admission, actual Tuple suffix
constructor, Catalog capture/prepare/install, and the selected full drain plus
128 worker polls after publication. Every actual grow has a temporary full-range
Allocation Hold. Exact immutable counts and readable extents belong to each
captured Representation; only the disjoint suffix is writable. Shared ownership
keeps storage alive; the Hold and fixed reservation prevent relocation.

The only writer operation chooses the current basis internally. No speculative
Owner or Prepared packet escapes it. On every failure their local lifetimes end
before another append can write the suffix. Construction failure may have grown
the physical extent; a smaller retry uses that extent without a shrink call.
Capture/prepare refusal leaves the old count/body/cut intact. Exhausting the
explicit reservation returns conflict before growth or writing. There is no
fall back to a relocated body. Published older versions only protect shorter
prefixes; caller/Ikea still owns the disjoint-write authority.

Differences from fresh copy, all part of the comparison:

- One initial Allocation uses caller-supplied `--reserve-bytes`; page rounding is
  reported separately as actual Pool reserved virtual bytes. The driver does not
  calculate this input from its future row count.
- One `allocate_shared<Allocation>` block is allocated from Material's counted
  PMR resource. Shared capture uses an explicit address plus shared owner; fresh
  copy transfers each fresh Allocation. On the native qualification these are a
  56-byte shared block and 48-byte per-version Representation versus 40 bytes.
- Each extent increase calls Hold/grow rather than allocate/copy. There is no
  full-body writable view or prefix-copy write journal because that prefix is
  never written. Suffix construction keeps the actual Ikea write machinery.
- Append reuses its admitted immutable Shape directly instead of constructing a
  four-Part ownership carrier and calling Material's already-populated proof
  cache. The initial proof is identical. No caller-provided Shape/count is trusted.
- The comparison is serial and append-only. The broader Backend's update and
  conversion methods are absent. Material's existing control slots are retained
  and counted, including controls not used by this case.

The adapted driver preserves the original seeds/input, trace and oracle setup
before backend construction, tail array, batch sizes, reader witness/rotation,
post-publication drain/poll, end validation and reader-release envelope. Both
arms use the same loop. Extra component observations occur outside the measured
loop; accounting replay is marked timing-invalid. The new `--verify-only 1`
mode calls no benchmark clocks. Existing allocation/metadata operations are not
claimed to avoid internal OS clock use.

## Cost accounting

`requested_body_bytes` is the sum of logical requested prefix extents in both
arms; it is **not** newly allocated bytes for shared-prefix. Pool allocation,
reuse, growth and relocation counters are actual. Payload prefix copies are
zero for shared-prefix. Input/oracle setup copies and suffix encoding are
separate and remain present in both arms.

PMR live/peak includes the shared block, captured Shapes, Representations and
Catalog metadata. Component fields suffixed `in_metadata`/`in_backend` are
subsets, not additional totals. The known persistent metadata subtotal is
`sizeof(backend) + Schema inline + borrowed Tuple Shape inline + actual Tuple /
Series interpretation heap + PMR live + Pool setup/inventory + Pool metadata`.
It excludes body mappings, unmeasured Schema heap, temporary caller stack and
common diagnostic input/trace/oracle/sample buffers. It is not total memory.
The native backend objects both occupy 2048 bytes; the candidate's shared handle,
reservation and two diagnostic fields fit the existing alignment rounding.
Temporary Scope/Prepared/Change/Hold sizes are reported separately, not added to
that persistent subtotal or misrepresented as a measured stack high-water mark.

Mapped, reserved, active capacity, idle bytes and Arena charge are separate.
Shared-prefix has one nonshrinking Window and no retired body, so final physical
capacity/mapping/reservation/charge is its peak in a successful no-refusal driver
run. The driver verifies one Allocation, no reuses/relocations/idle bytes. Fresh
copy's transient physical peaks remain explicitly unavailable; replay samples
cannot observe its within-publication peak. PMR peak is tracked in both arms.
Reservation is virtual capacity, not silently counted as mapped physical bytes.
`held_extent_bytes`, retirement-record count and Schema heap remain unavailable,
not zero.

## Build and run

The experimental targets are now in the repository's opt-in MVCC study.
Use the pinned Linux toolchain; prefix these commands with `orb -m ubuntu` from
this macOS/OrbStack workspace. Preserve an existing compatible build directory:

```sh
cmake -S . -B build/clang/prefix-growth -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-21 -DSIXDB_SPIKES=orbital-mvcc
cmake --build build/clang/prefix-growth --target orbital_prefix_growth_bench orbital_prefix_growth_check -j1
ctest --test-dir build/clang/prefix-growth -R '^orbital_prefix_growth_check$' --output-on-failure
```

Both arms use `orbital_prefix_growth_bench`. For the measured case, the caller
explicitly offers 2 MiB; this is a fixture input, not a reservation policy:

```sh
orbital_prefix_growth_bench --backend fresh-copy --rows 65536 --ops 513 \
  --batch 1 --retention-age 0 --mode throughput --accounting 0
orbital_prefix_growth_bench --backend shared-prefix --reserve-bytes 2097152 \
  --rows 65536 --ops 513 --batch 1 --retention-age 0 --mode throughput --accounting 0
```

Use the executable's full path beneath the chosen build directory.
`--batch` accepts 1 or 64. `--mode latency --latency-samples NEW_FILE` retains
per-publication samples. `--verify-only 1 --accounting 1` records clock-free
counter/accounting replay.

The [bounded paired evidence](../../evidence/zen5-prefix-growth-pair-20261010/prefix-pair/summary.csv)
contains two AB/BA observations in each timing mode plus separate accounting.
Complete throughput is about29130 ns/row for fresh-copy and684 ns/row for the
whole shared-prefix alternative; see the [findings](../../FEEDBACK.md#bounded-shared-prefix-append-alternative)
for bundled differences and selection limits. Regenerate compact tables with
`run.py --report-only PATH --output EXISTING_DIRECTORY`. Its relocated report
entry was verified; the measured archive preserves the original runner path.

`owner-qualification.json` records the historical private handoff: native132
publications, five refusals,3014 row checks and69322 wire-byte checks, plus three
clock-free paired cases. Its overlay and warm-build filenames identify that
ignored handoff, rather than prerequisites for the current targets. The later
worker built the normal target graph and passed its focused CTest. Complete
source, binaries, logs and ordered samples are recovered through the evidence's
verified archive reference. The successful timing case does not qualify a
rollover policy, exhaustion recovery or actual OS-grow failure injection.
