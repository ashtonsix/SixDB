# Native partial edges in the expression materializer

Native edges, a reader that crosses the internal stripe boundary, and selected
short stores now beat ordinary decoding with separated samples in 66 of 68
comparisons. Two AVX512/u32 cases change direction between timing blocks and
remain unresolved; the candidate is not ready for promotion. The first
eight-row edge version had 43 separated wins and 21 losses. The remaining
output context is also examined by the separate [short-store comparison](stores.md).

## Physical change and admission

The candidate keeps full native8/16 regions and the original full-output sink.
Only the former scalar branch changes. A native8 window stays within a Local8
tile or Striped32 extraction group. A nonempty interval token names its first
active lane and count. The sink computes the first active original coordinate
before subtracting the output origin, then writes precisely the corresponding
output objects. It never forms a pointer at an inactive coordinate before the
output allocation.

Every actual expression leaf supplies its own source and independent stride.
The native reader requires its complete physical read window even where the
request clips it; a mask supplies no read permission. Projected heads touch
only the selected head, and substituted tails touch only their residual
dependency. Final physical padding remains inactive output. The separate
[composition contract probe](../ikea-composition/native-regions/partial-materialization.md) also checks
sparse holes; this optimized sink accepts only a contiguous active interval
and an expression domain of at most 32 bits, producing u32/u64 output.

AVX2 permutes selected values to low lanes and uses exact masked stores. NEON
uses a register table lookup and exact prefix stores. AVX512 uses a compress
store for this contiguous interval. These are physical spellings of positional
output within the request, not a general promise to remove filter holes. No
scratch array bridges native values to the sink.

Native NEON and QEMU AVX2 each pass the existing 581,604 guarded expression
queries, including 146,928 actual callback calls. Another 24,192 partial-sink
cases per target cover every nonempty subinterval of eight rows, full unsigned
working domains through 32 bits, poisoned high lanes, natural output alignments
and both guard edges. The original 12,928 full-sink cases also pass. Native
AVX2/AVX512 repeat all these checks in the timing job's matched compiler profile.

## Matched hardware result

Zen job `20260910T215149Z-fed0358f`, capture
`0565cafddc6b3b4ee4ca7c7c5cd8ed69f35a22a09b090f54d6545c321d1cb180`,
uses the frozen scalar-run benchmark and identical runtime/selector objects,
libraries and other benchmark objects. Its baseline benchmark and guard relink
byte-identically. Only two candidate translation units change. Binary order is
before/after/after/before; provider order reverses after two blocks. Each case
has three sequential 30 ms repetitions per process on CPU0.

The [retained audit and observations](evidence/materialize-region-20260910/20260910T215149Z-fed0358f/provenance.json)
verify 516 source records, unchanged cached sources, all native guards and 2,700
samples with independently reconstructed runtime counters/checksums. Actual
callbacks retain no inner calls or vector stack staging. Static instruction
counts shrink in the Local callbacks but grow in the striped callbacks; those
counts are not measured executed work per request.

| Case | Ordinary in revised program | Native edges |
| --- | ---: | ---: |
| AVX2 Local7/u32, origin1/count17 | 9.866 | 7.654 |
| AVX2 Local23/u64, head-gapped origin1/count17 | 14.509 | 9.136 |
| AVX512 Local23/u64, independent gaps origin1/count17 | 13.348 | 9.260 |
| AVX2 Striped12/u64, origin17/count16 | 7.242 | 9.012 |
| AVX2 Striped28/u64, origin17/count17 | 10.009 | 9.676 |
| AVX512 Striped12/u32, origin16/count16 | 2.978 | 5.165 |

