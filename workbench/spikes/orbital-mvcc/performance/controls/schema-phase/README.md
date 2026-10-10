# Archived Schema phase diagnostic

Source projections are the exact `e509697457a4a61bee8adcd2c5631bd40ee9f221`
archive plus the explicitly reviewed six-file candidate patch (two behavior
files and their census/ownership explanations). The same probe TU is compiled against each projection without changing
their CMake files. Root owns that candidate patch and execution permission.

Build only, from the macOS workspace:

```sh
orb -m ubuntu python3 /home/ashtonsix/sixdb/workbench/spikes/orbital-mvcc/performance/controls/schema-phase/schema_phase_probe.py \
  --base-root /home/ashtonsix/sixdb/build/orbital-mvcc/schema-phase/base \
  --candidate-root /home/ashtonsix/sixdb/build/orbital-mvcc/schema-phase/candidate \
  --output /home/ashtonsix/sixdb/build/orbital-mvcc/schema-phase/native-probe --cpu 2
```

Add `--check` for local actual sizes and behavior: no clocks are read, timing
fields are null, and sample clock arrays are empty. Add `--benchmark` only in
Tooling's authorized remote timing slot. It executes actual sizes and one
bounded AB/BA sequence. Each invocation contains three repetitions of each
case, with the raw samples and median retained. No automatic ratios are reported.
The runner records pinned Clang 21.1.8, commands, binary/archive/probe hashes,
both projections' pre/post source hashes. Default compilation builds the actual
data archive and the two ordinary probe binaries. `--codegen` optionally adds
data.cpp LLVM/assembly and probe LLVM; the independent frame census owns the
required codegen evidence. The runner selects native Linux AArch64 with
armv8-a+simd/neoverse-v2, or native Linux x86_64 with x86-64-v3/znver5, identically
for base and candidate. It performs no cross execution.

The clock encloses complete existing `construct_into`/`convert_into` calls,
including their own Shape/target placement checks, real Ikea binding/value/effect
checks and all decode/initialization/effect accounting. In the candidate, its
complete existing constructor factory remains inside these calls. Timed result
checking and the caller's loop are common to both projections. One warmup call
per case occurs outside clocks. Source bindings are prepared once outside clocks.
Target storage is reused for authorized complete reinitialization across trials.

Real Lane/Arena/Pool/Allocation setup, byte allocation, fixture Shape creation,
expected rows/wire images, source construction/admission, target readback,
source immutability checks and destruction/physical close are outside clocks.
This probe does not model body discovery, Store capture, publication, conversion
adoption, durability, cache tiers or latency tails. Schema setup has its own
clock; Schema destruction is outside it. Type sizes are actual `sizeof`/`alignof`.
The existing `scratch_bound_bytes` counter describes kernel scratch and is not
relabeled as total compiler-frame or transient plan memory.

Construction and bidirectional conversion cover 129 and 4097 rows, spanning
the 128-row conversion chunk and short final tiles.
Separate 64-body groups at 1 and 64 rows/body model repeated native setup calls
only: they are not complete Backend startup. Corpus currently issues one Tuple
construction call per image; actual Backend startup issues another per image
for Tuple initial format. Thus 64 Corpus calls plus 64 Backend calls would each
prepare a candidate constructor, versus one baseline shared factory in Schema
setup. This is a source deduction, not an instrumented factory counter or a
complete-Backend timing. Series targets prepare no Tuple constructor.

The expected image uses an independent bit law for Tuple's 23 byte ranks and
Series Local64/64/20/32 octets, including the Tuple spare nibble and Series final
tile slack. Generic row/field readback is independently compared with input rows.
Conversions use source spare/slack salts. Real Holds retain source bytes after
the mutable Allocations are released; the caller stops writing those bytes and
keeps Schema, metadata and Holds live until bindings finish. Holds provide
physical retention, not publication or immutable-access authority. The full old
source image must remain unchanged after each timed conversion. All bodies and
Holds end before physical Pool close/poll/remove; final Arena and Pool metadata
charges must be zero.

Root still runs the archived existing data/consumer checks for injected partial
construction, admission errors, generic full updates and old version lifetimes.
Those tests, not this success-only clock diagnostic, qualify unchanged semantics.

The exact baseline and six-file candidate input are in the retained
[phase archive](../../evidence/zen5-schema-phase-screen-20261010/artifact.json):
`source.tar.gz` contains `schema-phase-kit/schema-phase.patch` and its
`owned-source.json` pre/post hashes. Initialize the candidate as its own Git
root before applying the patch, then verify those hashes. A parent repository
can silently skip nested paths even with `git apply --no-index`.
The [compact source audit](../../evidence/zen5-schema-phase-screen-20261010/source-recipe.json)
checks all 374 native source/header/CMake postimages in both actual projections.
The full receipt includes irrelevant copies of cached build files; it remains
archived. Reports retain all raw samples, including the first-call observations,
and are direct diagnostic outputs; they are not regenerated timings.