Numbers are median CPU ns/query. All 18 AVX2 Local comparisons and 17 of 18 AVX512
Local comparisons have separated wins against ordinary. AVX512 Local7/u32
mixed remains a separated loss. The Local7/u32 fixed ranges improve beyond the
useful [default-loop exception](expressions.md#isolating-ordinary-compiler-loop-treatment).
AVX2 striped has six separated wins, eight separated losses and two overlaps;
AVX512 striped has two wins, 12 losses and two overlaps.

Against the frozen scalar-run materializer, 66 medians improve, 65 with separated
samples; the remaining three comparisons overlap, with no separated losses.
The linked ordinary/control comparison retains 15 separated losses and 14
separated wins among 157 cases. The adjacent primary, paired-revision and context
tables preserve every sample extremum and ordering edge; none of these losses
is waived by the broader recovery.

A request `[17,33)` still traverses a full window 17–24, a clipped window 24–31,
and a clipped window 32–39. The internal 32-row extraction pattern is a property
of this implementation, not the authored operation. A subsequent W12 residual
reader can investigate crossing that boundary while body/head reads stay
inside their own 64-row tile. Such a reader must explicitly admit the complete
32-byte residual plane of each actual tail child; head-only expressions must
not inherit that tail requirement.

## Crossing the residual boundary within a physical tile

Job `20260910T220255Z-c4495198`, capture
`4902849ec7e1ef8de094bc0fba275125c9b50c3d2e05a373b894726ada60ee6e`,
compares a revised W12 tail reader/traversal against the frozen native-edge
program. Local traversal, full and partial sinks, actual source dependencies,
runtime/selector objects and measurement protocol remain fixed. The baseline
relinks identically and all native guards repeat. The
[audit and retained samples](evidence/materialize-region-20260910/20260910T220255Z-c4495198/provenance.json)
cover 516 captured files and 2,700 observations.

The existing reader stays in use within each 32-row half. For a crossing
window, the reader accesses the actual tail child's complete 32-byte residual
plane and selects each original row's byte and low/high nibble. Body and head
windows stay inside their own 64-row physical tile, with independent strides.
The full-residual-plane admission is required only by that crossing tail read;
it is not supplied by an active mask or inherited by a head-only projection.

Striped traversal can consequently take 16 rows through lane48, eight through
lane56, and a clipped eight-row region at the physical tile boundary. A request
`[17,33)` now executes one native16 region. The pure AVX2 fallback uses byte
shuffles/blends, NEON a register table, and the measured full-feature x86 profile
permits VBMI/VL in both labelled adapter families. Pure AVX2 timing is not
established by this result; its separate QEMU guards pass.

All four actual Local callback instruction/relocation sequences are
[unchanged](evidence/materialize-region-20260910/20260910T220255Z-c4495198/reader-codegen.json).
Striped callbacks contain no inner calls or vector stack staging. This confirms
the intended compilation boundaries, not a cycle-by-cycle explanation.

| Crossing origin17/count16 case | Ordinary in revised program | Revised native reader |
| --- | ---: | ---: |
| AVX2 Striped12/u32 | 7.046 | 2.420 |
| AVX2 Striped12/u64 | 7.229 | 2.816 |
| AVX2 Striped28/u32 | 9.051 | 3.214 |
| AVX2 Striped28/u64 | 9.171 | 3.524 |
| AVX512 Striped12/u64 | 7.451 | 2.776 |
| AVX512 Striped28/u64 | 8.995 | 3.174 |

Numbers are median CPU ns/query. Sixty-four of 68 comparisons against ordinary
have separated wins, with all 68 directions agreeing at both provider-order
edges. The four separated losses are AVX512/u32: Local7 mixed 7.039 versus
6.712 ns/query, Striped12 mixed 6.307 versus 5.781, aligned Striped12 5.388
versus 2.973, and aligned Striped28 5.626 versus 3.595.

Against the previous native-edge program, 42 medians improve, with 26 separated
wins, eight losses and 34 overlaps. Seven separated losses are aligned striped
cases; the eighth is the unchanged Local23 AVX2/u64 head-gapped origin1/count16
callback (+0.31%). Those controls remain open. The linked ordinary/control
comparison also retains five separated losses and eight wins among 157 cases.
Every case remains in the adjacent primary, paired-revision and context tables.

The remaining mixed/short-output results motivate a combined comparison with
explicit stores selected for native ZMM Count16/u32 output only. That choice
keeps the original u64 and Count8 full stores and the same partial sink. It
must be checked in its actual whole callback before broader integration or
claims about other widths, footprints and targets.

## Combining the reader and selected full stores

Job `20260910T220828Z-cd9f363b`, capture
`5496c36e66d634e05539b2f635f188270b909d52b7d7d907b323d6cc99b9a030`,
changes only the full sink selection for native ZMM Count16/u32 output: its
64 bytes are written by two explicit 32-byte instructions. Other full stores
and the partial sink remain as in the whole-window candidate. The same
runtime and selector objects are reused, the frozen baseline relinks
byte-identically, and all matched native guards pass. The
[independent audit](evidence/materialize-region-20260910/20260910T220828Z-cd9f363b/provenance.json)
retains all 2,700 samples and their counters/checksums.

Sixty-six comparisons have separated wins against ordinary in the same revised
program, agreeing at both provider-order edges. Aligned AVX512/u32 Striped12
improves from ordinary 2.779 to 2.686 ns/query, and Striped28 from 3.678 to
2.984. Local7/u32 mixed improves from 6.715 to 6.313.

Both remaining cases are AVX512/u32 origin17/count17. Their candidate samples
overlap ordinary and change direction between the two provider-order blocks:

| Case | Ordinary median and range | Candidate median and range | Candidate block medians |
| --- | ---: | ---: | ---: |
| Striped12 | 7.684 (7.650–7.712) | 9.486 (7.246–11.749) | 7.249 / 11.693 |
| Striped28 | 9.992 (9.936–10.082) | 10.563 (7.886–13.431) | 7.904 / 13.242 |

Numbers are CPU ns/query. The original timed caller uses `output.data()` from
an ordinary `std::array<U, max_count>`; it does not select or record output
alignment. A separate verifier's canary subspan is not the timed output. The
17-row callback performs a full 16-row store followed by one active row in a
native partial region, and the caller consumes the first and last scalar
values. These observations motivate a controlled address experiment; they do
not establish an alignment cause, a universal masked-store penalty, or a
store-forwarding latency.

Against the frozen whole-window candidate, 30 medians improve, nine with
separated samples; the other 59 overlap and there are no separated losses.
Only 39 paired-revision directions agree at both ordering edges. The linked
ordinary/control comparison retains 18 separated losses and 20 wins among
157 cases. Neither these controls nor the two unresolved primary cases are
waived by the 66 wins. Other widths, output types, target profiles, footprints
and the public ordinary path remain outside this result's scope.

The subsequent [controlled output-alignment sweep](alignment.md)
does not reproduce the two slow modes at any tested natural offset. It retains
those unresolved observations and exposes a separate u64 output-context issue.
